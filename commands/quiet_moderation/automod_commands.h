#pragma once
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_config_operations.h"

namespace commands {
namespace quiet_moderation {

// ── Automod Configuration Slash Command ────────────────────────
// /automod <subcommand> — configure all auto-moderation guard features.
// Requires MANAGE_GUILD (admin-level) permission.

namespace automod_cmd_detail {

// Valid actions for guards
inline bool is_valid_action(const std::string& act) {
    return act == "kick" || act == "ban" || act == "timeout" || act == "mute";
}

// Build status overview embed showing all automod settings.
inline dpp::embed build_status_embed(const bronx::db::AutomodConfig& cfg) {
    auto embed = bronx::create_embed("", 0x5865F2); // blurple
    embed.set_title("automod configuration");

    // ── Account Age ──
    std::string age_status = cfg.account_age_enabled ? "✅ enabled" : "❌ disabled";
    age_status += "\nminimum: **" + std::to_string(cfg.account_age_days) + "** days";
    age_status += "\naction: **" + cfg.account_age_action + "**";
    embed.add_field("account age guard", age_status, true);

    // ── Default Avatar ──
    std::string avatar_status = cfg.default_avatar_enabled ? "✅ enabled" : "❌ disabled";
    avatar_status += "\naction: **" + cfg.default_avatar_action + "**";
    embed.add_field("avatar guard", avatar_status, true);

    // ── Mutual Servers ──
    std::string mutual_status = cfg.mutual_servers_enabled ? "✅ enabled" : "❌ disabled";
    mutual_status += "\nminimum: **" + std::to_string(cfg.mutual_servers_min) + "** servers";
    mutual_status += "\naction: **" + cfg.mutual_servers_action + "**";
    embed.add_field("mutual server guard", mutual_status, true);

    // ── Nickname Sanitize ──
    std::string nick_status = cfg.nickname_sanitize_enabled ? "✅ enabled" : "❌ disabled";
    nick_status += "\nformat: `" + cfg.nickname_sanitize_format + "`";
    // Count patterns
    int pattern_count = 0;
    try {
        auto arr = nlohmann::json::parse(cfg.nickname_bad_patterns);
        if (arr.is_array()) pattern_count = static_cast<int>(arr.size());
    } catch (...) {}
    nick_status += "\npatterns: **" + std::to_string(pattern_count) + "**";
    embed.add_field("nickname guard", nick_status, true);

    // ── Escalation ──
    std::string esc_status = cfg.infraction_escalation_enabled ? "✅ enabled" : "❌ disabled";
    embed.add_field("escalation", esc_status, true);

    embed.set_footer(dpp::embed_footer().set_text("use /automod <feature> to configure"));
    embed.set_timestamp(time(0));
    return embed;
}

} // namespace automod_cmd_detail


inline Command* get_automod_command(bronx::db::Database* db) {
    // ── Build slash command options ──
    std::vector<dpp::command_option> opts;

    // /automod account-age <enable|disable> [days] [action]
    {
        dpp::command_option sub(dpp::co_sub_command, "account-age", "configure account age guard");
        sub.add_option(dpp::command_option(dpp::co_string, "toggle", "enable or disable", true)
            .add_choice(dpp::command_option_choice("enable", std::string("enable")))
            .add_choice(dpp::command_option_choice("disable", std::string("disable"))));
        sub.add_option(dpp::command_option(dpp::co_integer, "days", "minimum account age in days", false));
        sub.add_option(dpp::command_option(dpp::co_string, "action", "action to take", false)
            .add_choice(dpp::command_option_choice("kick", std::string("kick")))
            .add_choice(dpp::command_option_choice("ban", std::string("ban")))
            .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
            .add_choice(dpp::command_option_choice("mute", std::string("mute"))));
        opts.push_back(sub);
    }

    // /automod avatar <enable|disable> [action]
    {
        dpp::command_option sub(dpp::co_sub_command, "avatar", "configure avatar guard");
        sub.add_option(dpp::command_option(dpp::co_string, "toggle", "enable or disable", true)
            .add_choice(dpp::command_option_choice("enable", std::string("enable")))
            .add_choice(dpp::command_option_choice("disable", std::string("disable"))));
        sub.add_option(dpp::command_option(dpp::co_string, "action", "action to take", false)
            .add_choice(dpp::command_option_choice("kick", std::string("kick")))
            .add_choice(dpp::command_option_choice("ban", std::string("ban")))
            .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
            .add_choice(dpp::command_option_choice("mute", std::string("mute"))));
        opts.push_back(sub);
    }

    // /automod mutual <enable|disable> [min_servers] [action]
    {
        dpp::command_option sub(dpp::co_sub_command, "mutual", "configure mutual server guard");
        sub.add_option(dpp::command_option(dpp::co_string, "toggle", "enable or disable", true)
            .add_choice(dpp::command_option_choice("enable", std::string("enable")))
            .add_choice(dpp::command_option_choice("disable", std::string("disable"))));
        sub.add_option(dpp::command_option(dpp::co_integer, "min-servers", "minimum mutual servers required", false));
        sub.add_option(dpp::command_option(dpp::co_string, "action", "action to take", false)
            .add_choice(dpp::command_option_choice("kick", std::string("kick")))
            .add_choice(dpp::command_option_choice("ban", std::string("ban")))
            .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
            .add_choice(dpp::command_option_choice("mute", std::string("mute"))));
        opts.push_back(sub);
    }

    // /automod nickname <subcommand>
    {
        dpp::command_option group(dpp::co_sub_command_group, "nickname", "configure nickname guard");

        // /automod nickname toggle <enable|disable> [format]
        {
            dpp::command_option sub(dpp::co_sub_command, "toggle", "enable or disable nickname guard");
            sub.add_option(dpp::command_option(dpp::co_string, "state", "enable or disable", true)
                .add_choice(dpp::command_option_choice("enable", std::string("enable")))
                .add_choice(dpp::command_option_choice("disable", std::string("disable"))));
            sub.add_option(dpp::command_option(dpp::co_string, "format", "nickname format string (e.g. Moderated User {n})", false));
            group.add_option(sub);
        }

        // /automod nickname add-pattern <pattern>
        {
            dpp::command_option sub(dpp::co_sub_command, "add-pattern", "add a bad-nickname regex pattern");
            sub.add_option(dpp::command_option(dpp::co_string, "pattern", "regex pattern", true));
            group.add_option(sub);
        }

        // /automod nickname remove-pattern <index>
        {
            dpp::command_option sub(dpp::co_sub_command, "remove-pattern", "remove a bad-nickname pattern by index");
            sub.add_option(dpp::command_option(dpp::co_integer, "index", "1-based pattern index", true));
            group.add_option(sub);
        }

        // /automod nickname list-patterns
        {
            dpp::command_option sub(dpp::co_sub_command, "list-patterns", "list all bad-nickname patterns");
            group.add_option(sub);
        }

        opts.push_back(group);
    }

    // /automod escalation <enable|disable>
    {
        dpp::command_option sub(dpp::co_sub_command, "escalation", "toggle infraction escalation for automod actions");
        sub.add_option(dpp::command_option(dpp::co_string, "toggle", "enable or disable", true)
            .add_choice(dpp::command_option_choice("enable", std::string("enable")))
            .add_choice(dpp::command_option_choice("disable", std::string("disable"))));
        opts.push_back(sub);
    }

    // /automod status
    {
        dpp::command_option sub(dpp::co_sub_command, "status", "show current automod configuration");
        opts.push_back(sub);
    }

    // ── Create the Command object ──
    auto* cmd = new Command(
        "automod",
        "configure automatic moderation guards",
        "moderation",
        {},   // no aliases
        true, // slash command
        nullptr, // no text handler
        // ── Slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // ── Permission check ──
            auto guild = dpp::find_guild(event.command.guild_id);
            if (!guild) {
                event.reply(dpp::message("guild not found").set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t perms = 0;
            // Guild owner always has all permissions
            if (guild->owner_id == event.command.member.user_id) {
                perms = UINT64_MAX;
            } else {
                for (const auto& role_id : event.command.member.get_roles()) {
                    auto role = dpp::find_role(role_id);
                    if (role) perms |= role->permissions;
                }
            }
            if (!(perms & dpp::p_manage_guild)) {
                event.reply(dpp::message("you need **manage server** permission to use this command.").set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t guild_id = event.command.guild_id;
            auto config = bronx::db::infraction_config_operations::get_automod_config(db, guild_id)
                              .value_or(bronx::db::AutomodConfig{});

            // ── Determine which subcommand was invoked ──
            auto data = event.command.get_command_interaction();
            if (data.options.empty()) {
                event.reply(dpp::message("please specify a subcommand.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto& top = data.options[0];

            // ────────────────────── status ──────────────────────
            if (top.name == "status") {
                event.reply(dpp::message().add_embed(
                    automod_cmd_detail::build_status_embed(config)));
                return;
            }

            // ────────────────────── account-age ────────────────
            if (top.name == "account-age") {
                std::string toggle;
                for (auto& o : top.options) {
                    if (o.name == "toggle") toggle = std::get<std::string>(o.value);
                    else if (o.name == "days") config.account_age_days = static_cast<uint32_t>(std::get<int64_t>(o.value));
                    else if (o.name == "action") config.account_age_action = std::get<std::string>(o.value);
                }
                config.account_age_enabled = (toggle == "enable");
                bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                auto embed = bronx::create_embed("", 0x57F287);
                embed.set_title("account age guard updated");
                embed.add_field("status", config.account_age_enabled ? "✅ enabled" : "❌ disabled", true);
                embed.add_field("minimum age", std::to_string(config.account_age_days) + " days", true);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // ────────────────────── avatar ─────────────────────
            if (top.name == "avatar") {
                std::string toggle;
                for (auto& o : top.options) {
                    if (o.name == "toggle") toggle = std::get<std::string>(o.value);
                    else if (o.name == "action") config.default_avatar_action = std::get<std::string>(o.value);
                }
                config.default_avatar_enabled = (toggle == "enable");
                bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                auto embed = bronx::create_embed("", 0x57F287);
                embed.set_title("avatar guard updated");
                embed.add_field("status", config.default_avatar_enabled ? "✅ enabled" : "❌ disabled", true);
                embed.add_field("action", config.default_avatar_action, true);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // ────────────────────── mutual ─────────────────────
            if (top.name == "mutual") {
                std::string toggle;
                for (auto& o : top.options) {
                    if (o.name == "toggle") toggle = std::get<std::string>(o.value);
                    else if (o.name == "min-servers") config.mutual_servers_min = static_cast<uint32_t>(std::get<int64_t>(o.value));
                    else if (o.name == "action") config.mutual_servers_action = std::get<std::string>(o.value);
                }
                config.mutual_servers_enabled = (toggle == "enable");
                bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                auto embed = bronx::create_embed("", 0x57F287);
                embed.set_title("mutual server guard updated");
                embed.add_field("status", config.mutual_servers_enabled ? "✅ enabled" : "❌ disabled", true);
                embed.add_field("minimum servers", std::to_string(config.mutual_servers_min), true);
                embed.add_field("action", config.mutual_servers_action, true);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // ────────────────────── nickname (subcommand group) ─
            if (top.name == "nickname") {
                if (top.options.empty()) {
                    event.reply(dpp::message("specify a nickname subcommand.").set_flags(dpp::m_ephemeral));
                    return;
                }

                auto& sub = top.options[0];

                // ── toggle ──
                if (sub.name == "toggle") {
                    std::string state;
                    for (auto& o : sub.options) {
                        if (o.name == "state") state = std::get<std::string>(o.value);
                        else if (o.name == "format") config.nickname_sanitize_format = std::get<std::string>(o.value);
                    }
                    config.nickname_sanitize_enabled = (state == "enable");
                    bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                    auto embed = bronx::create_embed("", 0x57F287);
                    embed.set_title("nickname guard updated");
                    embed.add_field("status", config.nickname_sanitize_enabled ? "✅ enabled" : "❌ disabled", true);
                    embed.add_field("format", "`" + config.nickname_sanitize_format + "`", true);
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                // ── add-pattern ──
                if (sub.name == "add-pattern") {
                    std::string pattern;
                    for (auto& o : sub.options) {
                        if (o.name == "pattern") pattern = std::get<std::string>(o.value);
                    }

                    // Validate the regex
                    try {
                        std::regex test(pattern, std::regex::icase | std::regex::ECMAScript);
                    } catch (const std::regex_error&) {
                        event.reply(dpp::message("❌ invalid regex pattern: `" + pattern + "`").set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // Parse existing patterns array
                    nlohmann::json arr = nlohmann::json::array();
                    try {
                        auto parsed = nlohmann::json::parse(config.nickname_bad_patterns);
                        if (parsed.is_array()) arr = parsed;
                    } catch (...) {}

                    arr.push_back(pattern);
                    config.nickname_bad_patterns = arr.dump();
                    bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                    auto embed = bronx::create_embed("", 0x57F287);
                    embed.set_title("nickname pattern added");
                    embed.set_description("added pattern #" + std::to_string(arr.size()) + ": `" + pattern + "`");
                    embed.add_field("total patterns", std::to_string(arr.size()), true);
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                // ── remove-pattern ──
                if (sub.name == "remove-pattern") {
                    int64_t index = 0;
                    for (auto& o : sub.options) {
                        if (o.name == "index") index = std::get<int64_t>(o.value);
                    }

                    nlohmann::json arr = nlohmann::json::array();
                    try {
                        auto parsed = nlohmann::json::parse(config.nickname_bad_patterns);
                        if (parsed.is_array()) arr = parsed;
                    } catch (...) {}

                    if (index < 1 || index > static_cast<int64_t>(arr.size())) {
                        event.reply(dpp::message("❌ invalid index. use `/automod nickname list-patterns` to see indices.").set_flags(dpp::m_ephemeral));
                        return;
                    }

                    std::string removed = arr[static_cast<size_t>(index - 1)].get<std::string>();
                    arr.erase(arr.begin() + (index - 1));
                    config.nickname_bad_patterns = arr.dump();
                    bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                    auto embed = bronx::create_embed("", 0xED4245);
                    embed.set_title("nickname pattern removed");
                    embed.set_description("removed pattern #" + std::to_string(index) + ": `" + removed + "`");
                    embed.add_field("remaining patterns", std::to_string(arr.size()), true);
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                // ── list-patterns ──
                if (sub.name == "list-patterns") {
                    nlohmann::json arr = nlohmann::json::array();
                    try {
                        auto parsed = nlohmann::json::parse(config.nickname_bad_patterns);
                        if (parsed.is_array()) arr = parsed;
                    } catch (...) {}

                    if (arr.empty()) {
                        event.reply(dpp::message("no nickname patterns configured. use `/automod nickname add-pattern` to add one.").set_flags(dpp::m_ephemeral));
                        return;
                    }

                    std::string desc;
                    for (size_t i = 0; i < arr.size(); ++i) {
                        desc += "**" + std::to_string(i + 1) + ".** `" + arr[i].get<std::string>() + "`\n";
                    }

                    auto embed = bronx::create_embed("", 0x5865F2);
                    embed.set_title("nickname bad patterns");
                    embed.set_description(desc);
                    embed.set_footer(dpp::embed_footer().set_text(std::to_string(arr.size()) + " pattern(s)"));
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                event.reply(dpp::message("unknown nickname subcommand.").set_flags(dpp::m_ephemeral));
                return;
            }

            // ────────────────────── escalation ─────────────────
            if (top.name == "escalation") {
                std::string toggle;
                for (auto& o : top.options) {
                    if (o.name == "toggle") toggle = std::get<std::string>(o.value);
                }
                config.infraction_escalation_enabled = (toggle == "enable");
                bronx::db::infraction_config_operations::upsert_automod_config(db, config);

                auto embed = bronx::create_embed("", 0x57F287);
                embed.set_title("automod escalation updated");
                embed.add_field("status", config.infraction_escalation_enabled ? "✅ enabled" : "❌ disabled", true);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            event.reply(dpp::message("unknown subcommand.").set_flags(dpp::m_ephemeral));
        },
        opts
    );

    // ── Extended help ──
    cmd->extended_description =
        "configure automatic moderation guards that act on member join events and "
        "nickname changes. each guard can independently be enabled/disabled with its "
        "own action (kick, ban, timeout, mute).";
    cmd->subcommands = {
        {"account-age <enable|disable> [days] [action]", "block accounts younger than the specified age"},
        {"avatar <enable|disable> [action]",             "block accounts with the default discord avatar"},
        {"mutual <enable|disable> [min_servers] [action]","block accounts sharing fewer mutual servers with the bot"},
        {"nickname toggle <enable|disable> [format]",    "enable/disable nickname sanitization"},
        {"nickname add-pattern <pattern>",               "add a regex pattern to the bad-nickname list"},
        {"nickname remove-pattern <index>",              "remove a pattern by its 1-based index"},
        {"nickname list-patterns",                       "list all configured bad-nickname patterns"},
        {"escalation <enable|disable>",                  "toggle escalation checks for automod infractions"},
        {"status",                                       "show current automod configuration overview"},
    };
    cmd->examples = {
        "/automod account-age enable days:7 action:kick",
        "/automod avatar enable action:timeout",
        "/automod nickname toggle enable format:Moderated User {n}",
        "/automod nickname add-pattern pattern:(?i)n[i1]gg",
        "/automod status",
    };
    cmd->notes = "all guards create infractions in the moderation log. "
                 "nickname sanitization is cosmetic and does not trigger escalation.";

    return cmd;
}

} // namespace quiet_moderation
} // namespace commands