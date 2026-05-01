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
            uint64_t target_id = parse_mention(args[0]);
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

            // apply timeout
            apply_timeout_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config.value(),
                [&bot, &event, target_id](const bronx::db::InfractionRow& inf, bool quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **timed out** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(inf.duration_seconds)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("timeout"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                },
                [&bot, &event](const std::string& error_msg) {
                    bronx::send_message(bot, event, bronx::error(error_msg));
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

            // parse optional duration
            uint32_t duration = 0;
            auto dur_param = event.get_parameter("duration");
            if (std::holds_alternative<std::string>(dur_param)) {
                duration = parse_duration(std::get<std::string>(dur_param));
            }

            // parse optional reason
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            // if missing duration or reason, show modal
            if (duration == 0 || reason.empty()) {
                dpp::interaction_modal_response modal("mod_timeout_modal_" + std::to_string(target_id), "Timeout User");
                
                auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
                std::string default_dur = "10m";
                if (config.has_value() && config.value().default_duration_timeout > 0) {
                    default_dur = format_duration(config.value().default_duration_timeout);
                }

                modal.add_component(
                    dpp::component().set_label("Duration (e.g. 10m, 1h, 3d)")
                        .set_id("duration")
                        .set_type(dpp::cot_text)
                        .set_placeholder(default_dur)
                        .set_min_length(1)
                        .set_max_length(32)
                );

                modal.add_component(
                    dpp::component().set_label("Reason")
                        .set_id("reason")
                        .set_type(dpp::cot_text)
                        .set_placeholder("No reason provided")
                        .set_min_length(0)
                        .set_max_length(512)
                );

                event.dialog(modal);
                return;
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // apply timeout
            apply_timeout_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config.value(),
                [event](const bronx::db::InfractionRow& inf, bool quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **timed out** <@" + std::to_string(inf.user_id) + ">"
                        + "\n**duration:** " + format_duration(inf.duration_seconds)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("timeout"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& error_msg) {
                    event.reply(dpp::message().add_embed(bronx::error(error_msg)).set_flags(dpp::m_ephemeral));
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
