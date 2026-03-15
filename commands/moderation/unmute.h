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

inline Command* get_unmute_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("unmute", "remove mute role from a user", "moderation", {}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: unmute <@user> [reason]"));
                return;
            }

            // parse user mention
            std::string mention = args[0];
            uint64_t user_id = 0;
            if (mention.size() > 3 && mention[0] == '<' && mention[1] == '@') {
                std::string id_str = mention.substr(mention.find_first_of("0123456789"));
                id_str = id_str.substr(0, id_str.find('>'));
                try { user_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { user_id = std::stoull(mention); } catch (...) {}
            }
            if (user_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user"));
                return;
            }

            std::string reason = "no reason provided";
            if (args.size() > 1) {
                reason.clear();
                for (size_t i = 1; i < args.size(); i++) {
                    if (!reason.empty()) reason += " ";
                    reason += args[i];
                }
            }

            // get mute role from config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config.has_value() || config.value().mute_role_id == 0) {
                bronx::send_message(bot, event, bronx::error("no mute role configured — use `/muterole` to set one"));
                return;
            }

            bot.guild_member_remove_role(guild_id, user_id, config.value().mute_role_id,
                [&bot, db, guild_id, user_id, mod_id, reason, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to remove mute role: " + cb.get_error().message));
                        return;
                    }

                    // pardon active mute infractions
                    auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infractions) {
                        if (inf.type == "mute" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    bronx::send_message(bot, event, bronx::success(
                        "unmuted <@" + std::to_string(user_id) + ">"
                        + (pardoned_count > 0 ? " — pardoned **" + std::to_string(pardoned_count) + "** infraction(s)" : "")
                    ));
                });
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to use this command")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::snowflake user_id = std::get<dpp::snowflake>(event.get_parameter("user"));
            std::string reason = "no reason provided";
            try { reason = std::get<std::string>(event.get_parameter("reason")); } catch (...) {}

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config.has_value() || config.value().mute_role_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("no mute role configured — use `/muterole` to set one")).set_flags(dpp::m_ephemeral));
                return;
            }

            bot.guild_member_remove_role(guild_id, user_id, config.value().mute_role_id,
                [&bot, db, guild_id, user_id, mod_id, reason, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to remove mute role: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infractions) {
                        if (inf.type == "mute" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    event.reply(dpp::message().add_embed(bronx::success(
                        "unmuted <@" + std::to_string(static_cast<uint64_t>(user_id)) + ">"
                        + (pardoned_count > 0 ? " — pardoned **" + std::to_string(pardoned_count) + "** infraction(s)" : "")
                    )));
                });
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_user, "user", "the user to unmute", true),
            dpp::command_option(dpp::co_string, "reason", "reason for unmuting", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
