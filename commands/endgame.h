#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "games/blacktea.h"
#include "games/react.h"
#include "games/heist.h"
#include "games/tictactoe.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>

using namespace bronx::db;

namespace commands {

// Handle endgame confirmation buttons
inline void handle_endgame_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db) {
    std::string id = event.custom_id;
    if (id.rfind("endgame_", 0) != 0) return;

    // Only server admins can click
    auto perms = event.command.get_resolved_permission(event.command.get_issuing_user().id);
    bool is_admin = (perms & dpp::p_manage_guild) || (perms & dpp::p_administrator);
    if (!is_admin) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("you need **Manage Server** permission to end games")).set_flags(dpp::m_ephemeral));
        return;
    }

    uint64_t channel_id = event.command.channel_id;
    uint64_t guild_id   = event.command.guild_id;

    if (id == "endgame_cancel") {
        event.reply(dpp::ir_update_message,
            dpp::message().set_content("").add_embed(bronx::info("game cancellation cancelled")));
        return;
    }

    if (id == "endgame_confirm") {
        int ended = 0;
        std::string details;

        // BlackTea: set force_stop flag
        for (auto& [gid, game] : games::active_blacktea_games) {
            if (game.channel_id == channel_id) {
                game.force_stop = true;
                ended++;
                details += "• blacktea (round " + std::to_string(game.round) + ")\n";
            }
        }

        // React games: just erase (single-message games)
        for (auto it = games::active_react_games.begin(); it != games::active_react_games.end(); ) {
            if (it->second.channel_id == channel_id) {
                ended++;
                details += "• react game\n";
                it = games::active_react_games.erase(it);
            } else { ++it; }
        }

        // TicTacToe: erase
        for (auto it = games::active_tictactoe_games.begin(); it != games::active_tictactoe_games.end(); ) {
            if (it->second.channel_id == channel_id) {
                ended++;
                details += "• tic-tac-toe\n";
                it = games::active_tictactoe_games.erase(it);
            } else { ++it; }
        }

        // Heist: erase from channel-keyed map
        auto heist_it = games::g_heist_sessions.find(channel_id);
        if (heist_it != games::g_heist_sessions.end()) {
            ended++;
            details += "• heist\n";
            games::g_heist_sessions.erase(heist_it);
        }

        std::string desc;
        if (ended == 0) {
            desc = "no active games found in this channel";
        } else {
            desc = "ended **" + std::to_string(ended) + "** game" + (ended > 1 ? "s" : "") + " in this channel\n\n" + details;
        }

        event.reply(dpp::ir_update_message,
            dpp::message().set_content("").add_embed(
                ended > 0 ? bronx::success(desc) : bronx::info(desc)
            ));
        return;
    }

    // endgame_all — end all games in the server
    if (id == "endgame_all") {
        int ended = 0;

        for (auto& [gid, game] : games::active_blacktea_games) {
            if (game.guild_id == guild_id) { game.force_stop = true; ended++; }
        }
        for (auto it = games::active_react_games.begin(); it != games::active_react_games.end(); ) {
            if (it->second.guild_id == guild_id) { it = games::active_react_games.erase(it); ended++; }
            else { ++it; }
        }
        for (auto it = games::active_tictactoe_games.begin(); it != games::active_tictactoe_games.end(); ) {
            if (it->second.guild_id == guild_id) { it = games::active_tictactoe_games.erase(it); ended++; }
            else { ++it; }
        }
        for (auto it = games::g_heist_sessions.begin(); it != games::g_heist_sessions.end(); ) {
            if (it->second.guild_id == guild_id) { it = games::g_heist_sessions.erase(it); ended++; }
            else { ++it; }
        }

        std::string desc = ended > 0
            ? "ended **" + std::to_string(ended) + "** game" + (ended > 1 ? "s" : "") + " across the server"
            : "no active games found in this server";

        event.reply(dpp::ir_update_message,
            dpp::message().set_content("").add_embed(
                ended > 0 ? bronx::success(desc) : bronx::info(desc)
            ));
        return;
    }
}

