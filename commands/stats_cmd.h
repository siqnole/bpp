#pragma once
// ============================================================
//  commands/stats_cmd.h — server stats commands with chart images
//  subcommands: members, messages, voice, boosts, commands, channel
// ============================================================

#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/stats/stats_query_operations.h"
#include "../performance/chart_renderer.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <functional>

namespace commands {

static const std::vector<std::pair<std::string, std::string>> STATS_GRAPH_TYPES = {
    {"line",    "📈 line chart"},
    {"bar",     "📊 bar chart"},
    {"pie",     "🥧 pie chart"},
    {"scatter", "⚬ scatter plot"},
    {"stacked", "▦ stacked bar"},
    {"heatmap", "🟧 heatmap"}
};

static const std::vector<std::pair<std::string, std::string>> STATS_CATEGORIES = {
    {"overview",  "📋 overview"},
    {"members",   "👥 members"},
    {"messages",  "💬 messages"},
    {"voice",     "🎙️ voice"},
    {"boosts",    "🚀 boosts"},
    {"commands",  "⌨️ commands"},
    {"top",       "🏆 top 10"}
};

std::string default_graph_type(const std::string& cat);
dpp::component stats_range_row(const std::string& gtype, const std::string& cat, int active_days, uint64_t uid);
dpp::component stats_graph_type_row(const std::string& active_gtype, int days, const std::string& cat, uint64_t uid);
dpp::component stats_category_row(const std::string& active_cat, int days, const std::string& gtype, uint64_t uid);
dpp::component stats_top_filter_row(const std::string& active_topic, int days, const std::string& gtype, uint64_t uid);

void attach_stats_buttons(dpp::message& msg, const std::string& category, int days, uint64_t user_id,
                         const std::string& gtype = "", const std::string& top_filter = "all");

int parse_days(const std::vector<std::string>& args, size_t idx = 1);
int parse_days_slash(const dpp::slashcommand_t& event);
dpp::command_option range_option();

std::string fmt_date_label(const std::string& date);
std::string range_label(int days);
std::string fmt_duration(int64_t minutes);

std::string render_series_as(const std::string& gtype,
                            const std::string& title,
                            const std::vector<std::string>& labels,
                            const std::vector<bronx::chart::LineSeries>& series);

std::string render_ranked_as(const std::string& gtype,
                            const std::string& title,
                            const std::vector<bronx::chart::BarItem>& bars);

void handle_stats_summary(dpp::cluster& bot, bronx::db::Database* db,
                         uint64_t guild_id, uint64_t channel_id,
                         int days, std::function<void(const dpp::message&)> reply_fn,
                         uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_members(dpp::cluster& bot, bronx::db::Database* db,
                         uint64_t guild_id, uint64_t channel_id,
                         int days, std::function<void(const dpp::message&)> reply_fn,
                         uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_messages(dpp::cluster& bot, bronx::db::Database* db,
                          uint64_t guild_id, uint64_t channel_id,
                          int days, std::function<void(const dpp::message&)> reply_fn,
                          uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_voice(dpp::cluster& bot, bronx::db::Database* db,
                       uint64_t guild_id, uint64_t channel_id,
                       int days, std::function<void(const dpp::message&)> reply_fn,
                       uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_boosts(dpp::cluster& bot, bronx::db::Database* db,
                        uint64_t guild_id, uint64_t channel_id,
                        int days, std::function<void(const dpp::message&)> reply_fn,
                        uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_commands(dpp::cluster& bot, bronx::db::Database* db,
                          uint64_t guild_id, uint64_t channel_id,
                          int days, std::function<void(const dpp::message&)> reply_fn,
                          uint64_t user_id = 0, const std::string& gtype = "");

void handle_stats_channel(dpp::cluster& bot, bronx::db::Database* db,
                         uint64_t guild_id, uint64_t channel_id,
                         uint64_t filter_channel, int days, std::function<void(const dpp::message&)> reply_fn);

void handle_stats_top(dpp::cluster& bot, bronx::db::Database* db,
                     uint64_t guild_id, uint64_t channel_id,
                     int days, std::function<void(const dpp::message&)> reply_fn,
                     uint64_t user_id = 0, const std::string& gtype = "",
                     const std::string& top_filter = "all");

void handle_stats_user(dpp::cluster& bot, bronx::db::Database* db,
                      uint64_t guild_id, uint64_t channel_id,
                      uint64_t target_user_id, int days,
                      std::function<void(const dpp::message&)> reply_fn);

void handle_stats_heatmap(dpp::cluster& bot, bronx::db::Database* db,
                         uint64_t guild_id, uint64_t channel_id,
                         int days, std::function<void(const dpp::message&)> reply_fn,
                         uint64_t user_id = 0);

void handle_stats_buttons(dpp::cluster& bot, const dpp::button_click_t& event, bronx::db::Database* db);
void handle_stats_select(dpp::cluster& bot, const dpp::select_click_t& event, bronx::db::Database* db);
void register_stats_interactions(dpp::cluster& bot, bronx::db::Database* db);

std::vector<Command*> get_stats_commands(bronx::db::Database* db);

} // namespace commands