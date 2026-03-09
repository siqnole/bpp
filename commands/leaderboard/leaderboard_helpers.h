#pragma once
#include "../../database/core/database.h"
#include "../../embed_style.h"
#include "../titles.h"
#include "../economy/helpers.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <iomanip>

namespace commands {
namespace leaderboard {

using commands::economy::format_number;

// Convert prestige level to roman numerals
inline ::std::string prestige_to_roman(int prestige) {
    if (prestige <= 0) return "";
    
    // Roman numeral conversion (supports up to 3999)
    static const ::std::vector<::std::pair<int, ::std::string>> roman_map = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
        {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
    };
    
    ::std::string result;
    int remaining = prestige;
    
    for (const auto& [value, numeral] : roman_map) {
        while (remaining >= value) {
            result += numeral;
            remaining -= value;
        }
    }
    
    return result;
}

// Get prestige display string (e.g., "[II] " for prestige 2)
inline ::std::string get_prestige_display(bronx::db::Database* db, uint64_t user_id) {
    if (!db) return "";
    int prestige = db->get_prestige(user_id);
    if (prestige <= 0) return "";
    return "[" + prestige_to_roman(prestige) + "] ";
}

// Filter leaderboard entries to only include members of a specific guild
inline ::std::vector<bronx::db::LeaderboardEntry> filter_by_guild_members(
    dpp::cluster& bot,
    const ::std::vector<bronx::db::LeaderboardEntry>& entries,
    uint64_t guild_id
) {
    if (guild_id == 0) {
        // Global scope, no filtering needed
        return entries;
    }
    
    // Get the guild
    dpp::guild* g = dpp::find_guild(guild_id);
    if (!g) {
        // Guild not in cache, return unfiltered (fallback)
        return entries;
    }
    
    // Filter entries to only include users who are in the guild
    ::std::vector<bronx::db::LeaderboardEntry> filtered;
    for (const auto& entry : entries) {
        // Check if user is a member of this guild
        auto member = g->members.find(entry.user_id);
        if (member != g->members.end()) {
            filtered.push_back(entry);
        }
    }
    
    // Re-rank the filtered results
    for (size_t i = 0; i < filtered.size(); i++) {
        filtered[i].rank = i + 1;
    }
    
    return filtered;
}

// All available categories in order
static const ::std::vector<::std::string> ALL_CATEGORIES = {
    "networth", "wallet", "bank", "prestige",
    "fish-caught", "fish-sold", "fish-value", "fishing-profit",
    "gambling-profit", "gambling-losses",
    "commands",
    "global-xp", "server-xp"
};

// Resolve alias to full category name
inline ::std::string resolve_category_alias(const ::std::string& input) {
    ::std::string lower = input;
    ::std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Check if it's already a valid category
    if (::std::find(ALL_CATEGORIES.begin(), ALL_CATEGORIES.end(), lower) != ALL_CATEGORIES.end()) {
        return lower;
    }
    
    // Check aliases
    if (lower == "nw" || lower == "net") return "networth";
    if (lower == "bal" || lower == "balance") return "wallet";
    if (lower == "fc" || lower == "fishcaught") return "fish-caught";
    if (lower == "fs" || lower == "fishsold") return "fish-sold";
    if (lower == "fv" || lower == "fishvalue") return "fish-value";
    if (lower == "fp" || lower == "fishprofit") return "fishing-profit";
    if (lower == "gp" || lower == "gambleprofit" || lower == "gambling-wins" || lower == "gw") return "gambling-profit";
    if (lower == "gl" || lower == "gamblelosses") return "gambling-losses";
    if (lower == "cmd" || lower == "cmds") return "commands";
    if (lower == "p" || lower == "pres") return "prestige";
    if (lower == "gxp" || lower == "globalxp" || lower == "level" || lower == "lvl") return "global-xp";
    if (lower == "sxp" || lower == "serverxp" || lower == "slevel" || lower == "slvl") return "server-xp";
    
    return lower; // Return as-is if no alias matches
}

// Get index of category in ALL_CATEGORIES (-1 if not found)
inline int get_category_index(const ::std::string& category) {
    for (size_t i = 0; i < ALL_CATEGORIES.size(); i++) {
        if (ALL_CATEGORIES[i] == category) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Get next category in the list (wraps around)
inline ::std::string get_next_category(const ::std::string& category) {
    int idx = get_category_index(category);
    if (idx == -1) return ALL_CATEGORIES[0];
    return ALL_CATEGORIES[(idx + 1) % ALL_CATEGORIES.size()];
}

// Get previous category in the list (wraps around)
inline ::std::string get_prev_category(const ::std::string& category) {
    int idx = get_category_index(category);
    if (idx == -1) return ALL_CATEGORIES[0];
    return ALL_CATEGORIES[(idx - 1 + ALL_CATEGORIES.size()) % ALL_CATEGORIES.size()];
}

// Structure to hold category information
struct CategoryInfo {
    ::std::string title;
    ::std::string emoji;
    bool is_currency;
};

// Get category information based on category string
inline CategoryInfo get_category_info(const ::std::string& category) {
    if (category == "networth") {
        return {"net worth", "💰", true};
    } else if (category == "wallet") {
        return {"wallet", "💵", true};
    } else if (category == "bank") {
        return {"bank", "🏦", true};
    } else if (category == "fish-caught") {
        return {"fish caught", "🐟", false};
    } else if (category == "fish-sold") {
        return {"fish sold", "📦", false};
    } else if (category == "fish-value") {
        return {"most valuable fish", "💎", true};
    } else if (category == "fishing-profit") {
        return {"fishing profit", "🎣", true};
    } else if (category == "gambling-profit" || category == "gambling-wins") {
        return {"gambling profit", "🎰", true};
    } else if (category == "gambling-losses") {
        return {"gambling losses", "💸", true};
    } else if (category == "commands") {
        return {"commands used", "⌨️", false};
    } else if (category == "prestige") {
        return {"prestige level", bronx::EMOJI_STAR, false};
    } else if (category == "global-xp") {
        return {"XP", "🌐", false};
    } else if (category == "server-xp") {
        return {"server XP", "📊", false};
    }
    return {"unknown", "❓", false};
}

// Create select menu for categories
inline dpp::component create_category_select_menu(uint64_t user_id) {
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("select a category")
        .set_id("lb_category_" + ::std::to_string(user_id));
    
    select_menu.add_select_option(dpp::select_option("💰 net worth", "networth", "total wallet + bank"))
               .add_select_option(dpp::select_option("💵 wallet", "wallet", "wallet balance"))
               .add_select_option(dpp::select_option("🏦 bank", "bank", "bank balance"))
               .add_select_option(dpp::select_option("🐟 fish caught", "fish-caught", "total fish caught"))
               .add_select_option(dpp::select_option("📦 fish sold", "fish-sold", "total fish sold"))
               .add_select_option(dpp::select_option("💎 valuable fish", "fish-value", "most valuable catch"))
               .add_select_option(dpp::select_option("🎣 fishing profit", "fishing-profit", "total fishing earnings"))
               .add_select_option(dpp::select_option("🎰 gambling profit", "gambling-profit", "gambling earnings"))
               .add_select_option(dpp::select_option("💸 gambling losses", "gambling-losses", "gambling losses"))
               .add_select_option(dpp::select_option("⌨️ commands used", "commands", "command usage count"))
               .add_select_option(dpp::select_option(bronx::EMOJI_STAR + " prestige level", "prestige", "prestige rank"))
               .add_select_option(dpp::select_option("🌐 global XP", "global-xp", "total XP across all servers"))
               .add_select_option(dpp::select_option("📊 server XP", "server-xp", "XP in this server"));
    
    return select_menu;
}

// Get leaderboard entries based on category
inline ::std::vector<bronx::db::LeaderboardEntry> get_entries_for_category(
    bronx::db::Database* db,
    const ::std::string& category,
    uint64_t guild_id,
    int limit
) {
    if (category == "networth") {
        return db->get_networth_leaderboard(guild_id, limit);
    } else if (category == "wallet") {
        return db->get_wallet_leaderboard(guild_id, limit);
    } else if (category == "bank") {
        return db->get_bank_leaderboard(guild_id, limit);
    } else if (category == "fish-caught") {
        return db->get_fish_caught_leaderboard(guild_id, limit);
    } else if (category == "fish-sold") {
        return db->get_fish_sold_leaderboard(guild_id, limit);
    } else if (category == "fish-value") {
        return db->get_most_valuable_fish_leaderboard(guild_id, limit);
    } else if (category == "fishing-profit") {
        return db->get_fishing_profit_leaderboard(guild_id, limit);
    } else if (category == "gambling-profit" || category == "gambling-wins") {
        return db->get_gambling_profit_leaderboard(guild_id, limit);
    } else if (category == "gambling-losses") {
        return db->get_gambling_losses_leaderboard(guild_id, limit);
    } else if (category == "commands") {
        return db->get_commands_used_leaderboard(guild_id, limit);
    } else if (category == "prestige") {
        return db->get_prestige_leaderboard(guild_id, limit);
    } else if (category == "global-xp") {
        return db->get_global_xp_leaderboard(limit);
    } else if (category == "server-xp") {
        return db->get_server_xp_leaderboard(guild_id, limit);
    }
    return {};
}

// ------------------------------------------------------------
//  Automatic title awards based on global leaderboard
// ------------------------------------------------------------

// Title display strings for top 3 of each leaderboard category
struct LeaderboardTitleConfig {
    ::std::string category;
    ::std::string title_prefix;  // item_id prefix (e.g., "title_lb_networth")
    ::std::string display_1st;   // display for rank 1
    ::std::string display_2nd;   // display for rank 2
    ::std::string display_3rd;   // display for rank 3
};

inline const ::std::vector<LeaderboardTitleConfig>& get_leaderboard_title_configs() {
    static const ::std::vector<LeaderboardTitleConfig> configs = {
        {"networth",        "title_lb_networth",     "🥇 Broke No More",     "🥈 Suspiciously Rich",  "🥉 Probably Laundering"},
        {"wallet",          "title_lb_wallet",       "🥇 Cash Hoarder",      "🥈 Wallet Thicc",       "🥉 Coin Collector"},
        {"bank",            "title_lb_bank",         "🥇 The Fed's Favorite", "🥈 Compound Interest Simp", "🥉 Penny Pincher"},
        {"prestige",        "title_lb_prestige",     "🥇 Tryhard Supreme",   "🥈 No-Life King",       "🥉 Touch Grass Later"},
        {"fish-caught",     "title_lb_fishcaught",   "🥇 Fish Genocide",     "🥈 Ocean Terror",       "🥉 Fishmeister"},
        {"fish-sold",       "title_lb_fishsold",     "🥇 Fish Dealer",       "🥈 Aquatic Pusher",     "🥉 Fish Hustler"},
        {"fish-value",      "title_lb_fishvalue",    "🥇 That One Guy",      "🥈 Suspiciously Lucky", "🥉 Fish Blessed"},
        {"fishing-profit",  "title_lb_fishprofit",   "🥇 Fishing Addict",    "🥈 Touch Grass, Bro",   "🥉 Fish Obsessed"},
        {"gambling-profit", "title_lb_gambleprofit", "🥇 Luck Has Left Me",  "🥈 Vegas Refugee",      "🥉 Gambling Consequences"},
        {"gambling-losses", "title_lb_gamblelosses", "🥇 Professional Loser", "🥈 Debt Iterator",      "🥉 Bankruptcy Speedrunner"},
        {"commands",        "title_lb_commands",     "🥇 Keyboard Warrior", "🥈 Obsessive Clicker",  "🥉 Bot Enabler"},
    };
    return configs;
}

// Award top 3 titles for a specific leaderboard category
// Only one person can hold each rank title at a time
inline void award_leaderboard_titles_for_category(
    bronx::db::Database* db,
    const LeaderboardTitleConfig& config
) {
    if (!db) return;
    
    // Fetch top 3 entries for this category (global scope)
    auto entries = get_entries_for_category(db, config.category, 0, 3);
    
    // Map rank to user_id (0 if not enough entries)
    uint64_t rank_users[3] = {0, 0, 0};
    for (size_t i = 0; i < entries.size() && i < 3; i++) {
        rank_users[i] = entries[i].user_id;
    }
    
    // Process each rank (1st, 2nd, 3rd)
    const ::std::string displays[3] = {config.display_1st, config.display_2nd, config.display_3rd};
    const ::std::string suffixes[3] = {"_1st", "_2nd", "_3rd"};
    
    for (int rank = 0; rank < 3; rank++) {
        ::std::string item_id = config.title_prefix + suffixes[rank];
        uint64_t new_holder = rank_users[rank];
        
        // Get current holder(s) of this title
        auto current_holders = db->get_users_with_item(item_id);
        
        // Remove title from anyone who isn't the new holder
        for (uint64_t uid : current_holders) {
            if (uid != new_holder) {
                db->remove_item(uid, item_id, 1);
            }
        }
        
        // Grant title to new holder if they don't already have it
        if (new_holder != 0) {
            bool already_has = false;
            for (uint64_t uid : current_holders) {
                if (uid == new_holder) {
                    already_has = true;
                    break;
                }
            }
            if (!already_has) {
                ::std::string meta = title_display_to_json(displays[rank]);
                db->add_item(new_holder, item_id, "title", 1, meta, 1);
            }
        }
    }
}

// Award top 3 titles for ALL leaderboard categories
// Call this daily at 00:00 EST
inline void award_all_leaderboard_titles(bronx::db::Database* db) {
    if (!db) return;
    
    for (const auto& config : get_leaderboard_title_configs()) {
        award_leaderboard_titles_for_category(db, config);
    }
}

inline void award_top10_global_titles(bronx::db::Database* db) {
    if (!db) return;

    // fetch the current top ten global networth entries
    auto entries = db->get_networth_leaderboard(0, 10);
    std::set<uint64_t> winners;
    for (const auto& e : entries) {
        winners.insert(e.user_id);
    }

    // remove the title from any users who still own it but are no longer
    // in the top ten
    const std::string kItem = "title_global_top10";
    auto owners = db->get_users_with_item(kItem);
    for (uint64_t uid : owners) {
        if (winners.find(uid) == winners.end()) {
            db->remove_item(uid, kItem, 1);
        }
    }

    // grant the title to new winners (metadata contains display string so it's
    // usable by get_equipped_title_display)
    std::string meta = title_display_to_json("🌍 Top 10 Global");
    for (uint64_t uid : winners) {
        if (!db->has_item(uid, kItem)) {
            db->add_item(uid, kItem, "title", 1, meta, 1);
        }
    }
}

// Combined function to run all daily title awards at 00:00 EST
inline void run_daily_title_awards(bronx::db::Database* db) {
    award_all_leaderboard_titles(db);
    award_top10_global_titles(db);
}


// Create paginator buttons for navigating within a leaderboard
inline dpp::component create_paginator_buttons(
    const ::std::string& category,
    bool is_global,
    int current_page,
    int total_pages,
    uint64_t user_id
) {
    ::std::string scope = is_global ? "global" : "server";
    ::std::string user_id_str = ::std::to_string(user_id);
    
    dpp::component action_row;
    action_row.set_type(dpp::cot_action_row);
    
    // Previous page button
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button);
    prev_btn.set_style(dpp::cos_primary);
    prev_btn.set_id("lb_pageprev_" + category + "_" + scope + "_" + ::std::to_string(current_page) + "_" + user_id_str);
    prev_btn.set_label("◀");
    prev_btn.set_disabled(current_page <= 1);
    
    // Page indicator button (non-interactive)
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button);
    page_btn.set_style(dpp::cos_secondary);
    page_btn.set_id("lb_pageinfo_" + category + "_" + scope + "_" + ::std::to_string(current_page) + "_" + user_id_str);
    page_btn.set_label(::std::to_string(current_page) + "/" + ::std::to_string(total_pages));
    page_btn.set_disabled(true);
    
    // Next page button
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button);
    next_btn.set_style(dpp::cos_primary);
    next_btn.set_id("lb_pagenext_" + category + "_" + scope + "_" + ::std::to_string(current_page) + "_" + user_id_str);
    next_btn.set_label("▶");
    next_btn.set_disabled(current_page >= total_pages);
    
