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

namespace commands {

// ── helper: parse "7d" / "14d" / "30d" from args, default 7 ───
inline int parse_days(const std::vector<std::string>& args, size_t idx = 1) {
    if (idx < args.size()) {
        const auto& a = args[idx];
        if (a == "14d" || a == "14") return 14;
        if (a == "30d" || a == "30") return 30;
    }
    return 7;
}

inline int parse_days_slash(const dpp::slashcommand_t& event) {
    auto opt = event.get_parameter("range");
    if (std::holds_alternative<std::string>(opt)) {
        auto v = std::get<std::string>(opt);
        if (v == "14d") return 14;
        if (v == "30d") return 30;
    }
    return 7;
}

// ── range option reused across subcommands ─────────────────────
inline dpp::command_option range_option() {
    return dpp::command_option(dpp::co_string, "range", "time range (7d, 14d, 30d)")
        .add_choice(dpp::command_option_choice("7 days",  std::string("7d")))
        .add_choice(dpp::command_option_choice("14 days", std::string("14d")))
        .add_choice(dpp::command_option_choice("30 days", std::string("30d")));
}

// ── helper: format date string "YYYY-MM-DD" → "M/D" ───────────
inline std::string fmt_date_label(const std::string& date) {
    auto pos = date.find('-');
    if (pos != std::string::npos) {
        auto rest = date.substr(pos + 1);
        auto pos2 = rest.find('-');
        return rest.substr(0, pos2) + "/" + rest.substr(pos2 + 1);
    }
    return date;
}

// ================================================================
//  stats  — summary overview with image card
// ================================================================
inline void handle_stats_summary(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 int days, std::function<void(const dpp::message&)> reply_fn) {
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

    auto trend = sq::daily_command_trend(db, guild_id, days);
    std::string trend_img;
    if (!trend.empty()) {
        std::vector<std::string> labels;
        std::vector<double> values;
        for (auto& d : trend) {
            labels.push_back(fmt_date_label(d.date));
            values.push_back(static_cast<double>(d.value));
        }
        trend_img = ch::render_line_chart(
            "commands / day", labels,
            {{ "commands", ch::COL_ACCENT, values }});
    }

    auto embed = bronx::create_embed(
        "**server stats** — last " + std::to_string(days) + " days\n"
        "top command: **" + top_cmd + "**")
        .set_image("attachment://stats.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("stats.png", img);

    if (!trend_img.empty()) {
        auto trend_embed = bronx::create_embed("")
            .set_image("attachment://trend.png");
        msg.add_embed(trend_embed);
        msg.add_file("trend.png", trend_img);
    }

    reply_fn(msg);
}

// ================================================================
//  stats members — joins, leaves & active users line chart
// ================================================================
inline void handle_stats_members(dpp::cluster& bot, bronx::db::Database* db,
                                 uint64_t guild_id, uint64_t channel_id,
                                 int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto members = sq::daily_member_flow(db, guild_id, days);
    auto dau     = sq::daily_active_users(db, guild_id, days);

    if (members.empty() && dau.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no member data yet — check back later")));
        return;
    }

    std::map<std::string, std::tuple<double,double,double>> merged;
    for (auto& m : members) merged[m.date] = {(double)m.joins, (double)m.leaves, 0.0};
    for (auto& d : dau) {
        auto& entry = merged[d.date];
        std::get<2>(entry) = (double)d.value;
    }

    std::vector<std::string> labels;
    std::vector<double> join_vals, leave_vals, active_vals;
    for (auto& [date, vals] : merged) {
        labels.push_back(fmt_date_label(date));
        join_vals.push_back(std::get<0>(vals));
        leave_vals.push_back(std::get<1>(vals));
        active_vals.push_back(std::get<2>(vals));
    }

    std::string img = ch::render_line_chart(
        "member stats (" + std::to_string(days) + "d)",
        labels,
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

    std::string desc = "**member stats** \xe2\x80\x94 last " + std::to_string(days) + " days\n";
    desc += "\xf0\x9f\x93\xa5 total joins: **" + ch::fmt_num(total_joins) + "**\n";
    desc += "\xf0\x9f\x93\xa4 total leaves: **" + ch::fmt_num(total_leaves) + "**\n";
    desc += "\xf0\x9f\x91\xa5 avg active users/day: **" + ch::fmt_num(avg_active) + "**\n";
    desc += "\xf0\x9f\x93\x8a net growth: **" + (net >= 0 ? std::string("+") : std::string("")) + ch::fmt_num(net) + "**";

    auto embed = bronx::create_embed(desc)
        .set_image("attachment://members.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("members.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats messages — messages, edits, deletes line chart
// ================================================================
inline void handle_stats_messages(dpp::cluster& bot, bronx::db::Database* db,
                                  uint64_t guild_id, uint64_t channel_id,
                                  int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto msgs = sq::daily_message_breakdown(db, guild_id, days);

    if (msgs.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no message data yet \xe2\x80\x94 check back later")));
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
        total_msgs += m.messages;
        total_edits += m.edits;
        total_dels += m.deletes;
    }

    std::string img = ch::render_line_chart(
        "message stats (" + std::to_string(days) + "d)",
        labels,
        {
            {"messages", ch::COL_ACCENT, msg_vals},
            {"edits",    ch::COL_BLUE,   edit_vals},
            {"deletes",  ch::COL_RED,    del_vals}
        });

    std::string desc = "**message stats** \xe2\x80\x94 last " + std::to_string(days) + " days\n";
    desc += "\xf0\x9f\x92\xac total messages: **" + ch::fmt_num(total_msgs) + "**\n";
    desc += "\xe2\x9c\x8f\xef\xb8\x8f total edits: **" + ch::fmt_num(total_edits) + "**\n";
    desc += "\xf0\x9f\x97\x91\xef\xb8\x8f total deletes: **" + ch::fmt_num(total_dels) + "**";

    auto embed = bronx::create_embed(desc)
        .set_image("attachment://messages.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("messages.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats voice — voice joins & leaves line chart
// ================================================================
inline void handle_stats_voice(dpp::cluster& bot, bronx::db::Database* db,
                               uint64_t guild_id, uint64_t channel_id,
                               int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto voice = sq::daily_voice_activity(db, guild_id, days);

    if (voice.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no voice data yet \xe2\x80\x94 check back later")));
        return;
    }

    std::vector<std::string> labels;
    std::vector<double> join_vals, leave_vals;
    int64_t total_joins = 0, total_leaves = 0;
    for (auto& v : voice) {
        labels.push_back(fmt_date_label(v.date));
        join_vals.push_back((double)v.joins);
        leave_vals.push_back((double)v.leaves);
        total_joins += v.joins;
        total_leaves += v.leaves;
    }

    std::string img = ch::render_line_chart(
        "voice stats (" + std::to_string(days) + "d)",
        labels,
        {
            {"joins",  ch::COL_GREEN, join_vals},
            {"leaves", ch::COL_RED,   leave_vals}
        });

    auto total_sessions = sq::total_voice_sessions(db, guild_id, days);
    auto unique_users   = sq::unique_voice_users(db, guild_id, days);

    std::string desc = "**voice stats** \xe2\x80\x94 last " + std::to_string(days) + " days\n";
    desc += "\xf0\x9f\x94\x8a total sessions: **" + ch::fmt_num(total_sessions) + "**\n";
    desc += "\xf0\x9f\x91\xa4 unique users: **" + ch::fmt_num(unique_users) + "**\n";
    desc += "\xf0\x9f\x93\xa5 total joins: **" + ch::fmt_num(total_joins) + "**\n";
    desc += "\xf0\x9f\x93\xa4 total leaves: **" + ch::fmt_num(total_leaves) + "**";

    auto embed = bronx::create_embed(desc)
        .set_image("attachment://voice.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("voice.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats boosts — boost & unboost activity line chart
// ================================================================
inline void handle_stats_boosts(dpp::cluster& bot, bronx::db::Database* db,
                                uint64_t guild_id, uint64_t channel_id,
                                int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto boosts = sq::daily_boost_activity(db, guild_id, days);

    if (boosts.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no boost data yet \xe2\x80\x94 check back later")));
        return;
    }

    std::vector<std::string> labels;
    std::vector<double> boost_vals, unboost_vals;
    int64_t total_boosts_count = 0, total_unboosts = 0;
    for (auto& b : boosts) {
        labels.push_back(fmt_date_label(b.date));
        boost_vals.push_back((double)b.boosts);
        unboost_vals.push_back((double)b.unboosts);
        total_boosts_count += b.boosts;
        total_unboosts += b.unboosts;
    }

    std::string img = ch::render_line_chart(
        "boost stats (" + std::to_string(days) + "d)",
        labels,
        {
            {"boosts",   ch::COL_ACCENT, boost_vals},
            {"unboosts", ch::COL_RED,    unboost_vals}
        });

    auto net_boosts     = sq::total_boosts(db, guild_id, days);
    auto unique_boosters_count = sq::unique_boosters(db, guild_id, days);

    std::string desc = "**boost stats** \xe2\x80\x94 last " + std::to_string(days) + " days\n";
    desc += "\xf0\x9f\x9a\x80 total boosts: **" + ch::fmt_num(total_boosts_count) + "**\n";
    desc += "\xf0\x9f\x93\x89 total unboosts: **" + ch::fmt_num(total_unboosts) + "**\n";
    desc += "\xe2\x9c\xa8 net boosts: **" + (net_boosts >= 0 ? std::string("+") : std::string("")) + ch::fmt_num(net_boosts) + "**\n";
    desc += "\xf0\x9f\x91\xa4 unique boosters: **" + ch::fmt_num(unique_boosters_count) + "**";

    auto embed = bronx::create_embed(desc)
        .set_image("attachment://boosts.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("boosts.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats commands — top commands horizontal bar chart
// ================================================================
inline void handle_stats_commands(dpp::cluster& bot, bronx::db::Database* db,
                                  uint64_t guild_id, uint64_t channel_id,
                                  int days, std::function<void(const dpp::message&)> reply_fn) {
    namespace sq = bronx::db::stats_queries;
    namespace ch = bronx::chart;

    auto cmds = sq::top_commands(db, guild_id, days, 15);

    if (cmds.empty()) {
        reply_fn(dpp::message(channel_id, bronx::info("no command usage data yet \xe2\x80\x94 check back later")));
        return;
    }

    std::vector<ch::BarItem> bars;
    for (auto& c : cmds) bars.push_back({c.command, c.count});

    std::string img = ch::render_horizontal_bar_chart(
        "top commands (" + std::to_string(days) + "d)", bars);

    int64_t total = 0;
    for (auto& c : cmds) total += c.count;
    std::string desc = "**top commands** \xe2\x80\x94 last " + std::to_string(days) + " days\n";
    for (size_t i = 0; i < std::min(cmds.size(), (size_t)10); ++i) {
        std::string medal = (i == 0) ? "\xf0\x9f\xa5\x87" : (i == 1) ? "\xf0\x9f\xa5\x88" : (i == 2) ? "\xf0\x9f\xa5\x89" : std::to_string(i + 1) + ".";
        double pct = total > 0 ? (cmds[i].count * 100.0 / total) : 0;
        char pct_buf[16];
        snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct);
        desc += medal + " `" + cmds[i].command + "` \xe2\x80\x94 " + ch::fmt_num(cmds[i].count) + " (" + pct_buf + ")\n";
    }

    auto embed = bronx::create_embed(desc)
        .set_image("attachment://commands.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("commands.png", img);
    reply_fn(msg);
}

// ================================================================
//  stats channel <#channel> — command usage in a specific channel
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
    for (auto& [cmd, cnt] : agg) bars.push_back({cmd, cnt});
    std::sort(bars.begin(), bars.end(), [](auto& a, auto& b) { return a.value > b.value; });
    if (bars.size() > 15) bars.resize(15);

    std::string chan_label = filter_channel ? "<#" + std::to_string(filter_channel) + ">" : "all channels";
    std::string img = ch::render_horizontal_bar_chart(
        std::string("commands in ") + (filter_channel ? "channel" : "all channels") + " (" + std::to_string(days) + "d)",
        bars);

    auto embed = bronx::create_embed("**command usage** in " + chan_label + " \xe2\x80\x94 last " + std::to_string(days) + " days")
        .set_image("attachment://channel.png");

    dpp::message msg(channel_id, "");
    msg.add_embed(embed);
    if (!img.empty()) msg.add_file("channel.png", img);
    reply_fn(msg);
}


// ================================================================
//  get_stats_commands — factory function
// ================================================================
inline std::vector<Command*> get_stats_commands(bronx::db::Database* db) {
    static std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    static Command stats_cmd("stats", "view server statistics with charts", "utility",
        {"serverstats", "statistics"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (event.msg.guild_id == 0) {
                bronx::send_message(bot, event, bronx::error("stats are only available in servers"));
                return;
            }
            uint64_t gid = event.msg.guild_id;
            uint64_t cid = event.msg.channel_id;

            std::string sub = args.size() > 0 ? args[0] : "";
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            auto reply = [&bot, &event](const dpp::message& msg) {
                bronx::send_message(bot, event, msg);
            };

            if (sub == "members" || sub == "member" || sub == "users" || sub == "user") {
                handle_stats_members(bot, db, gid, cid, parse_days(args, 1), reply);
            } else if (sub == "messages" || sub == "msgs" || sub == "msg") {
                handle_stats_messages(bot, db, gid, cid, parse_days(args, 1), reply);
            } else if (sub == "voice" || sub == "vc") {
                handle_stats_voice(bot, db, gid, cid, parse_days(args, 1), reply);
            } else if (sub == "boosts" || sub == "boost") {
                handle_stats_boosts(bot, db, gid, cid, parse_days(args, 1), reply);
            } else if (sub == "commands" || sub == "cmds") {
                handle_stats_commands(bot, db, gid, cid, parse_days(args, 1), reply);
            } else if (sub == "channel" || sub == "ch") {
                uint64_t filter_ch = 0;
                if (!event.msg.mention_channels.empty()) {
                    filter_ch = event.msg.mention_channels[0].id;
                } else if (args.size() > 1) {
                    try {
                        std::string id_str = args[1];
                        if (id_str.size() > 2 && id_str[0] == '<' && id_str[1] == '#')
                            id_str = id_str.substr(2, id_str.size() - 3);
                        filter_ch = std::stoull(id_str);
                    } catch (...) {}
                }
                handle_stats_channel(bot, db, gid, cid, filter_ch, parse_days(args, 2), reply);
            } else {
                handle_stats_summary(bot, db, gid, cid, parse_days(args, 0), reply);
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (event.command.guild_id == 0) {
                bronx::safe_slash_reply(bot, event, bronx::error("stats are only available in servers"));
                return;
            }
            uint64_t gid = event.command.guild_id;
            uint64_t cid = event.command.channel_id;

            auto reply = [&bot, &event](const dpp::message& msg) {
                bronx::safe_slash_reply(bot, event, msg);
            };

            auto sub_cmd = event.command.get_command_interaction();
            if (sub_cmd.options.empty()) {
                handle_stats_summary(bot, db, gid, cid, parse_days_slash(event), reply);
                return;
            }

            auto& sub = sub_cmd.options[0];
            auto get_range = [&sub]() -> int {
                int days = 7;
                for (auto& o : sub.options) {
                    if (o.name == "range") {
                        auto v = std::get<std::string>(o.value);
                        if (v == "14d") days = 14;
                        else if (v == "30d") days = 30;
                    }
                }
                return days;
            };

            if (sub.name == "members") {
                handle_stats_members(bot, db, gid, cid, get_range(), reply);
            } else if (sub.name == "messages") {
                handle_stats_messages(bot, db, gid, cid, get_range(), reply);
            } else if (sub.name == "voice") {
                handle_stats_voice(bot, db, gid, cid, get_range(), reply);
            } else if (sub.name == "boosts") {
                handle_stats_boosts(bot, db, gid, cid, get_range(), reply);
            } else if (sub.name == "commands") {
                handle_stats_commands(bot, db, gid, cid, get_range(), reply);
            } else if (sub.name == "channel") {
                int days = 7;
                dpp::snowflake ch_id = 0;
                for (auto& o : sub.options) {
                    if (o.name == "range") {
                        auto v = std::get<std::string>(o.value);
                        if (v == "14d") days = 14;
                        else if (v == "30d") days = 30;
                    } else if (o.name == "channel") {
                        ch_id = std::get<dpp::snowflake>(o.value);
                    }
                }
                handle_stats_channel(bot, db, gid, cid, ch_id, days, reply);
            } else {
                handle_stats_summary(bot, db, gid, cid, 7, reply);
            }
        },
        // slash command options (subcommands)
        {
            dpp::command_option(dpp::co_sub_command, "members", "member joins, leaves & active users chart")
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "messages", "messages, edits & deletes chart")
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "voice", "voice channel activity chart")
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "boosts", "server boost activity chart")
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "commands", "top used commands chart")
                .add_option(range_option()),
            dpp::command_option(dpp::co_sub_command, "channel", "command usage in a specific channel")
                .add_option(dpp::command_option(dpp::co_channel, "channel", "channel to view stats for", true))
                .add_option(range_option())
        }
    );

    stats_cmd.extended_description = "view server statistics with visual charts";
    stats_cmd.detailed_usage = "stats [members|messages|voice|boosts|commands|channel <#ch>] [7d|14d|30d]";
    stats_cmd.subcommands = {
        {"members",           "member joins, leaves & active users line chart"},
        {"messages",          "message volume, edits & deletes line chart"},
        {"voice",             "voice channel activity line chart"},
        {"boosts",            "server boost & unboost activity line chart"},
        {"commands",          "top used commands as a bar chart"},
        {"channel <#channel>","command usage breakdown for a specific channel"}
    };
    stats_cmd.examples = {
        "stats",
        "stats members 14d",
        "stats messages 30d",
        "stats voice 7d",
        "stats boosts 14d",
        "stats commands 14d",
        "stats channel #general"
    };

    cmds.push_back(&stats_cmd);
    return cmds;
}

} // namespace commands
