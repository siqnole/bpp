#include "database.h"
#include "connection_pool.h"
#include "migrations.h"
#include "../commands/market_state.h" 
#include "../operations/user/user_operations.h"
#include "../operations/user/cooldown_operations.h"
#include "../operations/economy/inventory_operations.h"
#include "../operations/leveling/leaderboard_operations.h"
#include "../operations/moderation/guild_settings_operations.h"
#include "../utils/database_utility.h"
#include <iostream>
#include <cstring>
#include <climits>
#include <stdexcept>
#include <chrono>
#include "../../utils/logger.h"
#include "../log.h"

namespace bronx {
namespace db {

// ============================================================================
// DATABASE IMPLEMENTATION
// ============================================================================

Database::Database(const DatabaseConfig& config)
    : config_(config), connection_debug_(config.log_connections) {
}

Database::~Database() {
    disconnect();
}

bool Database::connect() {
    if (connected_) return true;
    
    try {
        pool_ = std::make_unique<ConnectionPool>(config_);
        // propagate any debug flag which may have been set before connect
        ConnectionPool::set_verbose_logging(connection_debug_);
        // since the pool no longer pre‑creates connections, test by grabbing one
        auto test_conn = pool_->acquire();
        connected_ = (test_conn != nullptr);
        if (connected_) {
            bronx::logger::success("database", "connection established successfully");
            // release immediately (shared_ptr destructor closes it)
            test_conn.reset();

            // ============================================================
            // SCHEMA MIGRATIONS — batched onto a single connection for speed
            // ============================================================
            auto migration_start = std::chrono::steady_clock::now();
            std::vector<std::string> migrations = bronx::db::get_schema_migrations();

            // Run all migrations on a single connection for speed
            int ok = execute_batch(migrations);
            auto migration_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - migration_start);
            bronx::logger::info("database", "schema migrations applied: " + std::to_string(ok) + "/" + std::to_string(migrations.size()) + " in " + std::to_string(migration_elapsed.count()) + "ms");

            // end migration
            // ------------------------------------------------------------------
        }
        
        return connected_;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        bronx::logger::error("database", "connection failed: " + std::string(e.what()));
        return false;
    }
}

void Database::disconnect() {
    pool_.reset();
    connected_ = false;
}

bool Database::is_connected() const {
    return connected_;
}

void Database::set_inventory_debug(bool on) {
    inventory_debug_ = on;
}

bool Database::get_inventory_debug() const {
    return inventory_debug_;
}

void Database::set_connection_debug(bool on) {
    connection_debug_ = on;
    // if a pool already exists, update its global flag as well
    if (pool_) {
        ConnectionPool::set_verbose_logging(on);
    }
}

bool Database::get_connection_debug() const {
    return connection_debug_;
}

// ========================================
// FISHING GEAR OPERATIONS
// ========================================

std::pair<std::string, std::string> Database::get_active_fishing_gear(uint64_t user_id, uint64_t guild_id) {
    // retrieve the currently equipped rod and bait for the user
    // earlier versions of this function repeatedly crashed for a certain
    // problematic user due to invalid connections or a corrupt database row.
    // we now wrap the entire sequence in error handling, check pool acquisition,
    // and perform validation of returned item ids.  if the row contains an
    // unrecognized item, we automatically clear it to avoid future problems.

    std::pair<std::string,std::string> gear{
        "",""
    };
    // acquire connection separately to limit scope of exception logging
    std::shared_ptr<Connection> conn;
    try {
        conn = pool_->acquire();
    } catch (const std::exception &e) {
        bronx::logger::warn("database", "get_active_fishing_gear: pool acquire threw: " + std::string(e.what()));
        // treat as unequipped
        return gear;
    } catch (...) {
        bronx::logger::warn("database", "get_active_fishing_gear: pool acquire threw unknown exception");
        return gear;
    }
    if (!conn) {
        log_error("get_active_fishing_gear acquire returned null");
        return gear;
    }
    
    try {
        const char* query =
            "SELECT active_rod_id, active_bait_id FROM user_fishing_gear WHERE user_id = ? AND guild_id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (!stmt) {
            log_error("get_active_fishing_gear init stmt");
            pool_->release(conn);
            return gear;
        }
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            log_error("get_active_fishing_gear prepare");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return gear;
        }
        MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&guild_id;
        bind[1].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        if (mysql_stmt_execute(stmt) != 0) {
            log_error("get_active_fishing_gear execute");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return gear;
        }
        MYSQL_BIND result[2]; memset(result, 0, sizeof(result));
        char rod_buf[101]; unsigned long rod_len;
        char bait_buf[101]; unsigned long bait_len;
        my_bool rod_null = false, bait_null = false;
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = rod_buf;
        result[0].buffer_length = sizeof(rod_buf);
        result[0].is_null = &rod_null;
        result[0].length = &rod_len;
        result[1].buffer_type = MYSQL_TYPE_STRING;
        result[1].buffer = bait_buf;
        result[1].buffer_length = sizeof(bait_buf);
        result[1].is_null = &bait_null;
        result[1].length = &bait_len;
        mysql_stmt_bind_result(stmt, result);
        if (mysql_stmt_fetch(stmt) == 0) {
            if (!rod_null) gear.first = std::string(rod_buf, rod_len);
            if (!bait_null) gear.second = std::string(bait_buf, bait_len);
        }
        mysql_stmt_close(stmt);
        pool_->release(conn);

