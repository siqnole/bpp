#pragma once
#include "../../database/core/database.h"
#include "../../database/operations/leveling/xp_blacklist_operations.h"
#include "../../performance/xp_batch_writer.h"
#include <dpp/dpp.h>
#include <random>
#include <chrono>
#include <unordered_map>
#include <mutex>

namespace leveling {

// In-memory cooldown tracking to prevent race conditions
// Key format: "guild_id:user_id"
static ::std::unordered_map<::std::string, ::std::chrono::system_clock::time_point> xp_cooldown_cache;
static ::std::mutex xp_cooldown_mutex;

// ---------------------------------------------------------------------------
// Cached server leveling config — avoids a DB round-trip on every message
// ---------------------------------------------------------------------------
struct CachedLevelingConfig {
    bronx::db::ServerLevelingConfig config;
    std::chrono::steady_clock::time_point fetched_at;
};
static std::unordered_map<uint64_t, CachedLevelingConfig> leveling_config_cache;
static std::mutex leveling_config_mutex;
static constexpr auto LEVELING_CONFIG_TTL = std::chrono::seconds(60);

inline std::optional<bronx::db::ServerLevelingConfig> get_cached_leveling_config(
        bronx::db::Database* db, uint64_t guild_id) {
    {
        std::lock_guard<std::mutex> lock(leveling_config_mutex);
        auto it = leveling_config_cache.find(guild_id);
        if (it != leveling_config_cache.end()) {
            if (std::chrono::steady_clock::now() - it->second.fetched_at < LEVELING_CONFIG_TTL) {
                return it->second.config;
            }
        }
    }
    // Cache miss or expired — fetch from DB
    auto cfg = db->get_server_leveling_config(guild_id);
    if (!cfg) {
        db->create_server_leveling_config(guild_id);
        cfg = db->get_server_leveling_config(guild_id);
    }
    if (cfg) {
        std::lock_guard<std::mutex> lock(leveling_config_mutex);
        leveling_config_cache[guild_id] = { *cfg, std::chrono::steady_clock::now() };
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// Cached XP blacklist check — avoids 2-4 DB queries on every message
// ---------------------------------------------------------------------------
struct CachedBlacklistResult {
    bool blocked;
    std::chrono::steady_clock::time_point fetched_at;
};
// Key: "guild_id:channel_id:user_id" (role set changes rarely matter for caching)
static std::unordered_map<std::string, CachedBlacklistResult> xp_blacklist_result_cache;
static std::mutex xp_blacklist_result_mutex;
static constexpr auto XP_BLACKLIST_TTL = std::chrono::seconds(120);

inline bool is_xp_blocked_cached(bronx::db::Database* db, uint64_t guild_id,
                                  uint64_t channel_id, uint64_t user_id,
                                  const std::vector<uint64_t>& roles) {
    std::string key = std::to_string(guild_id) + ":" + std::to_string(channel_id)
                    + ":" + std::to_string(user_id);
    {
        std::lock_guard<std::mutex> lock(xp_blacklist_result_mutex);
        auto it = xp_blacklist_result_cache.find(key);
        if (it != xp_blacklist_result_cache.end()) {
            if (std::chrono::steady_clock::now() - it->second.fetched_at < XP_BLACKLIST_TTL) {
                return it->second.blocked;
            }
        }
    }
    bool blocked = bronx::db::xp_blacklist_operations::should_block_xp(
        db, guild_id, channel_id, user_id, roles);
    {
        std::lock_guard<std::mutex> lock(xp_blacklist_result_mutex);
        xp_blacklist_result_cache[key] = { blocked, std::chrono::steady_clock::now() };
    }
    return blocked;
}

// ---------------------------------------------------------------------------
// Thread-local RNG — avoids creating random_device + mt19937 per message
// ---------------------------------------------------------------------------
inline std::mt19937& get_thread_rng() {
    thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

// Global XP batch writer pointer — set by main.cpp during startup
static bronx::xp::XpBatchWriter* g_xp_batch_writer = nullptr;

inline void set_xp_batch_writer(bronx::xp::XpBatchWriter* writer) {
    g_xp_batch_writer = writer;
}

// Track XP from message events — NON-BLOCKING version that enqueues XP into
// the batch writer instead of doing synchronous DB writes on the gateway thread.
inline void handle_message_xp(dpp::cluster& bot, const dpp::message_create_t& event, bronx::db::Database* db) {
    // Ignore bots
    if (event.msg.author.is_bot()) return;
    
    // Only process in guilds
    if (!event.msg.guild_id) return;
    
    uint64_t user_id = event.msg.author.id;
    uint64_t guild_id = event.msg.guild_id;
    uint64_t channel_id = event.msg.channel_id;
    
    // Get user roles (from member if cached)
    std::vector<uint64_t> user_role_ids;
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g) {
        auto member_it = g->members.find(user_id);
        if (member_it != g->members.end()) {
            for (const auto& role : member_it->second.get_roles()) {
                user_role_ids.push_back(role);
            }
        }
    }
    
    // OPTIMIZED: Check XP blacklists via cache (avoids 2-4 DB queries per message)
    if (is_xp_blocked_cached(db, guild_id, channel_id, user_id, user_role_ids)) {
        return; // User, channel, or role is blacklisted from gaining XP
    }
    
    // OPTIMIZED: Get server config via cache (avoids DB query per message)
    auto config = get_cached_leveling_config(db, guild_id);
    
    // Check if leveling is enabled
    if (!config || !config->enabled) return;
    
    // Check message length requirement
    if (static_cast<int>(event.msg.content.length()) < config->min_message_chars) return;
    
    // Check cooldown using in-memory cache (prevents race conditions)
    ::std::string cache_key = ::std::to_string(guild_id) + ":" + ::std::to_string(user_id);
    {
        ::std::lock_guard<::std::mutex> lock(xp_cooldown_mutex);
        auto now = ::std::chrono::system_clock::now();
        
        auto it = xp_cooldown_cache.find(cache_key);
        if (it != xp_cooldown_cache.end()) {
            auto diff = ::std::chrono::duration_cast<::std::chrono::seconds>(now - it->second).count();
            if (diff < config->xp_cooldown_seconds) {
                return; // Still in cooldown
            }
        }
        
        // Update cooldown cache before processing (prevents concurrent processing)
        xp_cooldown_cache[cache_key] = now;
    }
    
    // OPTIMIZED: Use thread-local RNG instead of creating random_device per message
    auto& rng = get_thread_rng();
    std::uniform_int_distribution<> dis(config->min_xp_per_message, config->max_xp_per_message);
    uint64_t xp_to_award = dis(rng);
    
    // -------------------------------------------------------------------
    // PERFORMANCE FIX: Enqueue XP into the batch writer instead of doing
    // 4-9 synchronous blocking DB calls on the gateway event thread.
    // The batch writer flushes to MySQL every ~5 seconds on its own thread,
    // keeping the shard threads free to process heartbeats and events.
    // -------------------------------------------------------------------
    if (g_xp_batch_writer) {
        g_xp_batch_writer->enqueue_global_xp(user_id, xp_to_award);
        g_xp_batch_writer->enqueue_server_xp(user_id, guild_id, xp_to_award);
        if (config->reward_coins && config->coins_per_message > 0) {
            g_xp_batch_writer->enqueue_coins(user_id, config->coins_per_message);
        }
    } else {
        // Fallback: synchronous writes (should not happen in normal operation)
        uint32_t new_global_level = 0;
        bool global_leveled_up = false;
        db->add_xp(user_id, xp_to_award, new_global_level, global_leveled_up);
        
        uint32_t new_server_level = 0;
        bool server_leveled_up = false;
        db->add_server_xp(user_id, guild_id, xp_to_award, new_server_level, server_leveled_up);
        
        if (config->reward_coins && config->coins_per_message > 0) {
            db->update_wallet(user_id, config->coins_per_message);
        }
    }
    // Level-up announcements are handled by the batch writer's callback
    // (see main.cpp where the callback is registered).
}

// Cleanup old entries from cooldown cache (call periodically, e.g., every hour)
inline void cleanup_xp_cooldown_cache(int max_age_seconds = 3600) {
    ::std::lock_guard<::std::mutex> lock(xp_cooldown_mutex);
    auto now = ::std::chrono::system_clock::now();
    
    for (auto it = xp_cooldown_cache.begin(); it != xp_cooldown_cache.end(); ) {
        auto age = ::std::chrono::duration_cast<::std::chrono::seconds>(now - it->second).count();
        if (age > max_age_seconds) {
            it = xp_cooldown_cache.erase(it);
        } else {
            ++it;
        }
    }
    
    // Also clean up the leveling config cache
    {
        std::lock_guard<std::mutex> lclock(leveling_config_mutex);
        auto steady_now = std::chrono::steady_clock::now();
        for (auto it = leveling_config_cache.begin(); it != leveling_config_cache.end(); ) {
            if (steady_now - it->second.fetched_at > std::chrono::minutes(10)) {
                it = leveling_config_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Clean up XP blacklist result cache
    {
        std::lock_guard<std::mutex> bllock(xp_blacklist_result_mutex);
        auto steady_now = std::chrono::steady_clock::now();
        for (auto it = xp_blacklist_result_cache.begin(); it != xp_blacklist_result_cache.end(); ) {
            if (steady_now - it->second.fetched_at > std::chrono::minutes(10)) {
                it = xp_blacklist_result_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

} // namespace leveling
