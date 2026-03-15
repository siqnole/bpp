#pragma once
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_infractions_config_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    // ── helper: build "view" embed from config ──
    auto build_view_embed = [](const bronx::db::InfractionConfig& cfg) -> dpp::embed {
        auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
        embed.set_title("infraction configuration");

        // points
        std::string pts;
        pts += "**warn:** " + std::to_string(cfg.point_warn) + "\n";
        pts += "**timeout:** " + std::to_string(cfg.point_timeout) + "\n";
        pts += "**mute:** " + std::to_string(cfg.point_mute) + "\n";
        pts += "**kick:** " + std::to_string(cfg.point_kick) + "\n";
        pts += "**ban:** " + std::to_string(cfg.point_ban);
        embed.add_field("points", pts, true);

        // durations
        std::string dur;
        dur += "**warn:** " + format_duration(cfg.default_duration_warn) + "\n";
        dur += "**timeout:** " + format_duration(cfg.default_duration_timeout) + "\n";
        dur += "**mute:** " + format_duration(cfg.default_duration_mute) + "\n";
        dur += "**kick:** " + format_duration(cfg.default_duration_kick) + "\n";
        dur += "**ban:** " + format_duration(cfg.default_duration_ban);
        embed.add_field("default durations", dur, true);

        // roles/channels
        std::string setup;
        setup += "**mute role:** " + (cfg.mute_role_id ? "<@&" + std::to_string(cfg.mute_role_id) + ">" : "not set") + "\n";
        setup += "**jail role:** " + (cfg.jail_role_id ? "<@&" + std::to_string(cfg.jail_role_id) + ">" : "not set") + "\n";
        setup += "**jail channel:** " + (cfg.jail_channel_id ? "<#" + std::to_string(cfg.jail_channel_id) + ">" : "not set") + "\n";
        setup += "**log channel:** " + (cfg.log_channel_id ? "<#" + std::to_string(cfg.log_channel_id) + ">" : "not set") + "\n";
        setup += "**dm on action:** " + std::string(cfg.dm_on_action ? "yes" : "no");
        embed.add_field("setup", setup, false);

        // escalation rules
        if (!cfg.escalation_rules.empty() && cfg.escalation_rules != "[]") {
            auto rules = parse_escalation_rules(cfg.escalation_rules);
            std::string esc;
            int idx = 0;
            for (auto& r : rules) {
                esc += "`" + std::to_string(idx) + "` **" + std::to_string(r.threshold_points) + " pts** in **"
                     + std::to_string(r.within_days) + "d** → " + r.action;
                if (r.action_duration_seconds > 0) esc += " (" + format_duration(r.action_duration_seconds) + ")";
                esc += "\n";
                idx++;
            }
            embed.add_field("escalation rules", esc, false);
        } else {
            embed.add_field("escalation rules", "none configured", false);
        }

        return embed;
    };

    // ── helper: resolve type name to config field setters ──
    auto set_points_for_type = [](bronx::db::InfractionConfig& cfg, const std::string& type, double value) -> bool {
        if (type == "warn")    { cfg.point_warn = value; return true; }
        if (type == "timeout") { cfg.point_timeout = value; return true; }
        if (type == "mute")    { cfg.point_mute = value; return true; }
        if (type == "kick")    { cfg.point_kick = value; return true; }
        if (type == "ban")     { cfg.point_ban = value; return true; }
        return false;
    };

    auto set_duration_for_type = [](bronx::db::InfractionConfig& cfg, const std::string& type, uint32_t seconds) -> bool {
        if (type == "warn")    { cfg.default_duration_warn = seconds; return true; }
        if (type == "timeout") { cfg.default_duration_timeout = seconds; return true; }
        if (type == "mute")    { cfg.default_duration_mute = seconds; return true; }
        if (type == "kick")    { cfg.default_duration_kick = seconds; return true; }
        if (type == "ban")     { cfg.default_duration_ban = seconds; return true; }
        return false;
    };

    cmd = new Command("infractions", "configure infraction settings", "moderation", {"inf-config"}, true,
        // ── text handler ──
        [db, build_view_embed, set_points_for_type, set_duration_for_type](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can configure infractions"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: infractions <view|set-points|set-duration|set-escalation> [args...]"));
                return;
            }

            std::string sub = args[0];
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});

            // ── view ──
            if (sub == "view") {
                bronx::send_message(bot, event, build_view_embed(config));
                return;
            }

            // ── set-points <type> <value> ──
            if (sub == "set-points") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: infractions set-points <type> <value>"));
                    return;
                }
                std::string type = args[1];
                double value = 0;
                try { value = std::stod(args[2]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid point value"));
                    return;
                }
                if (!set_points_for_type(config, type, value)) {
                    bronx::send_message(bot, event, bronx::error("invalid type — use: warn, timeout, mute, kick, ban"));
                    return;
                }
                config.guild_id = guild_id;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                bronx::send_message(bot, event, bronx::success("set **" + type + "** points to **" + std::to_string(value) + "**"));
                return;
            }

            // ── set-duration <type> <duration> ──
            if (sub == "set-duration") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: infractions set-duration <type> <duration>"));
                    return;
                }
                std::string type = args[1];
                uint32_t seconds = parse_duration(args[2]);
                if (seconds == 0) {
                    bronx::send_message(bot, event, bronx::error("invalid duration — use e.g. 1h, 3d, 1w"));
                    return;
                }
                if (!set_duration_for_type(config, type, seconds)) {
                    bronx::send_message(bot, event, bronx::error("invalid type — use: warn, timeout, mute, kick, ban"));
                    return;
                }
                config.guild_id = guild_id;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                bronx::send_message(bot, event, bronx::success("set **" + type + "** default duration to **" + format_duration(seconds) + "**"));
                return;
            }

            // ── set-escalation add|remove|list ──
            if (sub == "set-escalation") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: infractions set-escalation <add|remove|list>"));
                    return;
                }
                std::string action = args[1];

                auto rules = parse_escalation_rules(config.escalation_rules);

                if (action == "list") {
                    if (rules.empty()) {
                        bronx::send_message(bot, event, bronx::info("no escalation rules configured"));
                        return;
                    }
                    std::string desc;
                    int idx = 0;
                    for (auto& r : rules) {
                        desc += "`" + std::to_string(idx) + "` **" + std::to_string(r.threshold_points) + " pts** in **"
                             + std::to_string(r.within_days) + "d** → " + r.action;
                        if (r.action_duration_seconds > 0) desc += " (" + format_duration(r.action_duration_seconds) + ")";
                        if (!r.reason_template.empty()) desc += " — " + r.reason_template;
                        desc += "\n";
                        idx++;
                    }
                    bronx::send_message(bot, event, bronx::create_embed(desc, bronx::COLOR_DEFAULT).set_title("escalation rules"));
                    return;
                }

                if (action == "add") {
                    // set-escalation add <points> <days> <action> [duration] [reason...]
                    if (args.size() < 5) {
                        bronx::send_message(bot, event, bronx::error("usage: infractions set-escalation add <points> <days> <action> [duration] [reason]"));
                        return;
                    }
                    bronx::db::EscalationRule rule;
                    try { rule.threshold_points = std::stod(args[2]); } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid points value"));
                        return;
                    }
                    try { rule.within_days = std::stoi(args[3]); } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid days value"));
                        return;
                    }
                    rule.action = args[4];
                    if (rule.action != "timeout" && rule.action != "mute" && rule.action != "kick" &&
                        rule.action != "ban" && rule.action != "jail") {
                        bronx::send_message(bot, event, bronx::error("invalid action — use: timeout, mute, jail, kick, ban"));
                        return;
                    }
                    rule.action_duration_seconds = 0;
                    rule.reason_template = "auto-escalation: {points} points in {days} days";
                    if (args.size() > 5) {
                        uint32_t dur = parse_duration(args[5]);
                        if (dur > 0) {
                            rule.action_duration_seconds = dur;
                            // remaining args are reason
                            if (args.size() > 6) {
                                rule.reason_template.clear();
                                for (size_t i = 6; i < args.size(); i++) {
                                    if (!rule.reason_template.empty()) rule.reason_template += " ";
                                    rule.reason_template += args[i];
                                }
                            }
                        } else {
                            // treat all remaining as reason
                            rule.reason_template.clear();
                            for (size_t i = 5; i < args.size(); i++) {
                                if (!rule.reason_template.empty()) rule.reason_template += " ";
                                rule.reason_template += args[i];
                            }
                        }
                    }

                    rules.push_back(rule);

                    // serialize back
                    nlohmann::json arr = nlohmann::json::array();
                    for (auto& r : rules) {
                        {
                            nlohmann::json robj;
                            robj["threshold_points"] = r.threshold_points;
                            robj["within_days"] = r.within_days;
                            robj["action"] = r.action;
                            robj["action_duration_seconds"] = r.action_duration_seconds;
                            robj["reason_template"] = r.reason_template;
                            arr.push_back(robj);
                        }
                    }
                    config.escalation_rules = arr.dump();
                    config.guild_id = guild_id;
                    bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                    bronx::send_message(bot, event, bronx::success("added escalation rule: **" + std::to_string(rule.threshold_points) + " pts** in **"
                        + std::to_string(rule.within_days) + "d** → " + rule.action));
                    return;
                }

                if (action == "remove") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: infractions set-escalation remove <index>"));
                        return;
                    }
                    int idx = 0;
                    try { idx = std::stoi(args[2]); } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid index"));
                        return;
                    }
                    if (idx < 0 || idx >= static_cast<int>(rules.size())) {
                        bronx::send_message(bot, event, bronx::error("index out of range (0-" + std::to_string(rules.size() - 1) + ")"));
                        return;
                    }
                    rules.erase(rules.begin() + idx);

                    nlohmann::json arr = nlohmann::json::array();
                    for (auto& r : rules) {
                        {
                            nlohmann::json robj;
                            robj["threshold_points"] = r.threshold_points;
                            robj["within_days"] = r.within_days;
                            robj["action"] = r.action;
                            robj["action_duration_seconds"] = r.action_duration_seconds;
                            robj["reason_template"] = r.reason_template;
                            arr.push_back(robj);
                        }
                    }
                    config.escalation_rules = arr.dump();
                    config.guild_id = guild_id;
                    bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                    bronx::send_message(bot, event, bronx::success("removed escalation rule at index **" + std::to_string(idx) + "**"));
                    return;
                }

                bronx::send_message(bot, event, bronx::error("unknown subcommand — use: add, remove, list"));
                return;
            }

            bronx::send_message(bot, event, bronx::error("unknown subcommand — use: view, set-points, set-duration, set-escalation"));
        },
        // ── slash handler ──
        [db, build_view_embed, set_points_for_type, set_duration_for_type](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can configure infractions")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto sub_cmd = event.command.get_command_interaction();
            if (sub_cmd.options.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("specify a subcommand")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string sub_name = sub_cmd.options[0].name;
            auto& sub_opts = sub_cmd.options[0].options;

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});

            // ── view ──
            if (sub_name == "view") {
                event.reply(dpp::message().add_embed(build_view_embed(config)));
                return;
            }

            // ── set-points ──
            if (sub_name == "set-points") {
                std::string type;
                double value = 0;
                for (auto& o : sub_opts) {
                    if (o.name == "type") type = std::get<std::string>(o.value);
                    if (o.name == "value") value = std::get<double>(o.value);
                }
                if (!set_points_for_type(config, type, value)) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid type")).set_flags(dpp::m_ephemeral));
                    return;
                }
                config.guild_id = guild_id;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                event.reply(dpp::message().add_embed(bronx::success("set **" + type + "** points to **" + std::to_string(value) + "**")));
                return;
            }

            // ── set-duration ──
            if (sub_name == "set-duration") {
                std::string type;
                std::string dur_str;
                for (auto& o : sub_opts) {
                    if (o.name == "type") type = std::get<std::string>(o.value);
                    if (o.name == "duration") dur_str = std::get<std::string>(o.value);
                }
                uint32_t seconds = parse_duration(dur_str);
                if (seconds == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid duration — use e.g. 1h, 3d, 1w")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!set_duration_for_type(config, type, seconds)) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid type")).set_flags(dpp::m_ephemeral));
                    return;
                }
                config.guild_id = guild_id;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                event.reply(dpp::message().add_embed(bronx::success("set **" + type + "** default duration to **" + format_duration(seconds) + "**")));
                return;
            }

            // ── set-escalation ──
            if (sub_name == "set-escalation") {
                if (sub_opts.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("specify an action: add, remove, or list")).set_flags(dpp::m_ephemeral));
                    return;
                }

                // sub_opts[0] is the nested sub-command (add/remove/list)
                std::string action_name = sub_opts[0].name;
                auto& action_opts = sub_opts[0].options;

                auto rules = parse_escalation_rules(config.escalation_rules);

                if (action_name == "list") {
                    if (rules.empty()) {
                        event.reply(dpp::message().add_embed(bronx::info("no escalation rules configured")));
                        return;
                    }
                    std::string desc;
                    int idx = 0;
                    for (auto& r : rules) {
                        desc += "`" + std::to_string(idx) + "` **" + std::to_string(r.threshold_points) + " pts** in **"
                             + std::to_string(r.within_days) + "d** → " + r.action;
                        if (r.action_duration_seconds > 0) desc += " (" + format_duration(r.action_duration_seconds) + ")";
                        if (!r.reason_template.empty()) desc += " — " + r.reason_template;
                        desc += "\n";
                        idx++;
                    }
                    event.reply(dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_DEFAULT).set_title("escalation rules")));
                    return;
                }

                if (action_name == "add") {
                    bronx::db::EscalationRule rule;
                    rule.action_duration_seconds = 0;
                    rule.reason_template = "auto-escalation: {points} points in {days} days";
                    for (auto& o : action_opts) {
                        if (o.name == "points") rule.threshold_points = std::get<double>(o.value);
                        if (o.name == "days") rule.within_days = static_cast<int>(std::get<int64_t>(o.value));
                        if (o.name == "action") rule.action = std::get<std::string>(o.value);
                        if (o.name == "duration") {
                            rule.action_duration_seconds = parse_duration(std::get<std::string>(o.value));
                        }
                        if (o.name == "reason") rule.reason_template = std::get<std::string>(o.value);
                    }
                    rules.push_back(rule);

                    nlohmann::json arr = nlohmann::json::array();
                    for (auto& r : rules) {
                        {
                            nlohmann::json robj;
                            robj["threshold_points"] = r.threshold_points;
                            robj["within_days"] = r.within_days;
                            robj["action"] = r.action;
                            robj["action_duration_seconds"] = r.action_duration_seconds;
                            robj["reason_template"] = r.reason_template;
                            arr.push_back(robj);
                        }
                    }
                    config.escalation_rules = arr.dump();
                    config.guild_id = guild_id;
                    bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                    event.reply(dpp::message().add_embed(bronx::success("added escalation rule: **" + std::to_string(rule.threshold_points) + " pts** in **"
                        + std::to_string(rule.within_days) + "d** → " + rule.action)));
                    return;
                }

                if (action_name == "remove") {
                    int64_t idx = 0;
                    for (auto& o : action_opts) {
                        if (o.name == "index") idx = std::get<int64_t>(o.value);
                    }
                    if (idx < 0 || idx >= static_cast<int64_t>(rules.size())) {
                        event.reply(dpp::message().add_embed(bronx::error("index out of range")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    rules.erase(rules.begin() + idx);

                    nlohmann::json arr = nlohmann::json::array();
                    for (auto& r : rules) {
                        {
                            nlohmann::json robj;
                            robj["threshold_points"] = r.threshold_points;
                            robj["within_days"] = r.within_days;
                            robj["action"] = r.action;
                            robj["action_duration_seconds"] = r.action_duration_seconds;
                            robj["reason_template"] = r.reason_template;
                            arr.push_back(robj);
                        }
                    }
                    config.escalation_rules = arr.dump();
                    config.guild_id = guild_id;
                    bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                    event.reply(dpp::message().add_embed(bronx::success("removed escalation rule at index **" + std::to_string(idx) + "**")));
                    return;
                }

                event.reply(dpp::message().add_embed(bronx::error("unknown action")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.reply(dpp::message().add_embed(bronx::error("unknown subcommand")).set_flags(dpp::m_ephemeral));
        },
        // ── slash options ──
        {
            // view
            dpp::command_option(dpp::co_sub_command, "view", "view current infraction config"),
            // set-points
            dpp::command_option(dpp::co_sub_command, "set-points", "set point value for an infraction type")
                .add_option(dpp::command_option(dpp::co_string, "type", "infraction type", true)
                    .add_choice(dpp::command_option_choice("warn", std::string("warn")))
                    .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
                    .add_choice(dpp::command_option_choice("mute", std::string("mute")))
                    .add_choice(dpp::command_option_choice("kick", std::string("kick")))
                    .add_choice(dpp::command_option_choice("ban", std::string("ban")))
                )
                .add_option(dpp::command_option(dpp::co_number, "value", "point value", true)),
            // set-duration
            dpp::command_option(dpp::co_sub_command, "set-duration", "set default duration for an infraction type")
                .add_option(dpp::command_option(dpp::co_string, "type", "infraction type", true)
                    .add_choice(dpp::command_option_choice("warn", std::string("warn")))
                    .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
                    .add_choice(dpp::command_option_choice("mute", std::string("mute")))
                    .add_choice(dpp::command_option_choice("kick", std::string("kick")))
                    .add_choice(dpp::command_option_choice("ban", std::string("ban")))
                )
                .add_option(dpp::command_option(dpp::co_string, "duration", "duration (e.g. 1h, 3d, 1w)", true)),
            // set-escalation (sub-command group)
            dpp::command_option(dpp::co_sub_command_group, "set-escalation", "manage escalation rules")
                .add_option(dpp::command_option(dpp::co_sub_command, "add", "add a new escalation rule")
                    .add_option(dpp::command_option(dpp::co_number, "points", "point threshold", true))
                    .add_option(dpp::command_option(dpp::co_integer, "days", "within how many days", true))
                    .add_option(dpp::command_option(dpp::co_string, "action", "action to take", true)
                        .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
                        .add_choice(dpp::command_option_choice("mute", std::string("mute")))
                        .add_choice(dpp::command_option_choice("jail", std::string("jail")))
                        .add_choice(dpp::command_option_choice("kick", std::string("kick")))
                        .add_choice(dpp::command_option_choice("ban", std::string("ban")))
                    )
                    .add_option(dpp::command_option(dpp::co_string, "duration", "duration for the action (e.g. 1h, 3d)", false))
                    .add_option(dpp::command_option(dpp::co_string, "reason", "reason template", false))
                )
                .add_option(dpp::command_option(dpp::co_sub_command, "remove", "remove an escalation rule by index")
                    .add_option(dpp::command_option(dpp::co_integer, "index", "rule index to remove", true))
                )
                .add_option(dpp::command_option(dpp::co_sub_command, "list", "list all escalation rules"))
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands