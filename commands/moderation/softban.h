#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_softban_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "softban", "ban and immediately unban a user to clear messages", "moderation",
        {"sban"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_ban_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to ban members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `softban @user [reason]`"));
                return;
            }

            uint64_t target_id = parse_mention(args[0]);
            if (target_id == 0) { bronx::send_message(bot, event, bronx::error("invalid user mention")); return; }
            if (target_id == mod_id) { bronx::send_message(bot, event, bronx::error("you can't softban yourself")); return; }
            if (target_id == bot.me.id) { bronx::send_message(bot, event, bronx::error("you can't softban the bot")); return; }

            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't softban that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't softban that user — their role is higher than yours"));
                    return;
                }
            }

            std::string reason;
            for (size_t i = 1; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }
            if (reason.empty()) reason = "no reason provided";

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_softban_internal(bot, db, guild_id, target_id, mod_id, reason, config,
                [&bot, &event, target_id, reason](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) return;
                    std::string desc = bronx::EMOJI_CHECK + " **softbanned** <@" + std::to_string(target_id) + "> (messages cleared)"
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
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

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_ban_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to ban members")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto user_param = event.get_parameter("user");
            uint64_t target_id = std::get<dpp::snowflake>(user_param);

            if (target_id == mod_id) { event.reply(dpp::message().add_embed(bronx::error("you can't softban yourself")).set_flags(dpp::m_ephemeral)); return; }
            if (target_id == bot.me.id) { event.reply(dpp::message().add_embed(bronx::error("you can't softban the bot")).set_flags(dpp::m_ephemeral)); return; }

            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't softban that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't softban that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_softban_modal_" + std::to_string(target_id), "softban member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the softban").set_min_length(1).set_max_length(512));
                event.dialog(modal);
                return;
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_softban_internal(bot, db, guild_id, target_id, mod_id, reason, config,
                [event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("user softbanned (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    std::string desc = bronx::EMOJI_CHECK + " **softbanned** <@" + std::to_string(target_id) + "> (messages cleared)"
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        },
        {
            dpp::command_option(dpp::co_user, "user", "the user to softban", true),
            dpp::command_option(dpp::co_string, "reason", "reason for softbanning", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
