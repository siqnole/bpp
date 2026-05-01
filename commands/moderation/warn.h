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

            uint64_t target_id = parse_mention(args[0]);
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

            // if no reason, for text command we'll just require it
            if (reason.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `warn @user <reason>`"));
                return;
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value()) {
                bronx::send_message(bot, event, bronx::error("failed to fetch moderation config"));
                return;
            }

            apply_warn_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                [&bot, &event, target_id, config_opt](const bronx::db::InfractionRow& inf, bool is_quiet, double active_points) {
                    if (is_quiet) return;
                    
                    std::string desc = bronx::EMOJI_CHECK + " **warned** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number)
                        + "\n**reason:** " + inf.reason
                        + "\n**points:** +" + std::to_string(config_opt.value().point_warn)
                        + " (total active: " + std::to_string(active_points) + ")";

                    auto embed = bronx::create_embed(desc, get_action_color("warn"));
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

            // get reason (optional in slash now, triggers modal if missing)
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            // if no reason, trigger modal
            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_warn_modal_" + std::to_string(target_id), "warn member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the warning").set_min_length(1).set_max_length(512));
                event.dialog(modal);
                return;
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to fetch moderation config")).set_flags(dpp::m_ephemeral));
                return;
            }

            apply_warn_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                [&bot, event, target_id, config_opt](const bronx::db::InfractionRow& inf, bool is_quiet, double active_points) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("user warned (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    
                    std::string desc = bronx::EMOJI_CHECK + " **warned** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number)
                        + "\n**reason:** " + inf.reason
                        + "\n**points:** +" + std::to_string(config_opt.value().point_warn)
                        + " (total active: " + std::to_string(active_points) + ")";

                    auto embed = bronx::create_embed(desc, get_action_color("warn"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
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
