#pragma once
// ---------------------------------------------------------------------------
// WriteBatchQueue — General-purpose mutation batch queue for remote DB writes
//
// Collects wallet deltas, bank deltas, inventory mutations, and stat deltas
// in a local queue and flushes them to the remote MariaDB on a background
// thread every 2 seconds.  This extends the XpBatchWriter pattern to cover
// ALL high-frequency write operations, not just XP.
//
// Combined with LocalDB for instant reads, this eliminates the blocking
// 40-100ms remote write latency from every economy command.
// ---------------------------------------------------------------------------

#include "../database/core/database.h"
#include "local_db.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include "../utils/logger.h"

// Forward declaration — avoid circular include with async_stat_writer.h
namespace bronx { namespace perf { class RemoteStatsConnection; } }

namespace bronx {
namespace batch {

// Inventory mutation — add or remove items
struct ItemMutation {
    enum Type { ADD, REMOVE };
    Type type;
    uint64_t user_id;
    std::string item_id;
    std::string item_type;
    int quantity;
    std::string category;
    int level;
    std::string metadata;
};

class WriteBatchQueue {
public:
    explicit WriteBatchQueue(bronx::db::Database* db,
                             bronx::local::LocalDB* local_db,
                             std::chrono::milliseconds flush_interval = std::chrono::milliseconds(2000))
        : db_(db), local_db_(local_db), flush_interval_(flush_interval) {}

    ~WriteBatchQueue() { stop(); }

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
    // Enqueue methods — called from gateway threads, MUST be fast (<1us)
    // -----------------------------------------------------------------------

    // Wallet delta — the local balance is already updated in LocalDB
    void enqueue_wallet_delta(uint64_t user_id, int64_t delta) {
        std::lock_guard<std::mutex> lk(wallet_mutex_);
        wallet_deltas_[user_id] += delta;
    }

    // Bank delta
    void enqueue_bank_delta(uint64_t user_id, int64_t delta) {
        std::lock_guard<std::mutex> lk(bank_mutex_);
        bank_deltas_[user_id] += delta;
    }

    // Stat delta (generalizes the existing AsyncStatWriter pattern)
    void enqueue_stat_delta(uint64_t user_id, const std::string& stat_name, int64_t amount) {
        std::lock_guard<std::mutex> lk(stat_mutex_);
        auto key = std::make_pair(user_id, stat_name);
        stat_deltas_[key] += amount;
    }

    // Inventory mutation (add or remove items)
    void enqueue_item_mutation(const ItemMutation& mutation) {
        std::lock_guard<std::mutex> lk(item_mutex_);
        pending_items_.push_back(mutation);
    }

    // Convenience: enqueue an item add
    void enqueue_add_item(uint64_t user_id, const std::string& item_id,
                          const std::string& item_type, int quantity,
                          const std::string& category = "", int level = 1,
                          const std::string& metadata = "") {
        ItemMutation m{};
        m.type = ItemMutation::ADD;
        m.user_id = user_id;
        m.item_id = item_id;
        m.item_type = item_type;
        m.quantity = quantity;
        m.category = category;
        m.level = level;
        m.metadata = metadata;
        enqueue_item_mutation(m);
    }

    // Convenience: enqueue an item remove
    void enqueue_remove_item(uint64_t user_id, const std::string& item_id, int quantity) {
        ItemMutation m{};
        m.type = ItemMutation::REMOVE;
        m.user_id = user_id;
        m.item_id = item_id;
        m.quantity = quantity;
        enqueue_item_mutation(m);
    }    

    // Force immediate flush (shutdown / owner commands)
    void flush_now() { flush(); }

    // Set remote stats connection for Aiven dual-write (call after AsyncStatWriter is initialized)
    void set_remote_connection(bronx::perf::RemoteStatsConnection* remote) {
        remote_stats_ = remote;
    }

    // Get flush statistics
    struct FlushStats {
        std::atomic<uint64_t> total_flushes{0};
        std::atomic<uint64_t> wallet_writes{0};
        std::atomic<uint64_t> bank_writes{0};
        std::atomic<uint64_t> stat_writes{0};
        std::atomic<uint64_t> item_writes{0};
        std::atomic<uint64_t> errors{0};
    };
    const FlushStats& stats() const { return stats_; }

private:
    // ---- wallet accumulator ----
    std::mutex wallet_mutex_;
    std::unordered_map<uint64_t, int64_t> wallet_deltas_;

    // ---- bank accumulator ----
    std::mutex bank_mutex_;
    std::unordered_map<uint64_t, int64_t> bank_deltas_;

    // ---- stat accumulator (key = {user_id, stat_name}) ----
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, std::string>& p) const {
            auto h1 = std::hash<uint64_t>{}(p.first);
            auto h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
        }
    };
    std::mutex stat_mutex_;
    std::unordered_map<std::pair<uint64_t, std::string>, int64_t, PairHash> stat_deltas_;

    // ---- item mutation queue ----
    std::mutex item_mutex_;
    std::vector<ItemMutation> pending_items_;

