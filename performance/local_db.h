#pragma once
// ---------------------------------------------------------------------------
// LocalDB — Embedded SQLite cache for sub-millisecond reads
//
// Mirrors the most frequently accessed remote (Aiven MariaDB) data locally
// using SQLite in WAL mode.  All reads are served from this local copy;
// a background sync thread periodically pulls fresh data from the remote DB
// and stale entries are refreshed on-demand.
//
// This eliminates the 40-100ms per-query TCP round-trip to the cloud database
// for the hottest data paths (get_user, get_wallet, get_inventory, get_stat,
// get_prestige, get_shop_items, cooldowns).
// ---------------------------------------------------------------------------

#include "../database/core/types.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <functional>

namespace bronx {
namespace local {

// TTL defaults (seconds) — tune per data type
struct CacheTTL {
    int user_data      = 30;    // user profiles: 30s (balances change often)
    int inventory      = 60;    // inventory: 60s
    int stats          = 60;    // user stats: 60s
    int shop_items     = 300;   // shop: 5 min (rarely changes)
    int prestige       = 120;   // prestige: 2 min
    int cooldowns      = 0;     // cooldowns: local-only, never expire via TTL
    int api_cache      = 120;   // API responses: 2 min
    int fishing_gear   = 60;    // active fishing rod/bait: 60s
    // Pre-check data (hot path — checked every command)
    int blacklist      = 300;   // blacklist: 5 min (rarely changes)
    int whitelist      = 300;   // whitelist: 5 min (rarely changes)
    int prefixes       = 300;   // prefixes: 5 min (rarely changes)
    int guild_toggles  = 300;   // guild toggles: 5 min (use setup command to refresh)
};

class LocalDB {
private:
    sqlite3* db_ = nullptr;
    mutable std::mutex db_mutex_;  // SQLite is threadsafe with serialized mode, but we add our own for safety
    CacheTTL ttl_;
    std::string db_path_;
    bool initialized_ = false;

    // Helper: execute a statement with no result
    bool exec(const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::cerr << "[local_db] SQL error: " << (err ? err : "unknown") << "\n";
            if (err) sqlite3_free(err);
            return false;
        }
        return true;
    }

    // Get current unix timestamp
    static int64_t now_unix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Check if a cached entry is still fresh
    static bool is_fresh(int64_t fetched_at, int ttl_seconds) {
        if (ttl_seconds <= 0) return true;  // no expiry
        return (now_unix() - fetched_at) < ttl_seconds;
    }

