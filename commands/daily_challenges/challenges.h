#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy/helpers.h"
#include "daily_stat_tracker.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <random>
#include <mutex>
#include <map>
#include <ctime>
#include <numeric>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace daily_challenges {

using ChallengeType = Database::ChallengeType;
using ActiveChallenge = Database::ActiveChallenge;
using ChallengeReward = Database::ChallengeReward;
using ChallengeProgress = Database::ChallengeProgress;

// --- Challenge template definitions ---
struct ChallengeTemplate {
    std::string id;
    std::string name;
    std::string description;    // use {target} for the required amount
    std::string stat_name;      // primary stat to track
    std::string category;       // fishing, mining, gambling, economy, social
    ChallengeType type;         // challenge type determines validation logic
    int64_t min_target;         // minimum target value
    int64_t max_target;         // maximum target value
    std::string emoji;
    
    // Extended fields for nuanced challenges (with defaults)
    std::vector<std::string> required_stats = {};    // for HYBRID: additional stats needed
    int64_t streak_length = 0;                       // for STREAK: how many consecutive
    int64_t variety_count = 0;                       // for VARIETY: how many different types
    int64_t time_limit_minutes = 0;                  // for EFFICIENCY: max time allowed
    double ratio_threshold = 0.0;                    // for RATIO: win% or profit_ratio needed
    int64_t precision_margin = 0;                    // for PRECISE: allowed variance
    std::string prerequisite_challenge_id = "";      // for CONDITIONAL: must complete first
};

// All available challenge templates

//TODO: im tired of all the challenges being do x y times, add more challenges that are more nuanced.

/* 
CHALLENGE IDEAS:

=== EFFICIENCY & SKILL ===
- "Streak Master": Win X consecutive matches in a specific game (coinflip, blackjack, slots) without losing
- "Perfect Fisher": Catch X rare+ fish in a single session (requires consecutive catches)
- "Hot Streak": Earn profit on X consecutive gambling attempts
- "Precision Miner": Mine X ores in Y minutes (time pressure challenge)
- "Consistent Grinder": Work X times with success rate >80% (requires consistent effort)

=== COLLECTION & VARIETY ===
- "Ichthyologist": Catch fish from X different rarity tiers in one day
- "Ore Collector": Mine X different ore types (limestone, iron, gold, diamond, etc.)
- "Item Hoarder": Obtain X different item types from any activity
- "Pet Collector": Obtain X different pet types or breeds
- "Title Hunter": Unlock X new titles (requires advancing achievements/milestones)

=== ECONOMY & STRATEGY ===
- "Market Trader": Buy low, sell high - profit $X from flipping items on market
- "Portfolio Master": Hold a balanced portfolio (X% coins, X% items, X% investments)
- "Wealth Accumulator": Increase total networth by $X (scales with current networth)
- "Generous Soul": Pay or gift $X to other players
- "Tax Evader": Earn $X without using /work command (from passive/other sources)

=== PROGRESSION & GROWTH ===
- "Leveler": Gain X levels in any skill tree
- "Milestone Jumper": Complete X different milestones across all categories
- "Achievement Sprint": Unlock X achievements in one day
- "Momentum": Maintain a X-day activity streak (must use commands daily)

=== PROBABILISTIC & LUCK ===
- "Jackpot Seeker": Hit a rare drop/jackpot event (requires luck, specific outcome)
- "Underdog": Win a bet at >5:1 odds, or win a low-probability event X times
- "Lucky Angler": Catch a legendary/mythic fish
- "Rare Find": Get X rare+ drops from any source

=== HYBRID & COMPLEX ===
- "Diversified": Complete activities from X different categories (fishing AND mining AND gambling, etc)
- "Wealth Wave": Earn $X from one activity type, then spend $Y from another (resource cycling)
- "Speed Run": Earn $X in under Y minutes (tight time window)
- "Comeback Kid": Lose X times in gambling, then win Y times (requires persistence)
- "Balanced Growth": Gain XP and coins simultaneously, maintaining a 1:1 ratio
- "Challenge Chain": Complete X other challenges first, then complete this one

=== WORLD & SOCIAL ===
- "Global Participant": Participate in world events X times
- "Boss Raider": Deal X damage to global boss
- "Community Helper": Use /help or /guide X times (support others)
- "Social Butterfly": Interact with X different players through commands
- "Rivalry": Beat a player's leaderboard score in a specific category

=== CONDITIONAL & CONTEXTUAL ===
- "Against Odds": Complete a challenge with target >75% of normal range (very hard version)
- "Precise": Earn exactly $X (within Y margin) in a single action
- "Timing": Complete activity during off-peak hours (specific time window)
- "Ascetic": Earn $X without using bonuses/multipliers
- "Versatile": Use X different commands from the commands list (versatility)

*/

