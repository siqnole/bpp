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

inline Command* get_case_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("case", "look up details for a moderation case", "moderation", {"inf", "infraction"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: case <number>"));
                return;
            }

            uint32_t case_number = 0;
            try { case_number = static_cast<uint32_t>(std::stoul(args[0])); } catch (...) {}
            if (case_number == 0) {
                bronx::send_message(bot, event, bronx::error("invalid case number"));
                return;
            }

            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt.has_value()) {
                bronx::send_message(bot, event, bronx::error("case #" + std::to_string(case_number) + " not found"));
                return;
            }

            auto& inf = inf_opt.value();
            auto embed = bronx::create_embed("", get_action_color(inf.type));
            embed.set_title("case #" + std::to_string(inf.case_number) + " — " + inf.type);

            embed.add_field("user", "<@" + std::to_string(inf.user_id) + ">", true);
            embed.add_field("moderator", "<@" + std::to_string(inf.moderator_id) + ">", true);
            embed.add_field("points", std::to_string(inf.points), true);

            if (!inf.reason.empty())
                embed.add_field("reason", inf.reason, false);

            if (inf.duration_seconds > 0)
                embed.add_field("duration", format_duration(inf.duration_seconds), true);

            if (inf.expires_at.has_value()) {
                auto exp_time = std::chrono::system_clock::to_time_t(inf.expires_at.value());
                embed.add_field("expires", "<t:" + std::to_string(exp_time) + ":R>", true);
            }

            // status
            std::string status;
            if (inf.pardoned) status = "pardoned";
            else if (inf.active) status = "active";
            else status = "expired";
            embed.add_field("status", "**" + status + "**", true);

            auto created_time = std::chrono::system_clock::to_time_t(inf.created_at);
            embed.add_field("created", "<t:" + std::to_string(created_time) + ":f>", true);

            // pardon info
            if (inf.pardoned) {
                embed.add_field("pardoned by", "<@" + std::to_string(inf.pardoned_by) + ">", true);
                if (inf.pardoned_at.has_value()) {
                    auto pardoned_time = std::chrono::system_clock::to_time_t(inf.pardoned_at.value());
                    embed.add_field("pardoned at", "<t:" + std::to_string(pardoned_time) + ":f>", true);
                }
                if (!inf.pardoned_reason.empty())
                    embed.add_field("pardon reason", inf.pardoned_reason, false);
            }

            bronx::send_message(bot, event, embed);
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to use this command")).set_flags(dpp::m_ephemeral));
                return;
            }

            int64_t case_num_raw = std::get<int64_t>(event.get_parameter("number"));
            uint32_t case_number = static_cast<uint32_t>(case_num_raw);
            if (case_number == 0) {
                event.reply(dpp::message().add_embed(bronx::error("invalid case number")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt.has_value()) {
                event.reply(dpp::message().add_embed(bronx::error("case #" + std::to_string(case_number) + " not found")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto& inf = inf_opt.value();
            auto embed = bronx::create_embed("", get_action_color(inf.type));
            embed.set_title("case #" + std::to_string(inf.case_number) + " — " + inf.type);

            embed.add_field("user", "<@" + std::to_string(inf.user_id) + ">", true);
            embed.add_field("moderator", "<@" + std::to_string(inf.moderator_id) + ">", true);
            embed.add_field("points", std::to_string(inf.points), true);

            if (!inf.reason.empty())
                embed.add_field("reason", inf.reason, false);

            if (inf.duration_seconds > 0)
                embed.add_field("duration", format_duration(inf.duration_seconds), true);

            if (inf.expires_at.has_value()) {
                auto exp_time = std::chrono::system_clock::to_time_t(inf.expires_at.value());
                embed.add_field("expires", "<t:" + std::to_string(exp_time) + ":R>", true);
            }

            std::string status;
            if (inf.pardoned) status = "pardoned";
            else if (inf.active) status = "active";
            else status = "expired";
            embed.add_field("status", "**" + status + "**", true);

            auto created_time = std::chrono::system_clock::to_time_t(inf.created_at);
            embed.add_field("created", "<t:" + std::to_string(created_time) + ":f>", true);

            if (inf.pardoned) {
                embed.add_field("pardoned by", "<@" + std::to_string(inf.pardoned_by) + ">", true);
                if (inf.pardoned_at.has_value()) {
                    auto pardoned_time = std::chrono::system_clock::to_time_t(inf.pardoned_at.value());
                    embed.add_field("pardoned at", "<t:" + std::to_string(pardoned_time) + ":f>", true);
                }
                if (!inf.pardoned_reason.empty())
                    embed.add_field("pardon reason", inf.pardoned_reason, false);
            }

            event.reply(dpp::message().add_embed(embed));
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_integer, "number", "the case number to look up", true)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
