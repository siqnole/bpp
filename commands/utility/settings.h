#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/logging_operations.h"
#include <dpp/dpp.h>
#include <algorithm>

namespace commands {
namespace utility {

inline Command* get_settings_command(bronx::db::Database* db) {
    static Command settings("settings", "Configure bot settings for your server", "utility", {"public_stats", "dashboard"}, true,
        // ── text handler ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            if (guild_id == 0) return;

            // Check admin permission
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) return;

            auto member_it = g->members.find(event.msg.author.id);
            if (member_it == g->members.end()) return;

            dpp::permission perms = g->base_permissions(member_it->second);
            if (!perms.can(dpp::p_administrator)) {
                bronx::send_message(bot, event, bronx::error("You need Administrator permission to use this command."));
                return;
            }

            if (args.empty()) {
                auto embed = bronx::create_embed(
                    "⚙ **Server Settings**\n\n"
                    "Manage your server's configuration.\n\n"
                    "**Available Settings:**\n"
                    "> `b.settings public_stats <on/off>` — Toggle public visibility of your server statistics on the dashboard.\n\n"
                    "Configure via dashboard: **https://dashboard.bronxbot.xyz/" + std::to_string(guild_id) + "**",
                    bronx::COLOR_DEFAULT);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            if (sub == "public_stats" || sub == "publicstats") {
                if (args.size() < 2) {
                    bool current = db->is_public_stats_enabled(guild_id);
                    bronx::send_message(bot, event, bronx::info("Public statistics is currently **" + std::string(current ? "ENABLED" : "DISABLED") + "**. Use `b.settings public_stats on/off` to change it."));
                    return;
                }

                std::string val = args[1];
                std::transform(val.begin(), val.end(), val.begin(), ::tolower);

                bool enable = (val == "on" || val == "true" || val == "enable" || val == "yes" || val == "1");
                if (db->set_public_stats_enabled(guild_id, enable)) {
                    bronx::send_message(bot, event, bronx::success("Public statistics has been **" + std::string(enable ? "ENABLED" : "DISABLED") + "** for this server."));
                } else {
                    bronx::send_message(bot, event, bronx::error("Failed to update settings. Please try again later."));
                }
                return;
            }

            bronx::send_message(bot, event, bronx::error("Unknown setting. Use `b.settings` to see available options."));
        },
        // ── slash handler ───────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            if (guild_id == 0) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " This command can only be used in a server.").set_flags(dpp::m_ephemeral));
                return;
            }

            // Check admin permission
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " Could not find guild.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto member_it = g->members.find(event.command.usr.id);
            if (member_it == g->members.end()) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " Member data not found.").set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::permission perms = g->base_permissions(member_it->second);
            if (!perms.can(dpp::p_administrator)) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " You need Administrator permission to use this command.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto command_interaction = std::get<dpp::command_interaction>(event.command.data);
            if (command_interaction.options.empty()) {
                event.reply(dpp::message("Please select a setting to configure.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto& sub = command_interaction.options[0];
            if (sub.name == "public_stats") {
                bool enabled = std::get<bool>(sub.options[0].value);
                if (db->set_public_stats_enabled(guild_id, enabled)) {
                    event.reply(dpp::message(bronx::EMOJI_CHECK + " Public statistics has been **" + std::string(enabled ? "enabled" : "disabled") + "** for this server.").set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to update settings. Please try again later.").set_flags(dpp::m_ephemeral));
                }
            }
        },
        // slash command options
        {
            dpp::command_option(dpp::co_sub_command, "public_stats", "Toggle public visibility of server statistics on the dashboard")
                .add_option(dpp::command_option(dpp::co_boolean, "enabled", "Whether to make statistics public", true))
        }
    );

    settings.extended_description = "Manage server-wide settings for Bronx Bot. These settings control how the bot interacts with your server and its data visibility.";
    settings.subcommands = {
        {"public_stats", "Toggle dashboard statistics transparency for members/outsiders"}
    };
    settings.examples = {"/settings public_stats enabled:True", "b.settings public_stats on"};

    return &settings;
}

} // namespace utility
} // namespace commands
