#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_duration_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "duration", "update the duration for an existing timed infraction", "moderation",
        {}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to update durations"));
                return;
            }

            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: `duration <case_number> <new_duration>` (e.g. 1h, 1d)"));
                return;
            }

            uint32_t case_number = 0;
            try { case_number = std::stoul(args[0]); } catch (...) {}
            if (case_number == 0) {
                bronx::send_message(bot, event, bronx::error("invalid case number"));
                return;
            }

            uint32_t new_duration = parse_duration(args[1]);
            if (new_duration == 0 && args[1] != "0") {
                bronx::send_message(bot, event, bronx::error("invalid duration format (e.g. 1h, 30m)"));
                return;
            }

            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt) {
                bronx::send_message(bot, event, bronx::error("case record not found"));
                return;
            }

            if (!inf_opt->active || inf_opt->pardoned) {
                bronx::send_message(bot, event, bronx::error("cannot update duration for inactive or pardoned cases"));
                return;
            }

            if (inf_opt->type != "timeout" && inf_opt->type != "mute" && inf_opt->type != "ban" && inf_opt->type != "jail") {
                bronx::send_message(bot, event, bronx::error("case type **" + inf_opt->type + "** does not support duration updates"));
                return;
            }

            if (bronx::db::infraction_operations::update_infraction_duration(db, guild_id, case_number, new_duration)) {
                // Fetch updated row to get recalculated expires_at
                auto updated_inf = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number).value();
                
                auto now = std::chrono::system_clock::now();
                int32_t remaining = 0;
                if (updated_inf.expires_at.has_value()) {
                    remaining = (int32_t)std::chrono::duration_cast<std::chrono::seconds>(updated_inf.expires_at.value() - now).count();
                }

                // Update Discord state if necessary
                if (updated_inf.type == "timeout") {
                    time_t discord_expiry = 0;
                    if (remaining > 0) discord_expiry = time(0) + remaining;
                    bot.guild_member_timeout(guild_id, updated_inf.user_id, discord_expiry);
                }

                // Reschedule internal timer
                reschedule_punishment_expiry(bot, db, guild_id, case_number, updated_inf.type, updated_inf.user_id, 
                                            (remaining > 0 ? (uint32_t)remaining : 0), updated_inf.metadata);

                auto embed = bronx::create_embed("updated duration for case **#" + std::to_string(case_number) + "**", get_action_color("warn"));
                embed.add_field("user", "<@" + std::to_string(updated_inf.user_id) + ">", true);
                embed.add_field("new total duration", format_duration(new_duration), true);
                if (updated_inf.expires_at.has_value()) {
                    embed.add_field("expires in", format_duration(remaining > 0 ? (uint32_t)remaining : 0), true);
                }
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                
                send_mod_log(bot, db, guild_id, updated_inf);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to update duration in database"));
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to update durations")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto case_param = event.get_parameter("case");
            uint32_t case_number = (uint32_t)std::get<int64_t>(case_param);

            auto duration_param = event.get_parameter("duration");
            std::string duration_str = std::get<std::string>(duration_param);
            uint32_t new_duration = parse_duration(duration_str);

            if (new_duration == 0 && duration_str != "0") {
                event.reply(dpp::message().add_embed(bronx::error("invalid duration format (e.g. 1h, 30m)")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
            if (!inf_opt) {
                event.reply(dpp::message().add_embed(bronx::error("case record not found")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (!inf_opt->active || inf_opt->pardoned) {
                event.reply(dpp::message().add_embed(bronx::error("cannot update duration for inactive or pardoned cases")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (inf_opt->type != "timeout" && inf_opt->type != "mute" && inf_opt->type != "ban" && inf_opt->type != "jail") {
                event.reply(dpp::message().add_embed(bronx::error("case type **" + inf_opt->type + "** does not support duration updates")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (bronx::db::infraction_operations::update_infraction_duration(db, guild_id, case_number, new_duration)) {
                auto updated_inf = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number).value();
                
                auto now = std::chrono::system_clock::now();
                int32_t remaining = 0;
                if (updated_inf.expires_at.has_value()) {
                    remaining = (int32_t)std::chrono::duration_cast<std::chrono::seconds>(updated_inf.expires_at.value() - now).count();
                }

                if (updated_inf.type == "timeout") {
                    time_t discord_expiry = 0;
                    if (remaining > 0) discord_expiry = time(0) + remaining;
                    bot.guild_member_timeout(guild_id, updated_inf.user_id, discord_expiry);
                }

                reschedule_punishment_expiry(bot, db, guild_id, case_number, updated_inf.type, updated_inf.user_id, 
                                            (remaining > 0 ? (uint32_t)remaining : 0), updated_inf.metadata);

                auto embed = bronx::create_embed("updated duration for case **#" + std::to_string(case_number) + "**", get_action_color("warn"));
                embed.add_field("user", "<@" + std::to_string(updated_inf.user_id) + ">", true);
                embed.add_field("new total duration", format_duration(new_duration), true);
                if (updated_inf.expires_at.has_value()) {
                    embed.add_field("expires in", format_duration(remaining > 0 ? (uint32_t)remaining : 0), true);
                }
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                
                send_mod_log(bot, db, guild_id, updated_inf);
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to update duration in database")).set_flags(dpp::m_ephemeral));
            }
        },
        {
            dpp::command_option(dpp::co_integer, "case", "the case number to update", true),
            dpp::command_option(dpp::co_string, "duration", "the new total duration (e.g. 1h, 1d, 0 for permanent)", true)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
