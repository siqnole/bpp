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
#include <algorithm>
#include <functional>
#include <map>
#include <tuple>
#include <sstream>
#include <unordered_map>

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

inline std::string default_graph_type(const std::string& cat) {
    if (cat == "commands") return "bar";
    if (cat == "top")      return "pie";
    if (cat == "overview") return "line";
    return "line";
}

inline dpp::component stats_range_row(const std::string& gtype, const std::string& cat, int active_days, uint64_t uid) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    for (auto& [d, label] : std::vector<std::pair<int,std::string>>{{0,"24h"},{7,"7d"},{14,"14d"},{30,"30d"}}) {
        dpp::component btn;
        btn.set_type(dpp::cot_button);
        btn.set_label(label);
        btn.set_id("stats_r_" + std::to_string(d) + ":" + gtype + ":" + cat + ":" + std::to_string(uid));
        btn.set_style(d == active_days ? dpp::cos_primary : dpp::cos_secondary);
        row.add_component(btn);
    }
    return row;
}

inline dpp::component stats_graph_type_row(const std::string& active_gtype, int days, const std::string& cat, uint64_t uid) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu);
    menu.set_id("stats_g_" + std::to_string(days) + ":" + cat + ":" + std::to_string(uid));
    menu.set_placeholder("graph type");
    for (auto& [val, label] : STATS_GRAPH_TYPES) {
        auto opt = dpp::select_option(label, val, val == active_gtype ? "selected" : "");
        opt.set_default(val == active_gtype);
        menu.add_select_option(opt);
    }
    row.add_component(menu);
    return row;
}

inline dpp::component stats_category_row(const std::string& active_cat, int days, const std::string& gtype, uint64_t uid) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu);
    menu.set_id("stats_c_" + std::to_string(days) + ":" + gtype + ":" + std::to_string(uid));
    menu.set_placeholder("category");
    for (auto& [val, label] : STATS_CATEGORIES) {
        auto opt = dpp::select_option(label, val, val == active_cat ? "selected" : "");
        opt.set_default(val == active_cat);
        menu.add_select_option(opt);
    }
    row.add_component(menu);
    return row;
}

inline dpp::component stats_top_filter_row(const std::string& active_topic, int days, const std::string& gtype, uint64_t uid) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    for (auto& [val, label] : std::vector<std::pair<std::string,std::string>>{
            {"all","all"},{"messages","messages"},{"voice","voice"},{"commands","commands"}}) {
        dpp::component btn;
        btn.set_type(dpp::cot_button);
        btn.set_label(label);
        btn.set_id("stats_tf_" + val + ":" + std::to_string(days) + ":" + gtype + ":" + std::to_string(uid));
        btn.set_style(val == active_topic ? dpp::cos_primary : dpp::cos_secondary);
        row.add_component(btn);
    }
    return row;
}

inline void attach_stats_buttons(dpp::message& msg, const std::string& category, int days, uint64_t user_id,
                                 const std::string& gtype = "", const std::string& top_filter = "all") {
    std::string gt = gtype.empty() ? default_graph_type(category) : gtype;
    if (category == "top")
        msg.add_component(stats_top_filter_row(top_filter, days, gt, user_id));
    msg.add_component(stats_range_row(gt, category, days, user_id));
    msg.add_component(stats_graph_type_row(gt, days, category, user_id));
    msg.add_component(stats_category_row(category, days, gt, user_id));
}

inline int parse_days(const std::vector<std::string>& args, size_t idx = 1) {
    if (idx < args.size()) {
        const auto& a = args[idx];
        if (a == "today" || a == "24h" || a == "0" || a == "0d") return 0;
        if (a == "14d" || a == "14") return 14;
        if (a == "30d" || a == "30") return 30;
    }
    return 7;
}

inline int parse_days_slash(const dpp::slashcommand_t& event) {
    auto opt = event.get_parameter("range");
    if (std::holds_alternative<std::string>(opt)) {
        auto v = std::get<std::string>(opt);
        if (v == "today") return 0;
        if (v == "14d") return 14;
        if (v == "30d") return 30;
    }
    return 7;
}

inline dpp::command_option range_option() {
    return dpp::command_option(dpp::co_string, "range", "time range (today, 7d, 14d, 30d)")
        .add_choice(dpp::command_option_choice("today",   std::string("today")))
        .add_choice(dpp::command_option_choice("7 days",  std::string("7d")))
        .add_choice(dpp::command_option_choice("14 days", std::string("14d")))
        .add_choice(dpp::command_option_choice("30 days", std::string("30d")));
}

inline std::string fmt_date_label(const std::string& date) {
    auto pos = date.find('-');
    if (pos != std::string::npos) {
        auto rest = date.substr(pos + 1);
        auto pos2 = rest.find('-');
        return rest.substr(0, pos2) + "/" + rest.substr(pos2 + 1);
    }
    return date;
}

inline std::string range_label(int days) {
    if (days == 0) return "today";
    return "last " + std::to_string(days) + " days";
}

inline std::string fmt_duration(int64_t minutes) {
    if (minutes >= 60)
        return std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m";
    return std::to_string(minutes) + "m";
}

