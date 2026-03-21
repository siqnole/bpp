#pragma once
// ============================================================
//  stats_query_operations.h — read-only queries for stats commands
//  retrieves aggregated data from guild_command_usage,
//  guild_daily_stats, guild_member_events, guild_message_events
// ============================================================

#include "../../core/database.h"
#include "../../core/connection_pool.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace bronx {
namespace db {
namespace stats_queries {

// ── result types ───────────────────────────────────────────────
struct CommandStat   { std::string command; int64_t count; };
struct DailyCount    { std::string date; int64_t value; };
struct DailyMessages { std::string date; int64_t messages; int64_t edits; int64_t deletes; };
struct DailyMembers  { std::string date; int64_t joins; int64_t leaves; };
struct ChannelCmd    { uint64_t channel_id; std::string command; int64_t count; };

// helper: run a mysql query and store the result set
// returns nullptr on failure (and logs)
inline MYSQL_RES* run_query(ConnectionPool* pool, const std::string& sql,
                            std::shared_ptr<Connection>& conn_out) {
    conn_out = pool->acquire();
    if (!conn_out) { std::cerr << "[stats_query] acquire failed\n"; return nullptr; }
    if (mysql_real_query(conn_out->get(), sql.c_str(), sql.size()) != 0) {
        std::cerr << "[stats_query] query failed: " << mysql_error(conn_out->get())
                  << "\n  sql: " << sql.substr(0, 200) << "\n";
        pool->release(conn_out);
        conn_out = nullptr;
        return nullptr;
    }
    MYSQL_RES* res = mysql_store_result(conn_out->get());
    if (!res) {
        pool->release(conn_out);
        conn_out = nullptr;
    }
    return res;
}

// ── top commands (last N days) ─────────────────────────────────
inline std::vector<CommandStat> top_commands(Database* db, uint64_t guild_id,
                                             int days = 7, int limit = 15) {
    std::vector<CommandStat> out;
    if (!db) return out;
    std::string sql =
        "SELECT command_name, SUM(use_count) AS cnt "
        "FROM guild_command_usage "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY command_name ORDER BY cnt DESC LIMIT " + std::to_string(limit);

    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        CommandStat cs;
        cs.command = row[0] ? row[0] : "";
        cs.count = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(cs);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── total commands (last N days) ───────────────────────────────
inline int64_t total_commands(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COALESCE(SUM(use_count),0) FROM guild_command_usage "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── total messages (last N days) ───────────────────────────────
inline int64_t total_messages(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    // first try daily_stats rollup table
    std::string sql =
        "SELECT COALESCE(SUM(messages_count),0) FROM guild_daily_stats "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND channel_id='__guild__' "
        "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    // fallback to raw events if rollup is empty
    if (val == 0) {
        sql = "SELECT COUNT(*) FROM guild_message_events "
              "WHERE guild_id='" + std::to_string(guild_id) + "' "
              "AND event_type='message' "
              "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
        res = run_query(db->get_pool(), sql, conn);
        if (!res) return 0;
        row = mysql_fetch_row(res);
        val = (row && row[0]) ? std::stoll(row[0]) : 0;
        mysql_free_result(res);
        db->get_pool()->release(conn);
    }
    return val;
}

// ── active unique users (last N days) ──────────────────────────
inline int64_t active_users(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COUNT(DISTINCT user_id) FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='message' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── daily command trend ────────────────────────────────────────
inline std::vector<DailyCount> daily_command_trend(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyCount> out;
    if (!db) return out;
    std::string sql =
        "SELECT usage_date, SUM(use_count) AS cnt "
        "FROM guild_command_usage "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY usage_date ORDER BY usage_date";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyCount dc;
        dc.date = row[0] ? row[0] : "";
        dc.value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(dc);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── daily message count (from guild_message_events) ───────────
inline std::vector<DailyCount> daily_message_count(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyCount> out;
    if (!db) return out;
    std::string sql =
        "SELECT DATE(created_at) AS d, COUNT(*) AS cnt "
        "FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='message' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY d ORDER BY d";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyCount dc;
        dc.date  = row[0] ? row[0] : "";
        dc.value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(dc);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── daily message breakdown (messages / edits / deletes) ───────
inline std::vector<DailyMessages> daily_message_breakdown(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyMessages> out;
    if (!db) return out;
    std::string sql =
        "SELECT stat_date, COALESCE(messages_count,0), COALESCE(edits_count,0), COALESCE(deletes_count,0) "
        "FROM guild_daily_stats "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND channel_id='__guild__' "
        "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "ORDER BY stat_date";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyMessages dm;
        dm.date     = row[0] ? row[0] : "";
        dm.messages = row[1] ? std::stoll(row[1]) : 0;
        dm.edits    = row[2] ? std::stoll(row[2]) : 0;
        dm.deletes  = row[3] ? std::stoll(row[3]) : 0;
        out.push_back(dm);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── daily member joins / leaves ────────────────────────────────
inline std::vector<DailyMembers> daily_member_flow(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyMembers> out;
    if (!db) return out;
    std::string sql =
        "SELECT DATE(created_at) AS d, "
        "SUM(event_type='join') AS joins, SUM(event_type='leave') AS leaves "
        "FROM guild_member_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY d ORDER BY d";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyMembers dm;
        dm.date   = row[0] ? row[0] : "";
        dm.joins  = row[1] ? std::stoll(row[1]) : 0;
        dm.leaves = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(dm);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── daily active users ─────────────────────────────────────────
inline std::vector<DailyCount> daily_active_users(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyCount> out;
    if (!db) return out;
    std::string sql =
        "SELECT DATE(created_at) AS d, COUNT(DISTINCT user_id) AS cnt "
        "FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='message' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY d ORDER BY d";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyCount dc;
        dc.date  = row[0] ? row[0] : "";
        dc.value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(dc);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── commands by channel ────────────────────────────────────────
inline std::vector<ChannelCmd> commands_by_channel(Database* db, uint64_t guild_id,
                                                   int days = 7, int limit = 20) {
    std::vector<ChannelCmd> out;
    if (!db) return out;
    std::string sql =
        "SELECT channel_id, command_name, SUM(use_count) AS cnt "
        "FROM guild_command_usage "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY channel_id, command_name ORDER BY cnt DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ChannelCmd cc;
        cc.channel_id = row[0] ? std::stoull(row[0]) : 0;
        cc.command    = row[1] ? row[1] : "";
        cc.count      = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(cc);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── new members in last N days ─────────────────────────────────
inline int64_t new_members(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COUNT(*) FROM guild_member_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── result types for voice / boost ─────────────────────────────
struct DailyVoice  { std::string date; int64_t joins; int64_t leaves; };
struct DailyBoosts { std::string date; int64_t boosts; int64_t unboosts; };

// ── daily voice joins / leaves ─────────────────────────────────
inline std::vector<DailyVoice> daily_voice_activity(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyVoice> out;
    if (!db) return out;
    std::string sql =
        "SELECT DATE(created_at) AS d, "
        "SUM(event_type='join') AS joins, SUM(event_type='leave') AS leaves "
        "FROM guild_voice_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY d ORDER BY d";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyVoice dv;
        dv.date   = row[0] ? row[0] : "";
        dv.joins  = row[1] ? std::stoll(row[1]) : 0;
        dv.leaves = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(dv);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── total voice sessions (joins) in last N days ────────────────
inline int64_t total_voice_sessions(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COUNT(*) FROM guild_voice_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── unique voice users in last N days ──────────────────────────
inline int64_t unique_voice_users(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COUNT(DISTINCT user_id) FROM guild_voice_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── daily boost / unboost events ───────────────────────────────
inline std::vector<DailyBoosts> daily_boost_activity(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyBoosts> out;
    if (!db) return out;
    std::string sql =
        "SELECT DATE(created_at) AS d, "
        "SUM(event_type='boost') AS boosts, SUM(event_type='unboost') AS unboosts "
        "FROM guild_boost_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY d ORDER BY d";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyBoosts db_row;
        db_row.date     = row[0] ? row[0] : "";
        db_row.boosts   = row[1] ? std::stoll(row[1]) : 0;
        db_row.unboosts = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(db_row);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── total active boosts (net boosts - unboosts) ────────────────
inline int64_t total_boosts(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT CAST(SUM(CASE WHEN event_type='boost' THEN 1 ELSE 0 END) - "
        "SUM(CASE WHEN event_type='unboost' THEN 1 ELSE 0 END) AS SIGNED) "
        "FROM guild_boost_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── unique boosters in last N days ─────────────────────────────
inline int64_t unique_boosters(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string sql =
        "SELECT COUNT(DISTINCT user_id) FROM guild_boost_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='boost' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// ── enriched voice analytics types ─────────────────────────────
struct ChannelVoiceStat {
    uint64_t channel_id;
    int64_t sessions;
    int64_t unique_users;
    int64_t total_minutes;
};
struct UserVoiceStat {
    uint64_t user_id;
    int64_t sessions;
    int64_t channels_used;
    int64_t total_minutes;
};
struct HourlyVoice { int hour; int64_t joins; };

// ── user activity leaderboard entry ────────────────────────────
struct UserActivityEntry {
    uint64_t user_id;
    int64_t  total_value;
};

// ── per-user daily breakdown entry ─────────────────────────────
struct UserDailyBreakdown {
    std::string date;
    int64_t messages;
    int64_t voice_minutes;
    int64_t commands_used;
};

// ── hourly heatmap entry (hour × day_of_week) ─────────────────
struct HourlyHeatmapEntry {
    int day_of_week; // 0=Sun, 1=Mon ... 6=Sat
    int hour;        // 0-23
    int64_t count;
};

// ── channel activity stat ──────────────────────────────────────
struct ChannelActivityStat {
    uint64_t channel_id;
    int64_t messages;
    int64_t commands;
};

// ── date condition helper (today=0, all-time=-1) ───────────────
inline std::string date_condition(const std::string& col, int days) {
    if (days == 0) return "DATE(" + col + ") = CURDATE()";
    if (days < 0)  return "1=1"; // all-time
    return "DATE(" + col + ") >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
}

inline std::string timestamp_condition(const std::string& col, int days) {
    if (days == 0) return "DATE(" + col + ") = CURDATE()";
    if (days < 0)  return "1=1"; // all-time
    return "DATE(" + col + ") >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
}

// ── top users by messages (from user_activity_daily) ───────────
inline std::vector<UserActivityEntry> top_users_messages(Database* db, uint64_t guild_id,
                                                          int days = 7, int limit = 10) {
    std::vector<UserActivityEntry> out;
    if (!db) return out;
    std::string sql =
        "SELECT user_id, SUM(messages) AS total "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND " + date_condition("stat_date", days) + " "
        "GROUP BY user_id ORDER BY total DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserActivityEntry e;
        e.user_id     = row[0] ? std::stoull(row[0]) : 0;
        e.total_value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── top users by voice minutes (from user_activity_daily) ──────
inline std::vector<UserActivityEntry> top_users_voice(Database* db, uint64_t guild_id,
                                                       int days = 7, int limit = 10) {
    std::vector<UserActivityEntry> out;
    if (!db) return out;
    std::string sql =
        "SELECT user_id, SUM(voice_minutes) AS total "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND " + date_condition("stat_date", days) + " "
        "GROUP BY user_id ORDER BY total DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserActivityEntry e;
        e.user_id     = row[0] ? std::stoull(row[0]) : 0;
        e.total_value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── top users by commands used (from user_activity_daily) ──────
inline std::vector<UserActivityEntry> top_users_commands(Database* db, uint64_t guild_id,
                                                          int days = 7, int limit = 10) {
    std::vector<UserActivityEntry> out;
    if (!db) return out;
    std::string sql =
        "SELECT user_id, SUM(commands_used) AS total "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND " + date_condition("stat_date", days) + " "
        "GROUP BY user_id ORDER BY total DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserActivityEntry e;
        e.user_id     = row[0] ? std::stoull(row[0]) : 0;
        e.total_value = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── per-user daily breakdown ───────────────────────────────────
inline std::vector<UserDailyBreakdown> user_daily_breakdown(Database* db, uint64_t guild_id,
                                                              uint64_t user_id, int days = 7) {
    std::vector<UserDailyBreakdown> out;
    if (!db) return out;
    std::string sql =
        "SELECT stat_date, messages, voice_minutes, commands_used "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND user_id='" + std::to_string(user_id) + "' "
        "AND " + date_condition("stat_date", days) + " "
        "ORDER BY stat_date";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserDailyBreakdown d;
        d.date          = row[0] ? row[0] : "";
        d.messages      = row[1] ? std::stoll(row[1]) : 0;
        d.voice_minutes = row[2] ? std::stoll(row[2]) : 0;
        d.commands_used = row[3] ? std::stoll(row[3]) : 0;
        out.push_back(d);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── hourly activity heatmap (hour × day_of_week) ──────────────
inline std::vector<HourlyHeatmapEntry> hourly_heatmap(Database* db, uint64_t guild_id,
                                                        int days = 30) {
    std::vector<HourlyHeatmapEntry> out;
    if (!db) return out;
    std::string sql =
        "SELECT DAYOFWEEK(created_at) - 1 AS dow, HOUR(created_at) AS h, COUNT(*) AS cnt "
        "FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='message' "
        "AND " + timestamp_condition("created_at", days) + " "
        "GROUP BY dow, h ORDER BY dow, h";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        HourlyHeatmapEntry e;
        e.day_of_week = row[0] ? std::stoi(row[0]) : 0;
        e.hour        = row[1] ? std::stoi(row[1]) : 0;
        e.count       = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── user-specific hourly heatmap ───────────────────────────────
inline std::vector<HourlyHeatmapEntry> user_hourly_heatmap(Database* db, uint64_t guild_id,
                                                              uint64_t user_id, int days = 30) {
    std::vector<HourlyHeatmapEntry> out;
    if (!db) return out;
    std::string sql =
        "SELECT DAYOFWEEK(created_at) - 1 AS dow, HOUR(created_at) AS h, COUNT(*) AS cnt "
        "FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND user_id='" + std::to_string(user_id) + "' "
        "AND event_type='message' "
        "AND " + timestamp_condition("created_at", days) + " "
        "GROUP BY dow, h ORDER BY dow, h";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        HourlyHeatmapEntry e;
        e.day_of_week = row[0] ? std::stoi(row[0]) : 0;
        e.hour        = row[1] ? std::stoi(row[1]) : 0;
        e.count       = row[2] ? std::stoll(row[2]) : 0;
        out.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── top channels by messages ───────────────────────────────────
inline std::vector<ChannelActivityStat> top_channels_messages(Database* db, uint64_t guild_id,
                                                                int days = 7, int limit = 15) {
    std::vector<ChannelActivityStat> out;
    if (!db) return out;
    std::string sql =
        "SELECT channel_id, COUNT(*) AS cnt "
        "FROM guild_message_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND event_type='message' "
        "AND " + timestamp_condition("created_at", days) + " "
        "GROUP BY channel_id ORDER BY cnt DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ChannelActivityStat c;
        c.channel_id = row[0] ? std::stoull(row[0]) : 0;
        c.messages   = row[1] ? std::stoll(row[1]) : 0;
        c.commands   = 0;
        out.push_back(c);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── user totals (messages + vc + commands) for profile ─────────
struct UserTotals {
    int64_t total_messages;
    int64_t total_voice_minutes;
    int64_t total_commands;
    std::string most_active_day; // date with highest messages
};

inline UserTotals user_totals(Database* db, uint64_t guild_id, uint64_t user_id, int days = -1) {
    UserTotals t = {0, 0, 0, ""};
    if (!db) return t;
    std::string sql =
        "SELECT SUM(messages), SUM(voice_minutes), SUM(commands_used), "
        "(SELECT stat_date FROM guild_user_activity_daily "
        " WHERE guild_id='" + std::to_string(guild_id) + "' AND user_id='" + std::to_string(user_id) + "'"
        " ORDER BY messages DESC LIMIT 1) "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND user_id='" + std::to_string(user_id) + "' "
        "AND " + date_condition("stat_date", days);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return t;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        t.total_messages      = row[0] ? std::stoll(row[0]) : 0;
        t.total_voice_minutes = row[1] ? std::stoll(row[1]) : 0;
        t.total_commands      = row[2] ? std::stoll(row[2]) : 0;
        t.most_active_day     = row[3] ? row[3] : "";
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return t;
}

// ── top voice channels by sessions + computed time ─────────────
inline std::vector<ChannelVoiceStat> top_voice_channels(Database* db, uint64_t guild_id,
                                                         int days = 7, int limit = 15) {
    std::vector<ChannelVoiceStat> out;
    if (!db) return out;
    std::string gid = std::to_string(guild_id);
    std::string d   = std::to_string(days);
    // sessions + unique users per channel
    std::string sql =
        "SELECT channel_id, COUNT(*) AS sessions, COUNT(DISTINCT user_id) AS users "
        "FROM guild_voice_events "
        "WHERE guild_id='" + gid + "' AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + d + " DAY) "
        "GROUP BY channel_id ORDER BY sessions DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ChannelVoiceStat cs;
        cs.channel_id   = row[0] ? std::stoull(row[0]) : 0;
        cs.sessions     = row[1] ? std::stoll(row[1]) : 0;
        cs.unique_users = row[2] ? std::stoll(row[2]) : 0;
        cs.total_minutes = 0;
        out.push_back(cs);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);

    // compute time per channel via paired join/leave
    if (!out.empty()) {
        std::string time_sql =
            "SELECT channel_id, COALESCE(SUM(dur),0) AS total_sec FROM ("
            "  SELECT j.channel_id, "
            "    TIMESTAMPDIFF(SECOND, j.created_at, MIN(l.created_at)) AS dur "
            "  FROM guild_voice_events j "
            "  INNER JOIN guild_voice_events l "
            "    ON l.guild_id=j.guild_id AND l.user_id=j.user_id "
            "    AND l.channel_id=j.channel_id AND l.event_type='leave' "
            "    AND l.created_at > j.created_at "
            "  WHERE j.guild_id='" + gid + "' AND j.event_type='join' "
            "  AND DATE(j.created_at) >= DATE_SUB(CURDATE(), INTERVAL " + d + " DAY) "
            "  GROUP BY j.id, j.channel_id, j.created_at"
            ") paired WHERE dur > 0 AND dur < 86400 GROUP BY channel_id";
        std::shared_ptr<Connection> conn2;
        MYSQL_RES* tres = run_query(db->get_pool(), time_sql, conn2);
        if (tres) {
            std::unordered_map<uint64_t, int64_t> ctime;
            MYSQL_ROW tr;
            while ((tr = mysql_fetch_row(tres))) {
                uint64_t cid = tr[0] ? std::stoull(tr[0]) : 0;
                int64_t sec  = tr[1] ? std::stoll(tr[1]) : 0;
                ctime[cid] = sec;
            }
            mysql_free_result(tres);
            db->get_pool()->release(conn2);
            for (auto& c : out) c.total_minutes = ctime.count(c.channel_id) ? ctime[c.channel_id] / 60 : 0;
        }
    }
    return out;
}

// ── top voice users by sessions + computed time ────────────────
inline std::vector<UserVoiceStat> top_voice_users(Database* db, uint64_t guild_id,
                                                   int days = 7, int limit = 15) {
    std::vector<UserVoiceStat> out;
    if (!db) return out;
    std::string gid = std::to_string(guild_id);
    std::string d   = std::to_string(days);
    std::string sql =
        "SELECT user_id, COUNT(*) AS sessions, COUNT(DISTINCT channel_id) AS chans "
        "FROM guild_voice_events "
        "WHERE guild_id='" + gid + "' AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + d + " DAY) "
        "GROUP BY user_id ORDER BY sessions DESC LIMIT " + std::to_string(limit);
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserVoiceStat us;
        us.user_id       = row[0] ? std::stoull(row[0]) : 0;
        us.sessions      = row[1] ? std::stoll(row[1]) : 0;
        us.channels_used = row[2] ? std::stoll(row[2]) : 0;
        us.total_minutes = 0;
        out.push_back(us);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);

    if (!out.empty()) {
        std::string time_sql =
            "SELECT user_id, COALESCE(SUM(dur),0) AS total_sec FROM ("
            "  SELECT j.user_id, "
            "    TIMESTAMPDIFF(SECOND, j.created_at, MIN(l.created_at)) AS dur "
            "  FROM guild_voice_events j "
            "  INNER JOIN guild_voice_events l "
            "    ON l.guild_id=j.guild_id AND l.user_id=j.user_id "
            "    AND l.channel_id=j.channel_id AND l.event_type='leave' "
            "    AND l.created_at > j.created_at "
            "  WHERE j.guild_id='" + gid + "' AND j.event_type='join' "
            "  AND DATE(j.created_at) >= DATE_SUB(CURDATE(), INTERVAL " + d + " DAY) "
            "  GROUP BY j.id, j.user_id, j.created_at"
            ") paired WHERE dur > 0 AND dur < 86400 GROUP BY user_id";
        std::shared_ptr<Connection> conn2;
        MYSQL_RES* tres = run_query(db->get_pool(), time_sql, conn2);
        if (tres) {
            std::unordered_map<uint64_t, int64_t> utime;
            MYSQL_ROW tr;
            while ((tr = mysql_fetch_row(tres))) {
                uint64_t uid = tr[0] ? std::stoull(tr[0]) : 0;
                int64_t sec  = tr[1] ? std::stoll(tr[1]) : 0;
                utime[uid] = sec;
            }
            mysql_free_result(tres);
            db->get_pool()->release(conn2);
            for (auto& u : out) u.total_minutes = utime.count(u.user_id) ? utime[u.user_id] / 60 : 0;
        }
    }
    return out;
}

// ── voice peak hours (hour-of-day distribution) ────────────────
inline std::vector<HourlyVoice> voice_peak_hours(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<HourlyVoice> out(24);
    for (int h = 0; h < 24; h++) { out[h].hour = h; out[h].joins = 0; }
    if (!db) return out;
    std::string sql =
        "SELECT HOUR(created_at) AS h, COUNT(*) AS cnt "
        "FROM guild_voice_events "
        "WHERE guild_id='" + std::to_string(guild_id) + "' AND event_type='join' "
        "AND DATE(created_at) >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
        "GROUP BY h ORDER BY h";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        int h = row[0] ? std::stoi(row[0]) : 0;
        int64_t cnt = row[1] ? std::stoll(row[1]) : 0;
        if (h >= 0 && h < 24) out[h].joins = cnt;
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

// ── total voice time in minutes (paired sessions) ──────────────
inline int64_t total_voice_minutes(Database* db, uint64_t guild_id, int days = 7) {
    if (!db) return 0;
    std::string gid = std::to_string(guild_id);
    std::string d   = std::to_string(days);
    std::string sql =
        "SELECT COALESCE(SUM(dur),0) FROM ("
        "  SELECT TIMESTAMPDIFF(SECOND, j.created_at, MIN(l.created_at)) AS dur "
        "  FROM guild_voice_events j "
        "  INNER JOIN guild_voice_events l "
        "    ON l.guild_id=j.guild_id AND l.user_id=j.user_id "
        "    AND l.channel_id=j.channel_id AND l.event_type='leave' "
        "    AND l.created_at > j.created_at "
        "  WHERE j.guild_id='" + gid + "' AND j.event_type='join' "
        "  AND DATE(j.created_at) >= DATE_SUB(CURDATE(), INTERVAL " + d + " DAY) "
        "  GROUP BY j.id, j.created_at"
        ") paired WHERE dur > 0 AND dur < 86400";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t sec = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return sec / 60;
}

// ── daily voice minutes from user_activity_daily ───────────────
struct DailyVoiceMinutes { std::string date; int64_t minutes; };
inline std::vector<DailyVoiceMinutes> daily_voice_minutes_series(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyVoiceMinutes> out;
    if (!db) return out;
    std::string sql =
        "SELECT stat_date, SUM(voice_minutes) AS mins "
        "FROM guild_user_activity_daily "
        "WHERE guild_id='" + std::to_string(guild_id) + "' "
        "AND " + date_condition("stat_date", days) + " "
        "GROUP BY stat_date ORDER BY stat_date";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DailyVoiceMinutes d;
        d.date    = row[0] ? row[0] : "";
        d.minutes = row[1] ? std::stoll(row[1]) : 0;
        out.push_back(d);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return out;
}

} // namespace stats_queries
} // namespace db
} // namespace bronx
