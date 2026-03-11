#pragma once
#include <dpp/dpp.h>
#include <vector>
#include <map>
#include "../milestones.h"
#include "../achievements.h"
#include "../global_boss.h"
#include "../pets/pets.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include "jackpot.h"

namespace commands {
namespace gambling {

using namespace bronx::db::server_economy_operations;

// Maximum bet limit (2 billion)
constexpr int64_t MAX_BET = 2000000000LL;

// Track gambling result and check for milestones, achievements, and jackpot.
// Call this after every gambling game completion.
// profit: net winnings (positive = won, negative/0 = lost). Used for global boss + jackpot tracking.
// Returns the jackpot amount won (0 if none). Caller should display jackpot embed if > 0.
inline int64_t track_gambling_result(dpp::cluster& bot, bronx::db::Database* db, 
                                  const dpp::snowflake& channel_id, uint64_t user_id, bool won,
                                  int64_t profit = 0) {
    int64_t jackpot_won = 0;

    // Always track games played
    milestones::track_milestone(bot, db, channel_id, user_id, 
                               milestones::MilestoneType::GAMES_PLAYED, 1);
    
    // Track wins separately
    if (won) {
        milestones::track_milestone(bot, db, channel_id, user_id, 
                                   milestones::MilestoneType::GAMBLING_WINS, 1);
        // Check for gambling wins achievements
        achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "gambling_wins");

        // Progressive Jackpot: check for jackpot win on every gambling win
        jackpot_won = jackpot_on_win(db, user_id);
        if (jackpot_won > 0) {
            // Credit the jackpot to the user's wallet
            db->update_wallet(user_id, jackpot_won);
            // Send the jackpot announcement
            auto jp_embed = build_jackpot_win_embed(user_id, jackpot_won);
            bot.message_create(dpp::message(channel_id, jp_embed));
        }
    } else {
        // Progressive Jackpot: feed losses into the pool
        if (profit < 0) {
            jackpot_on_loss(db, -profit);
        }
    }

    // Global boss: every game = 1 gamble command, profit only if positive
    global_boss::on_gamble_command(db, user_id, profit);
    ::commands::pets::pet_hooks::on_gamble(db, user_id);

    // Track daily challenge stats for gambling
    if (won) {
        ::commands::daily_challenges::track_daily_stat(db, user_id, "gambling_wins_today", 1);
    }
    if (profit > 0) {
        ::commands::daily_challenges::track_daily_stat(db, user_id, "gambling_profit_today", profit);
        ::commands::daily_challenges::track_daily_stat(db, user_id, "coins_earned_today", profit);
    }

    return jackpot_won;
}

// Track gambling profit and check for achievements
// Call this after recording profit from gambling
inline void track_gambling_profit(dpp::cluster& bot, bronx::db::Database* db,
                                  const dpp::snowflake& channel_id, uint64_t user_id) {
    // Check gambling profit achievements (stat already incremented elsewhere)
    achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "gambling_profit");
}

// Check if gambling is allowed in the current server economy
// Returns true if gambling is allowed (always true for global economy)
inline bool is_gambling_allowed(bronx::db::Database* db, std::optional<uint64_t> guild_id) {
    if (!guild_id || !is_server_economy(db, *guild_id)) return true;
    auto settings = get_guild_economy_settings(db, *guild_id);
    return settings ? settings->allow_gambling : true;
}

// Get gambling multiplier for the server (1.0 for global economy)
inline double get_gambling_multiplier(bronx::db::Database* db, std::optional<uint64_t> guild_id) {
    if (!guild_id || !is_server_economy(db, *guild_id)) return 1.0;
    auto settings = get_guild_economy_settings(db, *guild_id);
    return settings ? settings->gambling_multiplier : 1.0;
}

// Get user's wallet balance using unified operations
inline int64_t get_gambling_wallet(bronx::db::Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    return get_wallet_unified(db, user_id, guild_id);
}

// Update user's wallet using unified operations
inline bool update_gambling_wallet(bronx::db::Database* db, uint64_t user_id, std::optional<uint64_t> guild_id, int64_t amount) {
    return update_wallet_unified(db, user_id, guild_id, amount).has_value();
}

// Frogger game state
struct FroggerGame {
    uint64_t user_id;
    uint64_t message_id;
    int64_t initial_bet;
    int logs_hopped;
    int difficulty; // 1=easy, 2=medium, 3=hard
    int frog_lane; // 0, 1, or 2
    ::std::vector<::std::vector<bool>> upcoming_logs; // [log_row][lane] - true = safe, false = cracked
    bool active;
};

static ::std::map<uint64_t, FroggerGame> active_frogger_games;

} // namespace gambling
} // namespace commands