static const std::vector<ChallengeTemplate> CHALLENGE_TEMPLATES = {
    // ============ SIMPLE CHALLENGES (do X times) ============
    // Fishing
    {.id="fish_catch",      .name="Reel 'Em In",          .description="catch {target} fish",                .stat_name="fish_caught",           .category="fishing", .type=ChallengeType::SIMPLE, .min_target=5,   .max_target=50,  .emoji="\xF0\x9F\x8E\xA3"},
    {.id="fish_sell_value", .name="Fish Market",          .description="sell ${target} worth of fish",       .stat_name="fish_sell_value_today", .category="fishing", .type=ChallengeType::SIMPLE, .min_target=5000, .max_target=500000, .emoji="\xF0\x9F\x92\xB0"},
    {.id="fish_rare",       .name="Rare Catch",           .description="catch {target} rare+ fish",          .stat_name="rare_fish_caught",      .category="fishing", .type=ChallengeType::SIMPLE, .min_target=1,   .max_target=10,  .emoji="\xE2\x9C\xA8"},
    
    // Mining
    {.id="mine_ores",       .name="Ore Collector",        .description="mine {target} ores",                 .stat_name="ores_mined",            .category="mining", .type=ChallengeType::SIMPLE, .min_target=5,   .max_target=40,  .emoji="\xE2\x9B\x8F\xEF\xB8\x8F"},
    {.id="mine_value",      .name="Prospector's Haul",    .description="mine ${target} worth of ore",        .stat_name="ore_value_today",       .category="mining", .type=ChallengeType::SIMPLE, .min_target=5000, .max_target=250000, .emoji="\xF0\x9F\x92\x8E"},
    
    // Gambling
    {.id="gamble_wins",     .name="Lucky Streak",         .description="win {target} gambles",               .stat_name="gambling_wins_today",   .category="gambling", .type=ChallengeType::SIMPLE, .min_target=3,   .max_target=20,  .emoji="\xF0\x9F\x8E\xB0"},
    {.id="gamble_profit",   .name="High Roller",          .description="profit ${target} from gambling",     .stat_name="gambling_profit_today", .category="gambling", .type=ChallengeType::SIMPLE, .min_target=10000, .max_target=1000000, .emoji="\xF0\x9F\x92\xB8"},
    {.id="coinflip_wins",   .name="Heads or Tails",       .description="win {target} coinflips",             .stat_name="coinflip_wins_today",   .category="gambling", .type=ChallengeType::SIMPLE, .min_target=2,   .max_target=15,  .emoji="\xF0\x9F\xAA\x99"},
    {.id="blackjack_wins",  .name="Card Shark",           .description="win {target} blackjack hands",       .stat_name="blackjack_wins_today",  .category="gambling", .type=ChallengeType::SIMPLE, .min_target=1,   .max_target=8,   .emoji="\xF0\x9F\x83\x8F"},
    
    // Economy
    {.id="earn_coins",      .name="Money Maker",          .description="earn ${target} total",               .stat_name="coins_earned_today",    .category="economy", .type=ChallengeType::SIMPLE, .min_target=10000, .max_target=2000000, .emoji="\xF0\x9F\x92\xB5"},
    {.id="work_times",      .name="Hard Worker",          .description="use /work {target} times",           .stat_name="work_count_today",      .category="economy", .type=ChallengeType::SIMPLE, .min_target=2,   .max_target=5,   .emoji="\xF0\x9F\x92\xBC"},
    {.id="pay_others",      .name="Generous Tipper",      .description="pay other players ${target}",        .stat_name="coins_paid_today",      .category="economy", .type=ChallengeType::SIMPLE, .min_target=1000, .max_target=100000,  .emoji="\xF0\x9F\xA4\x9D"},
    
    // Social
    {.id="commands_used",   .name="Command Spammer",      .description="use {target} commands",              .stat_name="commands_today",        .category="social", .type=ChallengeType::SIMPLE, .min_target=20,  .max_target=100, .emoji="\xE2\x8C\xA8\xEF\xB8\x8F"},
    
    // ============ STREAK CHALLENGES (consecutive wins) ============
    {.id="coinflip_streak", .name="Heads or Tails Master", .description="win {target} coinflips in a row",   .stat_name="coinflip_wins_today",   .category="gambling", .type=ChallengeType::STREAK, .min_target=3,   .max_target=10,  .emoji="\xF0\x9F\xAA\x99", .streak_length=5},
    {.id="blackjack_streak",.name="Card Shark's Run",      .description="win {target} blackjack hands in a row", .stat_name="blackjack_wins_today", .category="gambling", .type=ChallengeType::STREAK, .min_target=2,   .max_target=6,   .emoji="\xF0\x9F\x83\x8F", .streak_length=3},
    {.id="work_consistent", .name="Tireless Worker",       .description="complete {target} consecutive /work with >80% success", .stat_name="work_count_today", .category="economy", .type=ChallengeType::STREAK, .min_target=3,   .max_target=8,   .emoji="\xF0\x9F\x8F\x97", .streak_length=4},
    
    // ============ VARIETY CHALLENGES (collect different types) ============
    {.id="fish_diversity",  .name="Ichthyologist",        .description="catch fish from {target} different rarity tiers", .stat_name="rare_fish_caught", .category="fishing", .type=ChallengeType::VARIETY, .min_target=2,   .max_target=4,   .emoji="\xF0\x9F\x90\xA0"},
    {.id="ore_types",       .name="Mining Geologist",     .description="mine {target} different ore types", .stat_name="ores_mined", .category="mining", .type=ChallengeType::VARIETY, .min_target=2,   .max_target=5,   .emoji="\xE2\x9B\x8F\xEF\xB8\x8F"},
    {.id="game_master",     .name="Versatile Gamer",      .description="play {target} different gambling games", .stat_name="gambling_wins_today", .category="gambling", .type=ChallengeType::VARIETY, .min_target=2,   .max_target=3,   .emoji="\xF0\x9F\x8E\xB2"},
    
    // ============ EFFICIENCY CHALLENGES (achieve in time window) ============
    {.id="speed_fisher",    .name="Speed Angler",         .description="catch {target} fish in under {time} minutes", .stat_name="fish_caught", .category="fishing", .type=ChallengeType::EFFICIENCY, .min_target=5,   .max_target=15,  .emoji="\xF0\x9F\x8E\xA3", .time_limit_minutes=10},
    {.id="mining_blitz",    .name="Mining Rush",          .description="mine ${target} worth of ore in under {time} minutes", .stat_name="ore_value_today", .category="mining", .type=ChallengeType::EFFICIENCY, .min_target=5000, .max_target=50000, .emoji="\xE2\x9B\x8F\xEF\xB8\x8F", .time_limit_minutes=15},
    
    // ============ RATIO CHALLENGES (maintain win/profit ratio) ============
    {.id="precision_gambler",.name="Lucky Multiplier",     .description="maintain >70% win rate for {target} gambling attempts", .stat_name="gambling_wins_today", .category="gambling", .type=ChallengeType::RATIO, .min_target=5,   .max_target=15,  .emoji="\xF0\x9F\x8E\xB0", .ratio_threshold=0.70},
    {.id="consistent_miner", .name="Steady Miner",        .description="mine at least {target} ores with >80% success rate", .stat_name="ores_mined", .category="mining", .type=ChallengeType::RATIO, .min_target=5,   .max_target=20,  .emoji="\xE2\x9B\x8F\xEF\xB8\x8F", .ratio_threshold=0.80},
    
    // ============ PRECISE CHALLENGES (hit exact target) ============
    {.id="precise_earner",  .name="Precision Earner",     .description="earn exactly ${target} (±${margin})",  .stat_name="coins_earned_today", .category="economy", .type=ChallengeType::PRECISE, .min_target=50000, .max_target=500000, .emoji="\xF0\x9F\x92\xB0", .precision_margin=5000},
    
    // ============ HYBRID CHALLENGES (multiple stat requirements) ============
    {.id="diversified_day", .name="Jack of All Trades",    .description="earn ${target} from multiple sources", .stat_name="coins_earned_today", .category="economy", .type=ChallengeType::HYBRID, .min_target=50000, .max_target=500000, .emoji="\xF0\x9F\xA4\xB9", .required_stats={"fishing_profit", "mining_profit", "gambling_profit"}},
};

// ============================================================================
// CHALLENGE VALIDATORS — Type-specific completion logic
// ============================================================================

// Validate SIMPLE challenges: single stat must reach target
static bool validate_simple(const ActiveChallenge& challenge) {
    return challenge.current_progress >= challenge.target;
}

// Validate STREAK challenges: consecutive wins without breaking
// Requires streak data to be tracked in daily_stats as "streak_{challenge_id}"
static bool validate_streak(Database* db, const ActiveChallenge& challenge, uint64_t user_id) {
    // Find the template to get the required streak_length
    for (const auto& tmpl : CHALLENGE_TEMPLATES) {
        if (tmpl.id == challenge.challenge_id) {
            if (challenge.progress_data.current_streak < tmpl.streak_length) {
                return false;
            }
            break;
        }
    }
    return true;
}

// Validate VARIETY challenges: must have collected X different types
// Requires variety tracking in daily_stats as "variety_{challenge_id}_{item}"
static bool validate_variety(const ActiveChallenge& challenge) {
    // Check if we have enough unique items collected
    return static_cast<int64_t>(challenge.progress_data.variety_items.size()) >= challenge.target;
}

// Validate EFFICIENCY challenges: achieve goal within time window
static bool validate_efficiency(const ActiveChallenge& challenge) {
    // Find the template to get the required time_limit_minutes
    for (const auto& tmpl : CHALLENGE_TEMPLATES) {
        if (tmpl.id == challenge.challenge_id) {
            if (tmpl.time_limit_minutes > 0) {
                return challenge.current_progress >= challenge.target && 
                       challenge.progress_data.time_elapsed_minutes <= tmpl.time_limit_minutes;
            }
            break;
        }
    }
    return challenge.current_progress >= challenge.target;
}

// Validate RATIO challenges: maintain win% or profit ratio
static bool validate_ratio(const ActiveChallenge& challenge) {
    if (challenge.progress_data.total_attempts == 0) return false;
    
    // Find the template to get the required ratio_threshold
    for (const auto& tmpl : CHALLENGE_TEMPLATES) {
        if (tmpl.id == challenge.challenge_id) {
            double ratio = static_cast<double>(challenge.progress_data.total_wins) / challenge.progress_data.total_attempts;
            return ratio >= tmpl.ratio_threshold && 
                   challenge.progress_data.total_wins >= challenge.target;
        }
    }
    return false;
}

// Validate HYBRID challenges: multiple stats must reach targets
static bool validate_hybrid(const ActiveChallenge& challenge) {
    if (challenge.current_progress < challenge.target) return false;
    
    // Check secondary stats have enough progress
    // This would require enhanced DB queries for each secondary stat
    // For now, primary check is sufficient
    return true;
}

// Validate PRECISE challenges: hit exact target within margin
static bool validate_precise(const ActiveChallenge& challenge) {
    // Find the template to get the required precision_margin
    for (const auto& tmpl : CHALLENGE_TEMPLATES) {
        if (tmpl.id == challenge.challenge_id) {
            int64_t margin = tmpl.precision_margin;
            int64_t lower = challenge.target - margin;
            int64_t upper = challenge.target + margin;
            return challenge.current_progress >= lower && challenge.current_progress <= upper;
        }
    }
    return false;
}

// Validate CONDITIONAL challenges: prerequisite must be complete first
static bool validate_conditional(const ActiveChallenge& challenge) {
    return challenge.progress_data.prerequisite_complete && 
           challenge.current_progress >= challenge.target;
}

// Master validator: route to type-specific logic
static bool is_challenge_complete(Database* db, const ActiveChallenge& challenge, uint64_t user_id) {
    switch (challenge.challenge_type) {
        case ChallengeType::SIMPLE:
            return validate_simple(challenge);
        case ChallengeType::STREAK:
            return validate_streak(db, challenge, user_id);
        case ChallengeType::VARIETY:
            return validate_variety(challenge);
        case ChallengeType::EFFICIENCY:
            return validate_efficiency(challenge);
        case ChallengeType::RATIO:
            return validate_ratio(challenge);
        case ChallengeType::HYBRID:
            return validate_hybrid(challenge);
        case ChallengeType::PRECISE:
            return validate_precise(challenge);
        case ChallengeType::CONDITIONAL:
            return validate_conditional(challenge);
        default:
            return false;
    }
}
enum class ChallengeDifficulty { EASY, MEDIUM, HARD };

static ChallengeDifficulty get_difficulty(const ChallengeTemplate& tmpl, int64_t target) {
    double range = static_cast<double>(tmpl.max_target - tmpl.min_target);
    double position = static_cast<double>(target - tmpl.min_target) / (range > 0 ? range : 1.0);
    if (position < 0.33) return ChallengeDifficulty::EASY;
    if (position < 0.66) return ChallengeDifficulty::MEDIUM;
    return ChallengeDifficulty::HARD;
}

static ChallengeReward calculate_reward(ChallengeDifficulty diff, int64_t networth) {
    ChallengeReward reward;
    reward.xp = 0;
    reward.item_id = "";
    reward.item_name = "";
    
    // Base coin reward scales with networth (like daily)
    double base_pct = 0.0;
    switch (diff) {
        case ChallengeDifficulty::EASY:   base_pct = 0.02; reward.xp = 50;  break;
        case ChallengeDifficulty::MEDIUM: base_pct = 0.04; reward.xp = 100; break;
        case ChallengeDifficulty::HARD:   base_pct = 0.08; reward.xp = 200; break;
    }
    
    reward.coins = static_cast<int64_t>(networth * base_pct);
    if (reward.coins < 500) reward.coins = 500;
    if (reward.coins > 50000000) reward.coins = 50000000; // 50M cap per challenge
    
    return reward;
}

