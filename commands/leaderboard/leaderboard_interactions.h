#pragma once
#include "../../database/core/database.h"
#include "../../embed_style.h"
#include "leaderboard_helpers.h"
#include <dpp/dpp.h>
#include <sstream>

namespace commands {
namespace leaderboard {

inline void register_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    // Handle category selection dropdown
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        // Check if this is a leaderboard category selection
        if (event.custom_id.find("lb_category_") != 0) return;
        
        // Extract user ID from custom_id
        ::std::string user_id_str = event.custom_id.substr(12); // "lb_category_".length()
        dpp::snowflake expected_user_id = ::std::stoull(user_id_str);
        
        // Verify the user clicking is the one who invoked the command
        if (event.command.get_issuing_user().id != expected_user_id) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        ::std::string selected_category = resolve_category_alias(event.values[0]);
        
        // Send the command as a message
        event.reply(dpp::ir_channel_message_with_source, 
            dpp::message().set_content("lb " + selected_category).set_flags(dpp::m_ephemeral));
    });
    
    // Handle button clicks - navigate between categories and pages
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        // Only handle leaderboard buttons
        if (custom_id.find("lb_") != 0) return;
        
        // Parse button data: lb_action_category_scope[_page]_userid
        ::std::vector<::std::string> parts;
        ::std::stringstream ss(custom_id);
        ::std::string part;
        while (::std::getline(ss, part, '_')) {
            parts.push_back(part);
        }
        
        if (parts.size() < 5) {
            // Invalid button format (need at least lb_action_category_scope_userid)
            return;
        }
        
        ::std::string action = parts[1];
        ::std::string category = parts[2];
        bool is_global = parts[3] == "global";
        int current_page = 1;
        uint64_t expected_user_id = 0;
        
        // For page buttons (pageprev/pagenext/pageinfo): lb_action_category_scope_page_userid
        // For nav buttons (prev/next/toggle): lb_action_category_scope_userid
        if (action == "pageprev" || action == "pagenext" || action == "pageinfo") {
            if (parts.size() >= 6) {
                try {
                    current_page = ::std::stoi(parts[4]);
                    expected_user_id = ::std::stoull(parts[5]);
                } catch (...) {
                    current_page = 1;
                }
            }
        } else {
            // Navigation buttons: lb_action_category_scope_userid
            if (parts.size() >= 5) {
                try {
                    expected_user_id = ::std::stoull(parts[4]);
                } catch (...) {
                    expected_user_id = 0;
                }
            }
        }
        
        // Verify the user clicking is the one who invoked the command
        if (expected_user_id != 0 && event.command.get_issuing_user().id != expected_user_id) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message().add_embed(bronx::error("this isn't your leaderboard")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Use the clicking user's ID for the new buttons
        uint64_t user_id = static_cast<uint64_t>(event.command.get_issuing_user().id);
        
        ::std::string new_category = category;
        bool new_global = is_global;
        int new_page = current_page;
        
        if (action == "prev") {
            // Previous category (reset to page 1)
            new_category = get_prev_category(category);
            new_page = 1;
        } else if (action == "next") {
            // Next category (reset to page 1)
            new_category = get_next_category(category);
            new_page = 1;
        } else if (action == "toggle") {
            // Toggle between server and global (reset to page 1)
            new_global = !is_global;
            new_page = 1;
        } else if (action == "pageprev") {
            // Previous page
            new_page = current_page - 1;
            if (new_page < 1) new_page = 1;
        } else if (action == "pagenext") {
            // Next page
            new_page = current_page + 1;
        } else if (action == "pageinfo") {
            // Page info button is disabled, but acknowledge click anyway
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            return;
        } else {
            // Unknown action, ignore
            return;
        }
        
        // Get guild_id based on scope
        uint64_t guild_id = new_global ? 0 : static_cast<uint64_t>(event.command.guild_id);
        
        // Get category info
        auto info = get_category_info(new_category);
        
        if (info.title == "unknown") {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("unknown category")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Get all leaderboard data (we need the full count for pagination)
        auto all_entries = get_entries_for_category(db, new_category, 0, 5000);
        
        // Filter by guild members if this is a server request
        if (!new_global && guild_id != 0) {
            all_entries = filter_by_guild_members(bot, all_entries, guild_id);
        }
        
        if (all_entries.empty()) {
            ::std::string description;
            if (!new_global) {
                description = "no users from this server found in the " + info.title + " leaderboard";
            } else {
                description = "no data available for the " + info.title + " leaderboard";
            }
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::info(description)).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Calculate pagination
        int total_entries = static_cast<int>(all_entries.size());
        int total_pages = (total_entries + LEADERBOARD_ENTRIES_PER_PAGE - 1) / LEADERBOARD_ENTRIES_PER_PAGE;
        
        // Clamp page to valid range
        if (new_page < 1) new_page = 1;
        if (new_page > total_pages) new_page = total_pages;
        
        // Calculate total value for percentage display
        int64_t total_value = calculate_total_value(all_entries);
        
        // Get entries for current page
        int start_idx = (new_page - 1) * LEADERBOARD_ENTRIES_PER_PAGE;
        int end_idx = ::std::min(start_idx + LEADERBOARD_ENTRIES_PER_PAGE, total_entries);
        
        ::std::vector<bronx::db::LeaderboardEntry> page_entries(
            all_entries.begin() + start_idx,
            all_entries.begin() + end_idx
        );
        
        // Build the leaderboard description
        int start_rank = start_idx + 1;
        ::std::string description = build_leaderboard_description(bot, db, page_entries, info, new_category, start_rank, new_global, event.command.usr.id, total_value);
        
        auto embed = bronx::create_embed(description);
        bronx::add_invoker_footer(embed, event.command.usr);
        
        dpp::message msg;
        msg.add_embed(embed);
        
        // Add paginator buttons (row 1)
        auto paginator = create_paginator_buttons(new_category, new_global, new_page, total_pages, user_id);
        msg.add_component(paginator);
        
        // Add category navigation buttons (row 2)
        auto nav_buttons = create_navigation_buttons(new_category, new_global, user_id);
        msg.add_component(nav_buttons);
        
        // Update the message
        event.reply(dpp::ir_update_message, msg);
    });
}

} // namespace leaderboard
} // namespace commands