    void init_schema() {
        // Enable WAL mode for concurrent reads + writes
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=NORMAL");
        exec("PRAGMA cache_size=-8000");      // 8MB cache
        exec("PRAGMA temp_store=MEMORY");
        exec("PRAGMA mmap_size=67108864");     // 64MB mmap

        // Users cache
        exec(R"(
            CREATE TABLE IF NOT EXISTS users_cache (
                user_id INTEGER PRIMARY KEY,
                wallet INTEGER DEFAULT 0,
                bank INTEGER DEFAULT 0,
                bank_limit INTEGER DEFAULT 10000,
                interest_rate REAL DEFAULT 0.01,
                interest_level INTEGER DEFAULT 0,
                prestige INTEGER DEFAULT 0,
                passive INTEGER DEFAULT 0,
                dev INTEGER DEFAULT 0,
                admin INTEGER DEFAULT 0,
                is_mod INTEGER DEFAULT 0,
                vip INTEGER DEFAULT 0,
                total_gambled INTEGER DEFAULT 0,
                total_won INTEGER DEFAULT 0,
                total_lost INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Inventory cache
        exec(R"(
            CREATE TABLE IF NOT EXISTS inventory_cache (
                user_id INTEGER,
                item_id TEXT,
                item_type TEXT DEFAULT '',
                quantity INTEGER DEFAULT 0,
                level INTEGER DEFAULT 1,
                metadata TEXT DEFAULT '',
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (user_id, item_id)
            )
        )");
        exec("CREATE INDEX IF NOT EXISTS idx_inv_user ON inventory_cache(user_id)");

        // Stats cache
        exec(R"(
            CREATE TABLE IF NOT EXISTS stats_cache (
                user_id INTEGER,
                stat_name TEXT,
                value INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (user_id, stat_name)
            )
        )");

        // Shop items cache (shared across all users)
        exec(R"(
            CREATE TABLE IF NOT EXISTS shop_cache (
                item_id TEXT PRIMARY KEY,
                name TEXT DEFAULT '',
                description TEXT DEFAULT '',
                category TEXT DEFAULT '',
                price INTEGER DEFAULT 0,
                max_quantity INTEGER DEFAULT -1,
                required_level INTEGER DEFAULT 0,
                level INTEGER DEFAULT 1,
                usable INTEGER DEFAULT 0,
                metadata TEXT DEFAULT '',
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Generic API response cache (key → JSON blob)
        exec(R"(
            CREATE TABLE IF NOT EXISTS api_cache (
                cache_key TEXT PRIMARY KEY,
                response_json TEXT DEFAULT '{}',
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Active fishing gear cache
        exec(R"(
            CREATE TABLE IF NOT EXISTS fishing_gear_cache (
                user_id INTEGER PRIMARY KEY,
                rod_id TEXT DEFAULT '',
                rod_level INTEGER DEFAULT 1,
                bait_id TEXT DEFAULT '',
                bait_level INTEGER DEFAULT 1,
                bait_quantity INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Cooldowns — fully local, never synced to remote
        exec(R"(
            CREATE TABLE IF NOT EXISTS local_cooldowns (
                user_id INTEGER,
                command TEXT,
                expires_at INTEGER,
                PRIMARY KEY (user_id, command)
            )
        )");

        // Wallet delta tracking — accumulates local changes not yet flushed to remote
        exec(R"(
            CREATE TABLE IF NOT EXISTS pending_wallet_deltas (
                user_id INTEGER PRIMARY KEY,
                delta INTEGER DEFAULT 0
            )
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS pending_bank_deltas (
                user_id INTEGER PRIMARY KEY,
                delta INTEGER DEFAULT 0
            )
        )");

        // =====================================================================
        // PRE-CHECK CACHE TABLES — For instant command pre-processing
        // These eliminate the 200-500ms remote DB round-trips per command
        // =====================================================================

        // Global blacklist cache — checked on every command
        exec(R"(
            CREATE TABLE IF NOT EXISTS blacklist_cache (
                user_id INTEGER PRIMARY KEY,
                is_blacklisted INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Global whitelist cache — checked on every command
        exec(R"(
            CREATE TABLE IF NOT EXISTS whitelist_cache (
                user_id INTEGER PRIMARY KEY,
                is_whitelisted INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // User custom prefixes (per-user)
        exec(R"(
            CREATE TABLE IF NOT EXISTS user_prefixes_cache (
                user_id INTEGER PRIMARY KEY,
                prefixes TEXT DEFAULT '[]',
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Guild custom prefixes (per-guild)
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_prefixes_cache (
                guild_id INTEGER PRIMARY KEY,
                prefixes TEXT DEFAULT '[]',
                fetched_at INTEGER DEFAULT 0
            )
        )");

        // Guild module defaults (guild_id, module → enabled)
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_module_defaults (
                guild_id INTEGER,
                module TEXT,
                enabled INTEGER DEFAULT 1,
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (guild_id, module)
            )
        )");
        exec("CREATE INDEX IF NOT EXISTS idx_gmd_guild ON guild_module_defaults(guild_id)");

        // Guild module scope overrides
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_module_scopes (
                guild_id INTEGER,
                module TEXT,
                scope_type TEXT,
                scope_id INTEGER,
                enabled INTEGER DEFAULT 1,
                exclusive INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (guild_id, module, scope_type, scope_id)
            )
        )");
        exec("CREATE INDEX IF NOT EXISTS idx_gms_guild ON guild_module_scopes(guild_id)");

        // Guild command defaults (guild_id, command → enabled)
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_command_defaults (
                guild_id INTEGER,
                command TEXT,
                enabled INTEGER DEFAULT 1,
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (guild_id, command)
            )
        )");
        exec("CREATE INDEX IF NOT EXISTS idx_gcd_guild ON guild_command_defaults(guild_id)");

        // Guild command scope overrides
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_command_scopes (
                guild_id INTEGER,
                command TEXT,
                scope_type TEXT,
                scope_id INTEGER,
                enabled INTEGER DEFAULT 1,
                exclusive INTEGER DEFAULT 0,
                fetched_at INTEGER DEFAULT 0,
                PRIMARY KEY (guild_id, command, scope_type, scope_id)
            )
        )");
        exec("CREATE INDEX IF NOT EXISTS idx_gcs_guild ON guild_command_scopes(guild_id)");

        // Guild toggle data validity marker (tracks when full guild toggle was loaded)
        exec(R"(
            CREATE TABLE IF NOT EXISTS guild_toggles_meta (
                guild_id INTEGER PRIMARY KEY,
                fetched_at INTEGER DEFAULT 0
            )
        )");
    }

public:
    explicit LocalDB(const std::string& path = "/tmp/bronxbot_cache.db", CacheTTL ttl = {})
        : db_path_(path), ttl_(ttl) {}

    ~LocalDB() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    bool initialize() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        int rc = sqlite3_open(db_path_.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::cerr << "[local_db] Failed to open " << db_path_ << ": "
                      << sqlite3_errmsg(db_) << "\n";
            return false;
        }
        init_schema();
        initialized_ = true;
        return true;
    }

    bool is_initialized() const { return initialized_; }

    // =====================================================================
    // USER DATA — cache entire UserData struct
    // =====================================================================

    void cache_user(const bronx::db::UserData& u) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = R"(
            INSERT OR REPLACE INTO users_cache
                (user_id, wallet, bank, bank_limit, interest_rate, interest_level,
                 prestige, passive, dev, admin, is_mod, vip,
                 total_gambled, total_won, total_lost, fetched_at)
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(u.user_id));
        sqlite3_bind_int64(stmt, 2, u.wallet);
        sqlite3_bind_int64(stmt, 3, u.bank);
        sqlite3_bind_int64(stmt, 4, u.bank_limit);
        sqlite3_bind_double(stmt, 5, u.interest_rate);
        sqlite3_bind_int(stmt, 6, u.interest_level);
        sqlite3_bind_int(stmt, 7, u.prestige);
        sqlite3_bind_int(stmt, 8, u.passive ? 1 : 0);
        sqlite3_bind_int(stmt, 9, u.dev ? 1 : 0);
        sqlite3_bind_int(stmt, 10, u.admin ? 1 : 0);
        sqlite3_bind_int(stmt, 11, u.is_mod ? 1 : 0);
        sqlite3_bind_int(stmt, 12, u.vip ? 1 : 0);
        sqlite3_bind_int64(stmt, 13, u.total_gambled);
        sqlite3_bind_int64(stmt, 14, u.total_won);
        sqlite3_bind_int64(stmt, 15, u.total_lost);
        sqlite3_bind_int64(stmt, 16, now_unix());

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<bronx::db::UserData> get_cached_user(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = R"(
            SELECT wallet, bank, bank_limit, interest_rate, interest_level,
                   prestige, passive, dev, admin, is_mod, vip,
                   total_gambled, total_won, total_lost, fetched_at
            FROM users_cache WHERE user_id = ?
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));

        std::optional<bronx::db::UserData> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 14);
            if (is_fresh(fetched_at, ttl_.user_data)) {
                bronx::db::UserData u{};
                u.user_id = user_id;
                u.wallet = sqlite3_column_int64(stmt, 0);
                u.bank = sqlite3_column_int64(stmt, 1);
                u.bank_limit = sqlite3_column_int64(stmt, 2);
                u.interest_rate = sqlite3_column_double(stmt, 3);
                u.interest_level = sqlite3_column_int(stmt, 4);
                u.prestige = sqlite3_column_int(stmt, 5);
                u.passive = sqlite3_column_int(stmt, 6) != 0;
                u.dev = sqlite3_column_int(stmt, 7) != 0;
                u.admin = sqlite3_column_int(stmt, 8) != 0;
                u.is_mod = sqlite3_column_int(stmt, 9) != 0;
                u.vip = sqlite3_column_int(stmt, 10) != 0;
                u.total_gambled = sqlite3_column_int64(stmt, 11);
                u.total_won = sqlite3_column_int64(stmt, 12);
                u.total_lost = sqlite3_column_int64(stmt, 13);
                result = u;
            }
        }
        sqlite3_finalize(stmt);

        // Apply any pending local wallet/bank deltas  
        if (result) {
            result->wallet += get_pending_wallet_delta_unlocked(user_id);
            result->bank += get_pending_bank_delta_unlocked(user_id);
        }

        return result;
    }

    // =====================================================================
    // WALLET / BANK — fast local balance with delta tracking
    // =====================================================================

    std::optional<int64_t> get_cached_wallet(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT wallet, fetched_at FROM users_cache WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));

        std::optional<int64_t> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.user_data)) {
                int64_t wallet = sqlite3_column_int64(stmt, 0);
                wallet += get_pending_wallet_delta_unlocked(user_id);
                result = wallet;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    std::optional<int64_t> get_cached_bank(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT bank, fetched_at FROM users_cache WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));