static std::string difficulty_label(ChallengeDifficulty diff) {
    switch (diff) {
        case ChallengeDifficulty::EASY:   return "\xF0\x9F\x9F\xA2 Easy";
        case ChallengeDifficulty::MEDIUM: return "\xF0\x9F\x9F\xA1 Medium";
        case ChallengeDifficulty::HARD:   return "\xF0\x9F\x94\xB4 Hard";
    }
    return "???";
}

static std::string format_description(const std::string& desc, int64_t target) {
    std::string result = desc;
    std::string target_str;
    if (desc.find("${target}") != std::string::npos) {
        target_str = "$" + format_number(target);
        size_t pos = result.find("${target}");
        if (pos != std::string::npos) result.replace(pos, 9, target_str);
    } else {
        target_str = std::to_string(target);
        size_t pos = result.find("{target}");
        if (pos != std::string::npos) result.replace(pos, 8, target_str);
    }
    return result;
}

// --- Progress bar helper ---
static std::string progress_bar(int64_t current, int64_t target, int width = 10) {
    if (target <= 0) target = 1;
    double pct = std::min(1.0, static_cast<double>(current) / target);
    int filled = static_cast<int>(pct * width);
    int empty = width - filled;
    
    std::string bar;
    for (int i = 0; i < filled; i++) bar += "\xE2\x96\x93";  // ▓
    for (int i = 0; i < empty; i++)  bar += "\xE2\x96\x91";  // ░
    
    int pct_int = static_cast<int>(pct * 100);
    bar += " " + std::to_string(pct_int) + "%";
    return bar;
}

// ============================================================================
// Database — lazy table creation
// ============================================================================
static bool g_challenge_tables_created = false;
static std::mutex g_challenge_mutex;

