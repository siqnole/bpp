#pragma once
#include "../database/core/database.h"
#include "../database/operations/leveling/leveling_operations.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bronx {
namespace xp {

// Callback fired when a user levels up so the caller can send announcements,
// award roles, etc.  Runs on the flush thread — keep it fast or dispatch work.
struct LevelUpEvent {
    uint64_t user_id;
    uint64_t guild_id;       // 0 for global level-ups
    uint32_t new_level;
};
using LevelUpCallback = std::function<void(const std::vector<LevelUpEvent>&)>;

// ---------------------------------------------------------------------------
// XpBatchWriter — accumulates per-message XP in memory and flushes to MySQL
// periodically, taking the blocking DB writes OFF the gateway event thread.
//
// Usage:
//   writer.enqueue_global_xp(user_id, xp);
//   writer.enqueue_server_xp(user_id, guild_id, xp);
//   writer.enqueue_coins(user_id, coins);
//
// The flush interval (default 5 s) controls the worst-case staleness of the
// XP tables.  All accumulated deltas are applied with atomic SQL
// (UPDATE ... SET col = col + ?) so no read-modify-write race.
// ---------------------------------------------------------------------------
class XpBatchWriter {
public:
    explicit XpBatchWriter(bronx::db::Database* db,
                           std::chrono::milliseconds flush_interval = std::chrono::milliseconds(5000))
        : db_(db), flush_interval_(flush_interval) {}

    ~XpBatchWriter() { stop(); }

    // Start the background flush thread.
    void start() {
        if (running_.exchange(true)) return;  // already running
        flush_thread_ = std::thread([this] { flush_loop(); });
    }

    // Stop the writer, flushing any remaining data.
    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (flush_thread_.joinable()) flush_thread_.join();
        flush();  // final drain
    }

    // Register a callback fired (on the flush thread) whenever level-ups are
    // detected during a flush.
    void set_levelup_callback(LevelUpCallback cb) {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        levelup_cb_ = std::move(cb);
    }

    // -----------------------------------------------------------------------
    // Enqueue methods — called from gateway event threads, must be fast.
    // -----------------------------------------------------------------------

    void enqueue_global_xp(uint64_t user_id, uint64_t xp) {
        std::lock_guard<std::mutex> lk(global_mutex_);
        global_xp_deltas_[user_id] += xp;
    }

    void enqueue_server_xp(uint64_t user_id, uint64_t guild_id, uint64_t xp) {
        std::lock_guard<std::mutex> lk(server_mutex_);
        auto key = std::make_pair(user_id, guild_id);
        server_xp_deltas_[key] += xp;
    }

    void enqueue_coins(uint64_t user_id, int64_t coins) {
        std::lock_guard<std::mutex> lk(coin_mutex_);
        coin_deltas_[user_id] += coins;
    }

    // Force an immediate flush (used at shutdown / by owner commands).
    void flush_now() { flush(); }

private:
    // ---- global XP accumulator ----
    std::mutex global_mutex_;
    std::unordered_map<uint64_t, uint64_t> global_xp_deltas_;

