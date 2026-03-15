#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"
#include <map>

namespace commands {
namespace moderation {

inline Command* get_modstats_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("modstats", "view moderation statistics for a moderator", "moderation", {"modstat"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            // target moderator (default to self)
            uint64_t target_id = mod_id;
            if (!args.empty()) {
                std::string mention = args[0];
                if (mention.size() > 3 && mention[0] == '<' && mention[1] == '@') {
                    std::string id_str = mention.substr(mention.find_first_of("0123456789"));
                    id_str = id_str.substr(0, id_str.find('>'));
                    try { target_id = std::stoull(id_str); } catch (...) {}
                } else {
                    try { target_id = std::stoull(mention); } catch (...) {}
                }
            }

            auto actions = bronx::db::infraction_operations::get_moderator_actions(db, guild_id, target_id, 100);

            if (actions.empty()) {
                bronx::send_message(bot, event, bronx::info("no moderation actions found for <@" + std::to_string(target_id) + ">"));
                return;
            }

            // breakdown by type
            std::map<std::string, int> type_counts;
            for (auto& a : actions) {
                type_counts[a.type]++;
            }

            std::string breakdown;
            for (auto& [type, count] : type_counts) {
                if (!breakdown.empty()) breakdown += " | ";
                breakdown += "**" + type + ":** " + std::to_string(count);
            }

            // recent 5
            std::string recent;
            int shown = 0;
            for (auto& a : actions) {
                if (shown >= 5) break;
                auto t = std::chrono::system_clock::to_time_t(a.created_at);
                recent += "`#" + std::to_string(a.case_number) + "` " + a.type
                        + " — <@" + std::to_string(a.user_id) + "> <t:" + std::to_string(t) + ":R>\n";
                shown++;
            }

            auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
            embed.set_title("moderation stats — <@" + std::to_string(target_id) + ">");
            embed.add_field("total actions", std::to_string(actions.size()), true);
            embed.add_field("breakdown", breakdown, false);
            if (!recent.empty())
                embed.add_field("recent actions", recent, false);

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

            uint64_t target_id = mod_id;
            try {
                dpp::snowflake s = std::get<dpp::snowflake>(event.get_parameter("moderator"));
                target_id = static_cast<uint64_t>(s);
            } catch (...) {}

            auto actions = bronx::db::infraction_operations::get_moderator_actions(db, guild_id, target_id, 100);

            if (actions.empty()) {
                event.reply(dpp::message().add_embed(bronx::info("no moderation actions found for <@" + std::to_string(target_id) + ">")));
                return;
            }

            std::map<std::string, int> type_counts;
            for (auto& a : actions) {
                type_counts[a.type]++;
            }

            std::string breakdown;
            for (auto& [type, count] : type_counts) {
                if (!breakdown.empty()) breakdown += " | ";
                breakdown += "**" + type + ":** " + std::to_string(count);
            }

            std::string recent;
            int shown = 0;
            for (auto& a : actions) {
                if (shown >= 5) break;
                auto t = std::chrono::system_clock::to_time_t(a.created_at);
                recent += "`#" + std::to_string(a.case_number) + "` " + a.type
                        + " — <@" + std::to_string(a.user_id) + "> <t:" + std::to_string(t) + ":R>\n";
                shown++;
            }

            auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
            embed.set_title("moderation stats — <@" + std::to_string(target_id) + ">");
            embed.add_field("total actions", std::to_string(actions.size()), true);
            embed.add_field("breakdown", breakdown, false);
            if (!recent.empty())
                embed.add_field("recent actions", recent, false);

            event.reply(dpp::message().add_embed(embed));
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_user, "moderator", "the moderator to view stats for (default: you)", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
