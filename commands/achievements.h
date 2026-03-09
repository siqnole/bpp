#pragma once
#include "../database/core/database.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <cstdint>

using namespace bronx::db;

namespace commands {
namespace achievements {

// Achievement reward structure - can award items (rods, bait, etc.)
struct AchievementReward {
    std::string item_id;      // e.g., "rod_diamond", "bait_legendary"
    std::string item_name;    // e.g., "Diamond Rod", "Legendary Bait"
    std::string item_type;    // "rod" or "bait"
    int quantity;             // how many to give
    int level;                // item level
    std::string metadata;     // item metadata (JSON)
};

// Achievement definition
struct Achievement {
    std::string id;                          // unique identifier
    std::string name;                        // display name
    std::string description;                 // what the achievement is for
    std::string stat_name;                   // which stat to check
    int64_t threshold;                       // value needed to unlock
    std::vector<AchievementReward> rewards;  // items awarded
};

// ============================================================================
// ACHIEVEMENT DEFINITIONS
// ============================================================================

// Fish Value Achievements (cumulative fish sold value)
static const std::vector<Achievement> FISH_VALUE_ACHIEVEMENTS = {
    {
        "fish_value_1m", "Fish Seller", "Sell 1 million coins worth of fish",
        "fish_value", 1000000,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}}
    },
    {
        "fish_value_10m", "Fish Merchant", "Sell 10 million coins worth of fish",
        "fish_value", 10000000,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_rare", "Rare Bait", "bait", 20, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "fish_value_100m", "Fish Tycoon", "Sell 100 million coins worth of fish",
        "fish_value", 100000000,
        {{"rod_diamond", "Diamond Rod", "rod", 1, 5, R"({"luck":50,"capacity":10})"}, {"bait_epic", "Epic Bait", "bait", 30, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    },
    {
        "fish_value_1b", "Fish Billionaire", "Sell 1 billion coins worth of fish",
        "fish_value", 1000000000,
        {{"rod_diamond", "Diamond Rod", "rod", 1, 5, R"({"luck":50,"capacity":10})"}, {"bait_legendary", "Legendary Bait", "bait", 40, 5, R"({"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40})"}}
    },
    {
        "fish_value_1t", "Fish Trillionaire", "Sell 1 trillion coins worth of fish",
        "fish_value", 1000000000000,
        {{"rod_prestige5", "Divine Rod", "rod", 1, 11, R"({"luck":150,"capacity":25,"prestige":5})"}, {"bait_prestige5", "Divine Bait", "bait", 40, 11, R"({"unlocks":["eternal fish"],"bonus":300,"multiplier":1000,"prestige":5})"}}
    }
};

// Fish Caught Achievements (total fish caught)
static const std::vector<Achievement> FISH_CAUGHT_ACHIEVEMENTS = {
    {
        "fish_caught_100", "Novice Angler", "Catch 100 fish",
        "fish_caught", 100,
        {{"bait_common", "Common Bait", "bait", 10, 1, R"({"unlocks":["common fish"],"bonus":2,"multiplier":2})"}}
    },
    {
        "fish_caught_1000", "Skilled Fisher", "Catch 1,000 fish",
        "fish_caught", 1000,
        {{"rod_bronze", "Bronze Rod", "rod", 1, 2, R"({"luck":10,"capacity":3})"}, {"bait_uncommon", "Uncommon Bait", "bait", 15, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "fish_caught_10k", "Expert Angler", "Catch 10,000 fish",
        "fish_caught", 10000,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_rare", "Rare Bait", "bait", 25, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "fish_caught_100k", "Master Angler", "Catch 100,000 fish",
        "fish_caught", 100000,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_epic", "Epic Bait", "bait", 35, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    },
    {
        "fish_caught_1m", "Legendary Angler", "Catch 1,000,000 fish",
        "fish_caught", 1000000,
        {{"rod_prestige3", "Ethereal Rod", "rod", 1, 9, R"({"luck":100,"capacity":18,"prestige":3})"}, {"bait_prestige3", "Ethereal Bait", "bait", 50, 9, R"({"unlocks":["nebula fish"],"bonus":200,"multiplier":600,"prestige":3})"}}
    }
};

// Gambling Wins Achievements
static const std::vector<Achievement> GAMBLING_ACHIEVEMENTS = {
    {
        "gambling_wins_50", "Lucky Beginner", "Win 50 gambling games",
        "gambling_wins", 50,
        {{"bait_uncommon", "Uncommon Bait", "bait", 10, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "gambling_wins_500", "Fortune Seeker", "Win 500 gambling games",
        "gambling_wins", 500,
        {{"rod_bronze", "Bronze Rod", "rod", 1, 2, R"({"luck":10,"capacity":3})"}, {"bait_rare", "Rare Bait", "bait", 15, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "gambling_wins_5000", "High Roller", "Win 5,000 gambling games",
        "gambling_wins", 5000,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_epic", "Epic Bait", "bait", 25, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    },
    {
        "gambling_wins_50k", "Casino Master", "Win 50,000 gambling games",
        "gambling_wins", 50000,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_legendary", "Legendary Bait", "bait", 30, 5, R"({"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40})"}}
    }
};

// Games Won Achievements (separate from gambling - minigames etc.)
static const std::vector<Achievement> GAMES_WON_ACHIEVEMENTS = {
    {
        "games_won_25", "Game Dabbler", "Win 25 games",
        "games_won", 25,
        {{"bait_common", "Common Bait", "bait", 15, 1, R"({"unlocks":["common fish"],"bonus":2,"multiplier":2})"}}
    },
    {
        "games_won_250", "Game Enthusiast", "Win 250 games",
        "games_won", 250,
        {{"rod_bronze", "Bronze Rod", "rod", 1, 2, R"({"luck":10,"capacity":3})"}, {"bait_uncommon", "Uncommon Bait", "bait", 20, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "games_won_2500", "Game Champion", "Win 2,500 games",
        "games_won", 2500,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_rare", "Rare Bait", "bait", 30, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "games_won_25k", "Game Legend", "Win 25,000 games",
        "games_won", 25000,
        {{"rod_diamond", "Diamond Rod", "rod", 1, 5, R"({"luck":50,"capacity":10})"}, {"bait_epic", "Epic Bait", "bait", 40, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    }
};

// Commands Used Achievements (bot engagement)
static const std::vector<Achievement> COMMANDS_USED_ACHIEVEMENTS = {
    {
        "commands_1000", "Active User", "Use 1,000 commands",
        "commands_used", 1000,
        {{"bait_common", "Common Bait", "bait", 20, 1, R"({"unlocks":["common fish"],"bonus":2,"multiplier":2})"}}
    },
    {
        "commands_10k", "Dedicated User", "Use 10,000 commands",
        "commands_used", 10000,
        {{"rod_bronze", "Bronze Rod", "rod", 1, 2, R"({"luck":10,"capacity":3})"}, {"bait_uncommon", "Uncommon Bait", "bait", 25, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "commands_100k", "Power User", "Use 100,000 commands",
        "commands_used", 100000,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_rare", "Rare Bait", "bait", 35, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "commands_1m", "Bot Devotee", "Use 1,000,000 commands",
        "commands_used", 1000000,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_epic", "Epic Bait", "bait", 50, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    }
};

// Gambling Profit Achievements (total coins won from gambling)
static const std::vector<Achievement> GAMBLING_PROFIT_ACHIEVEMENTS = {
    {
        "gambling_profit_100k", "Lucky Winner", "Profit 100,000 coins from gambling",
        "gambling_profit", 100000,
        {{"bait_uncommon", "Uncommon Bait", "bait", 15, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "gambling_profit_1m", "Big Winner", "Profit 1 million coins from gambling",
        "gambling_profit", 1000000,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_rare", "Rare Bait", "bait", 20, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "gambling_profit_100m", "Jackpot Master", "Profit 100 million coins from gambling",
        "gambling_profit", 100000000,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_epic", "Epic Bait", "bait", 30, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    },
    {
        "gambling_profit_1b", "Casino Mogul", "Profit 1 billion coins from gambling",
        "gambling_profit", 1000000000,
        {{"rod_diamond", "Diamond Rod", "rod", 1, 5, R"({"luck":50,"capacity":10})"}, {"bait_legendary", "Legendary Bait", "bait", 40, 5, R"({"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40})"}}
    }
};

// Fishdex Collection Achievements (% of all fish caught)
static const std::vector<Achievement> FISHDEX_ACHIEVEMENTS = {
    {
        "fishdex_10", "Fish Enthusiast", "Catch 10% of all fish species",
        "fishdex_10", 1,
        {{"bait_uncommon", "Uncommon Bait", "bait", 25, 2, R"({"unlocks":["uncommon fish"],"bonus":5,"multiplier":5})"}}
    },
    {
        "fishdex_25", "Fish Researcher", "Catch 25% of all fish species",
        "fishdex_25", 1,
        {{"rod_silver", "Silver Rod", "rod", 1, 3, R"({"luck":20,"capacity":5})"}, {"bait_rare", "Rare Bait", "bait", 30, 3, R"({"unlocks":["rare fish"],"bonus":8,"multiplier":10})"}}
    },
    {
        "fishdex_50", "Fish Expert", "Catch 50% of all fish species",
        "fishdex_50", 1,
        {{"rod_gold", "Gold Rod", "rod", 1, 4, R"({"luck":35,"capacity":7})"}, {"bait_epic", "Epic Bait", "bait", 40, 4, R"({"unlocks":["epic fish"],"bonus":12,"multiplier":20})"}}
    },
    {
        "fishdex_75", "Fish Master", "Catch 75% of all fish species",
        "fishdex_75", 1,
        {{"rod_diamond", "Diamond Rod", "rod", 1, 5, R"({"luck":50,"capacity":10})"}, {"bait_legendary", "Legendary Bait", "bait", 50, 5, R"({"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40})"}}
    }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Format large numbers nicely
inline std::string format_number(int64_t num) {
    if (num >= 1000000000000) {
        return std::to_string(num / 1000000000000) + "T";
    } else if (num >= 1000000000) {
        return std::to_string(num / 1000000000) + "B";
    } else if (num >= 1000000) {
        return std::to_string(num / 1000000) + "M";
    } else if (num >= 1000) {
        double val = num / 1000.0;
        if (num % 1000 == 0) {
            return std::to_string(num / 1000) + "k";
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fk", val);
        return buf;
    }
    return std::to_string(num);
}

// Format number with commas
inline std::string format_with_commas(int64_t num) {
    std::string s = std::to_string(num);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

// Check if user has already claimed an achievement
inline bool has_achievement(Database* db, uint64_t user_id, const std::string& achievement_id) {
    return db->has_item(user_id, "achievement_" + achievement_id, 1);
}

// Mark achievement as claimed
inline bool claim_achievement(Database* db, uint64_t user_id, const std::string& achievement_id) {
    return db->add_item(user_id, "achievement_" + achievement_id, "achievement", 1, "{}", 1);
}

// Award achievement rewards to user
inline void award_rewards(Database* db, uint64_t user_id, const std::vector<AchievementReward>& rewards) {
    for (const auto& reward : rewards) {
        db->add_item(user_id, reward.item_id, reward.item_type, reward.quantity, reward.metadata, reward.level);
    }
}

// Create achievement celebration embed
inline dpp::embed create_achievement_embed(uint64_t user_id, const Achievement& achievement) {
    std::string title = "achievement unlocked!";
    std::string desc = "<@" + std::to_string(user_id) + "> unlocked **" + achievement.name + "**!\n\n";
    desc += "*" + achievement.description + "*\n\n";
    
    desc += "**rewards:**\n";
    for (const auto& reward : achievement.rewards) {
        desc += "• " + std::to_string(reward.quantity) + "x **" + reward.item_name + "**\n";
    }
    
    dpp::embed embed;
    embed.set_title(title);
    embed.set_description(desc);
    embed.set_color(0x9B59B6); // Purple color for achievements
    embed.set_thumbnail("https://media.giphy.com/media/QMkPpxPDYY0fu/giphy.gif"); // trophy gif
    
    return embed;
}

// Send achievement celebration message (non-blocking)
inline void send_achievement_celebration(dpp::cluster& bot, const dpp::snowflake& channel_id,
                                         uint64_t user_id, const Achievement& achievement) {
    auto embed = create_achievement_embed(user_id, achievement);
    dpp::message msg;
    msg.set_channel_id(channel_id);
    msg.add_embed(embed);
    bot.message_create(msg, [channel_id, user_id](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            auto err = cb.get_error();
            std::cerr << "[achievements] failed to send achievement celebration in channel " << channel_id
                      << " for user " << user_id << ": " << err.code << " - " << err.message << "\n";
        }
    });
}

// Check all achievements in a category for a given stat value
inline std::vector<const Achievement*> check_achievements(
    const std::vector<Achievement>& achievements,
    Database* db,
    uint64_t user_id,
    int64_t current_value
) {
    std::vector<const Achievement*> unlocked;
    for (const auto& ach : achievements) {
        if (current_value >= ach.threshold && !has_achievement(db, user_id, ach.id)) {
            unlocked.push_back(&ach);
        }
    }
    return unlocked;
}

// Main function: check and award achievements for a specific stat
// Returns true if any achievements were unlocked
inline bool process_achievements_for_stat(
    dpp::cluster& bot,
    Database* db,
    const dpp::snowflake& channel_id,
    uint64_t user_id,
    const std::string& stat_name,
    int64_t current_value
) {
    std::vector<const std::vector<Achievement>*> categories;
    
    // Map stat name to achievement category
    if (stat_name == "fish_value" || stat_name == "fish_profit") {
        categories.push_back(&FISH_VALUE_ACHIEVEMENTS);
    } else if (stat_name == "fish_caught") {
        categories.push_back(&FISH_CAUGHT_ACHIEVEMENTS);
    } else if (stat_name == "gambling_wins") {
        categories.push_back(&GAMBLING_ACHIEVEMENTS);
    } else if (stat_name == "games_won") {
        categories.push_back(&GAMES_WON_ACHIEVEMENTS);
    } else if (stat_name == "commands_used") {
        categories.push_back(&COMMANDS_USED_ACHIEVEMENTS);
    } else if (stat_name == "gambling_profit") {
        categories.push_back(&GAMBLING_PROFIT_ACHIEVEMENTS);
    } else if (stat_name == "fishdex_10" || stat_name == "fishdex_25" || 
               stat_name == "fishdex_50" || stat_name == "fishdex_75") {
        categories.push_back(&FISHDEX_ACHIEVEMENTS);
    }
    
    bool any_unlocked = false;
    
    for (const auto* cat : categories) {
        auto unlocked = check_achievements(*cat, db, user_id, current_value);
        for (const auto* ach : unlocked) {
            // Claim achievement
            claim_achievement(db, user_id, ach->id);
            // Award items
            award_rewards(db, user_id, ach->rewards);
            // Send celebration
            send_achievement_celebration(bot, channel_id, user_id, *ach);
            any_unlocked = true;
        }
    }
    
    return any_unlocked;
}

// Convenience function: check achievements after incrementing a stat
// This reads the current stat value and checks for unlocks
inline bool check_achievements_for_stat(
    dpp::cluster& bot,
    Database* db,
    const dpp::snowflake& channel_id,
    uint64_t user_id,
    const std::string& stat_name
) {
    int64_t current_value = db->get_stat(user_id, stat_name);
    return process_achievements_for_stat(bot, db, channel_id, user_id, stat_name, current_value);
}

// Track achievement: increment stat and check for achievement unlock
// This is the main entry point for tracking achievements
inline void track_achievement(
    dpp::cluster& bot,
    Database* db,
    const dpp::snowflake& channel_id,
    uint64_t user_id,
    const std::string& stat_name,
    int64_t amount = 1
) {
    // Increment stat
    db->increment_stat(user_id, stat_name, amount);
    // Get new value and check achievements
    int64_t new_value = db->get_stat(user_id, stat_name);
    process_achievements_for_stat(bot, db, channel_id, user_id, stat_name, new_value);
}

// Get all achievements for display (achievements command)
inline dpp::embed get_achievements_list_embed(Database* db, uint64_t user_id) {
    dpp::embed embed;
    embed.set_title("achievements");
    embed.set_color(0x9B59B6);
    
    auto format_category = [&](const std::string& title, const std::vector<Achievement>& achievements) {
        std::string content;
        int64_t stat_value = 0;
        if (!achievements.empty()) {
            stat_value = db->get_stat(user_id, achievements[0].stat_name);
        }
        
        for (const auto& ach : achievements) {
            bool unlocked = has_achievement(db, user_id, ach.id);
            std::string status = unlocked ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY;
            content += status + " **" + ach.name + "** - " + ach.description;
            if (!unlocked) {
                content += " (" + format_with_commas(stat_value) + "/" + format_with_commas(ach.threshold) + ")";
            }
            content += "\n";
        }
        if (!content.empty()) {
            embed.add_field(title, content, false);
        }
    };
    
    format_category("🐟 Fish Value", FISH_VALUE_ACHIEVEMENTS);
    format_category("🎣 Fish Caught", FISH_CAUGHT_ACHIEVEMENTS);
    format_category("📖 Fishdex Collection", FISHDEX_ACHIEVEMENTS);
    format_category("🎰 Gambling Wins", GAMBLING_ACHIEVEMENTS);
    format_category("💰 Gambling Profit", GAMBLING_PROFIT_ACHIEVEMENTS);
    format_category("🎮 Games Won", GAMES_WON_ACHIEVEMENTS);
    format_category("💻 Commands Used", COMMANDS_USED_ACHIEVEMENTS);
    
    return embed;
}

// Count total unlocked achievements
inline int count_unlocked_achievements(Database* db, uint64_t user_id) {
    int count = 0;
    auto check_category = [&](const std::vector<Achievement>& achievements) {
        for (const auto& ach : achievements) {
            if (has_achievement(db, user_id, ach.id)) {
                count++;
            }
        }
    };
    
    check_category(FISH_VALUE_ACHIEVEMENTS);
    check_category(FISH_CAUGHT_ACHIEVEMENTS);
    check_category(FISHDEX_ACHIEVEMENTS);
    check_category(GAMBLING_ACHIEVEMENTS);
    check_category(GAMBLING_PROFIT_ACHIEVEMENTS);
    check_category(GAMES_WON_ACHIEVEMENTS);
    check_category(COMMANDS_USED_ACHIEVEMENTS);
    
    return count;
}

// Get total number of achievements
inline int get_total_achievements() {
    return FISH_VALUE_ACHIEVEMENTS.size() + 
           FISH_CAUGHT_ACHIEVEMENTS.size() + 
           FISHDEX_ACHIEVEMENTS.size() +
           GAMBLING_ACHIEVEMENTS.size() + 
           GAMBLING_PROFIT_ACHIEVEMENTS.size() +
           GAMES_WON_ACHIEVEMENTS.size() + 
           COMMANDS_USED_ACHIEVEMENTS.size();
}

// Claim all eligible achievements based on current stats (silent - no messages)
// Returns the number of newly claimed achievements
inline int claim_eligible_achievements(Database* db, uint64_t user_id) {
    int claimed = 0;
    
    auto check_and_claim = [&](const std::vector<Achievement>& achievements) {
        if (achievements.empty()) return;
        int64_t stat_value = db->get_stat(user_id, achievements[0].stat_name);
        
        for (const auto& ach : achievements) {
            if (stat_value >= ach.threshold && !has_achievement(db, user_id, ach.id)) {
                claim_achievement(db, user_id, ach.id);
                award_rewards(db, user_id, ach.rewards);
                claimed++;
            }
        }
    };
    
    check_and_claim(FISH_VALUE_ACHIEVEMENTS);
    check_and_claim(FISH_CAUGHT_ACHIEVEMENTS);
    check_and_claim(FISHDEX_ACHIEVEMENTS);
    check_and_claim(GAMBLING_ACHIEVEMENTS);
    check_and_claim(GAMBLING_PROFIT_ACHIEVEMENTS);
    check_and_claim(GAMES_WON_ACHIEVEMENTS);
    check_and_claim(COMMANDS_USED_ACHIEVEMENTS);
    
    return claimed;
}

} // namespace achievements
} // namespace commands
