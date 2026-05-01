#pragma once
// ============================================================================
// SnipeCache — hybrid in-memory + DB cache for deleted messages.
// Recent deleted messages are held in per-channel deques for instant access.
// A background thread periodically flushes pending writes to the DB for
// long-term storage (supports "all time" queries).
// ============================================================================

#include "../database/core/database.h"
#include "../database/operations/moderation/snipe_operations.h"
#include "../security/thread_safe_map.h"
#include "message_cache.h"
#include <deque>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <algorithm>
#include "../utils/logger.h"

namespace bronx {
namespace snipe {

struct DeletedMessage {
    uint64_t message_id = 0;
    uint64_t guild_id = 0;
    uint64_t channel_id = 0;
    uint64_t author_id = 0;
    std::string author_tag;
    std::string author_avatar;
    std::string content;
    std::vector<std::string> attachment_urls;
    std::string embeds_summary;
    std::chrono::system_clock::time_point deleted_at;
};

class SnipeCache {
public:
    static constexpr size_t MAX_PER_CHANNEL = 50;
    static constexpr auto FLUSH_INTERVAL = std::chrono::seconds(5);
    static constexpr auto PURGE_INTERVAL = std::chrono::hours(24);
    static constexpr int RETENTION_DAYS = 30;

    explicit SnipeCache(bronx::db::Database* db) : db_(db), running_(true) {
        flush_thread_ = std::thread([this] { flush_loop(); });
    }

    ~SnipeCache() { stop(); }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (flush_thread_.joinable()) flush_thread_.join();
        // Final flush
        flush_to_db();
    }

    // Add a deleted message to the cache.
    // Called from on_message_delete after popping from MessageCache.
    void add_deleted(DeletedMessage msg) {
        uint64_t cid = msg.channel_id;

        // Add to per-channel deque
        channel_cache_.with_lock([&](auto& map) {
            auto& deq = map[cid];
            deq.push_front(std::move(msg));
            // Copy the message for DB pending writes before potential eviction
            if (deq.size() > MAX_PER_CHANNEL) {
                deq.pop_back();
            }
        });

        // Also queue for DB write (copy the front message from channel cache)
        channel_cache_.with_shared_lock([&](const auto& map) {
            auto it = map.find(cid);
            if (it != map.end() && !it->second.empty()) {
                std::lock_guard<std::mutex> lk(pending_mutex_);
                pending_writes_.push_back(it->second.front());
            }
        });
    }

    // Get recent deleted messages for a channel from the in-memory cache.
    // Returns up to `limit` messages, optionally filtered by author and time.
    std::vector<DeletedMessage> get_channel_recent(
        uint64_t channel_id,
        uint64_t author_id = 0,
        std::chrono::system_clock::time_point since = {},
        int limit = 10) const
    {
        std::vector<DeletedMessage> result;
        channel_cache_.with_shared_lock([&](const auto& map) {
            auto it = map.find(channel_id);
            if (it == map.end()) return;
            for (const auto& msg : it->second) {
                if (author_id != 0 && msg.author_id != author_id) continue;
                if (since != std::chrono::system_clock::time_point{} && msg.deleted_at < since) continue;
                result.push_back(msg);
                if (static_cast<int>(result.size()) >= limit) break;
            }
        });
        return result;
    }

    // Get recent deleted messages across all channels in a guild from memory.
    std::vector<DeletedMessage> get_guild_recent(
        uint64_t guild_id,
        uint64_t author_id = 0,
        std::chrono::system_clock::time_point since = {},
        int limit = 10) const
    {
        std::vector<DeletedMessage> all;
        channel_cache_.with_shared_lock([&](const auto& map) {
            for (const auto& [cid, deq] : map) {
                for (const auto& msg : deq) {
                    if (msg.guild_id != guild_id) continue;
                    if (author_id != 0 && msg.author_id != author_id) continue;
                    if (since != std::chrono::system_clock::time_point{} && msg.deleted_at < since) continue;
                    all.push_back(msg);
                }
            }
        });
        // Sort by deleted_at descending
        std::sort(all.begin(), all.end(), [](const DeletedMessage& a, const DeletedMessage& b) {
            return a.deleted_at > b.deleted_at;
        });
        if (static_cast<int>(all.size()) > limit) all.resize(limit);
        return all;
    }

