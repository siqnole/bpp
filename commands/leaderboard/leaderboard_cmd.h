#pragma once
#include "../../database/core/database.h"
#include "../../embed_style.h"
#include "../../command.h"
#include "leaderboard_helpers.h"
#include <algorithm>
#include <sstream>

namespace commands {
namespace leaderboard {

inline ::std::vector<Command*> create_leaderboard_commands(bronx::db::Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Create options for the slash command - default to server scope
    ::std::vector<dpp::command_option> options = {
        dpp::command_option(dpp::co_string, "category", "leaderboard category (defaults to net worth)", false)
            .add_choice(dpp::command_option_choice("net worth", "networth"))
            .add_choice(dpp::command_option_choice("wallet", "wallet"))
            .add_choice(dpp::command_option_choice("bank", "bank"))
            .add_choice(dpp::command_option_choice("fish caught", "fish-caught"))
            .add_choice(dpp::command_option_choice("fish sold", "fish-sold"))
            .add_choice(dpp::command_option_choice("valuable fish", "fish-value"))
            .add_choice(dpp::command_option_choice("fishing profit", "fishing-profit"))
            .add_choice(dpp::command_option_choice("gambling profit", "gambling-profit"))
            .add_choice(dpp::command_option_choice("gambling losses", "gambling-losses"))
            .add_choice(dpp::command_option_choice("commands used", "commands"))
            .add_choice(dpp::command_option_choice("prestige level", "prestige"))
            .add_choice(dpp::command_option_choice("global XP", "global-xp"))
            .add_choice(dpp::command_option_choice("server XP", "server-xp")),
        dpp::command_option(dpp::co_string, "scope", "server or global rankings (defaults to server)", false)
            .add_choice(dpp::command_option_choice("server", "server"))
            .add_choice(dpp::command_option_choice("global", "global"))
    };
    
    // Main leaderboard command with category navigation
    static Command* leaderboard_cmd = new Command("leaderboard", "view server and global leaderboards", "leaderboard", {"lb", "top"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Default to networth and server scope
            ::std::string category = "networth";
            bool global_scope = false;
            
            // Parse arguments: [category] [global]
            if (!args.empty()) {
                category = resolve_category_alias(args[0]);
            }
            
            // Check for "global" in any remaining args
            for (size_t i = 1; i < args.size(); i++) {
                ::std::string arg_lower = args[i];
                ::std::transform(arg_lower.begin(), arg_lower.end(), arg_lower.begin(), ::tolower);
                if (arg_lower == "global" || arg_lower == "g") {
                    global_scope = true;
                    break;
                }
            }
            
            uint64_t guild_id = global_scope ? 0 : static_cast<uint64_t>(event.msg.guild_id);
            
            // Get category info
            auto info = get_category_info(category);
            
            // Check for invalid category
            if (info.title == "unknown") {
                ::std::string description = "**📊 leaderboard categories**\n\n";
                description += "**economy:** `networth` (nw), `wallet` (bal), `bank`, `prestige` (p)\n";
                description += "**fishing:** `fish-caught` (fc), `fish-sold` (fs), `fish-value` (fv), `fishing-profit` (fp)\n";
                description += "**gambling:** `gambling-profit` (gp), `gambling-losses` (gl)\n";
                description += "**activity:** `commands` (cmd)\n";
                description += "**leveling:** `global-xp` (gxp, level), `server-xp` (sxp, slevel)\n\n";
                description += "**usage:** `lb [category] [global]`\n";
                description += "**examples:**\n";
                description += "• `lb` — server net worth\n";
                description += "• `lb wallet` or `lb bal` — server wallet\n";
                description += "• `lb nw global` — global net worth\n";
                description += "• `lb fc` — server fish caught\n\n";
                description += "*use ◀▶ buttons to navigate between categories*";
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                
                // Add dropdown for easy selection
                auto select_menu = create_category_select_menu(event.msg.author.id);
                
                dpp::message msg;
                msg.add_embed(embed);
                msg.add_component(dpp::component().add_component(select_menu));
                
                bronx::send_message(bot, event, msg);
                return;
            }
            
            // Get leaderboard data - always fetch global (we'll filter by guild members after)
            auto entries = get_entries_for_category(db, category, 0, 5000); // Get top 5000 to ensure server members appear
            
            // Filter by guild members if this is a server request
            if (!global_scope && guild_id != 0) {
                entries = filter_by_guild_members(bot, entries, guild_id);
            }
            
            if (entries.empty()) {
                ::std::string description;
                if (!global_scope) {
                    description = "no users from this server found in the " + info.title + " leaderboard\n\n";
                    description += "try `lb " + category + " global` to see the global leaderboard";
                } else {
                    description = "no data available for the " + info.title + " leaderboard";
                }
                auto embed = bronx::info(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Calculate pagination (always start at page 1)
            int total_entries = static_cast<int>(entries.size());
            int total_pages = (total_entries + LEADERBOARD_ENTRIES_PER_PAGE - 1) / LEADERBOARD_ENTRIES_PER_PAGE;
            int current_page = 1;
            
            // Calculate total value for percentage display
            int64_t total_value = calculate_total_value(entries);
            
            // Get entries for first page
            int end_idx = ::std::min(LEADERBOARD_ENTRIES_PER_PAGE, total_entries);
            ::std::vector<bronx::db::LeaderboardEntry> page_entries(
                entries.begin(),
                entries.begin() + end_idx
            );
            
            ::std::string description = build_leaderboard_description(bot, db, page_entries, info, category, 1, global_scope, event.msg.author.id, total_value);
            
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            
            dpp::message msg;
            msg.add_embed(embed);
            
            // Add paginator buttons (row 1)
            uint64_t author_id = static_cast<uint64_t>(event.msg.author.id);
            auto paginator = create_paginator_buttons(category, global_scope, current_page, total_pages, author_id);
            msg.add_component(paginator);
            
            // Add category dropdown selector (row 2)
            auto dropdown = create_category_dropdown_row(category, global_scope, author_id);
            msg.add_component(dropdown);
            
            bronx::send_message(bot, event, msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Defer the response immediately since leaderboard queries can be slow
            event.thinking(false, [db, &bot, event](const dpp::confirmation_callback_t&) {
                // Default to networth and server scope
                ::std::string category = "networth";
                bool global_scope = false;
                
                // Get category parameter (optional)
                try {
                    auto cat_param = event.get_parameter("category");
                    if (::std::holds_alternative<::std::string>(cat_param)) {
                        category = resolve_category_alias(::std::get<::std::string>(cat_param));
                    }
                } catch (...) {
                    // category parameter is optional, use default
                }
                
                // Get scope parameter (optional)
                try {
                    auto scope_param = event.get_parameter("scope");
                    if (::std::holds_alternative<::std::string>(scope_param)) {
                        global_scope = (::std::get<::std::string>(scope_param) == "global");
                    }
                } catch (...) {
                    // scope parameter is optional, use default
                }
                
                uint64_t guild_id = global_scope ? 0 : static_cast<uint64_t>(event.command.guild_id);
                
                // Get category info
                auto info = get_category_info(category);
                
                if (info.title == "unknown") {
                    event.edit_response(dpp::message().add_embed(bronx::error("unknown leaderboard category")));
                    return;
                }
                
                // Get leaderboard data - always fetch global (we'll filter by guild members after)
                auto entries = get_entries_for_category(db, category, 0, 5000); // Get top 5000 to ensure server members appear
                
                // Filter by guild members if this is a server request
                if (!global_scope && guild_id != 0) {
                    entries = filter_by_guild_members(bot, entries, guild_id);
                }
            
                if (entries.empty()) {
                    ::std::string description;
                    if (!global_scope) {
                        description = "no users from this server found in the " + info.title + " leaderboard\n\n";
                        description += "try `/leaderboard " + category + " scope:global` to see the global leaderboard";
                    } else {
                        description = "no data available for the " + info.title + " leaderboard";
                    }
                    event.edit_response(dpp::message().add_embed(bronx::info(description)));
                    return;
                }
                
                // Calculate pagination (always start at page 1)
                int total_entries = static_cast<int>(entries.size());
                int total_pages = (total_entries + LEADERBOARD_ENTRIES_PER_PAGE - 1) / LEADERBOARD_ENTRIES_PER_PAGE;
                int current_page = 1;
                
                // Calculate total value for percentage display
                int64_t total_value = calculate_total_value(entries);
                
                // Get entries for first page
                int end_idx = ::std::min(LEADERBOARD_ENTRIES_PER_PAGE, total_entries);
                ::std::vector<bronx::db::LeaderboardEntry> page_entries(
                    entries.begin(),
                    entries.begin() + end_idx
                );
                
                ::std::string description = build_leaderboard_description(bot, db, page_entries, info, category, 1, global_scope, event.command.usr.id, total_value);
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.command.usr);
                
                dpp::message msg;
                msg.add_embed(embed);
                
                // Add paginator buttons (row 1)
                uint64_t user_id = static_cast<uint64_t>(event.command.usr.id);
                auto paginator = create_paginator_buttons(category, global_scope, current_page, total_pages, user_id);
                msg.add_component(paginator);
                
                // Add category dropdown selector (row 2)
                auto dropdown = create_category_dropdown_row(category, global_scope, user_id);
                msg.add_component(dropdown);
                
                event.edit_response(msg);
            }); // end event.thinking callback
        },
        options);
        
    cmds.push_back(leaderboard_cmd);
    
    return cmds;
}

} // namespace leaderboard
} // namespace commands