inline std::string render_series_as(const std::string& gtype,
                                    const std::string& title,
                                    const std::vector<std::string>& labels,
                                    const std::vector<bronx::chart::LineSeries>& series) {
    namespace ch = bronx::chart;
    if (labels.empty() || series.empty()) return {};
    if (gtype == "bar") {
        std::vector<ch::BarSeries> bar_series;
        for (auto& s : series) {
            ch::BarSeries bs;
            bs.name = s.name; bs.colour = s.colour;
            for (auto v : s.values) bs.values.push_back(static_cast<int64_t>(v));
            bar_series.push_back(bs);
        }
        return ch::render_stacked_bar_chart(title, labels, bar_series);
    }
    if (gtype == "pie") {
        std::vector<ch::PieSlice> slices;
        for (auto& s : series) {
            double total = 0;
            for (auto v : s.values) total += v;
            slices.push_back({s.name, total, s.colour});
        }
        return ch::render_pie_chart(title, slices);
    }
    if (gtype == "scatter") {
        std::vector<ch::ScatterSeries> sc;
        for (auto& s : series) {
            ch::ScatterSeries ss;
            ss.name = s.name; ss.colour = s.colour;
            for (size_t i = 0; i < s.values.size(); ++i)
                ss.points.push_back({static_cast<double>(i), s.values[i]});
            sc.push_back(ss);
        }
        return ch::render_scatter_chart(title, sc, "day", "value");
    }
    if (gtype == "stacked") {
        std::vector<ch::BarSeries> bar_series;
        for (auto& s : series) {
            ch::BarSeries bs;
            bs.name = s.name; bs.colour = s.colour;
            for (auto v : s.values) bs.values.push_back(static_cast<int64_t>(v));
            bar_series.push_back(bs);
        }
        return ch::render_stacked_bar_chart(title, labels, bar_series);
    }
    if (gtype == "heatmap") {
        std::vector<std::vector<double>> matrix;
        for (auto& s : series) matrix.push_back(s.values);
        std::vector<std::string> row_labels;
        for (auto& s : series) row_labels.push_back(s.name);
        return ch::render_heatmap(title, row_labels, labels, matrix);
    }
    return ch::render_line_chart(title, labels, series);
}

inline std::string render_ranked_as(const std::string& gtype,
                                    const std::string& title,
                                    const std::vector<bronx::chart::BarItem>& bars) {
    namespace ch = bronx::chart;
    if (bars.empty()) return {};
    if (gtype == "pie") {
        std::vector<ch::PieSlice> slices;
        for (size_t i = 0; i < bars.size(); ++i)
            slices.push_back({bars[i].label, static_cast<double>(bars[i].value), ch::PALETTE[i % ch::PALETTE.size()]});
        return ch::render_pie_chart(title, slices);
    }
    if (gtype == "line") {
        std::vector<std::string> labels;
        std::vector<double> vals;
        for (auto& b : bars) { labels.push_back(b.label); vals.push_back(static_cast<double>(b.value)); }
        return ch::render_line_chart(title, labels, {{"count", ch::COL_ACCENT, vals}});
    }
    if (gtype == "scatter") {
        std::vector<ch::ScatterPoint> pts;
        for (size_t i = 0; i < bars.size(); ++i)
            pts.push_back({static_cast<double>(i), static_cast<double>(bars[i].value)});
        return ch::render_scatter_chart(title, {{title, ch::COL_ACCENT, pts}}, "rank", "count");
    }
    return ch::render_horizontal_bar_chart(title, bars);
}

