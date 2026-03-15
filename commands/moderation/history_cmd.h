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

inline Command* get_history_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    static constexpr int PER_PAGE = 25;

    cmd = new Command("history", "view a user's infraction history", "moderation", {"infractions-list", "warnings"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: history <@user> [page]"));
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

            int page = 1;
            if (args.size() > 1) {
                try { page = std::stoi(args[1]); } catch (...) {}
                if (page < 1) page = 1;
            }

            int offset = (page - 1) * PER_PAGE;

            // get counts
            auto counts = bronx::db::infraction_operations::count_infractions(db, guild_id, user_id);

            // get infractions for this page
            auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, false, PER_PAGE, offset);

            if (infractions.empty() && page == 1) {
                bronx::send_message(bot, event, bronx::info("no infractions found for <@" + std::to_string(user_id) + ">"));
                return;
            }

            int total_pages = (counts.total + PER_PAGE - 1) / PER_PAGE;
            if (total_pages < 1) total_pages = 1;

            // get active points
            double active_points = bronx::db::infraction_operations::get_user_active_points(db, guild_id, user_id);

            std::string desc;
            for (auto& inf : infractions) {
                std::string status;
                if (inf.pardoned) status = "pardoned";
                else if (inf.active) status = "active";
                else status = "expired";

                auto created_time = std::chrono::system_clock::to_time_t(inf.created_at);

                desc += "`#" + std::to_string(inf.case_number) + "` | "
                      + inf.type + " | "
                      + std::to_string(inf.points) + " pts | "
                      + "<t:" + std::to_string(created_time) + ":d> | "
                      + status + "\n";
            }

            auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
            embed.set_title("infraction history — <@" + std::to_string(user_id) + ">");
            embed.add_field("summary", "**total:** " + std::to_string(counts.total)
                + " | **active:** " + std::to_string(counts.active)
                + " | **pardoned:** " + std::to_string(counts.pardoned), false);
            embed.set_footer(dpp::embed_footer().set_text(
                "active points: " + std::to_string(active_points)
                + " — page " + std::to_string(page) + " of " + std::to_string(total_pages)
            ));

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

            dpp::snowflake user_id = std::get<dpp::snowflake>(event.get_parameter("user"));
            int64_t page = 1;
            try { page = std::get<int64_t>(event.get_parameter("page")); } catch (...) {}
            if (page < 1) page = 1;

            int offset = static_cast<int>((page - 1) * PER_PAGE);

            auto counts = bronx::db::infraction_operations::count_infractions(db, guild_id, user_id);
            auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, false, PER_PAGE, offset);

            if (infractions.empty() && page == 1) {
                event.reply(dpp::message().add_embed(bronx::info("no infractions found for <@" + std::to_string(static_cast<uint64_t>(user_id)) + ">")));
                return;
            }

            int total_pages = (counts.total + PER_PAGE - 1) / PER_PAGE;
            if (total_pages < 1) total_pages = 1;

            double active_points = bronx::db::infraction_operations::get_user_active_points(db, guild_id, user_id);

            std::string desc;
            for (auto& inf : infractions) {
                std::string status;
                if (inf.pardoned) status = "pardoned";
                else if (inf.active) status = "active";
                else status = "expired";

                auto created_time = std::chrono::system_clock::to_time_t(inf.created_at);

                desc += "`#" + std::to_string(inf.case_number) + "` | "
                      + inf.type + " | "
                      + std::to_string(inf.points) + " pts | "
                      + "<t:" + std::to_string(created_time) + ":d> | "
                      + status + "\n";
            }

            auto embed = bronx::create_embed(desc, bronx::COLOR_DEFAULT);
            embed.set_title("infraction history — <@" + std::to_string(static_cast<uint64_t>(user_id)) + ">");
            embed.add_field("summary", "**total:** " + std::to_string(counts.total)
                + " | **active:** " + std::to_string(counts.active)
                + " | **pardoned:** " + std::to_string(counts.pardoned), false);
            embed.set_footer(dpp::embed_footer().set_text(
                "active points: " + std::to_string(active_points)
                + " — page " + std::to_string(page) + " of " + std::to_string(total_pages)
            ));

            event.reply(dpp::message().add_embed(embed));
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_user, "user", "the user to view history for", true),
            dpp::command_option(dpp::co_integer, "page", "page number", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