        std::optional<int64_t> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.user_data)) {
                int64_t bank = sqlite3_column_int64(stmt, 0);
                bank += get_pending_bank_delta_unlocked(user_id);
                result = bank;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Apply a wallet delta locally (immediate) — returns new local balance
    int64_t apply_wallet_delta(uint64_t user_id, int64_t delta) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        // Update the cached wallet value
        exec_bind("UPDATE users_cache SET wallet = wallet + ? WHERE user_id = ?",
                  delta, static_cast<int64_t>(user_id));
        // Track the pending delta for remote sync
        const char* sql = R"(
            INSERT INTO pending_wallet_deltas (user_id, delta)
            VALUES (?, ?)
            ON CONFLICT(user_id) DO UPDATE SET delta = delta + excluded.delta
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_bind_int64(stmt, 2, delta);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        // Return the new local wallet balance
        int64_t new_wallet = 0;
        const char* sel = "SELECT wallet FROM users_cache WHERE user_id = ?";
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                new_wallet = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return new_wallet;
    }

    int64_t apply_bank_delta(uint64_t user_id, int64_t delta) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        exec_bind("UPDATE users_cache SET bank = bank + ? WHERE user_id = ?",
                  delta, static_cast<int64_t>(user_id));
        const char* sql = R"(
            INSERT INTO pending_bank_deltas (user_id, delta)
            VALUES (?, ?)
            ON CONFLICT(user_id) DO UPDATE SET delta = delta + excluded.delta
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_bind_int64(stmt, 2, delta);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        int64_t new_bank = 0;
        const char* sel = "SELECT bank FROM users_cache WHERE user_id = ?";
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                new_bank = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return new_bank;
    }

    // Drain all pending wallet deltas (called by the batch writer)
    std::unordered_map<uint64_t, int64_t> drain_wallet_deltas() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        std::unordered_map<uint64_t, int64_t> deltas;
        const char* sel = "SELECT user_id, delta FROM pending_wallet_deltas WHERE delta != 0";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                uint64_t uid = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
                int64_t d = sqlite3_column_int64(stmt, 1);
                deltas[uid] = d;
            }
            sqlite3_finalize(stmt);
        }
        exec("DELETE FROM pending_wallet_deltas");
        return deltas;
    }

    std::unordered_map<uint64_t, int64_t> drain_bank_deltas() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        std::unordered_map<uint64_t, int64_t> deltas;
        const char* sel = "SELECT user_id, delta FROM pending_bank_deltas WHERE delta != 0";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sel, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                uint64_t uid = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
                int64_t d = sqlite3_column_int64(stmt, 1);
                deltas[uid] = d;
            }
            sqlite3_finalize(stmt);
        }
        exec("DELETE FROM pending_bank_deltas");
        return deltas;
    }

    // =====================================================================
    // INVENTORY
    // =====================================================================

    void cache_inventory(uint64_t user_id, const std::vector<bronx::db::InventoryItem>& items) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        // Clear old entries for this user
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, "DELETE FROM inventory_cache WHERE user_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        int64_t ts = now_unix();
        const char* sql = R"(
            INSERT OR REPLACE INTO inventory_cache
                (user_id, item_id, item_type, quantity, level, metadata, fetched_at)
            VALUES (?,?,?,?,?,?,?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

        exec("BEGIN TRANSACTION");
        for (const auto& item : items) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_bind_text(stmt, 2, item.item_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, item.item_type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, item.quantity);
            sqlite3_bind_int(stmt, 5, item.level);
            sqlite3_bind_text(stmt, 6, item.metadata.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 7, ts);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        exec("COMMIT");
    }

    std::optional<std::vector<bronx::db::InventoryItem>> get_cached_inventory(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        // First check freshness of any entry for this user
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_,
                "SELECT fetched_at FROM inventory_cache WHERE user_id = ? LIMIT 1",
                -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int64_t fetched_at = sqlite3_column_int64(stmt, 0);
                    if (!is_fresh(fetched_at, ttl_.inventory)) {
                        sqlite3_finalize(stmt);
                        return std::nullopt;  // stale
                    }
                } else {
                    sqlite3_finalize(stmt);
                    return std::nullopt;  // no data
                }
                sqlite3_finalize(stmt);
            } else {
                return std::nullopt;
            }
        }

        const char* sql = R"(
            SELECT item_id, item_type, quantity, level, metadata
            FROM inventory_cache WHERE user_id = ?
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));

        std::vector<bronx::db::InventoryItem> items;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            bronx::db::InventoryItem item{};
            item.item_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            item.item_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item.quantity = sqlite3_column_int(stmt, 2);
            item.level = sqlite3_column_int(stmt, 3);
            const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            item.metadata = meta ? meta : "";
            items.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
        return items;
    }

    // Quick item quantity check without full inventory fetch
    std::optional<int> get_cached_item_quantity(uint64_t user_id, const std::string& item_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT quantity, fetched_at FROM inventory_cache WHERE user_id = ? AND item_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_text(stmt, 2, item_id.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<int> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.inventory)) {
                result = sqlite3_column_int(stmt, 0);
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // =====================================================================
    // STATS
    // =====================================================================

    void cache_stat(uint64_t user_id, const std::string& stat_name, int64_t value) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = R"(
            INSERT OR REPLACE INTO stats_cache (user_id, stat_name, value, fetched_at)
            VALUES (?,?,?,?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_text(stmt, 2, stat_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, value);
        sqlite3_bind_int64(stmt, 4, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<int64_t> get_cached_stat(uint64_t user_id, const std::string& stat_name) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT value, fetched_at FROM stats_cache WHERE user_id = ? AND stat_name = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_text(stmt, 2, stat_name.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<int64_t> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.stats)) {
                result = sqlite3_column_int64(stmt, 0);
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // =====================================================================
    // SHOP ITEMS (global — same for all users)
    // =====================================================================

    void cache_shop_items(const std::vector<bronx::db::ShopItem>& items) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        int64_t ts = now_unix();
        const char* sql = R"(
            INSERT OR REPLACE INTO shop_cache
                (item_id, name, description, category, price, max_quantity,
                 required_level, level, usable, metadata, fetched_at)
            VALUES (?,?,?,?,?,?,?,?,?,?,?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        exec("BEGIN TRANSACTION");
        for (const auto& item : items) {
            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, item.item_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, item.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, item.description.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, item.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, item.price);
            sqlite3_bind_int(stmt, 6, item.max_quantity);
            sqlite3_bind_int(stmt, 7, item.required_level);
            sqlite3_bind_int(stmt, 8, item.level);
            sqlite3_bind_int(stmt, 9, item.usable ? 1 : 0);
            sqlite3_bind_text(stmt, 10, item.metadata.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 11, ts);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        exec("COMMIT");
    }

    std::optional<std::vector<bronx::db::ShopItem>> get_cached_shop_items() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        // Check freshness of first entry
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, "SELECT fetched_at FROM shop_cache LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int64_t fetched_at = sqlite3_column_int64(stmt, 0);
                    if (!is_fresh(fetched_at, ttl_.shop_items)) {
                        sqlite3_finalize(stmt);
                        return std::nullopt;
                    }
                } else {
                    sqlite3_finalize(stmt);
                    return std::nullopt;
                }
                sqlite3_finalize(stmt);
            }
        }
        const char* sql = "SELECT item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata FROM shop_cache";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

        std::vector<bronx::db::ShopItem> items;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            bronx::db::ShopItem item{};
            item.item_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.description = desc ? desc : "";
            const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item.category = cat ? cat : "";
            item.price = sqlite3_column_int64(stmt, 4);
            item.max_quantity = sqlite3_column_int(stmt, 5);
            item.required_level = sqlite3_column_int(stmt, 6);
            item.level = sqlite3_column_int(stmt, 7);
            item.usable = sqlite3_column_int(stmt, 8) != 0;
            const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            item.metadata = meta ? meta : "";
            items.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
        return items.empty() ? std::nullopt : std::optional(std::move(items));
    }

    std::optional<bronx::db::ShopItem> get_cached_shop_item(const std::string& item_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata, fetched_at FROM shop_cache WHERE item_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_text(stmt, 1, item_id.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<bronx::db::ShopItem> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 10);
            if (is_fresh(fetched_at, ttl_.shop_items)) {
                bronx::db::ShopItem item{};
                item.item_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                item.description = desc ? desc : "";
                const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                item.category = cat ? cat : "";
                item.price = sqlite3_column_int64(stmt, 4);
                item.max_quantity = sqlite3_column_int(stmt, 5);
                item.required_level = sqlite3_column_int(stmt, 6);
                item.level = sqlite3_column_int(stmt, 7);
                item.usable = sqlite3_column_int(stmt, 8) != 0;
                const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                item.metadata = meta ? meta : "";
                result = std::move(item);
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // =====================================================================
    // API RESPONSE CACHE (generic key → JSON blob)
    // =====================================================================

    void cache_api_response(const std::string& key, const std::string& json) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "INSERT OR REPLACE INTO api_cache (cache_key, response_json, fetched_at) VALUES (?,?,?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<std::string> get_cached_api_response(const std::string& key) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT response_json, fetched_at FROM api_cache WHERE cache_key = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<std::string> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.api_cache)) {
                const char* json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (json) result = std::string(json);
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // =====================================================================
    // INVALIDATION
    // =====================================================================

    void invalidate_user(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM users_cache WHERE user_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void invalidate_inventory(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM inventory_cache WHERE user_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void invalidate_stats(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM stats_cache WHERE user_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    void invalidate_shop() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        exec("DELETE FROM shop_cache");
    }

    void invalidate_api_cache(const std::string& key = "") {
        std::lock_guard<std::mutex> lk(db_mutex_);
        if (key.empty()) {
            exec("DELETE FROM api_cache");
        } else {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, "DELETE FROM api_cache WHERE cache_key = ?", -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Flush all cached data (used at shutdown or diagnostics)
    void flush_all() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        exec("DELETE FROM users_cache");
        exec("DELETE FROM inventory_cache");
        exec("DELETE FROM stats_cache");
        exec("DELETE FROM shop_cache");
        exec("DELETE FROM api_cache");
        exec("DELETE FROM fishing_gear_cache");
    }

    // =====================================================================
    // PRE-CHECK CACHE OPERATIONS — Sub-millisecond checks for every command
    // =====================================================================

    // --- BLACKLIST ---
    void cache_blacklist(uint64_t user_id, bool is_blacklisted) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = R"(
            INSERT OR REPLACE INTO blacklist_cache (user_id, is_blacklisted, fetched_at)
            VALUES (?, ?, ?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_int(stmt, 2, is_blacklisted ? 1 : 0);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<bool> is_blacklisted(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT is_blacklisted, fetched_at FROM blacklist_cache WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        
        std::optional<bool> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.blacklist)) {
                result = sqlite3_column_int(stmt, 0) != 0;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // --- WHITELIST ---
    void cache_whitelist(uint64_t user_id, bool is_whitelisted) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = R"(
            INSERT OR REPLACE INTO whitelist_cache (user_id, is_whitelisted, fetched_at)
            VALUES (?, ?, ?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_int(stmt, 2, is_whitelisted ? 1 : 0);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<bool> is_whitelisted(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT is_whitelisted, fetched_at FROM whitelist_cache WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        
        std::optional<bool> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.whitelist)) {
                result = sqlite3_column_int(stmt, 0) != 0;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // --- USER PREFIXES ---
    void cache_user_prefixes(uint64_t user_id, const std::vector<std::string>& prefixes) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        // Serialize prefixes to simple JSON array
        std::string json = "[";
        for (size_t i = 0; i < prefixes.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + prefixes[i] + "\"";
        }
        json += "]";
        
        const char* sql = R"(
            INSERT OR REPLACE INTO user_prefixes_cache (user_id, prefixes, fetched_at)
            VALUES (?, ?, ?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        sqlite3_bind_text(stmt, 2, json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<std::vector<std::string>> get_user_prefixes(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT prefixes, fetched_at FROM user_prefixes_cache WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
        
        std::optional<std::vector<std::string>> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.prefixes)) {
                const char* json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                result = parse_string_array_json(json ? json : "[]");
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // --- GUILD PREFIXES ---
    void cache_guild_prefixes(uint64_t guild_id, const std::vector<std::string>& prefixes) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        std::string json = "[";
        for (size_t i = 0; i < prefixes.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + prefixes[i] + "\"";
        }
        json += "]";
        
        const char* sql = R"(
            INSERT OR REPLACE INTO guild_prefixes_cache (guild_id, prefixes, fetched_at)
            VALUES (?, ?, ?)
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        sqlite3_bind_text(stmt, 2, json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now_unix());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::optional<std::vector<std::string>> get_guild_prefixes(uint64_t guild_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT prefixes, fetched_at FROM guild_prefixes_cache WHERE guild_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        
        std::optional<std::vector<std::string>> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.prefixes)) {
                const char* json = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                result = parse_string_array_json(json ? json : "[]");
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // --- GUILD TOGGLES (Bulk cache for module/command enable state) ---
    
    // Check if we have fresh toggle data for a guild
    bool has_fresh_guild_toggles(uint64_t guild_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT fetched_at FROM guild_toggles_meta WHERE guild_id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        
        bool fresh = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 0);
            fresh = is_fresh(fetched_at, ttl_.guild_toggles);
        }
        sqlite3_finalize(stmt);
        return fresh;
    }

    // Cache all toggle data for a guild (module defaults, module scopes, command defaults, command scopes)
    void cache_guild_toggles(uint64_t guild_id,
                             const std::unordered_map<std::string, bool>& module_defaults,
                             const std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>>& module_scopes,
                             const std::unordered_map<std::string, bool>& command_defaults,
                             const std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>>& command_scopes) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        int64_t now = now_unix();
        
        // Clear existing data for this guild
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "DELETE FROM guild_module_defaults WHERE guild_id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "DELETE FROM guild_module_scopes WHERE guild_id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "DELETE FROM guild_command_defaults WHERE guild_id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "DELETE FROM guild_command_scopes WHERE guild_id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        
        // Insert module defaults
        for (const auto& [module, enabled] : module_defaults) {
            const char* sql = R"(
                INSERT INTO guild_module_defaults (guild_id, module, enabled, fetched_at)
                VALUES (?, ?, ?, ?)
            )";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_bind_text(stmt, 2, module.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
                sqlite3_bind_int64(stmt, 4, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        
        // Insert module scopes
        for (const auto& [module, scopes] : module_scopes) {
            for (const auto& [scope_type, scope_id, enabled, exclusive] : scopes) {
                const char* sql = R"(
                    INSERT INTO guild_module_scopes (guild_id, module, scope_type, scope_id, enabled, exclusive, fetched_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                )";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                    sqlite3_bind_text(stmt, 2, module.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, scope_type.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(scope_id));
                    sqlite3_bind_int(stmt, 5, enabled ? 1 : 0);
                    sqlite3_bind_int(stmt, 6, exclusive ? 1 : 0);
                    sqlite3_bind_int64(stmt, 7, now);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
        
        // Insert command defaults
        for (const auto& [command, enabled] : command_defaults) {
            const char* sql = R"(
                INSERT INTO guild_command_defaults (guild_id, command, enabled, fetched_at)
                VALUES (?, ?, ?, ?)
            )";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_bind_text(stmt, 2, command.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
                sqlite3_bind_int64(stmt, 4, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
        
        // Insert command scopes
        for (const auto& [command, scopes] : command_scopes) {
            for (const auto& [scope_type, scope_id, enabled, exclusive] : scopes) {
                const char* sql = R"(
                    INSERT INTO guild_command_scopes (guild_id, command, scope_type, scope_id, enabled, exclusive, fetched_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                )";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                    sqlite3_bind_text(stmt, 2, command.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, scope_type.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(scope_id));
                    sqlite3_bind_int(stmt, 5, enabled ? 1 : 0);
                    sqlite3_bind_int(stmt, 6, exclusive ? 1 : 0);
                    sqlite3_bind_int64(stmt, 7, now);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        }
        
        // Update meta timestamp
        {
            const char* sql = "INSERT OR REPLACE INTO guild_toggles_meta (guild_id, fetched_at) VALUES (?, ?)";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
                sqlite3_bind_int64(stmt, 2, now);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }
    }

    // Get module default (returns nullopt if not cached)
    std::optional<bool> get_module_default(uint64_t guild_id, const std::string& module) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT enabled, fetched_at FROM guild_module_defaults WHERE guild_id = ? AND module = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        sqlite3_bind_text(stmt, 2, module.c_str(), -1, SQLITE_TRANSIENT);
        
        std::optional<bool> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.guild_toggles)) {
                result = sqlite3_column_int(stmt, 0) != 0;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Get command default (returns nullopt if not cached)
    std::optional<bool> get_command_default(uint64_t guild_id, const std::string& command) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        const char* sql = "SELECT enabled, fetched_at FROM guild_command_defaults WHERE guild_id = ? AND command = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        sqlite3_bind_text(stmt, 2, command.c_str(), -1, SQLITE_TRANSIENT);
        
        std::optional<bool> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched_at = sqlite3_column_int64(stmt, 1);
            if (is_fresh(fetched_at, ttl_.guild_toggles)) {
                result = sqlite3_column_int(stmt, 0) != 0;
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Get all module scopes for a guild/module (for in-memory evaluation)
    struct ScopeEntry { std::string scope_type; uint64_t scope_id; bool enabled; bool exclusive; };
    std::vector<ScopeEntry> get_module_scopes(uint64_t guild_id, const std::string& module) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        std::vector<ScopeEntry> result;
        const char* sql = "SELECT scope_type, scope_id, enabled, exclusive FROM guild_module_scopes WHERE guild_id = ? AND module = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        sqlite3_bind_text(stmt, 2, module.c_str(), -1, SQLITE_TRANSIENT);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScopeEntry e;
            const char* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            e.scope_type = st ? st : "";
            e.scope_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
            e.enabled = sqlite3_column_int(stmt, 2) != 0;
            e.exclusive = sqlite3_column_int(stmt, 3) != 0;
            result.push_back(e);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Get all command scopes for a guild/command
    std::vector<ScopeEntry> get_command_scopes(uint64_t guild_id, const std::string& command) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        std::vector<ScopeEntry> result;
        const char* sql = "SELECT scope_type, scope_id, enabled, exclusive FROM guild_command_scopes WHERE guild_id = ? AND command = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
        sqlite3_bind_text(stmt, 2, command.c_str(), -1, SQLITE_TRANSIENT);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScopeEntry e;
            const char* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            e.scope_type = st ? st : "";
            e.scope_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
            e.enabled = sqlite3_column_int(stmt, 2) != 0;
            e.exclusive = sqlite3_column_int(stmt, 3) != 0;
            result.push_back(e);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Evaluate module enabled status locally (mirrors CacheManager::GuildToggleData::is_module_enabled)
    bool evaluate_module_enabled(uint64_t guild_id, const std::string& module,
                                 uint64_t user_id, uint64_t channel_id,
                                 const std::vector<uint64_t>& roles) {
        auto scopes = get_module_scopes(guild_id, module);
        
        // 1) Check exclusive
        for (const auto& e : scopes) {
            if (e.exclusive && e.enabled) {
                if (e.scope_type == "user" && user_id == e.scope_id) return true;
                if (e.scope_type == "channel" && channel_id == e.scope_id) return true;
                if (e.scope_type == "role") {
                    for (uint64_t r : roles) { if (r == e.scope_id) return true; }
                }
                return false; // exclusive exists but caller doesn't match
            }
        }
        
        // 2) User override
        if (user_id != 0) {
            for (const auto& e : scopes) {
                if (e.scope_type == "user" && e.scope_id == user_id) return e.enabled;
            }
        }
        
        // 3) Channel override
        if (channel_id != 0) {
            for (const auto& e : scopes) {
                if (e.scope_type == "channel" && e.scope_id == channel_id) return e.enabled;
            }
        }
        
        // 4) Role overrides
        for (uint64_t r : roles) {
            for (const auto& e : scopes) {
                if (e.scope_type == "role" && e.scope_id == r) return e.enabled;
            }
        }
        
        // 5) Guild default
        auto def = get_module_default(guild_id, module);
        if (def) return *def;
        
        return true; // enabled by default
    }

    // Evaluate command enabled status locally
    bool evaluate_command_enabled(uint64_t guild_id, const std::string& command,
                                  uint64_t user_id, uint64_t channel_id,
                                  const std::vector<uint64_t>& roles) {
        auto scopes = get_command_scopes(guild_id, command);
        
        // 1) Check exclusive
        for (const auto& e : scopes) {
            if (e.exclusive && e.enabled) {
                if (e.scope_type == "user" && user_id == e.scope_id) return true;
                if (e.scope_type == "channel" && channel_id == e.scope_id) return true;
                if (e.scope_type == "role") {
                    for (uint64_t r : roles) { if (r == e.scope_id) return true; }
                }
                return false;
            }
        }
        
        // 2) User override
        if (user_id != 0) {
            for (const auto& e : scopes) {
                if (e.scope_type == "user" && e.scope_id == user_id) return e.enabled;
            }
        }
        
        // 3) Channel override
        if (channel_id != 0) {
            for (const auto& e : scopes) {
                if (e.scope_type == "channel" && e.scope_id == channel_id) return e.enabled;
            }
        }
        
        // 4) Role overrides
        for (uint64_t r : roles) {
            for (const auto& e : scopes) {
                if (e.scope_type == "role" && e.scope_id == r) return e.enabled;
            }
        }
        
        // 5) Guild default
        auto def = get_command_default(guild_id, command);
        if (def) return *def;
        
        return true; // enabled by default
    }

    // Invalidate all toggle data for a guild (call when settings change via dashboard)
    void invalidate_guild_toggles(uint64_t guild_id) {
        std::lock_guard<std::mutex> lk(db_mutex_);
        sqlite3_stmt* stmt = nullptr;
        
        if (sqlite3_prepare_v2(db_, "DELETE FROM guild_module_defaults WHERE guild_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db_, "DELETE FROM guild_module_scopes WHERE guild_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db_, "DELETE FROM guild_command_defaults WHERE guild_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db_, "DELETE FROM guild_command_scopes WHERE guild_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db_, "DELETE FROM guild_toggles_meta WHERE guild_id = ?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(guild_id));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Cleanup expired entries (call periodically)
    void cleanup_expired() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        int64_t now = now_unix();
        char buf[256];
        snprintf(buf, sizeof(buf), "DELETE FROM users_cache WHERE fetched_at < %lld",
                 (long long)(now - ttl_.user_data));
        exec(buf);
        snprintf(buf, sizeof(buf), "DELETE FROM inventory_cache WHERE fetched_at < %lld",
                 (long long)(now - ttl_.inventory));
        exec(buf);
        snprintf(buf, sizeof(buf), "DELETE FROM stats_cache WHERE fetched_at < %lld",
                 (long long)(now - ttl_.stats));
        exec(buf);
        snprintf(buf, sizeof(buf), "DELETE FROM shop_cache WHERE fetched_at < %lld",
                 (long long)(now - ttl_.shop_items));
        exec(buf);
        snprintf(buf, sizeof(buf), "DELETE FROM api_cache WHERE fetched_at < %lld",
                 (long long)(now - ttl_.api_cache));
        exec(buf);
        // Clean expired cooldowns
        snprintf(buf, sizeof(buf), "DELETE FROM local_cooldowns WHERE expires_at < %lld",
                 (long long)now);
        exec(buf);
    }

    // Get cache statistics for monitoring
    struct CacheStats {
        int user_entries;
        int inventory_entries;
        int stat_entries;
        int shop_entries;
        int api_entries;
        int pending_wallet_deltas;
        int pending_bank_deltas;
    };

    CacheStats get_stats() {
        std::lock_guard<std::mutex> lk(db_mutex_);
        CacheStats s{};
        auto count = [this](const char* table) -> int {
            char buf[128];
            snprintf(buf, sizeof(buf), "SELECT COUNT(*) FROM %s", table);
            sqlite3_stmt* stmt = nullptr;
            int result = 0;
            if (sqlite3_prepare_v2(db_, buf, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    result = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
            return result;
        };
        s.user_entries = count("users_cache");
        s.inventory_entries = count("inventory_cache");
        s.stat_entries = count("stats_cache");
        s.shop_entries = count("shop_cache");
        s.api_entries = count("api_cache");
        s.pending_wallet_deltas = count("pending_wallet_deltas");
        s.pending_bank_deltas = count("pending_bank_deltas");
        return s;
    }

private:
    // Internal helper to get pending delta without locking (caller must hold lock)
    int64_t get_pending_wallet_delta_unlocked(uint64_t user_id) {
        const char* sql = "SELECT delta FROM pending_wallet_deltas WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        int64_t delta = 0;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                delta = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return delta;
    }

    int64_t get_pending_bank_delta_unlocked(uint64_t user_id) {
        const char* sql = "SELECT delta FROM pending_bank_deltas WHERE user_id = ?";
        sqlite3_stmt* stmt = nullptr;
        int64_t delta = 0;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                delta = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return delta;
    }

    // Helper to bind and exec a simple 2-param int64 statement
    void exec_bind(const char* sql, int64_t p1, int64_t p2) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, p1);
            sqlite3_bind_int64(stmt, 2, p2);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Parse a simple JSON string array: ["a", "b", "c"] -> vector<string>
    static std::vector<std::string> parse_string_array_json(const std::string& json) {
        std::vector<std::string> result;
        size_t pos = 0;
        while ((pos = json.find('"', pos)) != std::string::npos) {
            size_t start = pos + 1;
            size_t end = json.find('"', start);
            if (end == std::string::npos) break;
            result.push_back(json.substr(start, end - start));
            pos = end + 1;
        }
        return result;
    }
};

} // namespace local
} // namespace bronx