// ================================================================
//  stats summary — overview card
// ================================================================
inline void handle_stats_summary(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 int days, std::function<void(const dpp::message&)> reply_fn,
                                 uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto cmds_total   = sq::total_commands(db, guild_id, days);
    auto msgs_total   = sq::total_messages(db, guild_id, days);
    auto users_active = sq::active_users(db, guild_id, days);
    auto top          = sq::top_commands(db, guild_id, days, 1);
    auto new_mem      = sq::new_members(db, guild_id, days);

    std::string top_cmd = top.empty() ? "—" : top[0].command;

    std::string img = ch::render_summary_card(
        "server stats (" + std::to_string(days) + "d)",
        {
            {"commands run",  ch::fmt_num(cmds_total)},
            {"messages",      ch::fmt_num(msgs_total)},
            {"active users",  ch::fmt_num(users_active)},
            {"new members",   ch::fmt_num(new_mem)}
        });

    auto cmd_trend = sq::daily_command_trend(db, guild_id, days);
    auto msg_trend = sq::daily_message_count(db, guild_id, days);
    auto dau_trend = sq::daily_active_users(db, guild_id, days);
    auto mem_trend = sq::daily_member_flow(db, guild_id, days);

    std::string trend_img;
    {
        std::map<std::string, std::tuple<double,double,double,double>> merged;
        for (auto& d : msg_trend) merged[d.date] = {(double)d.value, 0.0, 0.0, 0.0};
        for (auto& d : dau_trend) std::get<1>(merged[d.date]) = (double)d.value;
        for (auto& d : mem_trend) std::get<2>(merged[d.date]) = (double)d.joins;
        for (auto& d : cmd_trend) std::get<3>(merged[d.date]) = (double)d.value;

        if (!merged.empty()) {
            std::vector<std::string> labels;
            std::vector<double> msg_vals, active_vals, newmem_vals, cmd_vals;
            for (auto& [date, vals] : merged) {
                labels.push_back(fmt_date_label(date));
                msg_vals.push_back(std::get<0>(vals));
                active_vals.push_back(std::get<1>(vals));
                newmem_vals.push_back(std::get<2>(vals));
                cmd_vals.push_back(std::get<3>(vals));
            }
            std::string gt = gtype.empty() ? "line" : gtype;
            trend_img = render_series_as(gt, "server overview (" + std::to_string(days) + "d)", labels,
                {
                    {"messages",     ch::COL_ACCENT, msg_vals,    1},
                    {"active users", ch::COL_CYAN,   active_vals, 0},
                    {"new members",  ch::COL_GREEN,  newmem_vals, 0},
                    {"commands",     ch::COL_YELLOW, cmd_vals,    0}
                });
        }
    }

    auto embed = bronx::create_embed(
        "**server stats** -- last " + std::to_string(days) + " days\n"
        "top command: **" + top_cmd + "**");
    if (!img.empty()) embed.set_image("attachment://stats.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("stats.png", img);
    if (!trend_img.empty()) {
        auto trend_embed = bronx::create_embed("");
        trend_embed.set_image("attachment://trend.png");
        msg.add_embed(trend_embed);
        msg.add_file("trend.png", trend_img);
    }
    if (user_id) attach_stats_buttons(msg, "overview", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats members — joins, leaves & active users
// ================================================================
inline void handle_stats_members(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 int days, std::function<void(const dpp::message&)> reply_fn,
                                 uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto members = sq::daily_member_flow(db, guild_id, days);
    auto dau     = sq::daily_active_users(db, guild_id, days);

    if (members.empty() && dau.empty()) {
        dpp::message msg(channel_id, bronx::info("no member data yet -- check back later"));
        if (user_id) attach_stats_buttons(msg, "members", days, user_id, gtype);
        reply_fn(msg);
        return;
    }

    std::map<std::string, std::tuple<double,double,double>> merged;
    for (auto& m : members) merged[m.date] = {(double)m.joins, (double)m.leaves, 0.0};
    for (auto& d : dau) std::get<2>(merged[d.date]) = (double)d.value;

    std::vector<std::string> labels;
    std::vector<double> join_vals, leave_vals, active_vals;
    for (auto& [date, vals] : merged) {
        labels.push_back(fmt_date_label(date));
        join_vals.push_back(std::get<0>(vals));
        leave_vals.push_back(std::get<1>(vals));
        active_vals.push_back(std::get<2>(vals));
    }

    std::string gt = gtype.empty() ? "line" : gtype;
    std::string img = render_series_as(gt, "member stats (" + std::to_string(days) + "d)", labels,
        {
            {"joins",        ch::COL_GREEN,  join_vals},
            {"leaves",       ch::COL_RED,    leave_vals},
            {"active users", ch::COL_CYAN,   active_vals}
        });

    int64_t total_joins = 0, total_leaves = 0;
    for (auto& m : members) { total_joins += m.joins; total_leaves += m.leaves; }
    int64_t avg_active = 0;
    if (!dau.empty()) {
        int64_t sum = 0;
        for (auto& d : dau) sum += d.value;
        avg_active = sum / (int64_t)dau.size();
    }
    int64_t net = total_joins - total_leaves;

    std::string desc = "**member stats** -- last " + std::to_string(days) + " days\n";
    desc += "total joins: **" + ch::fmt_num(total_joins) + "**\n";
    desc += "total leaves: **" + ch::fmt_num(total_leaves) + "**\n";
    desc += "avg active users/day: **" + ch::fmt_num(avg_active) + "**\n";
    desc += "net growth: **" + (net >= 0 ? std::string("+") : std::string("")) + ch::fmt_num(net) + "**";

    auto embed = bronx::create_embed(desc);
    if (!img.empty()) embed.set_image("attachment://members.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("members.png", img);
    if (user_id) attach_stats_buttons(msg, "members", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats messages — messages, edits, deletes
// ================================================================
inline void handle_stats_messages(dpp::cluster& bot, bronx::db::Database* db,
                                  uint64_t guild_id, uint64_t channel_id,
                                  int days, std::function<void(const dpp::message&)> reply_fn,
                                  uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto msgs = sq::daily_message_breakdown(db, guild_id, days);

    if (msgs.empty()) {
        dpp::message msg(channel_id, bronx::info("no message data yet -- check back later"));
        if (user_id) attach_stats_buttons(msg, "messages", days, user_id, gtype);
        reply_fn(msg);
        return;
    }

    std::vector<std::string> labels;
    std::vector<double> msg_vals, edit_vals, del_vals;
    int64_t total_msgs = 0, total_edits = 0, total_dels = 0;
    for (auto& m : msgs) {
        labels.push_back(fmt_date_label(m.date));
        msg_vals.push_back((double)m.messages);
        edit_vals.push_back((double)m.edits);
        del_vals.push_back((double)m.deletes);
        total_msgs += m.messages; total_edits += m.edits; total_dels += m.deletes;
    }

    std::string gt = gtype.empty() ? "line" : gtype;
    std::string img = render_series_as(gt, "message stats (" + std::to_string(days) + "d)", labels,
        {
            {"messages", ch::COL_ACCENT, msg_vals},
            {"edits",    ch::COL_BLUE,   edit_vals},
            {"deletes",  ch::COL_RED,    del_vals}
        });

    std::string desc = "**message stats** -- last " + std::to_string(days) + " days\n";
    desc += "total messages: **" + ch::fmt_num(total_msgs) + "**\n";
    desc += "total edits: **" + ch::fmt_num(total_edits) + "**\n";
    desc += "total deletes: **" + ch::fmt_num(total_dels) + "**";

    auto embed = bronx::create_embed(desc);
    if (!img.empty()) embed.set_image("attachment://messages.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("messages.png", img);
    if (user_id) attach_stats_buttons(msg, "messages", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats voice — voice activity
// ================================================================
inline void handle_stats_voice(dpp::cluster& bot, bronx::db::Database* db,
                               uint64_t guild_id, uint64_t channel_id,
                               int days, std::function<void(const dpp::message&)> reply_fn,
                               uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto voice = sq::daily_voice_activity(db, guild_id, days);

    if (voice.empty()) {
        dpp::message msg(channel_id, bronx::info("no voice data yet -- check back later"));
        if (user_id) attach_stats_buttons(msg, "voice", days, user_id, gtype);
        reply_fn(msg);
        return;
    }

    std::vector<std::string> labels;
    std::vector<double> join_vals, leave_vals;
    int64_t total_joins = 0, total_leaves = 0;
    for (auto& v : voice) {
        labels.push_back(fmt_date_label(v.date));
        join_vals.push_back((double)v.joins);
        leave_vals.push_back((double)v.leaves);
        total_joins += v.joins; total_leaves += v.leaves;
    }

    auto daily_mins = sq::daily_voice_minutes_series(db, guild_id, days);
    std::unordered_map<std::string, int64_t> mins_by_date;
    for (auto& dm : daily_mins) mins_by_date[dm.date] = dm.minutes;
    std::vector<double> min_vals;
    for (auto& v : voice) {
        auto it = mins_by_date.find(v.date);
        min_vals.push_back(it != mins_by_date.end() ? (double)it->second : 0.0);
    }

    std::string gt = gtype.empty() ? "line" : gtype;
    std::string img = render_series_as(gt, "voice stats (" + std::to_string(days) + "d)", labels,
        {
            {"joins",   ch::COL_GREEN, join_vals},
            {"leaves",  ch::COL_RED,   leave_vals},
            {"vc mins", ch::COL_CYAN,  min_vals}
        });

    auto total_sessions = sq::total_voice_sessions(db, guild_id, days);
    auto unique_users   = sq::unique_voice_users(db, guild_id, days);
    auto voice_mins     = sq::total_voice_minutes(db, guild_id, days);
    int64_t avg_session = total_sessions > 0 ? voice_mins / total_sessions : 0;

    std::string desc = "**voice stats** -- last " + std::to_string(days) + " days\n";
    desc += "total time: **" + fmt_duration(voice_mins) + "**\n";
    desc += "avg session: **" + fmt_duration(avg_session) + "**\n";
    desc += "total sessions: **" + ch::fmt_num(total_sessions) + "**\n";
    desc += "unique users: **" + ch::fmt_num(unique_users) + "**\n";
    desc += "total joins: **" + ch::fmt_num(total_joins) + "** · leaves: **" + ch::fmt_num(total_leaves) + "**";

    auto top_vu = sq::top_voice_users(db, guild_id, days, 5);
    if (!top_vu.empty()) {
        desc += "\n\n**top users by vc time**\n";
        for (size_t i = 0; i < top_vu.size(); ++i) {
            desc += "`" + std::to_string(i + 1) + ".` <@" + std::to_string(top_vu[i].user_id) + "> -- **"
                  + fmt_duration(top_vu[i].total_minutes) + "** (" + ch::fmt_num(top_vu[i].sessions) + " sessions)\n";
        }
    }

    auto embed = bronx::create_embed(desc);
    if (!img.empty()) embed.set_image("attachment://voice.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("voice.png", img);
    if (user_id) attach_stats_buttons(msg, "voice", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats boosts
// ================================================================
inline void handle_stats_boosts(dpp::cluster& bot, bronx::db::Database* db,
                                uint64_t guild_id, uint64_t channel_id,
                                int days, std::function<void(const dpp::message&)> reply_fn,
                                uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto boosts = sq::daily_boost_activity(db, guild_id, days);

    if (boosts.empty()) {
        dpp::message msg(channel_id, bronx::info("no boost data yet -- check back later"));
        if (user_id) attach_stats_buttons(msg, "boosts", days, user_id, gtype);
        reply_fn(msg);
        return;
    }

    std::vector<std::string> labels;
    std::vector<double> boost_vals, unboost_vals;
    int64_t total_boosts_count = 0, total_unboosts = 0;
    for (auto& b : boosts) {
        labels.push_back(fmt_date_label(b.date));
        boost_vals.push_back((double)b.boosts);
        unboost_vals.push_back((double)b.unboosts);
        total_boosts_count += b.boosts; total_unboosts += b.unboosts;
    }

    std::string gt = gtype.empty() ? "line" : gtype;
    std::string img = render_series_as(gt, "boost stats (" + std::to_string(days) + "d)", labels,
        {
            {"boosts",   ch::COL_ACCENT, boost_vals},
            {"unboosts", ch::COL_RED,    unboost_vals}
        });

    auto net_boosts            = sq::total_boosts(db, guild_id, days);
    auto unique_boosters_count = sq::unique_boosters(db, guild_id, days);

    std::string desc = "**boost stats** -- last " + std::to_string(days) + " days\n";
    desc += "total boosts: **" + ch::fmt_num(total_boosts_count) + "**\n";
    desc += "total unboosts: **" + ch::fmt_num(total_unboosts) + "**\n";
    desc += "net boosts: **" + (net_boosts >= 0 ? std::string("+") : std::string("")) + ch::fmt_num(net_boosts) + "**\n";
    desc += "unique boosters: **" + ch::fmt_num(unique_boosters_count) + "**";

    auto embed = bronx::create_embed(desc);
    if (!img.empty()) embed.set_image("attachment://boosts.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("boosts.png", img);
    if (user_id) attach_stats_buttons(msg, "boosts", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats commands
// ================================================================
inline void handle_stats_commands(dpp::cluster& bot, bronx::db::Database* db,
                                  uint64_t guild_id, uint64_t channel_id,
                                  int days, std::function<void(const dpp::message&)> reply_fn,
                                  uint64_t user_id = 0, const std::string& gtype = "") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto cmds = sq::top_commands(db, guild_id, days, 15);

    if (cmds.empty()) {
        dpp::message msg(channel_id, bronx::info("no command usage data yet -- check back later"));
        if (user_id) attach_stats_buttons(msg, "commands", days, user_id, gtype);
        reply_fn(msg);
        return;
    }

    std::vector<ch::BarItem> bars;
    for (auto& c : cmds) bars.push_back(bronx::chart::BarItem{c.command, c.count});

    std::string gt = gtype.empty() ? "bar" : gtype;
    std::string img = render_ranked_as(gt, "top commands (" + std::to_string(days) + "d)", bars);

    int64_t total = 0;
    for (auto& c : cmds) total += c.count;
    std::string desc = "**top commands** -- last " + std::to_string(days) + " days\n";
    for (size_t i = 0; i < std::min(cmds.size(), (size_t)10); ++i) {
        double pct = total > 0 ? (cmds[i].count * 100.0 / total) : 0;
        char pct_buf[16]; snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);
        desc += std::to_string(i + 1) + ". `" + cmds[i].command + "` -- " + ch::fmt_num(cmds[i].count) + " (" + pct_buf + ")\n";
    }

    auto embed = bronx::create_embed(desc);
    if (!img.empty()) embed.set_image("attachment://commands.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("commands.png", img);
    if (user_id) attach_stats_buttons(msg, "commands", days, user_id, gtype);
    reply_fn(msg);
}

// ================================================================
//  stats channel
// ================================================================
inline void handle_stats_channel(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 uint64_t filter_channel, int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto all = sq::commands_by_channel(db, guild_id, days, 100);

    std::vector<sq::ChannelCmd> filtered;
    for (auto& c : all) {
        if (filter_channel == 0 || c.channel_id == filter_channel)
            filtered.push_back(c);
    }

    if (filtered.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no command usage in that channel yet")));
        return;
    }

    std::map<std::string, int64_t> agg;
    for (auto& c : filtered) agg[c.command] += c.count;
    std::vector<ch::BarItem> bars;
    for (auto& [cmd, cnt] : agg) bars.push_back(bronx::chart::BarItem{cmd, cnt});
    std::sort(bars.begin(), bars.end(), [](auto& a, auto& b) { return a.value > b.value; });
    if (bars.size() > 15) bars.resize(15);

    std::string chan_label = filter_channel ? "<#" + std::to_string(filter_channel) + ">" : "all channels";
    std::string img = ch::render_horizontal_bar_chart(
        std::string("commands in ") + (filter_channel ? "channel" : "all channels") + " (" + std::to_string(days) + "d)", bars);

    auto embed = bronx::create_embed("**command usage** in " + chan_label + " -- last " + std::to_string(days) + " days");
    if (!img.empty()) embed.set_image("attachment://channel.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("channel.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats top
// ================================================================
inline void handle_stats_top(dpp::cluster& bot, bronx::db::Database* db,
                             uint64_t guild_id, uint64_t channel_id,
                             int days, std::function<void(const dpp::message&)> reply_fn,
                             uint64_t user_id = 0, const std::string& gtype = "",
                             const std::string& top_filter = "all") {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    bool show_msgs = (top_filter == "all" || top_filter == "messages");
    bool show_vc   = (top_filter == "all" || top_filter == "voice");
    bool show_cmds = (top_filter == "all" || top_filter == "commands");

    std::vector<sq::UserActivityEntry> top_msgs, top_vc, top_cmds;
    if (show_msgs) top_msgs = sq::top_users_messages(db, guild_id, days, 10);
    if (show_vc)   top_vc   = sq::top_users_voice(db, guild_id, days, 10);
    if (show_cmds) top_cmds = sq::top_users_commands(db, guild_id, days, 10);

    auto strip_zero = [](std::vector<sq::UserActivityEntry>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [](auto& e){ return e.total_value <= 0; }), v.end());
    };
    strip_zero(top_msgs); strip_zero(top_vc); strip_zero(top_cmds);

    std::string desc = "**top 10** -- " + range_label(days) + "\n";

    auto render_section = [&](const std::string& title,
                              const std::vector<sq::UserActivityEntry>& entries,
                              bool is_duration) {
        if (entries.empty()) return;
        desc += "\n**" + title + "**\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            desc += "`" + std::to_string(i + 1) + ".` <@" + std::to_string(entries[i].user_id) + "> -- **"
                  + (is_duration ? fmt_duration(entries[i].total_value) : ch::fmt_num(entries[i].total_value))
                  + "**\n";
        }
    };

    if (show_msgs) render_section("messages", top_msgs, false);
    if (show_vc)   render_section("voice",    top_vc,   true);
    if (show_cmds) render_section("commands", top_cmds, false);

    if (top_msgs.empty() && top_vc.empty() && top_cmds.empty())
        desc += "\nno data for this period";

    std::string chart_img;
    auto build_chart = [&](const std::string& chart_title,
                           const std::vector<sq::UserActivityEntry>& entries) {
        if (entries.empty()) return;
        std::vector<ch::PieSlice> slices;
        for (size_t i = 0; i < entries.size(); ++i) {
            dpp::user* u = dpp::find_user(entries[i].user_id);
            std::string name = u ? (u->global_name.empty() ? u->username : u->global_name)
                                 : "User#" + std::to_string(entries[i].user_id);
            if (name.size() > 14) name = name.substr(0, 12) + "..";
            slices.push_back(ch::PieSlice{name, (double)entries[i].total_value, ch::PALETTE[i % ch::PALETTE.size()]});
        }
        std::string gt = gtype.empty() ? "pie" : gtype;
        if (gt == "pie") {
            chart_img = ch::render_pie_chart(chart_title, slices);
        } else {
            std::vector<ch::BarItem> bars;
            for (auto& s : slices) bars.push_back(bronx::chart::BarItem{s.label, static_cast<int64_t>(s.value)});
            chart_img = render_ranked_as(gt, chart_title, bars);
        }
    };

    if (!top_msgs.empty() && show_msgs)      build_chart("messages", top_msgs);
    else if (!top_vc.empty() && show_vc)     build_chart("voice", top_vc);
    else if (!top_cmds.empty() && show_cmds) build_chart("commands", top_cmds);

    auto embed = bronx::create_embed(desc);
    if (!chart_img.empty()) embed.set_image("attachment://top.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!chart_img.empty()) msg.add_file("top.png", chart_img);
    if (user_id) attach_stats_buttons(msg, "top", days, user_id, gtype, top_filter);
    reply_fn(msg);
}

// ================================================================
//  stats user
// ================================================================
inline void handle_stats_user(dpp::cluster& bot, bronx::db::Database* db,
                              uint64_t guild_id, uint64_t channel_id,
                              uint64_t target_user_id, int days,
                              std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto totals = sq::user_totals(db, guild_id, target_user_id, days);
    auto daily  = sq::user_daily_breakdown(db, guild_id, target_user_id, days);

    dpp::user* target = dpp::find_user(target_user_id);
    std::string name = target ? (target->global_name.empty() ? target->username : target->global_name)
                              : "User#" + std::to_string(target_user_id);

    std::string desc = "**user stats** — " + name + " (" + range_label(days) + ")\n\n";
    desc += "💬 messages: **" + ch::fmt_num(totals.total_messages) + "**\n";
    desc += "🎙️ voice: **" + fmt_duration(totals.total_voice_minutes) + "**\n";
    desc += "⌨️ commands: **" + ch::fmt_num(totals.total_commands) + "**\n";
    if (!totals.most_active_day.empty())
        desc += "🔥 most active day: **" + totals.most_active_day + "**\n";

    std::string chart_img;
    if (!daily.empty()) {
        std::vector<std::string> labels;
        std::vector<double> msg_vals, cmd_vals, vc_vals;
        for (auto& d : daily) {
            labels.push_back(fmt_date_label(d.date));
            msg_vals.push_back((double)d.messages);
            cmd_vals.push_back((double)d.commands_used);
            vc_vals.push_back((double)d.voice_minutes);
        }
        chart_img = ch::render_line_chart("daily activity — " + name, labels,
            {
                {"messages", ch::COL_ACCENT, msg_vals, 1},
                {"commands", ch::COL_GREEN,  cmd_vals, 0},
                {"VC mins",  ch::COL_CYAN,   vc_vals,  0}
            });
    }

    auto embed = bronx::create_embed(desc);
    if (!chart_img.empty()) embed.set_image("attachment://user.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!chart_img.empty()) msg.add_file("user.png", chart_img);
    reply_fn(msg);
}

// ================================================================
//  stats heatmap
// ================================================================
inline void handle_stats_heatmap(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 int days, std::function<void(const dpp::message&)> reply_fn,
                                 uint64_t user_id = 0) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto entries = sq::hourly_heatmap(db, guild_id, days > 0 ? days : 30);

    if (entries.empty()) {
        dpp::message msg(channel_id, bronx::info("no message data yet for heatmap -- check back later"));
        if (user_id) attach_stats_buttons(msg, "heatmap", days, user_id);
        reply_fn(msg);
        return;
    }

    std::vector<std::vector<double>> matrix(7, std::vector<double>(24, 0.0));
    for (auto& e : entries) {
        if (e.day_of_week >= 0 && e.day_of_week < 7 && e.hour >= 0 && e.hour < 24)
            matrix[e.day_of_week][e.hour] = (double)e.count;
    }

    std::vector<std::string> row_labels = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::vector<std::string> col_labels;
    for (int h = 0; h < 24; ++h) col_labels.push_back(std::to_string(h));

    std::string img = ch::render_heatmap("activity heatmap (" + range_label(days) + ")", row_labels, col_labels, matrix);

    auto embed = bronx::create_embed("**activity heatmap** — " + range_label(days) + "\nmessage activity by hour and day of week");
    if (!img.empty()) embed.set_image("attachment://heatmap.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("heatmap.png", img);
    if (user_id) attach_stats_buttons(msg, "heatmap", days, user_id);
    reply_fn(msg);
}

// ================================================================
//  button handler
// ================================================================
inline void handle_stats_buttons(dpp::cluster& bot, const dpp::button_click_t& event, bronx::db::Database* db) {
    std::string cid_str = event.custom_id;

    if (cid_str.find("stats_tf_") == 0) {
        std::vector<std::string> parts;
        std::stringstream ss(cid_str); std::string part;
        while (std::getline(ss, part, ':')) parts.push_back(part);
        if (parts.size() < 4) return;
        std::string topic = parts[1];
        int days = 7; try { days = std::stoi(parts[0].substr(8)); } catch (...) { days = 7; }
        std::string gtype = parts[2];
        uint64_t expected_user = 0; try { expected_user = std::stoull(parts[3]); } catch (...) { return; }
        if (days != 0 && days != 7 && days != 14 && days != 30) days = 7;
        if (expected_user != 0 && event.command.get_issuing_user().id != expected_user) {
            event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("this isn't your stats view")).set_flags(dpp::m_ephemeral));
            return;
        }
        uint64_t guild_id = event.command.guild_id, channel_id = event.command.channel_id;
        uint64_t user_id  = static_cast<uint64_t>(event.command.get_issuing_user().id);
        auto reply = [&event](const dpp::message& msg) { event.reply(dpp::ir_update_message, msg); };
        handle_stats_top(bot, db, guild_id, channel_id, days, reply, user_id, gtype, topic);
        return;
    }

    if (cid_str.find("stats_r_") != 0) return;

    std::vector<std::string> parts;
    std::stringstream ss(cid_str); std::string part;
    while (std::getline(ss, part, ':')) parts.push_back(part);
    if (parts.size() < 4) return;

    int days = 7; try { days = std::stoi(parts[0].substr(8)); } catch (...) { days = 7; }
    std::string gtype = parts[1], category = parts[2];
    uint64_t expected_user = 0; try { expected_user = std::stoull(parts[3]); } catch (...) { return; }
    if (days != 0 && days != 7 && days != 14 && days != 30) days = 7;

    if (expected_user != 0 && event.command.get_issuing_user().id != expected_user) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("this isn't your stats view")).set_flags(dpp::m_ephemeral));
        return;
    }

    uint64_t guild_id = event.command.guild_id, channel_id = event.command.channel_id;
    uint64_t user_id  = static_cast<uint64_t>(event.command.get_issuing_user().id);
    auto reply = [&event](const dpp::message& msg) { event.reply(dpp::ir_update_message, msg); };

    if (category == "overview")       handle_stats_summary(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "members")   handle_stats_members(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "messages")  handle_stats_messages(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "voice")     handle_stats_voice(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "boosts")    handle_stats_boosts(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "commands")  handle_stats_commands(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "top")       handle_stats_top(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else                              handle_stats_summary(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
}

// ================================================================
//  select handler
// ================================================================
inline void handle_stats_select(dpp::cluster& bot, const dpp::select_click_t& event, bronx::db::Database* db) {
    std::string cid_str = event.custom_id;
    if (cid_str.find("stats_g_") != 0 && cid_str.find("stats_c_") != 0) return;
    if (event.values.empty()) return;

    std::vector<std::string> parts;
    std::stringstream ss(cid_str); std::string part;
    while (std::getline(ss, part, ':')) parts.push_back(part);
    if (parts.size() < 4) return;

    std::string action = cid_str.substr(6, 1);
    int days = 7; try { days = std::stoi(parts[0].substr(8)); } catch (...) { days = 7; }
    if (days != 0 && days != 7 && days != 14 && days != 30) days = 7;

    std::string gtype, category;
    uint64_t expected_user = 0;
    if (action == "g") {
        category = parts[1]; try { expected_user = std::stoull(parts[2]); } catch (...) { return; }
        gtype = event.values[0];
    } else {
        gtype = parts[1]; try { expected_user = std::stoull(parts[2]); } catch (...) { return; }
        category = event.values[0];
    }

    if (expected_user != 0 && event.command.get_issuing_user().id != expected_user) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("this isn't your stats view")).set_flags(dpp::m_ephemeral));
        return;
    }

    uint64_t guild_id = event.command.guild_id, channel_id = event.command.channel_id;
    uint64_t user_id  = static_cast<uint64_t>(event.command.get_issuing_user().id);
    auto reply = [&event](const dpp::message& msg) { event.reply(dpp::ir_update_message, msg); };

    if (category == "overview")       handle_stats_summary(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "members")   handle_stats_members(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "messages")  handle_stats_messages(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "voice")     handle_stats_voice(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "boosts")    handle_stats_boosts(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "commands")  handle_stats_commands(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else if (category == "top")       handle_stats_top(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
    else                              handle_stats_summary(bot, db, guild_id, channel_id, days, reply, user_id, gtype);
}

// ================================================================
//  register interactions
// ================================================================
inline void register_stats_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        handle_stats_select(bot, event, db);
    });
}

// ================================================================
//  get_stats_commands — factory
// ================================================================
inline std::vector<Command*> get_stats_commands(bronx::db::Database* db) {
    static std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    static Command stats_cmd("stats", "view server statistics with charts", "utility",
        {"serverstats", "statistics"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (event.msg.guild_id == 0) { bronx::send_message(bot, event, bronx::error("stats are only available in servers")); return; }
            uint64_t gid = event.msg.guild_id, cid = event.msg.channel_id, uid = event.msg.author.id;
            std::string sub = args.size() > 0 ? args[0] : "";
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            auto reply = [&bot, &event](const dpp::message& msg) { bronx::send_message(bot, event, msg); };

            if (sub == "members" || sub == "member" || sub == "users")
                handle_stats_members(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "messages" || sub == "msgs" || sub == "msg")
                handle_stats_messages(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "voice" || sub == "vc")
                handle_stats_voice(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "boosts" || sub == "boost")
                handle_stats_boosts(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "commands" || sub == "cmds")
                handle_stats_commands(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "top" || sub == "top10" || sub == "leaderboard")
                handle_stats_top(bot, db, gid, cid, parse_days(args, 1), reply, uid);
            else if (sub == "user" || sub == "profile") {
                uint64_t target = uid;
                if (!event.msg.mentions.empty()) target = event.msg.mentions[0].first.id;
                else if (args.size() > 1) { try { std::string s = args[1]; if (s.size()>3&&s[0]=='<'&&s[1]=='@') s=s.substr(2,s.size()-3); target=std::stoull(s); } catch (...) {} }
                handle_stats_user(bot, db, gid, cid, target, parse_days(args, 2), reply);
            } else if (sub == "channel" || sub == "ch") {
                uint64_t filter_ch = 0;
                if (!event.msg.mention_channels.empty()) filter_ch = event.msg.mention_channels[0].id;
                else if (args.size() > 1) { try { std::string s = args[1]; if (s.size()>2&&s[0]=='<'&&s[1]=='#') s=s.substr(2,s.size()-3); filter_ch=std::stoull(s); } catch (...) {} }
                handle_stats_channel(bot, db, gid, cid, filter_ch, parse_days(args, 2), reply);
            } else {
                handle_stats_summary(bot, db, gid, cid, parse_days(args, 0), reply, uid);
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (event.command.guild_id == 0) { bronx::safe_slash_reply(bot, event, bronx::error("stats are only available in servers")); return; }
            uint64_t gid = event.command.guild_id, cid = event.command.channel_id, uid = event.command.usr.id;
            auto reply = [&bot, &event](const dpp::message& msg) { bronx::safe_slash_reply(bot, event, msg); };
            auto sub_cmd = event.command.get_command_interaction();
            if (sub_cmd.options.empty()) { handle_stats_summary(bot, db, gid, cid, parse_days_slash(event), reply, uid); return; }
            auto& sub = sub_cmd.options[0];
            auto get_range = [&sub]() -> int {
                int days = 7;
                for (auto& o : sub.options) { if (o.name == "range") { auto v = std::get<std::string>(o.value); if (v=="today") days=0; else if (v=="14d") days=14; else if (v=="30d") days=30; } }
                return days;
            };
            if (sub.name == "members")        handle_stats_members(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "messages")  handle_stats_messages(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "voice")     handle_stats_voice(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "boosts")    handle_stats_boosts(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "commands")  handle_stats_commands(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "top")       handle_stats_top(bot, db, gid, cid, get_range(), reply, uid);
            else if (sub.name == "user") {
                uint64_t target = uid;
                for (auto& o : sub.options) { if (o.name == "target") target = std::get<dpp::snowflake>(o.value); }
                handle_stats_user(bot, db, gid, cid, target, get_range(), reply);
            } else if (sub.name == "channel") {
                int days = 7; dpp::snowflake ch_id = 0;
                for (auto& o : sub.options) { if (o.name=="range") { auto v=std::get<std::string>(o.value); if(v=="14d") days=14; else if(v=="30d") days=30; } else if (o.name=="channel") ch_id=std::get<dpp::snowflake>(o.value); }
                handle_stats_channel(bot, db, gid, cid, ch_id, days, reply);
            } else {
                handle_stats_summary(bot, db, gid, cid, 7, reply, uid);
            }
        },
        {
            dpp::command_option(dpp::co_sub_command, "members", "member joins, leaves & active users chart").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "messages", "messages, edits & deletes chart").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "voice", "voice channel activity chart").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "boosts", "server boost activity chart").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "commands", "top used commands chart").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "channel", "command usage in a specific channel")
                .add_option(dpp::command_option(dpp::co_channel, "channel", "channel to view stats for", true))
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "top", "top 10 users by messages, vc hours & commands").add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "user", "personal stats profile for a user")
                .add_option(dpp::command_option(dpp::co_user, "target", "user to view stats for", false))
                .add_option(range_option())
        }
    );

    cmds.push_back(&stats_cmd);
    return cmds;
}

} // namespace commands