    // ---- control ----
    bronx::db::Database* db_;
    bronx::local::LocalDB* local_db_;
    bronx::perf::RemoteStatsConnection* remote_stats_ = nullptr;  // Aiven dual-write (optional)
    std::chrono::milliseconds flush_interval_;
    std::atomic<bool> running_{false};
    std::thread flush_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    FlushStats stats_;

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
        // 1. Swap out all accumulator maps (fast, under their respective mutexes)
        decltype(wallet_deltas_) wallet_snap;
        decltype(bank_deltas_) bank_snap;
        decltype(stat_deltas_) stat_snap;
        std::vector<ItemMutation> item_snap;

        { std::lock_guard<std::mutex> lk(wallet_mutex_); wallet_snap.swap(wallet_deltas_); }
        { std::lock_guard<std::mutex> lk(bank_mutex_);   bank_snap.swap(bank_deltas_); }
        { std::lock_guard<std::mutex> lk(stat_mutex_);   stat_snap.swap(stat_deltas_); }
        { std::lock_guard<std::mutex> lk(item_mutex_);   item_snap.swap(pending_items_); }

        // Also drain any deltas accumulated in LocalDB's pending tables
        if (local_db_) {
            auto local_wallet = local_db_->drain_wallet_deltas();
            for (auto& [uid, delta] : local_wallet) {
                wallet_snap[uid] += delta;
            }
            auto local_bank = local_db_->drain_bank_deltas();
            for (auto& [uid, delta] : local_bank) {
                bank_snap[uid] += delta;
            }
        }

        if (wallet_snap.empty() && bank_snap.empty() && stat_snap.empty() && item_snap.empty()) {
            return;
        }

        stats_.total_flushes++;

        // 2. Flush wallet deltas to remote with atomic UPDATE
        for (auto& [user_id, delta] : wallet_snap) {
            if (delta == 0) continue;
            try {
                db_->update_wallet(user_id, delta);
                stats_.wallet_writes++;
                // Dual-write to Aiven for dashboard
                if (remote_stats_) {
                    // Ensure user exists on Aiven before updating
                    std::string ensure = "INSERT IGNORE INTO users (user_id, wallet, bank) VALUES ("
                        + std::to_string(user_id) + ", 0, 0)";
                    remote_stats_->execute(ensure);
                    std::string sql = "UPDATE users SET wallet = wallet + (" + std::to_string(delta)
                        + ") WHERE user_id = " + std::to_string(user_id);
                    remote_stats_->execute(sql);
                }
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "wallet flush failed for " + std::to_string(user_id) + ": " + std::string(e.what()));
                // Re-queue the delta for next flush
                std::lock_guard<std::mutex> lk(wallet_mutex_);
                wallet_deltas_[user_id] += delta;
            }
        }

        // 3. Flush bank deltas
        for (auto& [user_id, delta] : bank_snap) {
            if (delta == 0) continue;
            try {
                db_->update_bank(user_id, delta);
                stats_.bank_writes++;
                // Dual-write to Aiven for dashboard
                if (remote_stats_) {
                    std::string ensure = "INSERT IGNORE INTO users (user_id, wallet, bank) VALUES ("
                        + std::to_string(user_id) + ", 0, 0)";
                    remote_stats_->execute(ensure);
                    std::string sql = "UPDATE users SET bank = bank + (" + std::to_string(delta)
                        + ") WHERE user_id = " + std::to_string(user_id);
                    remote_stats_->execute(sql);
                }
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "bank flush failed for " + std::to_string(user_id) + ": " + std::string(e.what()));
                std::lock_guard<std::mutex> lk(bank_mutex_);
                bank_deltas_[user_id] += delta;
            }
        }

        // 4. Flush stat deltas
        for (auto& [key, amount] : stat_snap) {
            if (amount == 0) continue;
            try {
                db_->increment_stat(key.first, key.second, amount);
                stats_.stat_writes++;
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "stat flush failed for " + std::to_string(key.first) + ":" + key.second + ": " + std::string(e.what()));
            }
        }

        // 5. Flush item mutations
        for (auto& m : item_snap) {
            try {
                if (m.type == ItemMutation::ADD) {
                    db_->add_item(m.user_id, m.item_id, m.item_type, m.quantity, m.category, m.level);
                } else {
                    db_->remove_item(m.user_id, m.item_id, m.quantity);
                }
                stats_.item_writes++;
            } catch (const std::exception& e) {
                stats_.errors++;
                bronx::logger::error("write_batch", "item flush failed for " + std::to_string(m.user_id) + ":" + m.item_id + ": " + std::string(e.what()));
            }
        }

        // Log periodically
        static std::atomic<uint64_t> flush_count{0};
        if (++flush_count % 30 == 1) {
            size_t total = wallet_snap.size() + bank_snap.size() + stat_snap.size() + item_snap.size();
            bronx::logger::debug("write_batch", "flushed " + std::to_string(total) + " mutations ("
                      + std::to_string(wallet_snap.size()) + "W " + std::to_string(bank_snap.size()) + "B "
                      + std::to_string(stat_snap.size()) + "S " + std::to_string(item_snap.size()) + "I)");
        }
    }
};

} // namespace batch
} // namespace bronx
