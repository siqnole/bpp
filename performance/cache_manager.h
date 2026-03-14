#pragma once
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <algorithm>

namespace bronx {
namespace cache {

// All toggle settings for a single guild, loaded in bulk.
// This eliminates per-command DB round-trips — the entire guild's toggle
// configuration is fetched once and evaluated in-memory.
struct GuildToggleData {
    struct DefaultSetting { bool enabled; };
    struct ScopedSetting { std::string scope_type; uint64_t scope_id; bool enabled; bool exclusive; };

    // module_name -> guild-wide default
    std::unordered_map<std::string, bool> module_defaults;
    // module_name -> list of scoped overrides
    std::unordered_map<std::string, std::vector<ScopedSetting>> module_scopes;
    // command_name -> guild-wide default
    std::unordered_map<std::string, bool> command_defaults;
    // command_name -> list of scoped overrides
    std::unordered_map<std::string, std::vector<ScopedSetting>> command_scopes;

    // Evaluate whether a module is enabled for the given context.
    // Replicates the exact same priority logic as Database::is_guild_module_enabled.
    bool is_module_enabled(const std::string& module, uint64_t user_id,
                           uint64_t channel_id, const std::vector<uint64_t>& roles) const {
        return evaluate(module_scopes, module_defaults, module, user_id, channel_id, roles);
    }

    bool is_command_enabled(const std::string& command, uint64_t user_id,
                            uint64_t channel_id, const std::vector<uint64_t>& roles) const {
        return evaluate(command_scopes, command_defaults, command, user_id, channel_id, roles);
    }

private:
    bool evaluate(const std::unordered_map<std::string, std::vector<ScopedSetting>>& scopes,
                  const std::unordered_map<std::string, bool>& defaults,
                  const std::string& name, uint64_t user_id,
                  uint64_t channel_id, const std::vector<uint64_t>& roles) const {
        auto scope_it = scopes.find(name);
        if (scope_it != scopes.end()) {
            const auto& entries = scope_it->second;
            // 1) Check exclusive — if ANY exclusive entries exist, caller must match at least one
            bool has_exclusive = false;
            bool matched_exclusive = false;
            for (const auto& e : entries) {
                if (e.exclusive && e.enabled) {
                    has_exclusive = true;
                    if (e.scope_type == "user" && user_id == e.scope_id) { matched_exclusive = true; break; }
                    if (e.scope_type == "channel" && channel_id == e.scope_id) { matched_exclusive = true; break; }
                    if (e.scope_type == "role") {
                        for (uint64_t r : roles) { if (r == e.scope_id) { matched_exclusive = true; break; } }
                        if (matched_exclusive) break;
                    }
                }
            }
            if (has_exclusive) return matched_exclusive;
            // 2) User override
            if (user_id != 0) {
                for (const auto& e : entries) {
                    if (e.scope_type == "user" && e.scope_id == user_id) return e.enabled;
                }
            }
            // 3) Channel override
            if (channel_id != 0) {
                for (const auto& e : entries) {
                    if (e.scope_type == "channel" && e.scope_id == channel_id) return e.enabled;
                }
            }
            // 4) Role overrides
            for (uint64_t r : roles) {
                for (const auto& e : entries) {
                    if (e.scope_type == "role" && e.scope_id == r) return e.enabled;
                }
            }
        }
        // 5) Guild default
        auto def_it = defaults.find(name);
        if (def_it != defaults.end()) return def_it->second;
        return true; // enabled by default
    }
};

// Thread-safe cache with TTL support
template<typename K, typename V>
class TTLCache {
private:
    struct CacheEntry {
        V value;
        std::chrono::steady_clock::time_point expiry;
    };
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<K, CacheEntry> cache_;
    std::chrono::milliseconds default_ttl_;
    size_t max_size_;  // max entries; 0 = unlimited

public:
    explicit TTLCache(std::chrono::milliseconds ttl = std::chrono::minutes(5),
                      size_t max_size = 50000)
        : default_ttl_(ttl), max_size_(max_size) {}
    
