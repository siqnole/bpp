#pragma once
// ============================================================================
// Lightweight daily stat tracker — include this from any game command to
// update the daily_stats table used by the daily challenges system.
//
// This file intentionally has minimal dependencies so it can be included
// from fishing, mining, gambling, economy, and any other command file
// without creating circular header dependencies.
// ============================================================================
#include "../../database/core/database.h"
#include "../economy/helpers.h"
#include <string>
#include <chrono>
#include <ctime>
namespace commands {
namespace daily_challenges {

// Get today's date string in EST (matches challenges.h definition)
inline std::string tracker_get_today_est() {
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    time_t est = tnow - 5 * 3600; // EST = UTC-5
    tm est_tm = *gmtime(&est);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &est_tm);
    return std::string(buf);
}

// Increment a daily stat counter for the daily challenges system.
// This writes to the daily_stats table so that get_daily_stat() in
// challenges.h can track progress towards daily challenge goals.
inline void track_daily_stat(bronx::db::Database* db, uint64_t user_id,
                             const std::string& stat_name, int64_t amount = 1) {
    if (!db || amount <= 0) return;
    std::string today = tracker_get_today_est();
    std::string sql = "INSERT INTO daily_stats (user_id, stat_name, stat_value, stat_date) "
                      "VALUES (" + std::to_string(user_id) + ", '" + stat_name + "', "
                      + std::to_string(amount) + ", '" + today + "') "
                      "ON DUPLICATE KEY UPDATE stat_value = stat_value + " + std::to_string(amount);
    economy::db_exec(db, sql);
}

// Read a daily stat value for today.  Returns 0 if no row exists yet.
//
// BRONX_GET_DAILY_STAT_DEFINED: set here so challenges.h can guard its own
// identical static definition with #ifndef BRONX_GET_DAILY_STAT_DEFINED,
// preventing a redefinition error when both headers appear in the same TU.
#define BRONX_GET_DAILY_STAT_DEFINED
inline int64_t get_daily_stat(bronx::db::Database* db, uint64_t user_id,
                              const std::string& stat_name) {
    if (!db) return 0;
    std::string today = tracker_get_today_est();
    std::string sql = "SELECT stat_value FROM daily_stats WHERE user_id = "
                      + std::to_string(user_id) + " AND stat_name = '" + stat_name
                      + "' AND stat_date = '" + today + "' LIMIT 1";
    MYSQL_RES* res = economy::db_select(db, sql);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    return val;
}

// Set a daily stat to an exact value (non-additive).
// Used for timestamps and other values that should not accumulate.
inline void set_daily_stat(bronx::db::Database* db, uint64_t user_id,
                           const std::string& stat_name, int64_t value) {
    if (!db) return;
    std::string today = tracker_get_today_est();
    std::string sql = "INSERT INTO daily_stats (user_id, stat_name, stat_value, stat_date) "
                      "VALUES (" + std::to_string(user_id) + ", '" + stat_name + "', "
                      + std::to_string(value) + ", '" + today + "') "
                      "ON DUPLICATE KEY UPDATE stat_value = " + std::to_string(value);
    economy::db_exec(db, sql);
}

} // namespace daily_challenges
} // namespace commands