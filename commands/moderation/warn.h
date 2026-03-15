#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

// parse @mention or raw snowflake
inline uint64_t warn_parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

inline Command* get_warn_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "warn", "issue a warning to a user", "moderation",
        {"w"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_moderate_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to warn members"));
                    return;
                }
            }

            // need at least user + reason
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: `warn @user <reason>`"));
                return;
            }

            uint64_t target_id = warn_parse_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            if (target_id == mod_id) {
                bronx::send_message(bot, event, bronx::error("you can't warn yourself"));
                return;
            }

            if (target_id == bot.me.id) {
                bronx::send_message(bot, event, bronx::error("you can't warn the bot"));
                return;
            }

            // build reason from remaining args (required)
            std::string reason;
            for (size_t i = 1; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            if (reason.empty()) {
                bronx::send_message(bot, event, bronx::error("a reason is required for warnings"));
                return;
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            std::string guild_name;
            auto* guild = dpp::find_guild(guild_id);
            guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "warn", reason, 0, config.value().point_warn);
            }

            // create infraction (no discord action, just a record)
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "warn", reason, config.value().point_warn,
                config.value().default_duration_warn);

            if (!inf.has_value()) {
                bronx::send_message(bot, event, bronx::error("failed to create warning record"));
                return;
            }

            // check escalation (warn points may trigger auto-action like timeout/ban)
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            // get total active points for context
            double active_points = bronx::db::infraction_operations::get_user_active_points(db, guild_id, target_id);

            // reply success
            std::string desc = bronx::EMOJI_CHECK + " **warned** <@" + std::to_string(target_id) + ">"
                + "\n**case:** #" + std::to_string(inf->case_number)
                + "\n**reason:** " + reason
                + "\n**points:** +" + std::to_string(config.value().point_warn)
                + " (total active: " + std::to_string(active_points) + ")";

            auto embed = bronx::create_embed(desc, get_action_color("warn"));
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_moderate_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to warn members")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get user param
            auto user_param = event.get_parameter("user");
            if (!std::holds_alternative<dpp::snowflake>(user_param)) {
                event.reply(dpp::message().add_embed(bronx::error("please mention a user")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t target_id = std::get<dpp::snowflake>(user_param);

            if (target_id == mod_id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't warn yourself")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (target_id == bot.me.id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't warn the bot")).set_flags(dpp::m_ephemeral));
                return;
            }

            // get reason (required)
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            if (reason.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("a reason is required for warnings")).set_flags(dpp::m_ephemeral));
                return;
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            auto* guild = dpp::find_guild(guild_id);
            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "warn", reason, 0, config.value().point_warn);
            }

            // create infraction (no discord action, just a record)
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "warn", reason, config.value().point_warn,
                config.value().default_duration_warn);

            if (!inf.has_value()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to create warning record")).set_flags(dpp::m_ephemeral));
                return;
            }

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            // get total active points for context
            double active_points = bronx::db::infraction_operations::get_user_active_points(db, guild_id, target_id);

            // reply success
            std::string desc = bronx::EMOJI_CHECK + " **warned** <@" + std::to_string(target_id) + ">"
                + "\n**case:** #" + std::to_string(inf->case_number)
                + "\n**reason:** " + reason
                + "\n**points:** +" + std::to_string(config.value().point_warn)
                + " (total active: " + std::to_string(active_points) + ")";

            auto embed = bronx::create_embed(desc, get_action_color("warn"));
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        // options
        {
            dpp::command_option(dpp::co_user, "user", "the user to warn", true),
            dpp::command_option(dpp::co_string, "reason", "reason for the warning", true)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
