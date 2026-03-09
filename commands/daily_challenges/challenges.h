#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy/helpers.h"
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
using commands::economy::format_number;

namespace commands {
namespace daily_challenges {

// ============================================================================
// DAILY CHALLENGES — Rotating objectives that reward varied gameplay
// ============================================================================
// Each day at midnight EST, 3 random challenges are generated for each user.
// Completing challenges awards bonus coins, XP, and lootboxes.
//
// Subcommands:
//   /challenges           — view today's challenges + progress
//   /challenges claim     — claim completed challenge rewards
// ============================================================================

// --- Challenge template definitions ---
struct ChallengeTemplate {
    std::string id;
    std::string name;
    std::string description;    // use {target} for the required amount
    std::string stat_name;      // which stat to track (matches db->get_stat keys)
    std::string category;       // fishing, mining, gambling, economy, social
    int64_t min_target;         // minimum target value
    int64_t max_target;         // maximum target value
    std::string emoji;
};

struct ChallengeReward {
    int64_t coins;
    int64_t xp;
    std::string item_id;        // optional item reward
    std::string item_name;
};

struct ActiveChallenge {
    std::string challenge_id;
    std::string name;
    std::string description;
    std::string stat_name;
    std::string category;
    int64_t target;
    int64_t start_value;        // stat value when challenge was assigned
    int64_t current_progress;   // current stat value - start_value
    bool completed;
    bool claimed;
    ChallengeReward reward;
    std::string emoji;
};

// All available challenge templates
static const std::vector<ChallengeTemplate> CHALLENGE_TEMPLATES = {
    // Fishing challenges
    {"fish_catch",      "Reel 'Em In",          "catch {target} fish",                "fish_caught",           "fishing",  5,   50,  "\xF0\x9F\x8E\xA3"},
    {"fish_sell_value", "Fish Market",           "sell ${target} worth of fish",       "fish_sell_value_today", "fishing",  5000, 500000, "\xF0\x9F\x92\xB0"},
    {"fish_rare",       "Rare Catch",            "catch {target} rare+ fish",          "rare_fish_caught",      "fishing",  1,   10,  "\xE2\x9C\xA8"},
    
    // Mining challenges
    {"mine_ores",       "Ore Collector",         "mine {target} ores",                 "ores_mined",            "mining",   5,   40,  "\xE2\x9B\x8F\xEF\xB8\x8F"},
    {"mine_value",      "Prospector's Haul",     "mine ${target} worth of ore",        "ore_value_today",       "mining",   5000, 250000, "\xF0\x9F\x92\x8E"},
    
    // Gambling challenges
    {"gamble_wins",     "Lucky Streak",          "win {target} gambles",               "gambling_wins_today",   "gambling", 3,   20,  "\xF0\x9F\x8E\xB0"},
    {"gamble_profit",   "High Roller",           "profit ${target} from gambling",     "gambling_profit_today", "gambling", 10000, 1000000, "\xF0\x9F\x92\xB8"},
    {"coinflip_wins",   "Heads or Tails",        "win {target} coinflips",             "coinflip_wins_today",   "gambling", 2,   15,  "\xF0\x9F\xAA\x99"},
    {"blackjack_wins",  "Card Shark",            "win {target} blackjack hands",       "blackjack_wins_today",  "gambling", 1,   8,   "\xF0\x9F\x83\x8F"},
    
    // Economy challenges
    {"earn_coins",      "Money Maker",           "earn ${target} total",               "coins_earned_today",    "economy",  10000, 2000000, "\xF0\x9F\x92\xB5"},
    {"work_times",      "Hard Worker",           "use /work {target} times",           "work_count_today",      "economy",  2,   5,   "\xF0\x9F\x92\xBC"},
    {"pay_others",      "Generous Tipper",       "pay other players ${target}",        "coins_paid_today",      "economy",  1000, 100000,  "\xF0\x9F\xA4\x9D"},
    
    // Social challenges
    {"commands_used",   "Command Spammer",       "use {target} commands",              "commands_today",        "social",   20,  100, "\xE2\x8C\xA8\xEF\xB8\x8F"},
};

// Difficulty tiers affect reward scaling
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

static int64_t get_daily_stat(Database* db, uint64_t user_id, const std::string& stat_name) {
    std::string today = get_today_est();
    std::string sql = "SELECT stat_value FROM daily_stats WHERE user_id = " + std::to_string(user_id)
                    + " AND stat_name = '" + stat_name + "'"
                    + " AND stat_date = '" + today + "'";
    MYSQL_RES* res = economy::db_select(db, sql);
    int64_t val = 0;
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) val = std::stoll(row[0]);
        mysql_free_result(res);
    }
    return val;
}

static void increment_daily_stat(Database* db, uint64_t user_id, const std::string& stat_name, int64_t amount = 1) {
    std::string today = get_today_est();
    std::string sql = "INSERT INTO daily_stats (user_id, stat_name, stat_value, stat_date) "
                      "VALUES (" + std::to_string(user_id) + ", '" + stat_name + "', "
                      + std::to_string(amount) + ", '" + today + "') "
                      "ON DUPLICATE KEY UPDATE stat_value = stat_value + " + std::to_string(amount);
    economy::db_exec(db, sql);
}

