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

            uint64_t target_id = parse_mention(args[0]);
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

            // if no reason, trigger modal
            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_kick_modal_" + std::to_string(target_id), "kick member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the kick").set_min_length(1).set_max_length(512));
                
                // we can't trigger modal from message_create_t easily without a button
                // so for text command, we'll just require it or use a default
                reason = "no reason provided";
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value()) {
                bronx::send_message(bot, event, bronx::error("failed to fetch moderation config"));
                return;
            }

            apply_kick_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                [&bot, &event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) return;
                    
                    std::string desc = bronx::EMOJI_CHECK + " **kicked** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("kick"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                },
                [&bot, &event](const std::string& err) {
                    bronx::send_message(bot, event, bronx::error(err));
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

            // parse optional reason
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            // if no reason, trigger modal
            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_kick_modal_" + std::to_string(target_id), "kick member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the kick").set_min_length(1).set_max_length(512));
                event.dialog(modal);
                return;
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to fetch moderation config")).set_flags(dpp::m_ephemeral));
                return;
            }

            apply_kick_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                [&bot, event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("user kicked (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    std::string desc = bronx::EMOJI_CHECK + " **kicked** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("kick"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
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
