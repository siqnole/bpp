#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../achievements.h"
#include <dpp/dpp.h>
#include <map>
#include <mutex>

using namespace bronx::db;

namespace commands {
namespace achievements_ui {

// Category names for pagination
static const std::vector<std::string> CATEGORY_NAMES = {
    "🐟 Fish Value",
    "🎣 Fish Caught", 
    "🎰 Gambling Wins",
    "💰 Gambling Profit",
    "🎮 Games Won",
    "💻 Commands Used"
};

// Get the achievement list for a specific category
inline const std::vector<achievements::Achievement>* get_category_achievements(int category) {
    switch (category) {
        case 0: return &achievements::FISH_VALUE_ACHIEVEMENTS;
        case 1: return &achievements::FISH_CAUGHT_ACHIEVEMENTS;
        case 2: return &achievements::GAMBLING_ACHIEVEMENTS;
        case 3: return &achievements::GAMBLING_PROFIT_ACHIEVEMENTS;
        case 4: return &achievements::GAMES_WON_ACHIEVEMENTS;
        case 5: return &achievements::COMMANDS_USED_ACHIEVEMENTS;
        default: return nullptr;
    }
}

// Create embed for a specific category
inline dpp::embed create_category_embed(Database* db, uint64_t user_id, int category) {
    dpp::embed embed;
    embed.set_title("achievements");
    embed.set_color(0x9B59B6);
    
    const auto* achs = get_category_achievements(category);
    if (!achs || achs->empty()) {
        embed.set_description("No achievements in this category.");
        return embed;
    }
    
    int64_t stat_value = db->get_stat(user_id, (*achs)[0].stat_name);
    
    std::string content;
    for (const auto& ach : *achs) {
        bool unlocked = achievements::has_achievement(db, user_id, ach.id);
        std::string status = unlocked ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY;
        content += status + " **" + ach.name + "**\n";
        content += "   " + ach.description + "\n";
        if (!unlocked) {
            content += "   progress: " + achievements::format_with_commas(stat_value) + "/" + achievements::format_with_commas(ach.threshold) + "\n";
        }
        // Show rewards
        content += "   rewards: ";
        for (size_t i = 0; i < ach.rewards.size(); i++) {
            if (i > 0) content += ", ";
            content += std::to_string(ach.rewards[i].quantity) + "x " + ach.rewards[i].item_name;
        }
        content += "\n\n";
    }
    
    embed.add_field(CATEGORY_NAMES[category], content, false);
    
    // Add progress summary
    int unlocked = achievements::count_unlocked_achievements(db, user_id);
    int total = achievements::get_total_achievements();
    std::string footer = "page " + std::to_string(category + 1) + "/" + std::to_string(CATEGORY_NAMES.size());
    footer += " | progress: " + std::to_string(unlocked) + "/" + std::to_string(total) + " achievements";
    embed.set_footer(dpp::embed_footer().set_text(footer));
    
    return embed;
}

// Create navigation buttons
inline dpp::component create_nav_buttons(uint64_t user_id, int category) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);
    
    int total_categories = CATEGORY_NAMES.size();
    std::string uid_str = std::to_string(user_id);
    
    // Previous button
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button);
    prev_btn.set_style(dpp::cos_primary);
    prev_btn.set_label("◀ Prev");
    prev_btn.set_id("ach_prev_" + std::to_string(category) + "_" + uid_str);
    prev_btn.set_disabled(category <= 0);
    row.add_component(prev_btn);
    
    // Category indicator
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button);
    page_btn.set_style(dpp::cos_secondary);
    page_btn.set_label(CATEGORY_NAMES[category]);
    page_btn.set_id("ach_page_" + std::to_string(category) + "_" + uid_str);
    page_btn.set_disabled(true);
    row.add_component(page_btn);
    
    // Next button
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button);
    next_btn.set_style(dpp::cos_primary);
    next_btn.set_label("Next ▶");
    next_btn.set_id("ach_next_" + std::to_string(category) + "_" + uid_str);
    next_btn.set_disabled(category >= total_categories - 1);
    row.add_component(next_btn);
    
    return row;
}

// Create message with embed and navigation
inline dpp::message create_achievements_message(Database* db, uint64_t user_id, int category = 0) {
    dpp::message msg;
    msg.add_embed(create_category_embed(db, user_id, category));
    msg.add_component(create_nav_buttons(user_id, category));
    return msg;
}

} // namespace achievements_ui

// Achievements command - view your achievements progress
inline Command* get_achievements_command(Database* db) {
    static Command* ach = new Command("achievements", "view your achievements and progress", "economy", {"ach", "achieve"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t uid = event.msg.author.id;
            
            // Claim any eligible achievements based on current stats (retroactive)
            achievements::claim_eligible_achievements(db, uid);
            
            auto msg = achievements_ui::create_achievements_message(db, uid, 0);
            bronx::send_message(bot, event, msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            
            // Claim any eligible achievements based on current stats (retroactive)
            achievements::claim_eligible_achievements(db, uid);
            
            auto msg = achievements_ui::create_achievements_message(db, uid, 0);
            event.reply(msg);
        });
    return ach;
}

// Register achievements button interactions
inline void register_achievements_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        
        // Check if this is an achievements navigation button
        if (custom_id.rfind("ach_prev_", 0) != 0 && custom_id.rfind("ach_next_", 0) != 0) {
            return;
        }
        
        // Parse: ach_[prev|next]_<category>_<user_id>
        bool is_prev = (custom_id.rfind("ach_prev_", 0) == 0);
        std::string rest = custom_id.substr(is_prev ? 9 : 9); // "ach_prev_" or "ach_next_"
        
        size_t sep = rest.find('_');
        if (sep == std::string::npos) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message().add_embed(bronx::error("invalid button")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        int category = std::stoi(rest.substr(0, sep));
        uint64_t owner_id = std::stoull(rest.substr(sep + 1));
        
        // Check if the clicker is the owner
        if (event.command.get_issuing_user().id != owner_id) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this isn't your achievements view")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Calculate new category
        int new_category = is_prev ? category - 1 : category + 1;
        int total = achievements_ui::CATEGORY_NAMES.size();
        if (new_category < 0) new_category = 0;
        if (new_category >= total) new_category = total - 1;
        
        // Update the message
        auto msg = achievements_ui::create_achievements_message(db, owner_id, new_category);
        event.reply(dpp::ir_update_message, msg);
    });
}

} // namespace commands
