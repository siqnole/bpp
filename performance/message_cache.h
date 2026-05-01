#pragma once
// ============================================================================
// MessageCache — in-memory cache of recent message content.
// Captures messages on create so their content is available when
// on_message_delete fires (which only provides message ID, not content).
// Evicts messages older than 2 hours to bound memory usage.
// ============================================================================

#include "../security/thread_safe_map.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <sstream>
#include "../utils/logger.h"

namespace bronx {
namespace snipe {

struct CachedMessage {
    uint64_t message_id = 0;
    uint64_t guild_id = 0;
    uint64_t channel_id = 0;
    uint64_t author_id = 0;
    std::string author_tag;       // "username#0" or display name
    std::string author_avatar;    // avatar URL
    std::string content;
    std::vector<std::string> content_revisions;   // Stores all edits, including initial edit
    uint64_t webhook_log_msg_id = 0;              // Stores the ID of the webhook message for this log
    std::vector<std::string> attachment_urls;
    std::string embeds_json;      // serialized embed data (simplified)
    std::chrono::system_clock::time_point created_at;
};

class MessageCache {
public:
    static constexpr size_t MAX_CACHE_SIZE = 100000;
    static constexpr auto EVICTION_INTERVAL = std::chrono::minutes(5);
    static constexpr auto MAX_MESSAGE_AGE = std::chrono::hours(2);

    MessageCache() : running_(true) {
        eviction_thread_ = std::thread([this] { eviction_loop(); });
    }

    ~MessageCache() { stop(); }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (eviction_thread_.joinable()) eviction_thread_.join();
    }

    // Cache a message from on_message_create
    void cache_message(const dpp::message& msg) {
        if (msg.author.is_bot()) return;
        if (msg.guild_id == 0) return;  // skip DMs

        CachedMessage cached;
        cached.message_id = static_cast<uint64_t>(msg.id);
        cached.guild_id = static_cast<uint64_t>(msg.guild_id);
        cached.channel_id = static_cast<uint64_t>(msg.channel_id);
        cached.author_id = static_cast<uint64_t>(msg.author.id);
        cached.author_tag = msg.author.global_name.empty()
            ? msg.author.username : msg.author.global_name;
        cached.author_avatar = msg.author.get_avatar_url();
        cached.content = msg.content;
        cached.created_at = std::chrono::system_clock::now();

        // Capture attachment URLs
        for (const auto& att : msg.attachments) {
            cached.attachment_urls.push_back(att.url);
        }

        // Capture embed summaries (simplified — just titles + descriptions)
        if (!msg.embeds.empty()) {
            std::ostringstream oss;
            for (size_t i = 0; i < msg.embeds.size() && i < 3; ++i) {
                const auto& e = msg.embeds[i];
                if (!e.title.empty()) oss << "**" << e.title << "**\n";
                if (!e.description.empty()) {
                    std::string desc = e.description;
                    if (desc.size() > 200) desc = desc.substr(0, 200) + "...";
                    oss << desc << "\n";
                }
            }
            cached.embeds_json = oss.str();
        }

        // Check size before insert — if too large, skip (eviction will catch up)
        if (cache_.size() >= MAX_CACHE_SIZE) {
            // Evict oldest ~10% on overflow
            evict_oldest();
        }

        cache_.set(static_cast<uint64_t>(msg.id), std::move(cached));
    }

    // Pop a message by ID (removes from cache and returns it)
    std::optional<CachedMessage> pop_message(uint64_t message_id) {
        auto msg = cache_.get(message_id);
        if (msg) {
            cache_.erase(message_id);
        }
        return msg;
    }

    // Get a message by ID without removing it
    std::optional<CachedMessage> get_message(uint64_t message_id) {
        return cache_.get(message_id);
    }
    
    // Update cache with a modified message
    void update_message(uint64_t message_id, const CachedMessage& msg) {
        cache_.set(message_id, msg);
    }

    size_t size() const { return cache_.size(); }

private:
    void eviction_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, EVICTION_INTERVAL, [this] { return !running_.load(); });
            if (!running_.load()) break;

            auto cutoff = std::chrono::system_clock::now() - MAX_MESSAGE_AGE;
            size_t evicted = cache_.erase_if([&cutoff](const uint64_t&, const CachedMessage& m) {
                return m.created_at < cutoff;
            });
            if (evicted > 0) {
                bronx::logger::debug("message cache", "evicted " + std::to_string(evicted) + 
                                   " old messages (remaining: " + std::to_string(cache_.size()) + ")");
            }
        }
    }

    void evict_oldest() {
        // Remove messages older than 1 hour (more aggressive than normal eviction)
        auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(1);
        cache_.erase_if([&cutoff](const uint64_t&, const CachedMessage& m) {
            return m.created_at < cutoff;
        });
    }

    bronx::security::ThreadSafeMap<uint64_t, CachedMessage> cache_;
    std::thread eviction_thread_;
    std::atomic<bool> running_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace snipe
} // namespace bronx
