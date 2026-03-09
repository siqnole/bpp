#pragma once
#include "../database/core/database.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <cstdint>

using namespace bronx::db;

namespace commands {
namespace milestones {

// Milestone thresholds for various achievements
static const std::vector<int64_t> MILESTONE_THRESHOLDS = {
    100, 500, 1000, 2500, 5000, 10000, 15000, 25000, 50000, 100000
};

// Economic rewards for each milestone threshold (coins)
static const std::vector<int64_t> MILESTONE_REWARDS = {
    5000,      // 100 milestone -> 5000 coins
    25000,     // 500 milestone -> 25000 coins
    75000,     // 1000 milestone -> 75000 coins
    150000,    // 2500 milestone -> 150000 coins
    350000,    // 5000 milestone -> 350000 coins
    750000,    // 10000 milestone -> 750000 coins
    1250000,   // 15000 milestone -> 1250000 coins
    2000000,   // 25000 milestone -> 2000000 coins
    5000000,   // 50000 milestone -> 5000000 coins
    10000000   // 100000 milestone -> 10000000 coins
};

// Get the reward amount for a milestone threshold
inline int64_t get_milestone_reward(int64_t threshold) {
    for (size_t i = 0; i < MILESTONE_THRESHOLDS.size(); i++) {
        if (MILESTONE_THRESHOLDS[i] == threshold) {
            return MILESTONE_REWARDS[i];
        }
    }
    return 0;
}

// Different milestone categories
enum class MilestoneType {
    FISH_CAUGHT,
    GAMES_PLAYED,
    GAMES_WON,
    GAMBLING_WINS,
    COMMANDS_USED
};

// Get the stat name for a milestone type
inline std::string get_stat_name(MilestoneType type) {
    switch (type) {
        case MilestoneType::FISH_CAUGHT: return "fish_caught";
        case MilestoneType::GAMES_PLAYED: return "games_played";
        case MilestoneType::GAMES_WON: return "games_won";
        case MilestoneType::GAMBLING_WINS: return "gambling_wins";
        case MilestoneType::COMMANDS_USED: return "commands_used";
        default: return "";
    }
}

// Get display name for a milestone type
inline std::string get_display_name(MilestoneType type) {
    switch (type) {
        case MilestoneType::FISH_CAUGHT: return "fish caught";
        case MilestoneType::GAMES_PLAYED: return "games played";
        case MilestoneType::GAMES_WON: return "games won";
        case MilestoneType::GAMBLING_WINS: return "gambling wins";
        case MilestoneType::COMMANDS_USED: return "commands used";
        default: return "unknown";
    }
}

// Get emoji for a milestone type (empty - emojis disabled)
inline std::string get_emoji(MilestoneType type) {
    (void)type; // unused
    return "";
}

// Format large numbers nicely
inline std::string format_milestone(int64_t num) {
    if (num >= 1000000) {
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

// Check if incrementing a stat crosses a milestone threshold
// Returns the milestone value if crossed, 0 otherwise
inline int64_t check_milestone_crossed(int64_t old_value, int64_t new_value) {
    for (auto threshold : MILESTONE_THRESHOLDS) {
        if (old_value < threshold && new_value >= threshold) {
            return threshold;
        }
    }
    return 0;
}

// Increment a stat and check for milestone, returns milestone value if crossed
inline int64_t increment_stat_with_milestone(Database* db, uint64_t user_id, 
                                             const std::string& stat_name, int64_t amount = 1) {
    int64_t old_value = db->get_stat(user_id, stat_name);
    db->increment_stat(user_id, stat_name, amount);
    int64_t new_value = old_value + amount;
    return check_milestone_crossed(old_value, new_value);
}

// Format coins with commas
inline std::string format_coins(int64_t coins) {
    std::string s = std::to_string(coins);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

// Create a celebration embed for a milestone
inline dpp::embed create_milestone_embed(uint64_t user_id, MilestoneType type, int64_t milestone_value, int64_t reward = 0) {
    std::string display = get_display_name(type);
    std::string formatted = format_milestone(milestone_value);
    
    std::string title = "milestone reached!";
    std::string desc = "<@" + std::to_string(user_id) + "> has reached **" + formatted + " " + display + "**!\n\n";
    
    // Add tier-specific messages
    if (milestone_value >= 100000) {
        desc += "***legendary status!*** you are among the most dedicated users!\n";
        desc += "you've earned the **" + display + " legend** title!";
    } else if (milestone_value >= 50000) {
        desc += "**master tier!** your dedication is unmatched!\n";
        desc += "you've earned the **" + display + " master** title!";
    } else if (milestone_value >= 25000) {
        desc += "**expert tier!** you're becoming a true veteran!\n";
        desc += "keep going for even greater rewards!";
    } else if (milestone_value >= 10000) {
        desc += "*veteran tier!* your commitment is impressive!\n";
        desc += "the bot salutes your dedication!";
    } else if (milestone_value >= 5000) {
        desc += "*experienced!* you're really getting into it!\n";
        desc += "keep pushing for greater milestones!";
    } else if (milestone_value >= 1000) {
        desc += "__seasoned!__ you've come a long way!\n";
        desc += "more milestones await!";
    } else {
        desc += "congratulations on this achievement!\n";
        desc += "keep going for bigger milestones!";
    }
    
    // Add reward information
    if (reward > 0) {
        desc += "\n\n**reward:** " + format_coins(reward) + " coins added to your wallet!";
    }
    
    dpp::embed embed;
    embed.set_title(title);
    embed.set_description(desc);
    embed.set_color(0xFFD700); // Gold color
    embed.set_thumbnail("https://media.giphy.com/media/g9582DNuQppxC/giphy.gif"); // celebration gif
    
    return embed;
}

// Send a milestone celebration message (non-blocking)
inline void send_milestone_celebration(dpp::cluster& bot, const dpp::snowflake& channel_id,
                                       uint64_t user_id, MilestoneType type, int64_t milestone_value, int64_t reward = 0) {
    auto embed = create_milestone_embed(user_id, type, milestone_value, reward);
    dpp::message msg;
    msg.set_channel_id(channel_id);
    msg.add_embed(embed);
    bot.message_create(msg, [channel_id, user_id](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            auto err = cb.get_error();
            std::cerr << "[milestones] failed to send milestone celebration in channel " << channel_id
                      << " for user " << user_id << ": " << err.code << " - " << err.message << "\n";
        }
    });
}

// Helper that increments stat, checks milestone, awards coins, and sends celebration if needed
inline void track_milestone(dpp::cluster& bot, Database* db, const dpp::snowflake& channel_id,
                           uint64_t user_id, MilestoneType type, int64_t amount = 1) {
    std::string stat_name = get_stat_name(type);
    int64_t milestone = increment_stat_with_milestone(db, user_id, stat_name, amount);
    if (milestone > 0) {
        // Award economic reward for reaching the milestone
        int64_t reward = get_milestone_reward(milestone);
        if (reward > 0) {
            db->update_wallet(user_id, reward);
        }
        send_milestone_celebration(bot, channel_id, user_id, type, milestone, reward);
    }
}

} // namespace milestones
} // namespace commands