static std::vector<ActiveChallenge> get_user_challenges(Database* db, uint64_t user_id) {
    std::string today = get_today_est();
    std::vector<ActiveChallenge> challenges;
    
    std::string sql = "SELECT challenge_id, challenge_name, challenge_desc, stat_name, category, "
                      "target, start_value, completed, claimed, reward_coins, reward_xp, emoji "
                      "FROM daily_challenges WHERE user_id = " + std::to_string(user_id)
                    + " AND assigned_date = '" + today + "' ORDER BY id";
    MYSQL_RES* res = economy::db_select(db, sql);
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            ActiveChallenge c;
            c.challenge_id = row[0] ? row[0] : "";
            c.name = row[1] ? row[1] : "";
            c.description = row[2] ? row[2] : "";
            c.stat_name = row[3] ? row[3] : "";
            c.category = row[4] ? row[4] : "";
            c.target = row[5] ? std::stoll(row[5]) : 0;
            c.start_value = row[6] ? std::stoll(row[6]) : 0;
            c.completed = row[7] && std::string(row[7]) == "1";
            c.claimed = row[8] && std::string(row[8]) == "1";
            c.reward.coins = row[9] ? std::stoll(row[9]) : 0;
            c.reward.xp = row[10] ? std::stoi(row[10]) : 0;
            c.emoji = row[11] ? row[11] : "";
            
            // Calculate current progress from daily stats
            c.current_progress = get_daily_stat(db, user_id, c.stat_name);
            
            // Auto-mark completed if target met
            if (c.current_progress >= c.target && !c.completed) {
                c.completed = true;
                db->execute("UPDATE daily_challenges SET completed = TRUE "
                           "WHERE user_id = " + std::to_string(user_id) + 
                           " AND challenge_id = '" + c.challenge_id + "'"
                           " AND assigned_date = '" + today + "'");
            }
            
            challenges.push_back(c);
        }
        mysql_free_result(res);
    }
    
    return challenges;
}

static void assign_daily_challenges(Database* db, uint64_t user_id) {
    std::string today = get_today_est();
    int64_t networth = db->get_networth(user_id);
    
    // Pick 3 random unique challenges
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Create shuffled indices
    std::vector<size_t> indices(CHALLENGE_TEMPLATES.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), gen);
    
    int assigned = 0;
    for (size_t idx : indices) {
        if (assigned >= 3) break;
        
        const auto& tmpl = CHALLENGE_TEMPLATES[idx];
        
        // Randomize the target within range
        std::uniform_int_distribution<int64_t> target_dist(tmpl.min_target, tmpl.max_target);
        int64_t target = target_dist(gen);
        
        // Calculate reward based on difficulty
        auto diff = get_difficulty(tmpl, target);
        auto reward = calculate_reward(diff, networth);
        
        std::string desc = format_description(tmpl.description, target);
        
        // All string values come from our internal CHALLENGE_TEMPLATES constants, safe to concat
        std::string sql = "INSERT IGNORE INTO daily_challenges "
                          "(user_id, challenge_id, challenge_name, challenge_desc, stat_name, category, "
                          " target, start_value, reward_coins, reward_xp, emoji, assigned_date) "
                          "VALUES (" + std::to_string(user_id) + ", "
                          "'" + economy::db_escape(db, tmpl.id) + "', "
                          "'" + economy::db_escape(db, tmpl.name) + "', "
                          "'" + economy::db_escape(db, desc) + "', "
                          "'" + economy::db_escape(db, tmpl.stat_name) + "', "
                          "'" + economy::db_escape(db, tmpl.category) + "', "
                          + std::to_string(target) + ", 0, "
                          + std::to_string(reward.coins) + ", "
                          + std::to_string(reward.xp) + ", "
                          "'" + economy::db_escape(db, tmpl.emoji) + "', "
                          "'" + today + "')";
        if (economy::db_exec(db, sql)) {
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
                        
                        // Mark as claimed
                        db->execute("UPDATE daily_challenges SET claimed = TRUE "
                                   "WHERE user_id = " + std::to_string(user_id) + 
                                   " AND challenge_id = '" + c.challenge_id + "'"
                                   " AND assigned_date = '" + today + "'");
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
                auto challenges = get_user_challenges(db, user_id);
                if (challenges.empty()) {
                    event.reply(dpp::message().add_embed(
                        bronx::error("no challenges found! use `/challenges` to generate today's challenges.")));
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
                        
                        db->execute("UPDATE daily_challenges SET claimed = TRUE "
                                   "WHERE user_id = " + std::to_string(user_id) + 
                                   " AND challenge_id = '" + c.challenge_id + "'"
                                   " AND assigned_date = '" + today + "'");
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
                for (auto& c : challenges) {
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
            auto challenges = get_user_challenges(db, user_id);
            if (challenges.empty()) {
                assign_daily_challenges(db, user_id);
                challenges = get_user_challenges(db, user_id);
            }
            
            if (challenges.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to generate daily challenges!")));
                return;
            }
            
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
            
            desc += "**" + std::to_string(completed_count) + "/" + std::to_string(challenges.size()) + "** completed";
            if (claimable_count > 0) {
                desc += " \xE2\x80\x94 use `/challenges claim` to collect rewards!";
            }
            if (completed_count == static_cast<int>(challenges.size())) {
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
