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
        "SELECT COALESCE(SUM(messages),0) FROM guild_daily_stats "
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
              "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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

// ── daily message breakdown (messages / edits / deletes) ───────
inline std::vector<DailyMessages> daily_message_breakdown(Database* db, uint64_t guild_id, int days = 7) {
    std::vector<DailyMessages> out;
    if (!db) return out;
    std::string sql =
        "SELECT stat_date, COALESCE(messages,0), COALESCE(edits,0), COALESCE(deletes,0) "
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY) "
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY) "
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY) "
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY) "
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
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
        "AND created_at >= DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY)";
    std::shared_ptr<Connection> conn;
    MYSQL_RES* res = run_query(db->get_pool(), sql, conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

} // namespace stats_queries
} // namespace db
} // namespace bronx