    action_row.add_component(prev_btn);
    action_row.add_component(page_btn);
    action_row.add_component(next_btn);
    
    return action_row;
}

// Create navigation buttons for leaderboard (navigate between categories)
inline dpp::component create_navigation_buttons(
    const ::std::string& category,
    bool is_global,
    uint64_t user_id
) {
    ::std::string scope = is_global ? "global" : "server";
    ::std::string user_id_str = ::std::to_string(user_id);
    
    dpp::component action_row;
    action_row.set_type(dpp::cot_action_row);
    
    // Previous category button
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button);
    prev_btn.set_style(dpp::cos_secondary);
    prev_btn.set_id("lb_prev_" + category + "_" + scope + "_" + user_id_str);
    prev_btn.set_label("⏮");
    
    // Scope toggle button
    dpp::component toggle_btn;
    toggle_btn.set_type(dpp::cot_button);
    toggle_btn.set_style(is_global ? dpp::cos_success : dpp::cos_primary);
    toggle_btn.set_id("lb_toggle_" + category + "_" + scope + "_" + user_id_str);
    toggle_btn.set_label(is_global ? "🌍 global" : "🏠 server");
    
    // Next category button
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button);
    next_btn.set_style(dpp::cos_secondary);
    next_btn.set_id("lb_next_" + category + "_" + scope + "_" + user_id_str);
    next_btn.set_label("⏭");
    
    action_row.add_component(prev_btn);
    action_row.add_component(toggle_btn);
    action_row.add_component(next_btn);
    
    return action_row;
}

