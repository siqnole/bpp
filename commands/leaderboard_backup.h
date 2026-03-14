#pragma once
#include "../database/database.h"
#include "../embed_style.h"
#include "command.h"
#include <algorithm>
#include <sstream>

namespace commands {

::std::vector<Command*> get_leaderboard_commands(bronx::db::Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Main leaderboard command with category selection
    static Command* leaderboard = new Command("leaderboard", "view various leaderboards", "leaderboard", {"lb", "top"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Default to networth if no category provided
            ::std::string category = args.empty() ? "networth" : args[0];
            int page = 1;
            bool global_scope = false;
            
            // Parse arguments: category [page] [global]
            if (args.size() >= 2) {
                // Check if second arg is a number (page) or "global"
                try {
                    page = ::std::stoi(args[1]);
                    if (args.size() >= 3 && args[2] == "global") {
                        global_scope = true;
                    }
                } catch (const ::std::exception&) {
                    // Not a number, check if it's "global"
                    if (args[1] == "global") {
                        global_scope = true;
                    }
                }
            }
            
            if (page < 1) page = 1;
            
            uint64_t guild_id = global_scope ? 0 : static_cast<uint64_t>(event.msg.guild_id);
            int per_page = 10;
            
            // Get leaderboard data
            ::std::vector<bronx::db::LeaderboardEntry> entries;
            ::std::string title, emoji;
            
            if (category == "networth") {
                entries = db->get_networth_leaderboard(guild_id, per_page + 1);
                title = "Net Worth"; emoji = "💰";
            } else if (category == "wallet") {
                entries = db->get_wallet_leaderboard(guild_id, per_page + 1);
                title = "Wallet Balance"; emoji = "💵";
            } else if (category == "bank") {
                entries = db->get_bank_leaderboard(guild_id, per_page + 1);
                title = "Bank Balance"; emoji = "🏦";
            } else if (category == "fish-caught") {
                entries = db->get_fish_caught_leaderboard(guild_id, per_page + 1);
                title = "Fish Caught"; emoji = "🐟";
            } else if (category == "fish-sold") {
                entries = db->get_fish_sold_leaderboard(guild_id, per_page + 1);
                title = "Fish Sold"; emoji = "📦";
            } else if (category == "fish-value") {
                entries = db->get_most_valuable_fish_leaderboard(guild_id, per_page + 1);
                title = "Most Valuable Fish"; emoji = "💎";
            } else if (category == "fishing-profit") {
                entries = db->get_fishing_profit_leaderboard(guild_id, per_page + 1);
                title = "Fishing Profit"; emoji = "🎣";
            } else if (category == "gambling-profit") {
                entries = db->get_gambling_profit_leaderboard(guild_id, per_page + 1);
                title = "Gambling Profit"; emoji = "🎰";
            } else if (category == "gambling-losses") {
                entries = db->get_gambling_losses_leaderboard(guild_id, per_page + 1);
                title = "Gambling Losses"; emoji = "💸";
            } else if (category == "commands") {
                entries = db->get_commands_used_leaderboard(guild_id, per_page + 1);
                title = "Commands Used"; emoji = "⌨️";
            } else {
                // Invalid category, show available options
                ::std::string description = "📊 **leaderboard types:**\n\n";
                description += "💰 `networth` - total wealth\n";
                description += "💵 `wallet` - wallet balance\n";
                description += "🏦 `bank` - bank balance\n";
                description += "🐟 `fish-caught` - fish caught\n";
                description += "📦 `fish-sold` - fish sold\n";
                description += "💎 `fish-value` - most valuable fish\n";
                description += "🎣 `fishing-profit` - fishing profit\n";
                description += "🎰 `gambling-profit` - gambling profit\n";
                description += "💸 `gambling-losses` - gambling losses\n";
                description += "⌨️ `commands` - commands used\n\n";
                description += "**Usage:** `lb [category] [page] [global]`";
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (entries.empty()) {
                ::std::string description = bronx::EMOJI_DENY + " No data available for " + title + " leaderboard.";
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Check if there's a next page and handle pagination
            bool has_next = entries.size() > per_page;
            if (has_next) entries.pop_back();
            
            // Calculate start index for this page
            int start_rank = (page - 1) * per_page + 1;
            
            ::std::string scope_text = global_scope ? "Global" : "Server";
            ::std::string description = emoji + " **" + scope_text + " " + title + " Leaderboard**";
            if (page > 1 || has_next) {
                description += " (Page " + ::std::to_string(page) + ")";
            }
            description += "\n\n";
            
            for (size_t i = 0; i < entries.size(); i++) {
                const auto& entry = entries[i];
                int rank = start_rank + i;
                ::std::string position_emoji;
                
                if (rank <= 3) {
                    position_emoji = (rank == 1 ? "🥇" : (rank == 2 ? "🥈" : "🥉"));
                } else {
                    position_emoji = "▫️";
                }
                
                ::std::string display_name = "<@" + ::std::to_string(entry.user_id) + ">";
                description += position_emoji + " **" + ::std::to_string(rank) + ".** " + display_name;
                
                if (category == "networth" || category == "wallet" || category == "bank" || 
                    category == "fish-value" || category == "fishing-profit" || category == "gambling-profit") {
                    description += " - $" + format_number(entry.value);
                } else {
                    description += " - " + format_number(entry.value);
                }
                
                if (!entry.extra_info.empty()) {
                    description += " " + entry.extra_info;
                }
                
                description += "\n";
            }
            
            // Add scope info
            if (global_scope) {
                description += "\n*Use `lb " + category + (page > 1 ? " " + ::std::to_string(page) : "") + "` for server rankings*";
            } else {
                description += "\n*Use `lb " + category + (page > 1 ? " " + ::std::to_string(page) : "") + " global` for global rankings*";
            }
            
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            
            dpp::message msg;
            msg.add_embed(embed);
            
            // Add navigation buttons if there are multiple pages
            if (page > 1 || has_next) {
                dpp::component action_row;
                action_row.set_type(dpp::cot_action_row);
                
                // Previous button
                dpp::component prev_btn;
                prev_btn.set_type(dpp::cot_button);
                prev_btn.set_style(dpp::cos_secondary);
                prev_btn.set_id("lb_prev_" + category + "_" + ::std::to_string(page) + "_" + (global_scope ? "global" : "server"));
                prev_btn.set_label("Previous");
                prev_btn.set_emoji("⬅️");
                prev_btn.set_disabled(page <= 1);
                
                // Category info button
                dpp::component info_btn;
                info_btn.set_type(dpp::cot_button);
                info_btn.set_style(dpp::cos_primary);
                info_btn.set_id("lb_info_" + category + "_" + ::std::to_string(page) + "_" + (global_scope ? "global" : "server"));
                info_btn.set_label(title);
                info_btn.set_emoji("📊");
                
                // Next button
                dpp::component next_btn;
                next_btn.set_type(dpp::cot_button);
                next_btn.set_style(dpp::cos_secondary);
                next_btn.set_id("lb_next_" + category + "_" + ::std::to_string(page) + "_" + (global_scope ? "global" : "server"));
                next_btn.set_label("Next");
                next_btn.set_emoji("➡️");
                next_btn.set_disabled(!has_next);
                
                // Global/Server toggle
                dpp::component toggle_btn;
                toggle_btn.set_type(dpp::cot_button);
                toggle_btn.set_style(dpp::cos_success);
                toggle_btn.set_id("lb_toggle_" + category + "_" + ::std::to_string(page) + "_" + (global_scope ? "global" : "server"));
                toggle_btn.set_label(global_scope ? "Server" : "Global");
                toggle_btn.set_emoji(global_scope ? "🏠" : "🌍");
                
                action_row.add_component(prev_btn);
                action_row.add_component(info_btn);
                action_row.add_component(next_btn);
                action_row.add_component(toggle_btn);
                
                msg.add_component(action_row);
            }
            
            bronx::send_message(bot, event, msg);
            
            ::std::transform(category.begin(), category.end(), category.begin(), ::tolower);
            
            ::std::vector<bronx::db::LeaderboardEntry> entries;
            ::std::string title;
            ::std::string emoji;
            
            // Economy leaderboards
            if (category == "networth") {
                entries = db->get_networth_leaderboard(guild_id, 10);
                title = "Net Worth";
                emoji = "💰";
            }
            else if (category == "wallet") {
                entries = db->get_wallet_leaderboard(guild_id, 10);
                title = "Wallet Balance";
                emoji = "💵";
            }
            else if (category == "bank") {
                entries = db->get_bank_leaderboard(guild_id, 10);
                title = "Bank Balance";
                emoji = "🏦";
            }
            else if (category == "inventory") {
                entries = db->get_inventory_value_leaderboard(guild_id, 10);
                title = "Inventory Value";
                emoji = "🎒";
            }
            // Fishing leaderboards
            else if (category == "fish-caught") {
                entries = db->get_fish_caught_leaderboard(guild_id, 10);
                title = "Fish Caught";
                emoji = "🎣";
            }
            else if (category == "fish-sold") {
                entries = db->get_fish_sold_leaderboard(guild_id, 10);
                title = "Fish Sold";
                emoji = "🐟";
            }
            else if (category == "fish-value") {
                entries = db->get_most_valuable_fish_leaderboard(guild_id, 10);
                title = "Most Valuable Fish";
                emoji = "💎";
            }
            else if (category == "fishing-profit") {
                entries = db->get_fishing_profit_leaderboard(guild_id, 10);
                title = "Fishing Profit";
                emoji = "💰";
            }
            // Gambling leaderboards
            else if (category == "gambling-wins") {
                entries = db->get_gambling_wins_leaderboard(guild_id, 10);
                title = "Gambling Wins";
                emoji = "🎰";
            }
            else if (category == "gambling-losses") {
                entries = db->get_gambling_losses_leaderboard(guild_id, 10);
                title = "Gambling Losses";
                emoji = "💸";
            }
            else if (category == "gambling-profit") {
                entries = db->get_gambling_profit_leaderboard(guild_id, 10);
                title = "Gambling Profit";
                emoji = "💵";
            }
            else if (category == "slots-wins") {
                entries = db->get_slots_wins_leaderboard(guild_id, 10);
                title = "Slots Wins";
                emoji = "🎰";
            }
            else if (category == "coinflip-wins") {
                entries = db->get_coinflip_wins_leaderboard(guild_id, 10);
                title = "Coinflip Wins";
                emoji = "🪙";
            }
            // Activity leaderboards
            else if (category == "commands") {
                entries = db->get_commands_used_leaderboard(guild_id, 10);
                title = "Commands Used";
                emoji = "⚡";
            }
            else if (category == "daily-streak") {
                entries = db->get_daily_streak_leaderboard(guild_id, 10);
                title = "Daily Streak";
                emoji = "🔥";
            }
            else if (category == "work-count") {
                entries = db->get_work_count_leaderboard(guild_id, 10);
                title = "Work Commands";
                emoji = "💼";
            }
            else {
                bronx::send_message(bot, event, bronx::error("Unknown leaderboard category. Use `lb` to see all categories."));
                return;
            }
            
            if (entries.empty()) {
                bronx::send_message(bot, event, bronx::info("No data available for this leaderboard."));
                return;
            }
            
            ::std::string scope_text = global_scope ? "Global" : "Server";
            ::std::string description = emoji + " **" + scope_text + " " + title + " Leaderboard**\n\n";
            
            for (size_t i = 0; i < entries.size(); i++) {
                const auto& entry = entries[i];
                ::std::string position_emoji;
                
                switch (i + 1) {
                    case 1: position_emoji = "🥇"; break;
                    case 2: position_emoji = "🥈"; break;
                    case 3: position_emoji = "🥉"; break;
                    default: position_emoji = "▫️"; break;
                }
                
                // Try to get actual username from Discord
                ::std::string display_name = "<@" + ::std::to_string(entry.user_id) + ">";
                
                description += position_emoji + " **" + ::std::to_string(i + 1) + ".** " + display_name;
                
                // Format value based on category
                if (category == "networth" || category == "wallet" || category == "bank" || 
                    category == "inventory" || category == "fishing-profit" || category == "gambling-profit" ||
                    category == "fish-value") {
                    description += " - $" + format_number(entry.value);
                } else {
                    description += " - " + format_number(entry.value);
                }
                
                if (!entry.extra_info.empty()) {
                    description += " " + entry.extra_info;
                }
                
                description += "\n";
            }
            
            if (global_scope) {
                description += "\n*Use `lb " + category + "` for server rankings*";
            } else {
                description += "\n*Use `lb " + category + " global` for global rankings*";
            }
            
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ::std::string category = ::std::get<::std::string>(event.get_parameter("category"));
            bool global_scope = false;
            
            try {
                auto scope_param = event.get_parameter("scope");
                if (::std::holds_alternative<::std::string>(scope_param)) {
                    global_scope = (::std::get<::std::string>(scope_param) == "global");
                }
            } catch (...) {
                // scope parameter is optional
            }
            
            ::std::transform(category.begin(), category.end(), category.begin(), ::tolower);
            uint64_t guild_id = global_scope ? 0 : static_cast<uint64_t>(event.command.guild_id);
            
            ::std::vector<bronx::db::LeaderboardEntry> entries;
            ::std::string title;
            ::std::string emoji;
            
            // Same logic as text command but condensed
            if (category == "networth") {
                entries = db->get_networth_leaderboard(guild_id, 10);
                title = "Net Worth"; emoji = "💰";
            } else if (category == "wallet") {
                entries = db->get_wallet_leaderboard(guild_id, 10);
                title = "Wallet Balance"; emoji = "💵";
            } else if (category == "bank") {
                entries = db->get_bank_leaderboard(guild_id, 10);
                title = "Bank Balance"; emoji = "🏦";
            } else if (category == "fish-caught") {
                entries = db->get_fish_caught_leaderboard(guild_id, 10);
                title = "Fish Caught"; emoji = "🎣";
            } else if (category == "gambling-wins") {
                entries = db->get_gambling_wins_leaderboard(guild_id, 10);
                title = "Gambling Wins"; emoji = "🎰";
            } else {
                event.reply(dpp::message().add_embed(bronx::error("Category not supported in slash command yet.")));
                return;
            }
            
            if (entries.empty()) {
                event.reply(dpp::message().add_embed(bronx::info("No data available for this leaderboard.")));
                return;
            }
            
            ::std::string scope_text = global_scope ? "Global" : "Server";
            ::std::string description = emoji + " **" + scope_text + " " + title + " Leaderboard**\n\n";
            
            for (size_t i = 0; i < entries.size(); i++) {
                const auto& entry = entries[i];
                ::std::string position_emoji = (i < 3) ? (i == 0 ? "🥇" : (i == 1 ? "🥈" : "🥉")) : "▫️";
                
                // Use Discord mention for actual username display
                ::std::string display_name = "<@" + ::std::to_string(entry.user_id) + ">";
                
                description += position_emoji + " **" + ::std::to_string(i + 1) + ".** " + display_name;
                description += " - " + (category == "networth" || category == "wallet" || category == "bank" ? 
                    "$" + format_number(entry.value) : format_number(entry.value));
                description += "\n";
            }
            
            auto embed = bronx::create_embed(description);
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "category", "Leaderboard category", true)
                .add_choice(dpp::command_option_choice("Net Worth", "networth"))
                .add_choice(dpp::command_option_choice("Wallet Balance", "wallet"))
                .add_choice(dpp::command_option_choice("Bank Balance", "bank"))
                .add_choice(dpp::command_option_choice("Fish Caught", "fish-caught"))
                .add_choice(dpp::command_option_choice("Gambling Wins", "gambling-wins")),
            dpp::command_option(dpp::co_string, "scope", "Global or server rankings", false)
                .add_choice(dpp::command_option_choice("Server", "server"))
                .add_choice(dpp::command_option_choice("Global", "global"))
        });
        
    cmds.push_back(leaderboard);
    
    // Navigation leaderboard command with pagination
    static Command* nav_leaderboard = new Command("nlb", "paginated leaderboard navigator", "nlb", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Default to networth leaderboard, page 1
            ::std::string category = args.empty() ? "networth" : args[0];
            int page = (args.size() > 1) ? ::std::stoi(args[1]) : 1;
            bool is_global = (args.size() > 2 && args[2] == "global");
            
            if (page < 1) page = 1;
            
            uint64_t guild_id = is_global ? 0 : static_cast<uint64_t>(event.msg.guild_id);
            int per_page = 10;
            int offset = (page - 1) * per_page;
            
            // Get leaderboard data
            ::std::vector<bronx::db::LeaderboardEntry> entries;
            ::std::string title, emoji;
            
            if (category == "networth") {
                entries = db->get_networth_leaderboard(guild_id, per_page + 1); // +1 to check if there's a next page
                title = "Net Worth"; emoji = "💰";
            } else if (category == "wallet") {
                entries = db->get_wallet_leaderboard(guild_id, per_page + 1);
                title = "Wallet Balance"; emoji = "💵";
            } else if (category == "bank") {
                entries = db->get_bank_leaderboard(guild_id, per_page + 1);
                title = "Bank Balance"; emoji = "🏦";
            } else if (category == "fish-caught") {
                entries = db->get_fish_caught_leaderboard(guild_id, per_page + 1);
                title = "Fish Caught"; emoji = "🐟";
            } else if (category == "fish-sold") {
                entries = db->get_fish_sold_leaderboard(guild_id, per_page + 1);
                title = "Fish Sold"; emoji = "📦";
            } else if (category == "fish-value") {
                entries = db->get_most_valuable_fish_leaderboard(guild_id, per_page + 1);
                title = "Most Valuable Fish"; emoji = "💎";
            } else if (category == "fishing-profit") {
                entries = db->get_fishing_profit_leaderboard(guild_id, per_page + 1);
                title = "Fishing Profit"; emoji = "🎣";
            } else if (category == "gambling-profit") {
                entries = db->get_gambling_profit_leaderboard(guild_id, per_page + 1);
                title = "Gambling Profit"; emoji = "🎰";
            } else if (category == "gambling-losses") {
                entries = db->get_gambling_losses_leaderboard(guild_id, per_page + 1);
                title = "Gambling Losses"; emoji = "💸";
            } else if (category == "commands") {
                entries = db->get_commands_used_leaderboard(guild_id, per_page + 1);
                title = "Commands Used"; emoji = "⌨️";
            } else {
                // Invalid category, show help
                ::std::string description = "📊 **Available Leaderboard Categories:**\n\n";
                description += "💰 **Economy**: `networth`, `wallet`, `bank`\n";
                description += "🐟 **Fishing**: `fish-caught`, `fish-sold`, `fish-value`, `fishing-profit`\n";
                description += "🎰 **Gambling**: `gambling-profit`, `gambling-losses`\n";
                description += "⌨️ **Activity**: `commands`\n\n";
                description += "**Usage:** `nlb [category] [page] [global]`\n";
                description += "**Example:** `nlb networth 2 global`";
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (entries.empty()) {
                ::std::string description = bronx::EMOJI_DENY + " No data available for " + title + " leaderboard.";
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Check if there's a next page
            bool has_next = entries.size() > per_page;
            if (has_next) entries.pop_back(); // Remove the extra entry
            
            // Skip entries for pagination
            if (offset > 0) {
                if (offset >= entries.size()) {
                    // Page out of range
                    ::std::string description = bronx::EMOJI_DENY + " Page " + ::std::to_string(page) + " is out of range for this leaderboard.";
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
            }
            
            ::std::string scope_text = is_global ? "Global" : "Server";
            ::std::string description = emoji + " **" + scope_text + " " + title + " Leaderboard** (Page " + ::std::to_string(page) + ")\n\n";
            
            for (size_t i = 0; i < ::std::min(entries.size(), static_cast<size_t>(per_page)); i++) {
                const auto& entry = entries[i];
                int global_rank = offset + i + 1;
                ::std::string position_emoji;
                
                if (global_rank <= 3) {
                    position_emoji = (global_rank == 1 ? "🥇" : (global_rank == 2 ? "🥈" : "🥉"));
                } else {
                    position_emoji = "▫️";
                }
                
                ::std::string display_name = "<@" + ::std::to_string(entry.user_id) + ">";
                description += position_emoji + " **" + ::std::to_string(global_rank) + ".** " + display_name;
                
                if (category == "networth" || category == "wallet" || category == "bank" || 
                    category == "fish-value" || category == "fishing-profit" || category == "gambling-profit") {
                    description += " - $" + format_number(entry.value);
                } else {
                    description += " - " + format_number(entry.value);
                }
                
                if (!entry.extra_info.empty()) {
                    description += " " + entry.extra_info;
                }
                
                description += "\n";
            }
            
            // Add navigation info
            description += "\n➡️ **Use the buttons below to navigate pages**";
            if (is_global) {
                description += "\n*Use `nlb " + category + " " + ::std::to_string(page) + "` for server rankings*";
            } else {
                description += "\n*Use `nlb " + category + " " + ::std::to_string(page) + " global` for global rankings*";
            }
            
            dpp::embed embed;
            embed.set_description(description);
            embed.set_color(0x00ff00);
            embed.set_timestamp(time(0));
            
            dpp::message msg;
            msg.add_embed(embed);
            
            // Add navigation buttons
            dpp::component action_row;
            action_row.set_type(dpp::cot_action_row);
            
            // Previous button
            dpp::component prev_btn;
            prev_btn.set_type(dpp::cot_button);
            prev_btn.set_style(dpp::cos_secondary);
            prev_btn.set_id("lb_prev_" + category + "_" + ::std::to_string(page) + "_" + (is_global ? "global" : "server"));
            prev_btn.set_label("Previous");
            prev_btn.set_emoji("⬅️");
            prev_btn.set_disabled(page <= 1);
            
            // Category dropdown
            dpp::component category_btn;
            category_btn.set_type(dpp::cot_button);
            category_btn.set_style(dpp::cos_primary);
            category_btn.set_id("lb_category_" + category + "_" + ::std::to_string(page) + "_" + (is_global ? "global" : "server"));
            category_btn.set_label(title);
            category_btn.set_emoji("📊");
            
            // Next button
            dpp::component next_btn;
            next_btn.set_type(dpp::cot_button);
            next_btn.set_style(dpp::cos_secondary);
            next_btn.set_id("lb_next_" + category + "_" + ::std::to_string(page) + "_" + (is_global ? "global" : "server"));
            next_btn.set_label("Next");
            next_btn.set_emoji("➡️");
            next_btn.set_disabled(!has_next);
            
            // Global/Server toggle
            dpp::component toggle_btn;
            toggle_btn.set_type(dpp::cot_button);
            toggle_btn.set_style(dpp::cos_success);
            toggle_btn.set_id("lb_toggle_" + category + "_" + ::std::to_string(page) + "_" + (is_global ? "global" : "server"));
            toggle_btn.set_label(is_global ? "Server" : "Global");
            toggle_btn.set_emoji(is_global ? "🏠" : "🌍");
            
            action_row.add_component(prev_btn);
            action_row.add_component(category_btn);
            action_row.add_component(next_btn);
            action_row.add_component(toggle_btn);
            
            msg.add_component(action_row);
            
            bronx::send_message(bot, event, msg);
        },
        nullptr
    );
    cmds.push_back(nav_leaderboard);
    
    return cmds;
}

inline void register_leaderboard_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    // Handle pagination button clicks
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        if (custom_id.find("lb_") == 0) {
            // Parse button data: lb_action_category_page_scope
            ::std::vector<::std::string> parts;
            ::std::stringstream ss(custom_id);
            ::std::string part;
            while (::std::getline(ss, part, '_')) {
                parts.push_back(part);
            }
            
            if (parts.size() >= 5) {
                ::std::string action = parts[1];
                ::std::string category = parts[2];
                int current_page = ::std::stoi(parts[3]);
                bool is_global = parts[4] == "global";
                
                ::std::string new_command;
                
                if (action == "prev" && current_page > 1) {
                    new_command = "lb " + category + " " + ::std::to_string(current_page - 1);
                } else if (action == "next") {
                    new_command = "lb " + category + " " + ::std::to_string(current_page + 1);
                } else if (action == "toggle") {
                    new_command = "lb " + category + " " + ::std::to_string(current_page);
                    if (!is_global) new_command += " global";
                } else if (action == "info") {
                    // Show category help
                    ::std::string description = "📊 **Available Leaderboard Categories:**\n\n";
                    description += "💰 **Economy**: `networth`, `wallet`, `bank`\n";
                    description += "🐟 **Fishing**: `fish-caught`, `fish-sold`, `fish-value`, `fishing-profit`\n";
                    description += "🎰 **Gambling**: `gambling-profit`, `gambling-losses`\n";
                    description += "⌨️ **Activity**: `commands`\n\n";
                    description += "**Current:** " + category + " (Page " + ::std::to_string(current_page) + ")";
                    
                    dpp::embed embed;
                    embed.set_description(description);
                    embed.set_color(0x00ff00);
                    
                    event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                if (!new_command.empty()) {
                    // Acknowledge the interaction first
                    event.reply(dpp::ir_deferred_update_message, "Updating leaderboard...");
                    
                    // Then run the command via message simulation
                    bot.message_create(dpp::message(event.command.channel_id, new_command));
                }
            }
        }
    });
}

} // namespace commands