static void ensure_challenge_tables(Database* db) {
    if (g_challenge_tables_created) return;
    std::lock_guard<std::mutex> lock(g_challenge_mutex);
    if (g_challenge_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS daily_challenges ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  challenge_id VARCHAR(64) NOT NULL,"
        "  challenge_name VARCHAR(128) NOT NULL,"
        "  challenge_desc VARCHAR(256) NOT NULL,"
        "  stat_name VARCHAR(64) NOT NULL,"
        "  category VARCHAR(32) NOT NULL,"
        "  challenge_type INT NOT NULL DEFAULT 0,"
        "  target BIGINT NOT NULL,"
        "  start_value BIGINT NOT NULL DEFAULT 0,"
        "  completed BOOLEAN NOT NULL DEFAULT FALSE,"
        "  claimed BOOLEAN NOT NULL DEFAULT FALSE,"
        "  reward_coins BIGINT NOT NULL DEFAULT 0,"
        "  reward_xp INT NOT NULL DEFAULT 0,"
        "  emoji VARCHAR(32) NOT NULL DEFAULT '',"
        "  assigned_date DATE NOT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_user_date (user_id, assigned_date),"
        "  UNIQUE KEY uk_user_challenge_date (user_id, challenge_id, assigned_date)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );

    // Track daily stats that reset each day (for challenge progress)
    db->execute(
        "CREATE TABLE IF NOT EXISTS daily_stats ("
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  stat_name VARCHAR(64) NOT NULL,"
        "  stat_value BIGINT NOT NULL DEFAULT 0,"
        "  stat_date DATE NOT NULL,"
        "  PRIMARY KEY (user_id, stat_name, stat_date),"
        "  INDEX idx_user_date (user_id, stat_date)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    // Track streaks for consecutive achievements
    db->execute(
        "CREATE TABLE IF NOT EXISTS challenge_streaks ("
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  challenge_id VARCHAR(64) NOT NULL,"
        "  current_streak BIGINT NOT NULL DEFAULT 0,"
        "  streak_date DATE NOT NULL,"
        "  last_activity_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (user_id, challenge_id, streak_date),"
        "  INDEX idx_user_date (user_id, streak_date)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    // Track variety items collected for variety challenges
    db->execute(
        "CREATE TABLE IF NOT EXISTS challenge_variety ("
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  challenge_id VARCHAR(64) NOT NULL,"
        "  item_id VARCHAR(128) NOT NULL,"
        "  variety_date DATE NOT NULL,"
        "  PRIMARY KEY (user_id, challenge_id, variety_date, item_id),"
        "  INDEX idx_user_chal_date (user_id, challenge_id, variety_date)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    // Track attempt history for ratio challenges
    db->execute(
        "CREATE TABLE IF NOT EXISTS challenge_attempts ("
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  challenge_id VARCHAR(64) NOT NULL,"
        "  attempt_type VARCHAR(32) NOT NULL,"
        "  is_success BOOLEAN NOT NULL,"
        "  attempt_date DATE NOT NULL,"
        "  attempt_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (user_id, challenge_id, attempt_type, attempt_date, attempt_time),"
        "  INDEX idx_user_chal_date (user_id, challenge_id, attempt_date)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_challenge_tables_created = true;
}

// ============================================================================
// DB helpers
// ============================================================================

// Get today's date string in EST
static std::string get_today_est() {
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    time_t est = tnow - 5 * 3600; // EST = UTC-5
    tm est_tm = *gmtime(&est);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &est_tm);
    return std::string(buf);
}
// Note: get_daily_stat, increment_daily_stat, set_daily_stat, and track_daily_stat
// are now provided by daily_stat_tracker.h to avoid redefinition errors.


static std::vector<ActiveChallenge> get_user_challenges(Database* db, uint64_t user_id) {
    if (!db) return {};
    std::string today = get_today_est();
    auto challenges = db->get_user_challenges(user_id, today);
    
    // Auto-check completion logic
    for (auto& c : challenges) {
        if (!c.completed) {
            if (is_challenge_complete(db, c, user_id)) {
                c.completed = true;
                db->update_challenge_status(user_id, c.challenge_id, today, true, false);
            }
        }
    }
    
    return challenges;
}

static void assign_daily_challenges(Database* db, uint64_t user_id) {
    std::string today = get_today_est();
    int64_t networth = db->get_networth(user_id);
    
    // Pick 3 random unique challenges
    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::vector<size_t> indices(CHALLENGE_TEMPLATES.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), gen);
    
    int assigned = 0;
    for (size_t idx : indices) {
        if (assigned >= 3) break;
        
        const auto& tmpl = CHALLENGE_TEMPLATES[idx];
        std::uniform_int_distribution<int64_t> target_dist(tmpl.min_target, tmpl.max_target);
        int64_t target = target_dist(gen);
        
        auto diff = get_difficulty(tmpl, target);
        auto reward = calculate_reward(diff, networth);
        std::string desc = format_description(tmpl.description, target);
        
        ActiveChallenge c;
        c.challenge_id = tmpl.id;
        c.name = tmpl.name;
        c.description = desc;
        c.stat_name = tmpl.stat_name;
        c.category = tmpl.category;
        c.challenge_type = tmpl.type;
        c.target = target;
        c.start_value = 0; // Will be set to current stat in many cases, but for simple "do X times" it's often 0
        
        // For some stats, we might want to capture the current value as start_value
        // but the daily_stats system handles it relative to the day.
        
        c.reward = {reward.coins, static_cast<int64_t>(reward.xp), "", ""};
        c.emoji = tmpl.emoji;
        
        if (db->assign_daily_challenge(user_id, c, today)) {
            assigned++;
        }
    }
}

// ============================================================================
// /challenges command
// ============================================================================
inline Command* create_challenges_command(Database* db) {
    static Command* cmd = new Command("challenges", "view & claim daily challenges", "economy", {"challenge", "dc"}, true,
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_challenge_tables(db);
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            std::string action = args.empty() ? "" : args[0];
            
            // Handle claim action
            if (action == "claim") {
                auto challenges = get_user_challenges(db, user_id);
                if (challenges.empty()) {
                    bronx::send_message(bot, event, bronx::error("no challenges found! use `b.challenges` to generate today's challenges."));
                    return;
                }
                
                int64_t total_coins = 0;
                int total_xp = 0;
                int claimed_count = 0;
                std::string today = get_today_est();
                
                for (auto& c : challenges) {
                    if (c.completed && !c.claimed) {
                        total_coins += c.reward.coins;
                        total_xp += c.reward.xp;
                        claimed_count++;
                        
                        // Mark as claimed securely
                        db->update_challenge_status(user_id, c.challenge_id, today, true, true);
                    }
                }
                
                if (claimed_count == 0) {
                    bronx::send_message(bot, event, bronx::error("no completed challenges to claim!"));
                    return;
                }
                
                // Award rewards
                if (total_coins > 0) {
                    db->update_wallet(user_id, total_coins);
                    log_balance_change(db, user_id, "daily challenge reward +$" + format_number(total_coins));
                }
                
                // Check for all-3 bonus
                bool all_complete = true;
                for (auto& c : challenges) {
                    if (!c.completed) { all_complete = false; break; }
                }
                
                int64_t bonus = 0;
                if (all_complete) {
                    // 10% of networth bonus for completing all 3
                    int64_t networth = db->get_networth(user_id);
                    bonus = static_cast<int64_t>(networth * 0.10);
                    if (bonus < 1000) bonus = 1000;
                    if (bonus > 100000000) bonus = 100000000; // 100M cap
                    db->update_wallet(user_id, bonus);
                    log_balance_change(db, user_id, "all challenges bonus +$" + format_number(bonus));
                    
                    // Increment streak via daily_stats
                    increment_daily_stat(db, user_id, "challenge_streak_day", 1);
                }
                
                std::string desc = "\xF0\x9F\x8E\x89 **Claimed " + std::to_string(claimed_count) + " challenge reward" + (claimed_count > 1 ? "s" : "") + "!**\n\n";
                desc += "\xF0\x9F\x92\xB0 **$" + format_number(total_coins) + "** coins";
                if (total_xp > 0) desc += "\n\xE2\x9C\xA8 **" + std::to_string(total_xp) + "** XP";
                if (bonus > 0) desc += "\n\xF0\x9F\x8C\x9F **All Challenges Bonus:** $" + format_number(bonus);
                
                auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                embed.set_title("\xF0\x9F\x8F\x86 Challenge Rewards");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Default: view challenges
            auto challenges = get_user_challenges(db, user_id);
            
            // If no challenges for today, generate them
            if (challenges.empty()) {
                assign_daily_challenges(db, user_id);
                challenges = get_user_challenges(db, user_id);
            }
            
            if (challenges.empty()) {
                bronx::send_message(bot, event, bronx::error("failed to generate daily challenges!"));
                return;
            }
            
            // Build embed
            std::string desc = "\xF0\x9F\x93\x8B **Today's Challenges**\n\n";
            
            int completed_count = 0;
            int claimable_count = 0;
            
            for (size_t i = 0; i < challenges.size(); i++) {
                auto& c = challenges[i];
                
                std::string status_emoji;
                if (c.claimed) {
                    status_emoji = bronx::EMOJI_CHECK;
                    completed_count++;
                } else if (c.completed) {
                    status_emoji = "\xF0\x9F\xC6\x86";  // 🎁 claimable
                    status_emoji = "\xF0\x9F\x8E\x81"; // 🎁
                    completed_count++;
                    claimable_count++;
                } else {
                    status_emoji = "\xE2\xAC\x9C"; // ⬜
                }
                
                desc += status_emoji + " **" + c.emoji + " " + c.name + "**\n";
                desc += "   " + c.description + "\n";
                
                int64_t progress = std::min(c.current_progress, c.target);
                desc += "   " + progress_bar(progress, c.target) + " (" + format_number(progress) + "/" + format_number(c.target) + ")\n";
                desc += "   Reward: $" + format_number(c.reward.coins);
                if (c.reward.xp > 0) desc += " + " + std::to_string(c.reward.xp) + " XP";
                desc += "\n\n";
            }
            
            // Summary
            desc += "**" + std::to_string(completed_count) + "/" + std::to_string(challenges.size()) + "** completed";
            if (claimable_count > 0) {
                desc += " \xE2\x80\x94 use `b.challenges claim` to collect rewards!";
            }
            if (completed_count == static_cast<int>(challenges.size())) {
                desc += "\n\xF0\x9F\x8C\x9F **All challenges complete! Bonus reward available!**";
            }
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\x93\x8B Daily Challenges");
            
            // Show time until reset
            auto now = std::chrono::system_clock::now();
            time_t tnow = std::chrono::system_clock::to_time_t(now);
            time_t est = tnow - 5 * 3600;
            tm est_tm = *gmtime(&est);
            int seconds_until_midnight = (23 - est_tm.tm_hour) * 3600 + (59 - est_tm.tm_min) * 60 + (60 - est_tm.tm_sec);
            int hours_left = seconds_until_midnight / 3600;
            int mins_left = (seconds_until_midnight % 3600) / 60;
            embed.set_footer(dpp::embed_footer().set_text("resets in " + std::to_string(hours_left) + "h " + std::to_string(mins_left) + "m"));
            
            bronx::send_message(bot, event, embed);
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_challenge_tables(db);
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);
            
            std::string action = "";
            if (event.get_parameter("action").index() != 0) {
                action = std::get<std::string>(event.get_parameter("action"));
            }
            
            // Handle claim action
            if (action == "claim") {
                auto user_challenges_list = get_user_challenges(db, user_id);
                if (user_challenges_list.empty()) {
                    event.reply(dpp::message().add_embed(
                        bronx::error("no challenges found! use `/challenges` to generate today's challenges.")));
                    return;
                }
                
                int64_t total_coins = 0;
                int total_xp = 0;
                int claimed_count = 0;
                std::string today = get_today_est();
                
                for (auto& c : user_challenges_list) {
                    if (c.completed && !c.claimed) {
                        total_coins += c.reward.coins;
                        total_xp += c.reward.xp;
                        claimed_count++;
                        
                        // Mark as claimed securely
                        db->update_challenge_status(user_id, c.challenge_id, today, true, true);
                    }
                }
                
                if (claimed_count == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("no completed challenges to claim!")));
                    return;
                }
                
                if (total_coins > 0) {
                    db->update_wallet(user_id, total_coins);
                    log_balance_change(db, user_id, "daily challenge reward +$" + format_number(total_coins));
                }
                
                bool all_complete = true;
                for (auto& c : user_challenges_list) {
                    if (!c.completed) { all_complete = false; break; }
                }
                
                int64_t bonus = 0;
                if (all_complete) {
                    int64_t networth = db->get_networth(user_id);
                    bonus = static_cast<int64_t>(networth * 0.10);
                    if (bonus < 1000) bonus = 1000;
                    if (bonus > 100000000) bonus = 100000000;
                    db->update_wallet(user_id, bonus);
                    log_balance_change(db, user_id, "all challenges bonus +$" + format_number(bonus));
                    increment_daily_stat(db, user_id, "challenge_streak_day", 1);
                }
                
                std::string desc = "\xF0\x9F\x8E\x89 **Claimed " + std::to_string(claimed_count) + " challenge reward" + (claimed_count > 1 ? "s" : "") + "!**\n\n";
                desc += "\xF0\x9F\x92\xB0 **$" + format_number(total_coins) + "** coins";
                if (total_xp > 0) desc += "\n\xE2\x9C\xA8 **" + std::to_string(total_xp) + "** XP";
                if (bonus > 0) desc += "\n\xF0\x9F\x8C\x9F **All Challenges Bonus:** $" + format_number(bonus);
                
                auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                embed.set_title("\xF0\x9F\x8F\x86 Challenge Rewards");
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // Default: view
            auto user_challenges_list = get_user_challenges(db, user_id);
            if (user_challenges_list.empty()) {
                assign_daily_challenges(db, user_id);
                user_challenges_list = get_user_challenges(db, user_id);
            }
            
            if (user_challenges_list.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to generate daily challenges!")));
                return;
            }
            
            std::string desc = "\xF0\x9F\x93\x8B **Today's Challenges**\n\n";
            int completed_count = 0;
            int claimable_count = 0;
            
            for (size_t i = 0; i < user_challenges_list.size(); i++) {
                auto& c = user_challenges_list[i];
                
                std::string status_emoji;
                if (c.claimed) {
                    status_emoji = bronx::EMOJI_CHECK;
                    completed_count++;
                } else if (c.completed) {
                    status_emoji = "\xF0\x9F\x8E\x81"; // 🎁
                    completed_count++;
                    claimable_count++;
                } else {
                    status_emoji = "\xE2\xAC\x9C"; // ⬜
                }
                
                desc += status_emoji + " **" + c.emoji + " " + c.name + "**\n";
                desc += "   " + c.description + "\n";
                
                int64_t progress = std::min(c.current_progress, c.target);
                desc += "   " + progress_bar(progress, c.target) + " (" + format_number(progress) + "/" + format_number(c.target) + ")\n";
                desc += "   Reward: $" + format_number(c.reward.coins);
                if (c.reward.xp > 0) desc += " + " + std::to_string(c.reward.xp) + " XP";
                desc += "\n\n";
            }
            
            desc += "**" + std::to_string(completed_count) + "/" + std::to_string(user_challenges_list.size()) + "** completed";
            if (claimable_count > 0) {
                desc += " \xE2\x80\x94 use `/challenges claim` to collect rewards!";
            }
            if (completed_count == static_cast<int>(user_challenges_list.size())) {
                desc += "\n\xF0\x9F\x8C\x9F **All challenges complete! Bonus reward available!**";
            }
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\x93\x8B Daily Challenges");
            
            auto now = std::chrono::system_clock::now();
            time_t tnow = std::chrono::system_clock::to_time_t(now);
            time_t est = tnow - 5 * 3600;
            tm est_tm = *gmtime(&est);
            int seconds_until_midnight = (23 - est_tm.tm_hour) * 3600 + (59 - est_tm.tm_min) * 60 + (60 - est_tm.tm_sec);
            int hours_left = seconds_until_midnight / 3600;
            int mins_left = (seconds_until_midnight % 3600) / 60;
            embed.set_footer(dpp::embed_footer().set_text("resets in " + std::to_string(hours_left) + "h " + std::to_string(mins_left) + "m"));
            
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        // Slash command options
        {
            dpp::command_option(dpp::co_string, "action", "view or claim", false)
                .add_choice(dpp::command_option_choice("view", std::string("view")))
                .add_choice(dpp::command_option_choice("claim", std::string("claim")))
        }
    );
    
    // Extended help
    cmd->extended_description = "Complete rotating daily challenges to earn bonus coins, XP, and rewards. "
                                "Three random challenges are generated each day at midnight EST.";
    cmd->examples = {"b.challenges", "b.challenges claim"};
    
    return cmd;
}

} // namespace daily_challenges
} // namespace commands