    void set(const K& key, const V& value, std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) {
        std::unique_lock lock(mutex_);
        auto expiry = std::chrono::steady_clock::now() + (ttl.count() > 0 ? ttl : default_ttl_);
        cache_[key] = {value, expiry};
        // Evict expired entries if we exceed max_size to prevent unbounded growth
        if (max_size_ > 0 && cache_.size() > max_size_) {
            evict_expired_unlocked();
            // If still over limit after evicting expired, remove oldest entries
            if (cache_.size() > max_size_) {
                evict_oldest_unlocked(cache_.size() - max_size_);
            }
        }
    }
    
    std::optional<V> get(const K& key) {
        std::shared_lock lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }
        
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            // Entry expired — return nullopt without upgrading to an exclusive
            // lock.  Attempting a shared→exclusive upgrade on the same
            // shared_mutex triggers EDEADLK on Linux's pthread rwlock even after
            // explicit unlock(), causing the "Resource deadlock avoided"
            // system_error in DPP's thread pool.  Expired entries will be
            // removed by the next cleanup() pass.
            return std::nullopt;
        }
        
        return it->second.value;
    }
    
    void invalidate(const K& key) {
        std::unique_lock lock(mutex_);
        cache_.erase(key);
    }
    
    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }
    
    size_t size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }
    
    // Clean up expired entries
    void cleanup() {
        std::unique_lock lock(mutex_);
        evict_expired_unlocked();
    }

private:
    // Must be called with mutex_ held exclusively
    void evict_expired_unlocked() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now > it->second.expiry) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Evict N entries with the earliest expiry (LRU-like)
    // Must be called with mutex_ held exclusively
    void evict_oldest_unlocked(size_t count) {
        if (count == 0 || cache_.empty()) return;
        // Find the N entries closest to expiry
        std::vector<typename std::unordered_map<K, CacheEntry>::iterator> candidates;
        candidates.reserve(cache_.size());
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            candidates.push_back(it);
        }
        // Sort by expiry (earliest first)
        std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a->second.expiry < b->second.expiry; });
        size_t to_remove = std::min(count, candidates.size());
        for (size_t i = 0; i < to_remove; ++i) {
            cache_.erase(candidates[i]);
        }
    }
};

// High-performance cache manager for bot data
class CacheManager {
private:
    // User-related caches
    TTLCache<uint64_t, bool> blacklist_cache_;
    TTLCache<uint64_t, bool> whitelist_cache_;
    TTLCache<uint64_t, std::vector<std::string>> user_prefixes_cache_;
    TTLCache<std::string, std::chrono::steady_clock::time_point> cooldown_cache_;
    
    // Guild-related caches
    TTLCache<uint64_t, std::vector<std::string>> guild_prefixes_cache_;
    TTLCache<std::string, bool> guild_module_cache_; // "guild_id:module" -> enabled
    TTLCache<std::string, bool> guild_command_cache_; // "guild_id:command" -> enabled
    TTLCache<uint64_t, GuildToggleData> guild_toggle_cache_; // guild_id -> ALL toggle data
    
    // Balance caches for frequently accessed data
    TTLCache<uint64_t, int64_t> wallet_cache_;
    TTLCache<uint64_t, int64_t> bank_cache_;
    
    std::shared_mutex cleanup_mutex_;
    std::chrono::steady_clock::time_point last_cleanup_;

public:
    CacheManager() : 
        blacklist_cache_(std::chrono::minutes(10)),  // blacklist rarely changes
        whitelist_cache_(std::chrono::minutes(10)),  // whitelist rarely changes  
        user_prefixes_cache_(std::chrono::minutes(5)), // user prefixes don't change often
        cooldown_cache_(std::chrono::minutes(1)),    // cooldowns are short-lived
        guild_prefixes_cache_(std::chrono::minutes(5)), // refreshed by periodic sync
        guild_module_cache_(std::chrono::minutes(5)),    // refreshed by periodic sync
        guild_command_cache_(std::chrono::minutes(5)),   // refreshed by periodic sync
        guild_toggle_cache_(std::chrono::seconds(300)),   // bulk guild toggle data (5 min)
        wallet_cache_(std::chrono::seconds(30)),     // balances change frequently
        bank_cache_(std::chrono::seconds(30)),       // bank balances change frequently
        last_cleanup_(std::chrono::steady_clock::now()) {}
    
