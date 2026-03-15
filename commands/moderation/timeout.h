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

// parse @mention or raw snowflake from text args
inline uint64_t parse_user_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

// max discord timeout is 28 days
static constexpr uint32_t MAX_TIMEOUT_SECONDS = 2419200;

inline Command* get_timeout_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "timeout", "apply discord native timeout to a user", "moderation",
        {"to"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                // check discord permission
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_moderate_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to timeout members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `timeout @user [duration] [reason]`"));
                return;
            }

            // parse target user
            uint64_t target_id = parse_user_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            if (target_id == mod_id) {
                bronx::send_message(bot, event, bronx::error("you can't timeout yourself"));
                return;
            }

            if (target_id == bot.me.id) {
                bronx::send_message(bot, event, bronx::error("you can't timeout the bot"));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't timeout that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't timeout that user — their role is higher than yours"));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // parse duration and reason from remaining args
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
                duration = config.value().default_duration_timeout;
            }

            // cap at discord max
            if (duration > MAX_TIMEOUT_SECONDS) {
                duration = MAX_TIMEOUT_SECONDS;
            }

            // build reason
            for (size_t i = reason_start; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            // get guild name for dm
            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "timeout", reason, duration, config.value().point_timeout);
            }

            // apply discord timeout
            bot.guild_member_timeout(guild_id, target_id, time(0) + duration,
                [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, &event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to timeout user: " + cb.get_error().message));
                        return;
                    }

                    // create infraction
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "timeout", reason, config.value().point_timeout,
                        duration);

                    if (!inf.has_value()) {
                        bronx::send_message(bot, event, bronx::error("timeout applied but failed to create infraction record"));
                        return;
                    }

                    // schedule expiry
                    schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "timeout", target_id, duration);

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **timed out** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("timeout"));
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
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to timeout members")).set_flags(dpp::m_ephemeral));
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
                event.reply(dpp::message().add_embed(bronx::error("you can't timeout yourself")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (target_id == bot.me.id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't timeout the bot")).set_flags(dpp::m_ephemeral));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't timeout that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't timeout that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // parse optional duration
            uint32_t duration = 0;
            auto dur_param = event.get_parameter("duration");
            if (std::holds_alternative<std::string>(dur_param)) {
                duration = parse_duration(std::get<std::string>(dur_param));
            }
            if (duration == 0) {
                duration = config.value().default_duration_timeout;
            }
            if (duration > MAX_TIMEOUT_SECONDS) {
                duration = MAX_TIMEOUT_SECONDS;
            }

            // parse optional reason
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            // get guild name for dm
            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user if enabled
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "timeout", reason, duration, config.value().point_timeout);
            }

            // apply discord timeout
            bot.guild_member_timeout(guild_id, target_id, time(0) + duration,
                [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to timeout user: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // create infraction
                    auto inf = bronx::db::infraction_operations::create_infraction(
                        db, guild_id, target_id, mod_id,
                        "timeout", reason, config.value().point_timeout,
                        duration);

                    if (!inf.has_value()) {
                        event.reply(dpp::message().add_embed(bronx::error("timeout applied but failed to create infraction record")).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    // schedule expiry
                    schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "timeout", target_id, duration);

                    // check escalation
                    check_and_escalate(bot, db, guild_id, target_id, guild_name);

                    // send mod log
                    send_mod_log(bot, db, guild_id, inf.value());

                    // reply success
                    std::string desc = bronx::EMOJI_CHECK + " **timed out** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    auto embed = bronx::create_embed(desc, get_action_color("timeout"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                });
        },
        // options
        {
            dpp::command_option(dpp::co_user, "user", "the user to timeout", true),
            dpp::command_option(dpp::co_string, "duration", "timeout duration (e.g. 10m, 1h, 3d)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for the timeout", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
