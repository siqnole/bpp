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

inline Command* get_pardon_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("pardon", "pardon (forgive) a moderation case", "moderation", {"forgive"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can pardon infractions"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: pardon <case_number> [reason]"));
                return;
            }

            uint32_t case_number = 0;
            try { case_number = static_cast<uint32_t>(std::stoul(args[0])); } catch (...) {}
            if (case_number == 0) {
                bronx::send_message(bot, event, bronx::error("invalid case number"));
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

            // check the case exists first
            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt.has_value()) {
                bronx::send_message(bot, event, bronx::error("case #" + std::to_string(case_number) + " not found"));
                return;
            }

            auto& inf = inf_opt.value();
            if (inf.pardoned) {
                bronx::send_message(bot, event, bronx::error("case #" + std::to_string(case_number) + " is already pardoned"));
                return;
            }

            bool ok = bronx::db::infraction_operations::pardon_infraction(db, guild_id, case_number, mod_id, reason);
            if (!ok) {
                bronx::send_message(bot, event, bronx::error("failed to pardon case #" + std::to_string(case_number)));
                return;
            }

            bronx::send_message(bot, event, bronx::success(
                "pardoned case **#" + std::to_string(case_number) + "** (" + inf.type + " on <@" + std::to_string(inf.user_id) + ">)"
                + (!reason.empty() && reason != "no reason provided" ? "\n**reason:** " + reason : "")
            ));

            // send mod log about the pardon
            bronx::db::InfractionRow pardon_log = inf;
            pardon_log.pardoned = true;
            pardon_log.pardoned_by = mod_id;
            pardon_log.pardoned_reason = reason;
            send_mod_log(bot, db, guild_id, pardon_log);
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can pardon infractions")).set_flags(dpp::m_ephemeral));
                return;
            }

            int64_t case_num_raw = std::get<int64_t>(event.get_parameter("case_number"));
            uint32_t case_number = static_cast<uint32_t>(case_num_raw);
            if (case_number == 0) {
                event.reply(dpp::message().add_embed(bronx::error("invalid case number")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string reason = "no reason provided";
            try { reason = std::get<std::string>(event.get_parameter("reason")); } catch (...) {}

            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt.has_value()) {
                event.reply(dpp::message().add_embed(bronx::error("case #" + std::to_string(case_number) + " not found")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto& inf = inf_opt.value();
            if (inf.pardoned) {
                event.reply(dpp::message().add_embed(bronx::error("case #" + std::to_string(case_number) + " is already pardoned")).set_flags(dpp::m_ephemeral));
                return;
            }

            bool ok = bronx::db::infraction_operations::pardon_infraction(db, guild_id, case_number, mod_id, reason);
            if (!ok) {
                event.reply(dpp::message().add_embed(bronx::error("failed to pardon case #" + std::to_string(case_number))).set_flags(dpp::m_ephemeral));
                return;
            }

            event.reply(dpp::message().add_embed(bronx::success(
                "pardoned case **#" + std::to_string(case_number) + "** (" + inf.type + " on <@" + std::to_string(inf.user_id) + ">)"
                + (!reason.empty() && reason != "no reason provided" ? "\n**reason:** " + reason : "")
            )));

            // send mod log
            bronx::db::InfractionRow pardon_log = inf;
            pardon_log.pardoned = true;
            pardon_log.pardoned_by = mod_id;
            pardon_log.pardoned_reason = reason;
            send_mod_log(bot, db, guild_id, pardon_log);
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_integer, "case_number", "the case number to pardon", true),
            dpp::command_option(dpp::co_string, "reason", "reason for pardoning", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
