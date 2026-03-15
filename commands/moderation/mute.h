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
inline uint64_t mute_parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

inline Command* get_mute_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "mute", "assign mute role to a user", "moderation",
        {"m"}, true,
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
                    bronx::send_message(bot, event, bronx::error("you don't have permission to mute members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `mute @user [duration] [reason]`"));
                return;
            }

            uint64_t target_id = mute_parse_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            if (target_id == mod_id) {
                bronx::send_message(bot, event, bronx::error("you can't mute yourself"));
                return;
            }

            if (target_id == bot.me.id) {
                bronx::send_message(bot, event, bronx::error("you can't mute the bot"));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't mute that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't mute that user — their role is higher than yours"));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // check mute role is configured
            if (!config.has_value() || config.value().mute_role_id == 0) {
                bronx::send_message(bot, event, bronx::error("mute role is not configured — use the setup command to set one"));
                return;
            }

            // parse duration and reason
            uint32_t duration = 0;
            std::string reason;
            size_t reason_start = 1;

            if (args.size() > 1) {
                duration = parse_duration(args[1]);
                if (duration > 0) {
                    reason_start = 2;
                } else {
                    reason_start = 1;
                }
            }

            if (duration == 0) {
                duration = config.value().default_duration_mute;
            }

            for (size_t i = reason_start; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "mute", reason, duration, config.value().point_mute);
            }

            // apply mute role
            uint64_t mute_role = config.value().mute_role_id;
            bot.guild_member_add_role(guild_id, target_id, mute_role,
                [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, &event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to add mute role: " + cb.get_error().message));
                        return;
                    }

                    // create infraction
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "mute", reason, config.value().point_mute,
                        duration);

                    if (!inf.has_value()) {
                        bronx::send_message(bot, event, bronx::error("mute applied but failed to create infraction record"));
                        return;
                    }

                    // schedule expiry — on expiry, the role needs to be removed
                    // we store the mute_role_id in metadata so the expiry handler can remove it
                    std::string meta = "{\"mute_role_id\":" + std::to_string(config.value().mute_role_id) + "}";
                    schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "mute", target_id, duration, meta);

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **muted** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("mute"));
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
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_moderate_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to mute members")).set_flags(dpp::m_ephemeral));
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
                event.reply(dpp::message().add_embed(bronx::error("you can't mute yourself")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (target_id == bot.me.id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't mute the bot")).set_flags(dpp::m_ephemeral));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't mute that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't mute that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            if (!config.has_value() || config.value().mute_role_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("mute role is not configured — use the setup command to set one")).set_flags(dpp::m_ephemeral));
                return;
            }

            // parse optional duration
            uint32_t duration = 0;
            auto dur_param = event.get_parameter("duration");
            if (std::holds_alternative<std::string>(dur_param)) {
                duration = parse_duration(std::get<std::string>(dur_param));
            }
            if (duration == 0) {
                duration = config.value().default_duration_mute;
            }

            // parse optional reason
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "mute", reason, duration, config.value().point_mute);
            }

            // apply mute role
            uint64_t mute_role = config.value().mute_role_id;
            bot.guild_member_add_role(guild_id, target_id, mute_role,
                [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to add mute role: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // create infraction
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "mute", reason, config.value().point_mute,
                        duration);

                    if (!inf.has_value()) {
                        event.reply(dpp::message().add_embed(bronx::error("mute applied but failed to create infraction record")).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // schedule expiry
                    std::string meta = "{\"mute_role_id\":" + std::to_string(config.value().mute_role_id) + "}";
                    schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "mute", target_id, duration, meta);

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **muted** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("mute"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                });
        },
        // options
        {
            dpp::command_option(dpp::co_user, "user", "the user to mute", true),
            dpp::command_option(dpp::co_string, "duration", "mute duration (e.g. 10m, 1h, 3d)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for the mute", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