    // Query with DB fallback — uses in-memory for recent data,
    // falls back to DB for older data or when cache doesn't have enough results.
    std::vector<DeletedMessage> query(
        uint64_t guild_id,
        uint64_t channel_id = 0,
        uint64_t author_id = 0,
        int64_t since_unix = 0,
        int limit = 10,
        int offset = 0) const
    {
        // Gather all matching in-memory messages first
        auto since_tp = (since_unix > 0)
            ? std::chrono::system_clock::from_time_t(since_unix)
            : std::chrono::system_clock::time_point{};

        std::vector<DeletedMessage> mem;
        if (channel_id != 0) {
            // fetch up to a generous amount so we can paginate from memory
            mem = get_channel_recent(channel_id, author_id, since_tp, limit + offset);
        } else {
            mem = get_guild_recent(guild_id, author_id, since_tp, limit + offset);
        }

        // If memory has enough to satisfy this page, return from memory
        if (static_cast<int>(mem.size()) > offset) {
            int start = offset;
            int end = std::min(static_cast<int>(mem.size()), offset + limit);
            return std::vector<DeletedMessage>(mem.begin() + start, mem.begin() + end);
        }

        // Fall back to DB for older data or higher pages
        auto db_results = bronx::db::snipe_operations::query_deleted_messages(
            db_, guild_id, channel_id, author_id, since_unix, limit, offset);

        // Convert DB rows to DeletedMessage structs
        std::vector<DeletedMessage> result;
        result.reserve(db_results.size());
        for (const auto& row : db_results) {
            DeletedMessage msg;
            msg.message_id = row.message_id;
            msg.guild_id = row.guild_id;
            msg.channel_id = row.channel_id;
            msg.author_id = row.author_id;
            msg.author_tag = row.author_tag;
            msg.author_avatar = row.author_avatar;
            msg.content = row.content;
            msg.embeds_summary = row.embeds_summary;
            msg.deleted_at = std::chrono::system_clock::from_time_t(row.deleted_at_unix);

            // Parse attachment URLs (newline-separated)
            if (!row.attachment_urls.empty()) {
                std::istringstream iss(row.attachment_urls);
                std::string url;
                while (std::getline(iss, url, '\n')) {
                    if (!url.empty()) msg.attachment_urls.push_back(url);
                }
            }
            result.push_back(std::move(msg));
        }

        // If DB also returned nothing but memory had some (just not enough for this offset),
        // return whatever memory had from the start
        if (result.empty() && !mem.empty() && offset == 0) {
            return mem;
        }
        return result;
    }

    // Count total results matching a query (for pagination).
    // Checks in-memory cache first, falls back to DB.
    int count(
        uint64_t guild_id,
        uint64_t channel_id = 0,
        uint64_t author_id = 0,
        int64_t since_unix = 0) const
    {
        // Count from in-memory first
        auto since_tp = (since_unix > 0)
            ? std::chrono::system_clock::from_time_t(since_unix)
            : std::chrono::system_clock::time_point{};

        int mem_count = 0;
        if (channel_id != 0) {
            mem_count = static_cast<int>(get_channel_recent(channel_id, author_id, since_tp, 100).size());
        } else {
            mem_count = static_cast<int>(get_guild_recent(guild_id, author_id, since_tp, 100).size());
        }

        int db_count = bronx::db::snipe_operations::count_deleted_messages(
            db_, guild_id, channel_id, author_id, since_unix);

        // Return the larger of the two (memory may have unflushed data)
        return std::max(mem_count, db_count);
    }

    bronx::db::Database* get_db() const { return db_; }

private:
    void flush_loop() {
        auto last_purge = std::chrono::steady_clock::now();

        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, FLUSH_INTERVAL, [this] { return !running_.load(); });
            if (!running_.load()) break;

            flush_to_db();

            // Periodic DB purge of old messages
            auto now = std::chrono::steady_clock::now();
            if (now - last_purge > PURGE_INTERVAL) {
                last_purge = now;
                bronx::db::snipe_operations::purge_old_deleted_messages(db_, RETENTION_DAYS);
                bronx::logger::debug("snipe cache", "purged deleted messages older than " + std::to_string(RETENTION_DAYS) + " days");
            }
        }
    }

    void flush_to_db() {
        std::vector<DeletedMessage> to_write;
        {
            std::lock_guard<std::mutex> lk(pending_mutex_);
            to_write.swap(pending_writes_);
        }
        if (to_write.empty()) return;

        // Convert to DB row format
        std::vector<bronx::db::snipe_operations::DeletedMessageRow> rows;
        rows.reserve(to_write.size());
        for (const auto& msg : to_write) {
            bronx::db::snipe_operations::DeletedMessageRow row;
            row.message_id = msg.message_id;
            row.guild_id = msg.guild_id;
            row.channel_id = msg.channel_id;
            row.author_id = msg.author_id;
            row.author_tag = msg.author_tag;
            row.author_avatar = msg.author_avatar;
            row.content = msg.content;
            row.embeds_summary = msg.embeds_summary;

            // Join attachment URLs with newlines
            std::ostringstream urls;
            for (size_t i = 0; i < msg.attachment_urls.size(); ++i) {
                if (i > 0) urls << "\n";
                urls << msg.attachment_urls[i];
            }
            row.attachment_urls = urls.str();
            rows.push_back(std::move(row));
        }

        bool ok = bronx::db::snipe_operations::save_deleted_messages_batch(db_, rows);
        if (!ok) {
            bronx::logger::error("snipe cache", "failed to flush " + std::to_string(rows.size()) + " deleted messages to DB");
        }
    }

    bronx::db::Database* db_;
    bronx::security::ThreadSafeMap<uint64_t, std::deque<DeletedMessage>> channel_cache_;

    std::mutex pending_mutex_;
    std::vector<DeletedMessage> pending_writes_;

    std::thread flush_thread_;
    std::atomic<bool> running_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace snipe
} // namespace bronx