    // Blacklist operations
    void set_blacklisted(uint64_t user_id, bool blacklisted) {
        blacklist_cache_.set(user_id, blacklisted);
    }
    
    std::optional<bool> is_blacklisted(uint64_t user_id) {
        return blacklist_cache_.get(user_id);
    }
    
    void invalidate_blacklist(uint64_t user_id) {
        blacklist_cache_.invalidate(user_id);
    }
    
    // Whitelist operations
    void set_whitelisted(uint64_t user_id, bool whitelisted) {
        whitelist_cache_.set(user_id, whitelisted);
    }
    
    std::optional<bool> is_whitelisted(uint64_t user_id) {
        return whitelist_cache_.get(user_id);
    }
    
    void invalidate_whitelist(uint64_t user_id) {
        whitelist_cache_.invalidate(user_id);
    }
    
    // User prefix operations
    void set_user_prefixes(uint64_t user_id, const std::vector<std::string>& prefixes) {
        user_prefixes_cache_.set(user_id, prefixes);
    }
    
    std::optional<std::vector<std::string>> get_user_prefixes(uint64_t user_id) {
        return user_prefixes_cache_.get(user_id);
    }
    
    void invalidate_user_prefixes(uint64_t user_id) {
        user_prefixes_cache_.invalidate(user_id);
    }
    
    // Guild prefix operations
    void set_guild_prefixes(uint64_t guild_id, const std::vector<std::string>& prefixes) {
        guild_prefixes_cache_.set(guild_id, prefixes);
    }
    
    std::optional<std::vector<std::string>> get_guild_prefixes(uint64_t guild_id) {
        return guild_prefixes_cache_.get(guild_id);
    }
    
    void invalidate_guild_prefixes(uint64_t guild_id) {
        guild_prefixes_cache_.invalidate(guild_id);
    }
    
    // Module/command enable status
    std::string make_module_key(uint64_t guild_id, const std::string& module) {
        return std::to_string(guild_id) + ":" + module;
    }
    
    std::string make_command_key(uint64_t guild_id, const std::string& command) {
        return std::to_string(guild_id) + ":" + command;
    }
    
    void set_module_enabled(uint64_t guild_id, const std::string& module, bool enabled) {
        guild_module_cache_.set(make_module_key(guild_id, module), enabled);
    }
    
    std::optional<bool> is_module_enabled(uint64_t guild_id, const std::string& module) {
        return guild_module_cache_.get(make_module_key(guild_id, module));
    }
    
    void set_command_enabled(uint64_t guild_id, const std::string& command, bool enabled) {
        guild_command_cache_.set(make_command_key(guild_id, command), enabled);
    }
    
    std::optional<bool> is_command_enabled(uint64_t guild_id, const std::string& command) {
        return guild_command_cache_.get(make_command_key(guild_id, command));
    }
    
    void invalidate_command(uint64_t guild_id, const std::string& command) {
        guild_command_cache_.invalidate(make_command_key(guild_id, command));
    }
    
    void invalidate_module(uint64_t guild_id, const std::string& module) {
        guild_module_cache_.invalidate(make_module_key(guild_id, module));
    }

    // Bulk guild toggle cache — stores ALL toggle data per guild
    void set_guild_toggles(uint64_t guild_id, const GuildToggleData& data) {
        guild_toggle_cache_.set(guild_id, data);
    }

    std::optional<GuildToggleData> get_guild_toggles(uint64_t guild_id) {
        return guild_toggle_cache_.get(guild_id);
    }

    void invalidate_guild_toggles(uint64_t guild_id) {
        guild_toggle_cache_.invalidate(guild_id);
    }

    void invalidate_all_guild_toggles() {
        guild_toggle_cache_.clear();
    }
    