constexpr int LEADERBOARD_ENTRIES_PER_PAGE = 10;

// Calculate total value from all entries
inline int64_t calculate_total_value(const ::std::vector<bronx::db::LeaderboardEntry>& entries) {
    int64_t total = 0;
    for (const auto& entry : entries) {
        total += entry.value;
    }
    return total;
}

// Build leaderboard description string
inline ::std::string build_leaderboard_description(
    dpp::cluster& bot,
    bronx::db::Database* db,
    const ::std::vector<bronx::db::LeaderboardEntry>& entries,
    const CategoryInfo& info,
    const ::std::string& category,
    int start_rank,
    bool is_global,
    uint64_t author_id = 0,
    int64_t total_value = 0
) {
    ::std::string scope_text = is_global ? "global" : "server";
    ::std::string description = info.emoji + " **" + scope_text + " " + info.title + " leaderboard**\n\n";
    
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        int rank = start_rank + i;
        ::std::string rank_emoji;
        
        if (rank == 1) {
            rank_emoji = "🥇";
        } else if (rank == 2) {
            rank_emoji = "🥈";
        } else if (rank == 3) {
            rank_emoji = "🥉";
        } else {
            rank_emoji = "`" + ::std::to_string(rank) + ".`";
        }
        
        // Get username from Discord API
        ::std::string display_name;
        dpp::user* user = dpp::find_user(entry.user_id);
        if (user && !user->global_name.empty()) {
            // Use display name (global_name) if available
            display_name = user->global_name;
        } else if (user && !user->username.empty()) {
            // Fall back to username if no display name
            display_name = user->username;
        } else {
            // Fall back to user ID if username fetch fails
            display_name = "User#" + ::std::to_string(entry.user_id);
        }

        // Prestige prefix (e.g., "[II] " for prestige 2)
        ::std::string prestige_prefix = get_prestige_display(db, entry.user_id);

        // Equipped title prefix (empty if none set)
        ::std::string title_prefix;
        if (db) {
            title_prefix = commands::get_equipped_title_display(db, entry.user_id);
            if (!title_prefix.empty()) title_prefix += " ";
        }
        
        // Underline the author's name if they appear in the leaderboard
        ::std::string name_formatted = "**" + display_name + "**";
        if (author_id != 0 && entry.user_id == author_id) {
            name_formatted = "__" + name_formatted + "__";
        }
        
        description += rank_emoji + " " + prestige_prefix + title_prefix + name_formatted + " — ";
        
        if (info.is_currency) {
            description += "$" + format_number(entry.value);
        } else {
            description += format_number(entry.value);
        }
        
        // Show percentage for top 3 if total_value is provided
        if (rank <= 3 && total_value > 0) {
            double percentage = (static_cast<double>(entry.value) / total_value) * 100.0;
            std::ostringstream pct_stream;
            pct_stream << std::fixed << std::setprecision(1) << percentage;
            description += " (" + pct_stream.str() + "%)";
        }
        
        if (!entry.extra_info.empty()) {
            description += " " + entry.extra_info;
        }
        
        description += "\n";
    }
    
    return description;
}

} // namespace leaderboard
} // namespace commands
