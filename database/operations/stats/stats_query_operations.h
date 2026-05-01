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
#include <mariadb/mysql.h>

namespace bronx {
namespace db {
namespace stats_queries {

// ── result types ───────────────────────────────────────────────
struct CommandStat   { std::string command; int64_t count; };
struct DailyCount    { std::string date; int64_t value; };
struct DailyMessages { std::string date; int64_t messages; int64_t edits; int64_t deletes; };
struct DailyMembers  { std::string date; int64_t joins; int64_t leaves; };
struct ChannelCmd    { uint64_t channel_id; std::string command; int64_t count; };

// ── voice / boost types ─────────────────────────────
struct DailyVoice  { std::string date; int64_t joins; int64_t leaves; };
struct DailyBoosts { std::string date; int64_t boosts; int64_t unboosts; };

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

struct UserActivityEntry {
    uint64_t user_id;
    int64_t  total_value;
};

struct UserDailyBreakdown {
    std::string date;
    int64_t messages;
    int64_t voice_minutes;
    int64_t commands_used;
};

struct HourlyHeatmapEntry {
    int day_of_week; // 0=Sun, 1=Mon ... 6=Sat
    int hour;        // 0-23
    int64_t count;
};

struct ChannelActivityStat {
    uint64_t channel_id;
    int64_t messages;
    int64_t commands;
};

struct UserTotals {
    int64_t total_messages;
    int64_t total_voice_minutes;
    int64_t total_commands;
    std::string most_active_day; // date with highest messages
};

struct DailyVoiceMinutes { std::string date; int64_t minutes; };

// helper: run a mysql query and store the result set
// returns nullptr on failure (and logs)
MYSQL_RES* run_query(ConnectionPool* pool, const std::string& sql,
                    std::shared_ptr<Connection>& conn_out);

// ── top commands (last N days) ─────────────────────────────────
std::vector<CommandStat> top_commands(Database* db, uint64_t guild_id,
                                     int days = 7, int limit = 15);

// ── total commands (last N days) ───────────────────────────────
int64_t total_commands(Database* db, uint64_t guild_id, int days = 7);

// ── total messages (last N days) ───────────────────────────────
int64_t total_messages(Database* db, uint64_t guild_id, int days = 7);

// ── active unique users (last N days) ──────────────────────────
int64_t active_users(Database* db, uint64_t guild_id, int days = 7);

// ── daily command trend ────────────────────────────────────────
std::vector<DailyCount> daily_command_trend(Database* db, uint64_t guild_id, int days = 7);

// ── daily message count (from guild_message_events) ───────────
std::vector<DailyCount> daily_message_count(Database* db, uint64_t guild_id, int days = 7);

// ── daily message breakdown (messages / edits / deletes) ───────
std::vector<DailyMessages> daily_message_breakdown(Database* db, uint64_t guild_id, int days = 7);

// ── daily member joins / leaves ────────────────────────────────
std::vector<DailyMembers> daily_member_flow(Database* db, uint64_t guild_id, int days = 7);

// ── daily active users ─────────────────────────────────────────
std::vector<DailyCount> daily_active_users(Database* db, uint64_t guild_id, int days = 7);

// ── commands by channel ────────────────────────────────────────
std::vector<ChannelCmd> commands_by_channel(Database* db, uint64_t guild_id,
                                           int days = 7, int limit = 20);

// ── new members in last N days ─────────────────────────────────
int64_t new_members(Database* db, uint64_t guild_id, int days = 7);

// ── daily voice joins / leaves ─────────────────────────────────
std::vector<DailyVoice> daily_voice_activity(Database* db, uint64_t guild_id, int days = 7);

// ── total voice sessions (joins) in last N days ────────────────
int64_t total_voice_sessions(Database* db, uint64_t guild_id, int days = 7);

// ── unique voice users in last N days ──────────────────────────
int64_t unique_voice_users(Database* db, uint64_t guild_id, int days = 7);

// ── daily boost / unboost events ───────────────────────────────
std::vector<DailyBoosts> daily_boost_activity(Database* db, uint64_t guild_id, int days = 7);

// ── total active boosts (net boosts - unboosts) ────────────────
int64_t total_boosts(Database* db, uint64_t guild_id, int days = 7);

// ── unique boosters in last N days ─────────────────────────────
int64_t unique_boosters(Database* db, uint64_t guild_id, int days = 7);

// ── date condition helpers ───────────────────────────────
std::string date_condition(const std::string& col, int days);
std::string timestamp_condition(const std::string& col, int days);

// ── top users by messages (from user_activity_daily) ───────────
std::vector<UserActivityEntry> top_users_messages(Database* db, uint64_t guild_id,
                                                  int days = 7, int limit = 10);

// ── top users by voice minutes (from user_activity_daily) ──────
std::vector<UserActivityEntry> top_users_voice(Database* db, uint64_t guild_id,
                                               int days = 7, int limit = 10);

// ── top users by commands used (from user_activity_daily) ──────
std::vector<UserActivityEntry> top_users_commands(Database* db, uint64_t guild_id,
                                                  int days = 7, int limit = 10);

// ── per-user daily breakdown ───────────────────────────────────
std::vector<UserDailyBreakdown> user_daily_breakdown(Database* db, uint64_t guild_id,
                                                     uint64_t user_id, int days = 7);

// ── hourly activity heatmap (hour × day_of_week) ──────────────
std::vector<HourlyHeatmapEntry> hourly_heatmap(Database* db, uint64_t guild_id,
                                                int days = 30);

// ── user-specific hourly heatmap ───────────────────────────────
std::vector<HourlyHeatmapEntry> user_hourly_heatmap(Database* db, uint64_t guild_id,
                                                      uint64_t user_id, int days = 30);

// ── top channels by messages ───────────────────────────────────
std::vector<ChannelActivityStat> top_channels_messages(Database* db, uint64_t guild_id,
                                                        int days = 7, int limit = 15);

// ── user totals (messages + vc + commands) for profile ─────────
UserTotals user_totals(Database* db, uint64_t guild_id, uint64_t user_id, int days = -1);

// ── top voice channels by sessions + computed time ─────────────
std::vector<ChannelVoiceStat> top_voice_channels(Database* db, uint64_t guild_id,
                                                 int days = 7, int limit = 15);

// ── top voice users by sessions + computed time ────────────────
std::vector<UserVoiceStat> top_voice_users(Database* db, uint64_t guild_id,
                                           int days = 7, int limit = 15);

// ── voice peak hours (hour-of-day distribution) ────────────────
std::vector<HourlyVoice> voice_peak_hours(Database* db, uint64_t guild_id, int days = 7);

// ── total voice time in minutes (paired sessions) ──────────────
int64_t total_voice_minutes(Database* db, uint64_t guild_id, int days = 7);

// ── daily voice minutes from user_activity_daily ───────────────
std::vector<DailyVoiceMinutes> daily_voice_minutes_series(Database* db, uint64_t guild_id, int days = 7);

} // namespace stats_queries
} // namespace db
} // namespace bronx