        // validate the ids against the shop catalog; if an id is no longer
        // valid we clear it (both in-memory and in the database) to avoid
        // repeated errors for the same user.  this handles the "corrupt row"
        // scenario mentioned in earlier comments.
        if (!gear.first.empty() && !get_shop_item(gear.first)) {
            bronx::logger::warn("database", "get_active_fishing_gear: invalid rod id '" + gear.first + "' for user " + std::to_string(user_id) + ", clearing");
            set_active_rod(user_id, "", guild_id);
            gear.first.clear();
        }
        if (!gear.second.empty() && !get_shop_item(gear.second)) {
            bronx::logger::warn("database", "get_active_fishing_gear: invalid bait id '" + gear.second + "' for user " + std::to_string(user_id) + ", clearing");
            set_active_bait(user_id, "", guild_id);
            gear.second.clear();
        }

        return gear;
    } catch (const std::exception& e) {
        bronx::logger::error("database", "get_active_fishing_gear exception: " + std::string(e.what()));
        return gear;
    }
}


bool Database::set_active_rod(uint64_t user_id, const std::string& rod_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("set_active_rod acquire failed");
        return false;
    }
    
    // Use UPSERT to avoid race conditions between UPDATE and INSERT
    const char* upsert_q = 
        "INSERT INTO user_fishing_gear (user_id, guild_id, active_rod_id) VALUES (?, ?, ?) "
        "ON DUPLICATE KEY UPDATE active_rod_id = VALUES(active_rod_id)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        log_error("set_active_rod stmt init failed");
        pool_->release(conn);
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, upsert_q, strlen(upsert_q)) != 0) {
        std::string err = mysql_stmt_error(stmt);
        log_error("set_active_rod prepare: " + err);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)rod_id.c_str();
    bind[2].buffer_length = rod_id.length();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    
    if (!success) {
        std::string err = mysql_stmt_error(stmt);
        log_error("set_active_rod execute: " + err);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::set_active_bait(uint64_t user_id, const std::string& bait_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("set_active_bait acquire failed");
        return false;
    }
    const char* upsert_q =
        "INSERT INTO user_fishing_gear (user_id, guild_id, active_bait_id) VALUES (?, ?, ?) "
        "ON DUPLICATE KEY UPDATE active_bait_id = VALUES(active_bait_id)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, upsert_q, strlen(upsert_q)) != 0) {
        log_error("set_active_bait upsert prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)bait_id.c_str();
    bind[2].buffer_length = bait_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("set_active_bait upsert execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}


// record anonymous fishing log data for ML
bool Database::record_fishing_log(int rod_level, int bait_level, int64_t net_profit) {
    auto conn = pool_->acquire();
    const char* insert_q =
        "INSERT INTO fishing_logs (rod_level, bait_level, net_profit) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, insert_q, strlen(insert_q)) != 0) {
        log_error("record_fishing_log prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&rod_level;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&bait_level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&net_profit;
    bind[2].is_unsigned = 0;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("record_fishing_log execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}


bool Database::tune_bait_prices_from_logs(int min_samples) {
    // optional scale factor controlled via ml_settings
    double scale = 1.0;
    if (auto s = get_ml_setting("tune_scale"); s && !s->empty()) {
        try { scale = std::stod(*s); } catch(...) { }
    }
    // Log which market regime is driving this tune cycle so mlstatus can
    // show it and so the audit trail in ml_price_changes is meaningful.
    // The classifier writes "market_regime" to ml_settings before this
    // function runs; if it hasn't run yet the key will be absent and we
    // record "UNKNOWN" to make the gap visible in the owner panel.
    {
        auto regime_val = get_ml_setting("market_regime");
        set_ml_setting("last_tune_regime", regime_val ? *regime_val : "UNKNOWN");
    }
    // optional price bounds (global or per-bait-level)
    auto parse_bound = [&](const std::string &key, int level, int64_t def)->int64_t {
        int64_t result = def;
        if (auto s = get_ml_setting(key); s && !s->empty()) {
            try { result = std::stoll(*s); } catch(...) { }
        }
        // try level-specific key if not equal to def or simply override
        std::string lvlkey = key + "_" + std::to_string(level);
        if (auto s2 = get_ml_setting(lvlkey); s2 && !s2->empty()) {
            try { result = std::stoll(*s2); } catch(...) { }
        }
        return result;
    };
    int64_t price_min = parse_bound("bait_price_min", 0, 1);
    int64_t price_max = parse_bound("bait_price_max", 0, LLONG_MAX);
    auto conn = pool_->acquire();
    const char* sel = "SELECT bait_level, AVG(net_profit), COUNT(*) FROM fishing_logs GROUP BY bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("tune_bait_prices select prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("tune_bait_prices select execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND res[3];
    memset(res, 0, sizeof(res));
    int bait_level;
    double avg_profit;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_DOUBLE;
    res[1].buffer = (char*)&avg_profit;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    // optional baseline profit target; adjustments are computed against this
    double profit_target = 0.0;
    if (auto t = get_ml_setting("tune_target"); t && !t->empty()) {
        try { profit_target = std::stod(*t); } catch(...) { }
    }

    // optional fixed decay applied after all individual adjustments
    int64_t fixed_decay = 0;
    if (auto d = get_ml_setting("tune_decay"); d && !d->empty()) {
        try { fixed_decay = std::stoll(*d); } catch(...) { }
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        if (cnt < min_samples) continue; // not enough data to trust
        // compute profit relative to target
        double profit = avg_profit - profit_target;
        if (profit == 0.0 && fixed_decay == 0) continue; // nothing to do

        // apply logarithmic scaling so huge profits move more slowly
        double adj_base;
        if (profit >= 0) adj_base = log(profit + 1.0);
        else adj_base = -log(-profit + 1.0);

        int64_t adjust = (int64_t)(adj_base / 10.0 * scale);
        if (adjust == 0) {
            // even if adjust rounds to zero, we might still want a fixed decay;
            if (fixed_decay == 0) continue;
        }
        // recalc per-level bounds (global may be overridden)
        int64_t min_for_level = parse_bound("bait_price_min", bait_level, price_min);
        int64_t max_for_level = parse_bound("bait_price_max", bait_level, price_max);
        std::string upd = "UPDATE shop_items SET price = LEAST("
                          + std::to_string(max_for_level) + ", GREATEST("
                          + std::to_string(min_for_level) + ", price + " + std::to_string(adjust) + "))"
                          " WHERE category='bait' AND level=" + std::to_string(bait_level);
        execute(upd.c_str());
        // record change history
        std::string logq = "INSERT INTO ml_price_changes (bait_level,adjust) VALUES (" +
                           std::to_string(bait_level) + "," + std::to_string(adjust) + ")";
        execute(logq.c_str());
    }

    // apply fixed decay across all bait levels (clamped to zero)
    if (fixed_decay > 0) {
        std::string decay_q = "UPDATE shop_items SET price = GREATEST(price - " +
                              std::to_string(fixed_decay) + ", 0) WHERE category='bait'";
        execute(decay_q.c_str());
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return true;
}

std::string Database::get_bait_tuning_report(int min_samples) {
    std::string report;
    auto conn = pool_->acquire();
    const char* sel = "SELECT bait_level, AVG(net_profit), COUNT(*) FROM fishing_logs GROUP BY bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_bait_tuning_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error preparing report)";
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_bait_tuning_report execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error executing report)";
    }
    MYSQL_BIND res[3];
    memset(res, 0, sizeof(res));
    int bait_level;
    double avg_profit;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_DOUBLE;
    res[1].buffer = (char*)&avg_profit;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    report += "level | samples | avg profit | suggested change\n";
    report += "-----+---------+------------+----------------\n";
    while (mysql_stmt_fetch(stmt) == 0) {
        report += std::to_string(bait_level) + "     | ";
        report += std::to_string(cnt) + "       | ";
        report += std::to_string((int64_t)avg_profit) + "       | ";
        if (cnt < min_samples) report += "(insufficient)";
        else {
            int64_t adjust = (int64_t)(avg_profit / 10.0);
            if (adjust>0) report += "+" + std::to_string(adjust);
            else if (adjust<0) report += std::to_string(adjust);
            else report += "(none)";
        }
        report += "\n";
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return report;
}

// produce a human-readable report of price adjustments over the past N hours
std::string Database::get_ml_effect_report(int hours) {
    std::string report;
    auto conn = pool_->acquire();
    // join with shop_items to get bait name for each level
    const char* sel =
        "SELECT m.bait_level, COALESCE(s.name, CONCAT('level',m.bait_level)), SUM(m.adjust), COUNT(*) "
        "FROM ml_price_changes m "
        "LEFT JOIN shop_items s ON s.category='bait' AND s.level=m.bait_level "
        "WHERE m.changed_at >= DATE_SUB(NOW(), INTERVAL ? HOUR) "
        "GROUP BY m.bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_ml_effect_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error preparing report)";
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&hours;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_ml_effect_report execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error executing report)";
    }
    MYSQL_BIND res[4]; memset(res,0,sizeof(res));
    int bait_level;
    char namebuf[128]; unsigned long namelen = 0; my_bool name_null = false;
    long long sum_adjust;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_STRING;
    res[1].buffer = namebuf;
    res[1].buffer_length = sizeof(namebuf);
    res[1].is_null = &name_null;
    res[1].length = &namelen;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&sum_adjust;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG;
    res[3].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    report += "bait   | adjustments | total change\n";
    report += "-------+-------------+--------------\n";
    while (mysql_stmt_fetch(stmt) == 0) {
        std::string name = name_null ? ("level" + std::to_string(bait_level)) : std::string(namebuf, namelen);
        report += name + " | ";
        report += std::to_string(cnt) + "           | ";
        report += (sum_adjust>=0?"+":"") + std::to_string(sum_adjust) + "\n";
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return report;
}

std::string Database::classify_market_state(int min_samples) {
    auto result = bronx::market::MarketStateClassifier::classify(this, min_samples);
 
    std::string out;
    out += std::string(bronx::market::regime_emoji(result.regime))
        + " **"
        + bronx::market::regime_name(result.regime)
        + "**";
 
    if (result.regime_changed) {
        out += "  *(changed from " + result.previous_regime_name + ")*";
    }
    out += "\n";
    out += result.notes + "\n";
 
    if (result.total_samples == 0) {
        out += "⚠️ no qualifying samples — run more fishing first\n";
    }
 
    return out;
}
 
std::string Database::get_market_state_report() {
    return bronx::market::MarketStateClassifier::build_report(this);
}

// ml settings support
std::optional<std::string> Database::get_ml_setting(const std::string& key) {
    auto conn = pool_->acquire();
    const char* sel = "SELECT `value` FROM ml_settings WHERE `key` = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_ml_setting execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    char buf[1024]; unsigned long len; my_bool isnull=false;
    res[0].buffer_type = MYSQL_TYPE_STRING;
    res[0].buffer = buf;
    res[0].buffer_length = sizeof(buf);
    res[0].is_null = &isnull;
    res[0].length = &len;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) == 0 && !isnull) {
        std::string val(buf,len);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return val;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return std::nullopt;
}

bool Database::set_ml_setting(const std::string& key, const std::string& value) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO ml_settings (`key`,`value`) VALUES (?,?) ON DUPLICATE KEY UPDATE `value`=VALUES(`value`)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("set_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)value.c_str();
    bind[1].buffer_length = value.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt)==0);
    if (!success) log_error("set_ml_setting execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_ml_setting(const std::string& key) {
    auto conn = pool_->acquire();
    const char* q = "DELETE FROM ml_settings WHERE `key` = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("delete_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt)==0);
    if (!success) log_error("delete_ml_setting execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::pair<std::string,std::string>> Database::list_ml_settings() {
    std::vector<std::pair<std::string,std::string>> out;
    auto conn = pool_->acquire();
    const char* q = "SELECT `key`,`value` FROM ml_settings";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("list_ml_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("list_ml_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND res[2]; memset(res,0,sizeof(res));
    char kbuf[64]; unsigned long klen;
    char vbuf[1024]; unsigned long vlen;
    res[0].buffer_type = MYSQL_TYPE_STRING; res[0].buffer = kbuf; res[0].buffer_length = sizeof(kbuf); res[0].length=&klen;
    res[1].buffer_type = MYSQL_TYPE_STRING; res[1].buffer = vbuf; res[1].buffer_length = sizeof(vbuf); res[1].length=&vlen;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(std::string(kbuf,klen), std::string(vbuf,vlen));
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// lottery entry helpers
bool Database::update_lottery_tickets(uint64_t user_id, int64_t tickets) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO lottery_entries (user_id, tickets) VALUES (?,?) "
                    "ON DUPLICATE KEY UPDATE tickets = tickets + VALUES(tickets)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("update_lottery_tickets prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&tickets;
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("update_lottery_tickets execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

int64_t Database::get_lottery_user_count() {
    auto conn = pool_->acquire();
    const char* q = "SELECT COUNT(*) FROM lottery_entries";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_lottery_user_count prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_lottery_user_count execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    int64_t count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) != 0) count = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

int64_t Database::get_lottery_total_tickets() {
    auto conn = pool_->acquire();
    const char* q = "SELECT COALESCE(SUM(tickets),0) FROM lottery_entries";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_lottery_total_tickets prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_lottery_total_tickets execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    int64_t sum = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&sum;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) != 0) sum = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return sum;
}

// ============================================================================
// AUTOFISHER OPERATIONS
// ============================================================================

bool Database::has_autofisher(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("has_autofisher acquire failed");
        return false;
    }
    const char* q = "SELECT count FROM user_autofishers WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("has_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("has_autofisher execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    
    bool has = (mysql_stmt_fetch(stmt) == 0 && count > 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return has;
}

bool Database::create_autofisher(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("create_autofisher acquire failed");
        return false;
    }
    const char* q = "INSERT INTO user_autofishers (user_id, guild_id, count, active) VALUES (?, ?, 1, FALSE) "
                    "ON DUPLICATE KEY UPDATE count = count + 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("create_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("create_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::upgrade_autofisher_efficiency(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("upgrade_autofisher_efficiency acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET efficiency_level = efficiency_level + 1, "
                    "efficiency_multiplier = 1.00 + (efficiency_level + 1) * 0.10 "
                    "WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("upgrade_autofisher_efficiency prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("upgrade_autofisher_efficiency execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int64_t Database::get_autofisher_balance(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_autofisher_balance acquire failed");
        return 0;
    }
    const char* q = "SELECT balance FROM user_autofishers WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_balance prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_balance execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int64_t balance = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&balance;
    mysql_stmt_bind_result(stmt, res);
    
    if (mysql_stmt_fetch(stmt) != 0) balance = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return balance;
}

bool Database::deposit_to_autofisher(uint64_t user_id, int64_t amount, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("deposit_to_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET balance = balance + ?, total_deposited = total_deposited + ? "
                    "WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("deposit_to_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[4]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&amount;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[2].is_unsigned = 1;
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&guild_id;
    bind[3].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("deposit_to_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::withdraw_from_autofisher(uint64_t user_id, int64_t amount, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("withdraw_from_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET balance = balance - ? "
                    "WHERE user_id = ? AND guild_id = ? AND balance >= ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("withdraw_from_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[4]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&guild_id;
    bind[2].is_unsigned = 1;
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&amount;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0);
    if (!success) log_error("withdraw_from_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::pair<uint64_t,uint64_t>> Database::get_all_active_autofishers() {
    std::vector<std::pair<uint64_t,uint64_t>> result;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_all_active_autofishers acquire failed");
        return result;
    }
    const char* q = "SELECT user_id, guild_id FROM user_autofishers WHERE active = TRUE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_all_active_autofishers prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_active_autofishers execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    MYSQL_BIND res[2]; memset(res, 0, sizeof(res));
    uint64_t user_id = 0;
    uint64_t guild_id = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&user_id;
    res[0].is_unsigned = 1;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG;
    res[1].buffer = (char*)&guild_id;
    res[1].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, res);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        result.push_back({user_id, guild_id});
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::activate_autofisher(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("activate_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET active = TRUE WHERE user_id = ? AND guild_id = ? AND count > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("activate_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("activate_autofisher execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    bool success = (mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::deactivate_autofisher(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("deactivate_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET active = FALSE WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("deactivate_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("deactivate_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int Database::get_autofisher_tier(uint64_t user_id, uint64_t guild_id) {
    // Determine tier from inventory (auto_fisher_1, auto_fisher_2 items)
    // Tier 2 > Tier 1, return highest tier available
    if (has_item(user_id, "auto_fisher_2", 1)) return 2;
    if (has_item(user_id, "auto_fisher_1", 1)) return 1;
    return 0;
}

bool Database::update_autofisher_last_run(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("update_autofisher_last_run acquire failed");
        return false;
    }
    const char* q = "UPDATE user_autofishers SET last_claim = NOW() WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("update_autofisher_last_run prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("update_autofisher_last_run execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<std::chrono::system_clock::time_point> Database::get_autofisher_last_run(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_autofisher_last_run acquire failed");
        return {};
    }
    const char* q = "SELECT UNIX_TIMESTAMP(last_claim) FROM user_autofishers WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_last_run prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_last_run execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int64_t timestamp = 0;
    my_bool is_null = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&timestamp;
    res[0].is_null = &is_null;
    mysql_stmt_bind_result(stmt, res);
    
    std::optional<std::chrono::system_clock::time_point> result;
    if (mysql_stmt_fetch(stmt) == 0 && !is_null && timestamp > 0) {
        result = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ============================================================================
// AUTOMINER OPERATIONS
// ============================================================================

std::vector<uint64_t> Database::get_all_active_autominers() {
    std::vector<uint64_t> result;
    auto conn = pool_->acquire();
    if (!conn) { log_error("get_all_active_autominers acquire"); return result; }
    const char* q = "SELECT user_id FROM user_autominers WHERE active = TRUE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_all_active_autominers prepare");
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_active_autominers execute");
        mysql_stmt_close(stmt); pool_->release(conn); return result;
    }
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    uint64_t uid = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&uid;
    res[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, res);
    while (mysql_stmt_fetch(stmt) == 0) result.push_back(uid);
    mysql_stmt_close(stmt); pool_->release(conn);
    return result;
}

bool Database::update_autominer_last_run(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("update_autominer_last_run acquire"); return false; }
    const char* q = "UPDATE user_autominers SET last_run = NOW() WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("update_autominer_last_run prepare");
        if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&user_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("update_autominer_last_run execute");
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

std::optional<std::chrono::system_clock::time_point> Database::get_autominer_last_run(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("get_autominer_last_run acquire"); return {}; }
    const char* q = "SELECT UNIX_TIMESTAMP(last_run) FROM user_autominers WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autominer_last_run prepare");
        if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return {};
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&user_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autominer_last_run execute");
        mysql_stmt_close(stmt); pool_->release(conn); return {};
    }
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int64_t ts = 0;
    my_bool is_null = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = (char*)&ts; res[0].is_null = &is_null;
    mysql_stmt_bind_result(stmt, res);
    std::optional<std::chrono::system_clock::time_point> result;
    if (mysql_stmt_fetch(stmt) == 0 && !is_null && ts > 0)
        result = std::chrono::system_clock::from_time_t(ts);
    mysql_stmt_close(stmt); pool_->release(conn);
    return result;
}



std::optional<AutofisherConfig> Database::get_autofisher_config(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("get_autofisher_config acquire"); return {}; }
    const char* q =
        "SELECT active, af_rod_id, af_bait_id, af_bait_qty, af_bait_level, af_bait_meta, "
        "max_bank_draw, auto_sell, as_trigger, as_threshold, bag_limit "
        "FROM user_autofishers WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_config prepare");
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &user_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &guild_id; bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_config execute");
        mysql_stmt_close(stmt); pool_->release(conn); return {};
    }

    char rod_buf[101]={}, bait_buf[101]={}, trigger_buf[16]={}, bait_meta_buf[4096]={};
    unsigned long rod_len=0, bait_len=0, trigger_len=0, meta_len=0;
    my_bool rod_null=1, bait_null=1, trigger_null=1, meta_null=1;
    int8_t active_v=0, auto_sell_v=0;
    int bait_qty=0, bait_level=1, bag_limit=10;
    int64_t max_bank_draw=0, as_threshold=0;

    MYSQL_BIND br[11]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_TINY;   br[0].buffer=&active_v;
    br[1].buffer_type=MYSQL_TYPE_STRING; br[1].buffer=rod_buf;   br[1].buffer_length=sizeof(rod_buf);   br[1].is_null=&rod_null;     br[1].length=&rod_len;
    br[2].buffer_type=MYSQL_TYPE_STRING; br[2].buffer=bait_buf;  br[2].buffer_length=sizeof(bait_buf);  br[2].is_null=&bait_null;    br[2].length=&bait_len;
    br[3].buffer_type=MYSQL_TYPE_LONG;   br[3].buffer=&bait_qty;
    br[4].buffer_type=MYSQL_TYPE_LONG;   br[4].buffer=&bait_level;
    br[5].buffer_type=MYSQL_TYPE_STRING; br[5].buffer=bait_meta_buf; br[5].buffer_length=sizeof(bait_meta_buf); br[5].is_null=&meta_null; br[5].length=&meta_len;
    br[6].buffer_type=MYSQL_TYPE_LONGLONG; br[6].buffer=&max_bank_draw;
    br[7].buffer_type=MYSQL_TYPE_TINY;   br[7].buffer=&auto_sell_v;
    br[8].buffer_type=MYSQL_TYPE_STRING; br[8].buffer=trigger_buf; br[8].buffer_length=sizeof(trigger_buf); br[8].is_null=&trigger_null; br[8].length=&trigger_len;
    br[9].buffer_type=MYSQL_TYPE_LONGLONG; br[9].buffer=&as_threshold;
    br[10].buffer_type=MYSQL_TYPE_LONG;  br[10].buffer=&bag_limit;
    mysql_stmt_bind_result(stmt, br);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return {};
    }
    AutofisherConfig cfg;
    cfg.guild_id      = guild_id;
    cfg.active        = (active_v != 0);
    cfg.tier          = get_autofisher_tier(user_id, guild_id);
    cfg.af_rod_id     = rod_null  ? "" : std::string(rod_buf,  rod_len);
    cfg.af_bait_id    = bait_null ? "" : std::string(bait_buf, bait_len);
    cfg.af_bait_qty   = bait_qty;
    cfg.af_bait_level = bait_level;
    cfg.af_bait_meta  = meta_null ? "" : std::string(bait_meta_buf, meta_len);
    cfg.max_bank_draw = max_bank_draw;
    cfg.auto_sell     = (auto_sell_v != 0);
    cfg.as_trigger    = trigger_null ? "bag" : std::string(trigger_buf, trigger_len);
    cfg.as_threshold  = as_threshold;
    cfg.bag_limit     = bag_limit;
    mysql_stmt_close(stmt); pool_->release(conn);
    return cfg;
}

