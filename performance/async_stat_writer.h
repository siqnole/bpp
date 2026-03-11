#pragma once
#include "../database/core/database.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <iostream>

namespace bronx {
namespace perf {

// ---------------------------------------------------------------------------
// AsyncStatWriter — batches synchronous telemetry writes (log_command,
// increment_stat, ensure_user_exists) off the gateway event threads.
//
// Instead of doing 3 blocking DB round-trips per command just for logging,
// we buffer the writes in memory and flush them every few seconds on a
// dedicated background thread.  This eliminates the #1 cause of slowness
// with remote databases.
// ---------------------------------------------------------------------------
class AsyncStatWriter {
public:
    explicit AsyncStatWriter(bronx::db::Database* db,
                             std::chrono::milliseconds flush_interval = std::chrono::milliseconds(3000))
        : db_(db), flush_interval_(flush_interval) {}

    ~AsyncStatWriter() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        flush_thread_ = std::thread([this] { flush_loop(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (flush_thread_.joinable()) flush_thread_.join();
        flush();  // final drain
    }

    // -----------------------------------------------------------------------
    // Enqueue methods — called from gateway event threads, MUST be fast.
    // -----------------------------------------------------------------------

    // Buffer a command history log entry
    void enqueue_log_command(uint64_t user_id, const std::string& command) {
        std::lock_guard<std::mutex> lk(log_mutex_);
        pending_logs_.push_back({user_id, command});
    }

    // Buffer a stat increment
    void enqueue_increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount) {
        std::lock_guard<std::mutex> lk(stat_mutex_);
        auto key = std::make_pair(user_id, stat_name);
        stat_deltas_[key] += amount;
    }

    // Mark a user as known to exist (avoid redundant ensure_user_exists calls)
    void mark_user_known(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(known_mutex_);
        known_users_.insert(user_id);
    }

    bool is_user_known(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(known_mutex_);
        return known_users_.count(user_id) > 0;
    }

    // Force an immediate flush
    void flush_now() { flush(); }

private:
    struct LogEntry {
        uint64_t user_id;
        std::string command;
    };

    // Key for stat deltas: (user_id, stat_name)
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, std::string>& p) const {
            auto h1 = std::hash<uint64_t>{}(p.first);
            auto h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 << 32);
        }
    };

    std::mutex log_mutex_;
    std::vector<LogEntry> pending_logs_;

    std::mutex stat_mutex_;
    std::unordered_map<std::pair<uint64_t, std::string>, int64_t, PairHash> stat_deltas_;

    std::mutex known_mutex_;
    std::unordered_set<uint64_t> known_users_;

    bronx::db::Database* db_;
    std::chrono::milliseconds flush_interval_;
    std::atomic<bool> running_{false};
    std::thread flush_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    void flush_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, flush_interval_, [this] { return !running_.load(); });
            if (!running_.load()) break;
            lk.unlock();
            flush();
        }
    }

    void flush() {
        // Swap out pending data under lock, then process without holding lock
        std::vector<LogEntry> logs;
        {
            std::lock_guard<std::mutex> lk(log_mutex_);
            logs.swap(pending_logs_);
        }

        std::unordered_map<std::pair<uint64_t, std::string>, int64_t, PairHash> stats;
        {
            std::lock_guard<std::mutex> lk(stat_mutex_);
            stats.swap(stat_deltas_);
        }

        if (logs.empty() && stats.empty()) return;

        // Collect unique user IDs that need ensure_user_exists
        std::unordered_set<uint64_t> users_to_ensure;
        for (const auto& log : logs) {
            if (!is_user_known(log.user_id)) {
                users_to_ensure.insert(log.user_id);
            }
        }
        for (const auto& [key, _] : stats) {
            if (!is_user_known(key.first)) {
                users_to_ensure.insert(key.first);
            }
        }

        // Batch ensure_user_exists
        for (uint64_t uid : users_to_ensure) {
            try {
                db_->ensure_user_exists(uid);
                mark_user_known(uid);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] ensure_user_exists failed for " << uid << ": " << e.what() << "\n";
            }
        }

        // Flush command logs
        for (const auto& log : logs) {
            try {
                db_->log_history(log.user_id, "CMD", "ran ." + log.command);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_history failed: " << e.what() << "\n";
            }
        }

        // Flush stat increments
        for (const auto& [key, amount] : stats) {
            try {
                // Call increment_stat_raw to skip the internal ensure_user_exists
                // (we already ensured above in batch)
                db_->increment_stat(key.first, key.second, amount);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] increment_stat failed: " << e.what() << "\n";
            }
        }

        if (!logs.empty() || !stats.empty()) {
            // Only log occasionally to avoid spam
            static std::atomic<uint64_t> flush_count{0};
            if (++flush_count % 20 == 1) {
                std::cerr << "[async_stat] flushed " << logs.size() << " logs, "
                          << stats.size() << " stat updates\n";
            }
        }
    }
};

} // namespace perf
} // namespace bronx
