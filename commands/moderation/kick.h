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
inline uint64_t kick_parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

inline Command* get_kick_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "kick", "kick a member from the server", "moderation",
        {"k"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_kick_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to kick members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `kick @user [reason]`"));
                return;
            }

            uint64_t target_id = kick_parse_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            if (target_id == mod_id) {
                bronx::send_message(bot, event, bronx::error("you can't kick yourself"));
                return;
            }

            if (target_id == bot.me.id) {
                bronx::send_message(bot, event, bronx::error("you can't kick the bot"));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't kick that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't kick that user — their role is higher than yours"));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // build reason from remaining args
            std::string reason;
            for (size_t i = 1; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user BEFORE kicking (they won't receive DM after being removed)
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "kick", reason, 0, config.value().point_kick);
            }

            // kick member
            bot.guild_member_delete(guild_id, target_id,
                [&bot, db, guild_id, target_id, mod_id, reason, config, guild_name, &event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to kick user: " + cb.get_error().message));
                        return;
                    }

                    // create infraction (duration is for record purposes only)
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "kick", reason, config.value().point_kick,
                        config.value().default_duration_kick);

                    if (!inf.has_value()) {
                        bronx::send_message(bot, event, bronx::error("user kicked but failed to create infraction record"));
                        return;
                    }

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **kicked** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("kick"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                });
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
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_kick_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to kick members")).set_flags(dpp::m_ephemeral));
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
                event.reply(dpp::message().add_embed(bronx::error("you can't kick yourself")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (target_id == bot.me.id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't kick the bot")).set_flags(dpp::m_ephemeral));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't kick that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't kick that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // parse optional reason
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user BEFORE kicking
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "kick", reason, 0, config.value().point_kick);
            }

            // kick member
            bot.guild_member_delete(guild_id, target_id,
                [&bot, db, guild_id, target_id, mod_id, reason, config, guild_name, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to kick user: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // create infraction
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "kick", reason, config.value().point_kick,
                        config.value().default_duration_kick);

                    if (!inf.has_value()) {
                        event.reply(dpp::message().add_embed(bronx::error("user kicked but failed to create infraction record")).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **kicked** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("kick"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                });
        },
        // options
        {
            dpp::command_option(dpp::co_user, "user", "the user to kick", true),
            dpp::command_option(dpp::co_string, "reason", "reason for the kick", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