    // Cooldown operations
    void set_cooldown_expiry(uint64_t user_id, const std::string& command, std::chrono::steady_clock::time_point expiry) {
        std::string key = std::to_string(user_id) + ":" + command;
        cooldown_cache_.set(key, expiry);
    }
    
    std::optional<std::chrono::steady_clock::time_point> get_cooldown_expiry(uint64_t user_id, const std::string& command) {
        std::string key = std::to_string(user_id) + ":" + command;
        return cooldown_cache_.get(key);
    }
    
    bool is_on_cooldown(uint64_t user_id, const std::string& command) {
        auto expiry = get_cooldown_expiry(user_id, command);
        if (!expiry) return false;
        return std::chrono::steady_clock::now() < *expiry;
    }
    
    // Balance caching
    void set_wallet(uint64_t user_id, int64_t amount) {
        wallet_cache_.set(user_id, amount);
    }
    
    std::optional<int64_t> get_wallet(uint64_t user_id) {
        return wallet_cache_.get(user_id);
    }
    
    void invalidate_wallet(uint64_t user_id) {
        wallet_cache_.invalidate(user_id);
    }
    
    void set_bank(uint64_t user_id, int64_t amount) {
        bank_cache_.set(user_id, amount);
    }
    
    std::optional<int64_t> get_bank(uint64_t user_id) {
        return bank_cache_.get(user_id);
    }
    
    void invalidate_bank(uint64_t user_id) {
        bank_cache_.invalidate(user_id);
    }
    
    void invalidate_user_balance(uint64_t user_id) {
        invalidate_wallet(user_id);
        invalidate_bank(user_id);
    }
    
    // Periodic cleanup
    void periodic_cleanup() {
        std::unique_lock lock(cleanup_mutex_);
        auto now = std::chrono::steady_clock::now();
        
        // Only cleanup every 5 minutes to avoid performance impact
        if (now - last_cleanup_ < std::chrono::minutes(5)) {
            return;
        }
        
        blacklist_cache_.cleanup();
        whitelist_cache_.cleanup();
        user_prefixes_cache_.cleanup();
        cooldown_cache_.cleanup();
        guild_prefixes_cache_.cleanup();
        guild_module_cache_.cleanup();
        guild_command_cache_.cleanup();
        guild_toggle_cache_.cleanup();
        wallet_cache_.cleanup();
        bank_cache_.cleanup();
        
        last_cleanup_ = now;
    }
    
    // Cache statistics
    struct CacheStats {
        size_t blacklist_entries;
        size_t whitelist_entries;
        size_t user_prefixes_entries;
        size_t cooldown_entries;
        size_t guild_prefixes_entries;
        size_t module_entries;
        size_t command_entries;
        size_t guild_toggle_entries;
        size_t wallet_entries;
        size_t bank_entries;
        size_t total_entries;
    };
    
    CacheStats get_stats() const {
        CacheStats stats{};
        stats.blacklist_entries = blacklist_cache_.size();
        stats.whitelist_entries = whitelist_cache_.size();
        stats.user_prefixes_entries = user_prefixes_cache_.size();
        stats.cooldown_entries = cooldown_cache_.size();
        stats.guild_prefixes_entries = guild_prefixes_cache_.size();
        stats.module_entries = guild_module_cache_.size();
        stats.command_entries = guild_command_cache_.size();
        stats.guild_toggle_entries = guild_toggle_cache_.size();
        stats.wallet_entries = wallet_cache_.size();
        stats.bank_entries = bank_cache_.size();
        stats.total_entries = stats.blacklist_entries + stats.whitelist_entries +
                            stats.user_prefixes_entries + stats.cooldown_entries +
                            stats.guild_prefixes_entries + stats.module_entries +
                            stats.command_entries + stats.guild_toggle_entries +
                            stats.wallet_entries + stats.bank_entries;
        return stats;
    }
};

// Global cache instance
extern std::unique_ptr<CacheManager> global_cache;

// Initialize cache system
void initialize_cache();

// Shutdown cache system
void shutdown_cache();

} // namespace cache
} // namespace bronx