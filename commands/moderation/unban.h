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

inline Command* get_unban_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("unban", "unban a user by id", "moderation", {}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: unban <user_id> [reason]"));
                return;
            }

            uint64_t user_id = 0;
            try { user_id = std::stoull(args[0]); } catch (...) {}
            if (user_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user id — provide a numeric user id"));
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

            std::string user_id_str = std::to_string(user_id);
            bot.guild_ban_delete(guild_id, user_id,
                [&bot, db, guild_id, user_id, mod_id, reason, event, user_id_str](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to unban: " + cb.get_error().message));
                        return;
                    }

                    // pardon active ban infractions
                    auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infractions) {
                        if (inf.type == "ban" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    bronx::send_message(bot, event, bronx::success(
                        "unbanned user `" + std::to_string(user_id) + "`"
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

            std::string user_id_str = std::get<std::string>(event.get_parameter("user_id"));
            uint64_t user_id = 0;
            try { user_id = std::stoull(user_id_str); } catch (...) {}
            if (user_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("invalid user id")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string reason = "no reason provided";
            try { reason = std::get<std::string>(event.get_parameter("reason")); } catch (...) {}

            bot.guild_ban_delete(guild_id, user_id,
                [&bot, db, guild_id, user_id, mod_id, reason, event, user_id_str](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to unban: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infractions) {
                        if (inf.type == "ban" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    event.reply(dpp::message().add_embed(bronx::success(
                        "unbanned user `" + user_id_str + "`"
                        + (pardoned_count > 0 ? " — pardoned **" + std::to_string(pardoned_count) + "** infraction(s)" : "")
                    )));
                });
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_string, "user_id", "the user id to unban", true),
            dpp::command_option(dpp::co_string, "reason", "reason for unbanning", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