    // ---- server XP accumulator (key = {user_id, guild_id}) ----
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, uint64_t>& p) const {
            // fast hash combine
            auto h1 = std::hash<uint64_t>{}(p.first);
            auto h2 = std::hash<uint64_t>{}(p.second);
            return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
    std::mutex server_mutex_;
    std::unordered_map<std::pair<uint64_t, uint64_t>, uint64_t, PairHash> server_xp_deltas_;

    // ---- coin accumulator ----
    std::mutex coin_mutex_;
    std::unordered_map<uint64_t, int64_t> coin_deltas_;

    // ---- control ----
    bronx::db::Database* db_;
    std::chrono::milliseconds flush_interval_;
    std::atomic<bool> running_{false};
    std::thread flush_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;

    std::mutex cb_mutex_;
    LevelUpCallback levelup_cb_;

    // Background loop
    void flush_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, flush_interval_, [&] { return !running_.load(); });
            if (!running_.load()) break;
            lk.unlock();
            flush();
        }
    }

    // ---------------------------------------------------------------------------
    // flush() — swap out the accumulated maps and write them to MySQL with
    // atomic UPDATE statements.  Each user/guild combo becomes a single query.
    // ---------------------------------------------------------------------------
    void flush() {
        // 1. Swap out the maps under their respective mutexes (fast)
        decltype(global_xp_deltas_) global_snap;
        decltype(server_xp_deltas_) server_snap;
        decltype(coin_deltas_) coin_snap;

        { std::lock_guard<std::mutex> lk(global_mutex_);  global_snap.swap(global_xp_deltas_); }
        { std::lock_guard<std::mutex> lk(server_mutex_);  server_snap.swap(server_xp_deltas_); }
        { std::lock_guard<std::mutex> lk(coin_mutex_);    coin_snap.swap(coin_deltas_); }

        if (global_snap.empty() && server_snap.empty() && coin_snap.empty()) return;

        std::vector<LevelUpEvent> level_ups;

        // 2. Flush global XP
        for (auto& [user_id, xp_delta] : global_snap) {
            flush_global_xp(user_id, xp_delta, level_ups);
        }

        // 3. Flush server XP
        for (auto& [key, xp_delta] : server_snap) {
            flush_server_xp(key.first, key.second, xp_delta, level_ups);
        }

        // 4. Flush coins
        for (auto& [user_id, coins] : coin_snap) {
            if (coins != 0) {
                db_->update_wallet(user_id, coins);
            }
        }

        // 5. Fire level-up callback
        if (!level_ups.empty()) {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            if (levelup_cb_) {
                try { levelup_cb_(level_ups); }
                catch (const std::exception& e) {
                    std::cerr << "[xp_batch] levelup callback error: " << e.what() << "\n";
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Atomic global XP flush — INSERT … ON DUPLICATE KEY UPDATE with xp += delta
    // Returns the new total_xp and level so we can detect level-ups.
    // ---------------------------------------------------------------------------
    void flush_global_xp(uint64_t user_id, uint64_t xp_delta,
                         std::vector<LevelUpEvent>& level_ups) {
        auto conn = db_->get_pool()->acquire();
        if (!conn) return;

        // Atomic upsert: inserts if new, otherwise adds delta
        const char* query =
            "INSERT INTO user_xp (user_id, total_xp, level, last_xp_gain) "
            "VALUES (?, ?, 1, NOW()) "
            "ON DUPLICATE KEY UPDATE total_xp = total_xp + VALUES(total_xp), "
            "last_xp_gain = NOW()";

        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            if (stmt) mysql_stmt_close(stmt);
            db_->get_pool()->release(conn);
            return;
        }

        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&xp_delta;
        bind[1].is_unsigned = 1;

        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
        mysql_stmt_close(stmt);

        // Now read back the current total_xp + level to recalculate level
        const char* sel = "SELECT total_xp, level FROM user_xp WHERE user_id = ?";
        stmt = mysql_stmt_init(conn->get());
        if (!stmt || mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
            if (stmt) mysql_stmt_close(stmt);
            db_->get_pool()->release(conn);
            return;
        }

        MYSQL_BIND sel_bind[1];
        memset(sel_bind, 0, sizeof(sel_bind));
        sel_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        sel_bind[0].buffer = (char*)&user_id;
        sel_bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, sel_bind);

        if (mysql_stmt_execute(stmt) == 0) {
            MYSQL_BIND res[2];
            uint64_t total_xp = 0;
            uint32_t old_level = 0;
            memset(res, 0, sizeof(res));
            res[0].buffer_type = MYSQL_TYPE_LONGLONG;
            res[0].buffer = (char*)&total_xp;
            res[0].is_unsigned = 1;
            res[1].buffer_type = MYSQL_TYPE_LONG;
            res[1].buffer = (char*)&old_level;
            res[1].is_unsigned = 1;
            mysql_stmt_bind_result(stmt, res);

            if (mysql_stmt_fetch(stmt) == 0) {
                uint32_t new_level = bronx::db::leveling_operations::calculate_level_from_xp(total_xp);
                if (new_level != old_level) {
                    // Update level column
                    mysql_stmt_free_result(stmt);
                    mysql_stmt_close(stmt);

                    const char* upd = "UPDATE user_xp SET level = ? WHERE user_id = ?";
                    stmt = mysql_stmt_init(conn->get());
                    if (stmt && mysql_stmt_prepare(stmt, upd, strlen(upd)) == 0) {
                        MYSQL_BIND ub[2];
                        memset(ub, 0, sizeof(ub));
                        ub[0].buffer_type = MYSQL_TYPE_LONG;
                        ub[0].buffer = (char*)&new_level;
                        ub[0].is_unsigned = 1;
                        ub[1].buffer_type = MYSQL_TYPE_LONGLONG;
                        ub[1].buffer = (char*)&user_id;
                        ub[1].is_unsigned = 1;
                        mysql_stmt_bind_param(stmt, ub);
                        mysql_stmt_execute(stmt);
                    }
                    if (stmt) mysql_stmt_close(stmt);
                    db_->get_pool()->release(conn);

                    if (new_level > old_level) {
                        level_ups.push_back({user_id, 0, new_level});
                    }
                    return;
                }
            }
            mysql_stmt_free_result(stmt);
        }
        mysql_stmt_close(stmt);
        db_->get_pool()->release(conn);
    }

    // ---------------------------------------------------------------------------
    // Atomic server XP flush
    // ---------------------------------------------------------------------------
    void flush_server_xp(uint64_t user_id, uint64_t guild_id, uint64_t xp_delta,
                         std::vector<LevelUpEvent>& level_ups) {
        auto conn = db_->get_pool()->acquire();
        if (!conn) return;

        const char* query =
            "INSERT INTO server_xp (user_id, guild_id, server_xp, server_level, last_server_xp_gain) "
            "VALUES (?, ?, ?, 1, NOW()) "
            "ON DUPLICATE KEY UPDATE server_xp = server_xp + VALUES(server_xp), "
            "last_server_xp_gain = NOW()";

        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            if (stmt) mysql_stmt_close(stmt);
            db_->get_pool()->release(conn);
            return;
        }

        MYSQL_BIND bind[3];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&guild_id;
        bind[1].is_unsigned = 1;
        bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[2].buffer = (char*)&xp_delta;
        bind[2].is_unsigned = 1;

        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
        mysql_stmt_close(stmt);

        // Read back to check for level-up
        const char* sel = "SELECT server_xp, server_level FROM server_xp WHERE user_id = ? AND guild_id = ?";
        stmt = mysql_stmt_init(conn->get());
        if (!stmt || mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
            if (stmt) mysql_stmt_close(stmt);
            db_->get_pool()->release(conn);
            return;
        }

        MYSQL_BIND sel_bind[2];
        memset(sel_bind, 0, sizeof(sel_bind));
        sel_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        sel_bind[0].buffer = (char*)&user_id;
        sel_bind[0].is_unsigned = 1;
        sel_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        sel_bind[1].buffer = (char*)&guild_id;
        sel_bind[1].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, sel_bind);

        if (mysql_stmt_execute(stmt) == 0) {
            MYSQL_BIND res[2];
            uint64_t total_xp = 0;
            uint32_t old_level = 0;
            memset(res, 0, sizeof(res));
            res[0].buffer_type = MYSQL_TYPE_LONGLONG;
            res[0].buffer = (char*)&total_xp;
            res[0].is_unsigned = 1;
            res[1].buffer_type = MYSQL_TYPE_LONG;
            res[1].buffer = (char*)&old_level;
            res[1].is_unsigned = 1;
            mysql_stmt_bind_result(stmt, res);

            if (mysql_stmt_fetch(stmt) == 0) {
                uint32_t new_level = bronx::db::leveling_operations::calculate_level_from_xp(total_xp);
                if (new_level != old_level) {
                    mysql_stmt_free_result(stmt);
                    mysql_stmt_close(stmt);

                    const char* upd = "UPDATE server_xp SET server_level = ? WHERE user_id = ? AND guild_id = ?";
                    stmt = mysql_stmt_init(conn->get());
                    if (stmt && mysql_stmt_prepare(stmt, upd, strlen(upd)) == 0) {
                        MYSQL_BIND ub[3];
                        memset(ub, 0, sizeof(ub));
                        ub[0].buffer_type = MYSQL_TYPE_LONG;
                        ub[0].buffer = (char*)&new_level;
                        ub[0].is_unsigned = 1;
                        ub[1].buffer_type = MYSQL_TYPE_LONGLONG;
                        ub[1].buffer = (char*)&user_id;
                        ub[1].is_unsigned = 1;
                        ub[2].buffer_type = MYSQL_TYPE_LONGLONG;
                        ub[2].buffer = (char*)&guild_id;
                        ub[2].is_unsigned = 1;
                        mysql_stmt_bind_param(stmt, ub);
                        mysql_stmt_execute(stmt);
                    }
                    if (stmt) mysql_stmt_close(stmt);
                    db_->get_pool()->release(conn);

                    if (new_level > old_level) {
                        level_ups.push_back({user_id, guild_id, new_level});
                    }
                    return;
                }
            }
            mysql_stmt_free_result(stmt);
        }
        mysql_stmt_close(stmt);
        db_->get_pool()->release(conn);
    }
};

} // namespace xp
} // namespace bronx