inline ::std::vector<Command*> get_endgame_commands(Database* db) {
    static ::std::vector<Command*> cmds;

    static Command* endgame = new Command("endgame", "end active games in this channel", "moderation",
        {"eg", "forcestop", "stopgame"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Permission check using guild base_permissions (no async call needed)
            auto* g = dpp::find_guild(event.msg.guild_id);
            if (!g) {
                bronx::send_message(bot, event, bronx::error("failed to check permissions"));
                return;
            }
            auto perms = g->base_permissions(event.msg.member);
            bool is_admin = perms.can(dpp::p_manage_guild) || perms.can(dpp::p_administrator);
            if (!is_admin) {
                bronx::send_message(bot, event,
                    bronx::error("you need **Manage Server** permission to use this command"));
                return;
            }

            bool all = !args.empty() && (args[0] == "all" || args[0] == "server");
            uint64_t channel_id = event.msg.channel_id;
            uint64_t guild_id   = event.msg.guild_id;

            // Count active games
            int count = 0;
            for (auto& [gid, game] : games::active_blacktea_games) {
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            }
            for (auto& [gid, game] : games::active_react_games) {
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            }
            for (auto& [gid, game] : games::active_tictactoe_games) {
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            }
            if (all) {
                for (auto& [cid, s] : games::g_heist_sessions) {
                    if (s.guild_id == guild_id) count++;
                }
            } else {
                if (games::g_heist_sessions.count(channel_id)) count++;
            }

            if (count == 0) {
                bronx::send_message(bot, event,
                    bronx::info("no active games found " + std::string(all ? "in this server" : "in this channel")));
                return;
            }

            // Confirmation with buttons
            std::string desc = "found **" + std::to_string(count) + "** active game" +
                (count > 1 ? "s" : "") + " " + (all ? "in this server" : "in this channel") +
                ".\n\nare you sure you want to force-end " + (count > 1 ? "them" : "it") + "?";

            auto embed = bronx::create_embed(desc, bronx::COLOR_WARNING);

            dpp::component row;
            row.set_type(dpp::cot_action_row);

            dpp::component confirm;
            confirm.set_type(dpp::cot_button);
            confirm.set_label("end " + std::to_string(count) + " game" + (count > 1 ? "s" : ""));
            confirm.set_style(dpp::cos_danger);
            confirm.set_id(all ? "endgame_all" : "endgame_confirm");
            confirm.set_emoji("🛑");

            dpp::component cancel;
            cancel.set_type(dpp::cot_button);
            cancel.set_label("cancel");
            cancel.set_style(dpp::cos_secondary);
            cancel.set_id("endgame_cancel");

            row.add_component(confirm);
            row.add_component(cancel);

            dpp::message msg;
            msg.channel_id = event.msg.channel_id;
            msg.add_embed(embed);
            msg.add_component(row);
            bot.message_create(msg);
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto perms = event.command.get_resolved_permission(event.command.get_issuing_user().id);
            bool is_admin = (perms & dpp::p_manage_guild) || (perms & dpp::p_administrator);
            if (!is_admin) {
                event.reply(dpp::message().add_embed(
                    bronx::error("you need **Manage Server** permission to use this command")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string scope = "channel";
            auto sp = event.get_parameter("scope");
            if (std::holds_alternative<std::string>(sp)) scope = std::get<std::string>(sp);
            bool all = (scope == "server");

            uint64_t channel_id = event.command.channel_id;
            uint64_t guild_id   = event.command.guild_id;

            int count = 0;
            for (auto& [gid, game] : games::active_blacktea_games)
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            for (auto& [gid, game] : games::active_react_games)
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            for (auto& [gid, game] : games::active_tictactoe_games)
                if (all ? (game.guild_id == guild_id) : (game.channel_id == channel_id)) count++;
            if (all) {
                for (auto& [cid, s] : games::g_heist_sessions) if (s.guild_id == guild_id) count++;
            } else {
                if (games::g_heist_sessions.count(channel_id)) count++;
            }

            if (count == 0) {
                event.reply(dpp::message().add_embed(
                    bronx::info("no active games found " + std::string(all ? "in this server" : "in this channel"))));
                return;
            }

            std::string desc = "found **" + std::to_string(count) + "** active game" +
                (count > 1 ? "s" : "") + " " + (all ? "in this server" : "in this channel") +
                ".\n\nare you sure you want to force-end " + (count > 1 ? "them" : "it") + "?";

            auto embed = bronx::create_embed(desc, bronx::COLOR_WARNING);

            dpp::component row;
            row.set_type(dpp::cot_action_row);

            dpp::component confirm;
            confirm.set_type(dpp::cot_button);
            confirm.set_label("end " + std::to_string(count) + " game" + (count > 1 ? "s" : ""));
            confirm.set_style(dpp::cos_danger);
            confirm.set_id(all ? "endgame_all" : "endgame_confirm");
            confirm.set_emoji("🛑");

            dpp::component cancel;
            cancel.set_type(dpp::cot_button);
            cancel.set_label("cancel");
            cancel.set_style(dpp::cos_secondary);
            cancel.set_id("endgame_cancel");

            row.add_component(confirm);
            row.add_component(cancel);

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(row);
            event.reply(msg);
        },
        {
            dpp::command_option(dpp::co_string, "scope", "end games in channel or entire server", false)
                .add_choice(dpp::command_option_choice("this channel", "channel"))
                .add_choice(dpp::command_option_choice("entire server", "server"))
        });

    cmds.push_back(endgame);
    return cmds;
}

} // namespace commands