bool Database::autofisher_set_rod(uint64_t user_id, const std::string& rod_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_rod acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET af_rod_id = ? WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_rod prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    unsigned long rid_len = rod_id.size();
    bp[0].buffer_type=MYSQL_TYPE_STRING; bp[0].buffer=(void*)rod_id.c_str(); bp[0].length=&rid_len;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&guild_id; bp[2].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_rod execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_bait(uint64_t user_id, const std::string& bait_id, int level, const std::string& meta, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_bait acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET af_bait_id=?, af_bait_level=?, af_bait_meta=? WHERE user_id=? AND guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    unsigned long bid_len=bait_id.size(), meta_len=meta.size();
    bp[0].buffer_type=MYSQL_TYPE_STRING;   bp[0].buffer=(void*)bait_id.c_str(); bp[0].length=&bid_len;
    bp[1].buffer_type=MYSQL_TYPE_LONG;     bp[1].buffer=&level;
    bp[2].buffer_type=MYSQL_TYPE_STRING;   bp[2].buffer=(void*)meta.c_str();    bp[2].length=&meta_len;
    bp[3].buffer_type=MYSQL_TYPE_LONGLONG; bp[3].buffer=&user_id; bp[3].is_unsigned=1;
    bp[4].buffer_type=MYSQL_TYPE_LONGLONG; bp[4].buffer=&guild_id; bp[4].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_deposit_bait(uint64_t user_id, int qty, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_deposit_bait acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET af_bait_qty = af_bait_qty + ? WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_deposit_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONG;     bp[0].buffer=&qty;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&guild_id; bp[2].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_deposit_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_consume_bait(uint64_t user_id, int qty, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_consume_bait acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET af_bait_qty = GREATEST(0, af_bait_qty - ?) WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_consume_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONG;     bp[0].buffer=&qty;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&guild_id; bp[2].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_consume_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_max_bank_draw(uint64_t user_id, int64_t amount, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_max_bank_draw acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET max_bank_draw = ? WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_max_bank_draw prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&amount;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&guild_id; bp[2].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_max_bank_draw execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_autosell(uint64_t user_id, bool enabled, const std::string& trigger, int64_t threshold, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_autosell acquire"); return false; }
    const char* q = "UPDATE user_autofishers SET auto_sell=?, as_trigger=?, as_threshold=? WHERE user_id=? AND guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_autosell prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    int8_t en = enabled ? 1 : 0;
    unsigned long trig_len = trigger.size();
    bp[0].buffer_type=MYSQL_TYPE_TINY;     bp[0].buffer=&en;
    bp[1].buffer_type=MYSQL_TYPE_STRING;   bp[1].buffer=(void*)trigger.c_str(); bp[1].length=&trig_len;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&threshold;
    bp[3].buffer_type=MYSQL_TYPE_LONGLONG; bp[3].buffer=&user_id; bp[3].is_unsigned=1;
    bp[4].buffer_type=MYSQL_TYPE_LONGLONG; bp[4].buffer=&guild_id; bp[4].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_autosell execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_add_fish(uint64_t user_id, const std::string& fish_name, int64_t value, const std::string& metadata, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_add_fish acquire"); return false; }
    const char* q = "INSERT INTO autofisher_fish (user_id, guild_id, fish_name, value, metadata) VALUES (?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_add_fish prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    unsigned long name_len=fish_name.size(), meta_len=metadata.size();
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id;  bp[0].is_unsigned=1;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&guild_id; bp[1].is_unsigned=1;
    bp[2].buffer_type=MYSQL_TYPE_STRING;   bp[2].buffer=(void*)fish_name.c_str(); bp[2].length=&name_len;
    bp[3].buffer_type=MYSQL_TYPE_LONGLONG; bp[3].buffer=&value;
    bp[4].buffer_type=MYSQL_TYPE_STRING;   bp[4].buffer=(void*)metadata.c_str();  bp[4].length=&meta_len;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_add_fish execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

// ---------------------------------------------------------------------------
// Batch insert multiple fish into autofisher storage in one round-trip.
// ---------------------------------------------------------------------------
bool Database::autofisher_add_fish_batch(uint64_t user_id, const std::vector<AutofishFishRow>& rows, uint64_t guild_id) {
    if (rows.empty()) return true;
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_add_fish_batch acquire"); return false; }

    std::string sql = "INSERT INTO autofisher_fish (user_id, guild_id, fish_name, value, metadata) VALUES ";
    std::string uid_str = std::to_string(user_id);
    std::string gid_str = std::to_string(guild_id);

    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) sql += ',';
        char esc_name[201], esc_meta[8193];
        mysql_real_escape_string(conn->get(), esc_name, rows[i].fish_name.c_str(), rows[i].fish_name.size());
        mysql_real_escape_string(conn->get(), esc_meta, rows[i].metadata.c_str(),
                                 std::min(rows[i].metadata.size(), (size_t)4096));
        sql += '(';
        sql += uid_str;
        sql += ',';
        sql += gid_str;
        sql += ",'";
        sql += esc_name;
        sql += "',";
        sql += std::to_string(rows[i].value);
        sql += ",'";
        sql += esc_meta;
        sql += "')";
    }

    bool ok = (mysql_real_query(conn->get(), sql.c_str(), sql.size()) == 0);
    if (!ok) {
        last_error_ = mysql_error(conn->get());
        log_error("autofisher_add_fish_batch");
    }
    pool_->release(conn);
    return ok;
}

std::vector<AutofishFish> Database::autofisher_get_fish(uint64_t user_id, uint64_t guild_id) {
    std::vector<AutofishFish> out;
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_get_fish acquire"); return out; }
    const char* q = "SELECT id, fish_name, value, metadata FROM autofisher_fish WHERE user_id=? AND guild_id=? ORDER BY caught_at";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_get_fish prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&guild_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("autofisher_get_fish execute"); mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    uint64_t id=0; int64_t value=0;
    char name_buf[101]={}, meta_buf[4096]={};
    unsigned long name_len=0, meta_len=0;
    my_bool meta_null=1;
    MYSQL_BIND br[4]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&id; br[0].is_unsigned=1;
    br[1].buffer_type=MYSQL_TYPE_STRING;   br[1].buffer=name_buf; br[1].buffer_length=sizeof(name_buf); br[1].length=&name_len;
    br[2].buffer_type=MYSQL_TYPE_LONGLONG; br[2].buffer=&value;
    br[3].buffer_type=MYSQL_TYPE_STRING;   br[3].buffer=meta_buf; br[3].buffer_length=sizeof(meta_buf); br[3].is_null=&meta_null; br[3].length=&meta_len;
    mysql_stmt_bind_result(stmt, br);
    while (mysql_stmt_fetch(stmt) == 0) {
        AutofishFish f;
        f.id        = id;
        f.user_id   = user_id;
        f.guild_id  = guild_id;
        f.fish_name = std::string(name_buf, name_len);
        f.value     = value;
        f.metadata  = meta_null ? "" : std::string(meta_buf, meta_len);
        out.push_back(f);
    }
    mysql_stmt_close(stmt); pool_->release(conn); return out;
}

int Database::autofisher_fish_count(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    const char* q = "SELECT COUNT(*) FROM autofisher_fish WHERE user_id=? AND guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&guild_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) { mysql_stmt_close(stmt); pool_->release(conn); return 0; }
    int64_t count=0;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&count;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt); pool_->release(conn); return (int)count;
}

int64_t Database::autofisher_clear_fish(uint64_t user_id, uint64_t guild_id) {
    // Sum values then delete
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_clear_fish acquire"); return 0; }
    // sum
    int64_t total = 0;
    {
        const char* q = "SELECT COALESCE(SUM(value),0) FROM autofisher_fish WHERE user_id=? AND guild_id=?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (stmt && mysql_stmt_prepare(stmt, q, strlen(q)) == 0) {
            MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
            bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
            bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&guild_id; bp[1].is_unsigned=1;
            mysql_stmt_bind_param(stmt, bp);
            if (mysql_stmt_execute(stmt) == 0) {
                MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
                br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&total;
                mysql_stmt_bind_result(stmt, br);
                mysql_stmt_fetch(stmt);
            }
            mysql_stmt_close(stmt);
        }
    }
    // delete
    {
        const char* q = "DELETE FROM autofisher_fish WHERE user_id=? AND guild_id=?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (stmt && mysql_stmt_prepare(stmt, q, strlen(q)) == 0) {
            MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
            bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
            bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&guild_id; bp[1].is_unsigned=1;
            mysql_stmt_bind_param(stmt, bp);
            mysql_stmt_execute(stmt);
            mysql_stmt_close(stmt);
        }
    }
    pool_->release(conn);
    return total;
}

std::vector<Database::ActiveGiveawayRow> Database::get_active_giveaways() {
    std::vector<ActiveGiveawayRow> result;
    auto conn = pool_->acquire();
    if (!conn) return result;
    
    const char* q = "SELECT id, guild_id, channel_id, COALESCE(message_id, 0), created_by, "
                    "prize_amount, max_winners, UNIX_TIMESTAMP(ends_at) "
                    "FROM guild_giveaways WHERE active = TRUE AND ends_at > NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    uint64_t id, guild_id, channel_id, message_id, created_by;
    int64_t prize;
    int max_winners;
    long long ends_at_ts;
    
    MYSQL_BIND br[8];
    memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &id;         br[0].is_unsigned = 1;
    br[1].buffer_type = MYSQL_TYPE_LONGLONG; br[1].buffer = &guild_id;   br[1].is_unsigned = 1;
    br[2].buffer_type = MYSQL_TYPE_LONGLONG; br[2].buffer = &channel_id; br[2].is_unsigned = 1;
    br[3].buffer_type = MYSQL_TYPE_LONGLONG; br[3].buffer = &message_id; br[3].is_unsigned = 1;
    br[4].buffer_type = MYSQL_TYPE_LONGLONG; br[4].buffer = &created_by; br[4].is_unsigned = 1;
    br[5].buffer_type = MYSQL_TYPE_LONGLONG; br[5].buffer = &prize;
    br[6].buffer_type = MYSQL_TYPE_LONG;     br[6].buffer = &max_winners;
    br[7].buffer_type = MYSQL_TYPE_LONGLONG; br[7].buffer = &ends_at_ts;
    
    if (mysql_stmt_bind_result(stmt, br) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        ActiveGiveawayRow row;
        row.id = id;
        row.guild_id = guild_id;
        row.channel_id = channel_id;
        row.message_id = message_id;
        row.created_by = created_by;
        row.prize = prize;
        row.max_winners = max_winners;
        row.ends_at = std::chrono::system_clock::from_time_t(static_cast<time_t>(ends_at_ts));
        result.push_back(row);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ========================================
// GIVEAWAY OPERATIONS
// ========================================

int64_t Database::get_guild_balance(uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* q = "SELECT balance FROM guild_balances WHERE guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    
    int64_t balance = 0;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &balance;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return balance;
}

bool Database::donate_to_guild(uint64_t user_id, uint64_t guild_id, int64_t amount) {
    if (amount <= 0) return false;
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    // Ensure guild_balances row exists
    const char* q1 = "INSERT IGNORE INTO guild_balances (guild_id, balance) VALUES (?, 0)";
    MYSQL_STMT* s1 = mysql_stmt_init(conn->get());
    if (s1 && mysql_stmt_prepare(s1, q1, strlen(q1)) == 0) {
        MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
        bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id; bp[0].is_unsigned = 1;
        mysql_stmt_bind_param(s1, bp);
        mysql_stmt_execute(s1);
    }
    if (s1) mysql_stmt_close(s1);
    
    // Add to guild balance
    const char* q2 = "UPDATE guild_balances SET balance = balance + ?, total_donated = total_donated + ? WHERE guild_id = ?";
    MYSQL_STMT* s2 = mysql_stmt_init(conn->get());
    if (!s2 || mysql_stmt_prepare(s2, q2, strlen(q2)) != 0) {
        if (s2) mysql_stmt_close(s2);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bp2[3]; memset(bp2, 0, sizeof(bp2));
    bp2[0].buffer_type = MYSQL_TYPE_LONGLONG; bp2[0].buffer = &amount;
    bp2[1].buffer_type = MYSQL_TYPE_LONGLONG; bp2[1].buffer = &amount;
    bp2[2].buffer_type = MYSQL_TYPE_LONGLONG; bp2[2].buffer = &guild_id; bp2[2].is_unsigned = 1;
    mysql_stmt_bind_param(s2, bp2);
    
    bool ok = mysql_stmt_execute(s2) == 0;
    mysql_stmt_close(s2);
    pool_->release(conn);
    return ok;
}

uint64_t Database::create_giveaway(uint64_t guild_id, uint64_t channel_id, uint64_t created_by,
                                   int64_t prize, int max_winners, int duration_seconds) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* q = "INSERT INTO guild_giveaways (guild_id, channel_id, created_by, prize_amount, max_winners, ends_at) "
                    "VALUES (?, ?, ?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bp[6]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id;          bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &channel_id;        bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONGLONG; bp[2].buffer = &created_by;        bp[2].is_unsigned = 1;
    bp[3].buffer_type = MYSQL_TYPE_LONGLONG; bp[3].buffer = &prize;
    bp[4].buffer_type = MYSQL_TYPE_LONG;     bp[4].buffer = &max_winners;
    bp[5].buffer_type = MYSQL_TYPE_LONG;     bp[5].buffer = &duration_seconds;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    uint64_t giveaway_id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return giveaway_id;
}

bool Database::enter_giveaway(uint64_t giveaway_id, uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* q = "INSERT IGNORE INTO guild_giveaway_entries (giveaway_id, user_id) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &giveaway_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &user_id;     bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    bool ok = mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

std::vector<uint64_t> Database::get_giveaway_entries(uint64_t giveaway_id) {
    std::vector<uint64_t> result;
    auto conn = pool_->acquire();
    if (!conn) return result;
    
    const char* q = "SELECT user_id FROM guild_giveaway_entries WHERE giveaway_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &giveaway_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return result;
    }
    
    uint64_t uid;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &uid; br[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_store_result(stmt);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        result.push_back(uid);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::end_giveaway(uint64_t giveaway_id, const std::vector<uint64_t>& winner_ids) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    // Build JSON array of winner IDs
    std::string json = "[";
    for (size_t i = 0; i < winner_ids.size(); ++i) {
        if (i > 0) json += ",";
        json += std::to_string(winner_ids[i]);
    }
    json += "]";
    
    const char* q = "UPDATE guild_giveaways SET active = FALSE, winner_ids = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    unsigned long json_len = json.size();
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_STRING;   bp[0].buffer = (void*)json.c_str(); bp[0].buffer_length = json_len; bp[0].length = &json_len;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &giveaway_id;        bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    bool ok = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ============================================================================
// GUILD PROFILE MANAGEMENT
// ============================================================================

std::optional<GuildProfile> Database::get_guild_profile(uint64_t guild_id) {
    return get_guild_profile_internal(*this, guild_id);
}

bool Database::set_guild_profile(const GuildProfile& profile) {
    bool ok = true;
    ok &= update_guild_profile_field_internal(*this, profile.guild_id, "server_bio", profile.bio);
    ok &= update_guild_profile_field_internal(*this, profile.guild_id, "server_website", profile.website);
    ok &= update_guild_profile_field_internal(*this, profile.guild_id, "server_banner_url", profile.banner_url);
    ok &= update_guild_profile_field_internal(*this, profile.guild_id, "server_avatar_url", profile.avatar_url);
    return ok;
}

bool Database::update_guild_bio(uint64_t guild_id, const std::string& bio) {
    return update_guild_profile_field_internal(*this, guild_id, "server_bio", bio);
}

bool Database::update_guild_website(uint64_t guild_id, const std::string& website) {
    return update_guild_profile_field_internal(*this, guild_id, "server_website", website);
}

bool Database::update_guild_banner(uint64_t guild_id, const std::string& banner_url) {
    return update_guild_profile_field_internal(*this, guild_id, "server_banner_url", banner_url);
}

bool Database::update_guild_avatar(uint64_t guild_id, const std::string& avatar_url) {
    return update_guild_profile_field_internal(*this, guild_id, "server_avatar_url", avatar_url);
}

bool Database::clear_guild_profile_field(uint64_t guild_id, const std::string& field_name) {
    std::string internal_field;
    if (field_name == "bio") internal_field = "server_bio";
    else if (field_name == "website") internal_field = "server_website";
    else if (field_name == "banner") internal_field = "server_banner_url";
    else if (field_name == "avatar") internal_field = "server_avatar_url";
    else if (field_name == "all") {
        bool ok = true;
        ok &= clear_guild_profile_field_internal(*this, guild_id, "server_bio");
        ok &= clear_guild_profile_field_internal(*this, guild_id, "server_website");
        ok &= clear_guild_profile_field_internal(*this, guild_id, "server_banner_url");
        ok &= clear_guild_profile_field_internal(*this, guild_id, "server_avatar_url");
        return ok;
    }
    else return false;

    return clear_guild_profile_field_internal(*this, guild_id, internal_field);
}

} // namespace db
} // namespace bronx
