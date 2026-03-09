#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy/helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <ctime>

using namespace bronx::db;
using namespace bronx::db::history_operations;
using commands::economy::format_number;

namespace commands {
namespace daily_challenges {

// ============================================================================
// DAILY STREAKS — Reward consecutive daily logins
// ============================================================================
// Track consecutive days the user claims their daily reward.
// Milestones at day 3, 7, 14, 21, 30, 60, 90, 180, 365 give bonus rewards.
//
//   /streak   — view your current streak + next milestone
// ============================================================================

struct StreakMilestone {
    int day;
    std::string name;
    int64_t bonus_coins;
    std::string bonus_item;     // optional item reward
    std::string emoji;
    std::string description;
};

static const std::vector<StreakMilestone> STREAK_MILESTONES = {
    {3,   "Getting Started",   5000,         "",               "\xF0\x9F\x94\xA5", "3-day streak bonus"},
    {7,   "One Week Strong",   50000,        "",               "\xF0\x9F\x94\xA5", "7-day streak + Common Lootbox"},
    {14,  "Two Weeks",         200000,       "",               "\xE2\xAD\x90",     "14-day streak + Rare Lootbox"},
    {21,  "Three Weeks",       500000,       "",               "\xF0\x9F\x8C\x9F", "21-day streak bonus"},
    {30,  "Monthly Master",    1000000,      "",               "\xF0\x9F\x91\x91", "30-day streak + Legendary Lootbox + \"Dedicated\" title"},
    {60,  "Two Months",        5000000,      "",               "\xF0\x9F\x92\x8E", "60-day streak + Prestige Lootbox"},
    {90,  "Quarter Year",      10000000,     "",               "\xF0\x9F\x8F\x86", "90-day streak + Epic rewards"},
    {180, "Half Year",         50000000,     "",               "\xF0\x9F\x91\x91", "180-day streak + massive bonus"},
    {365, "Year-Long Grind",   250000000,    "",               "\xF0\x9F\x8E\x86", "365-day streak — legendary dedication"},
};

// ============================================================================
// Database — lazy table creation  
// ============================================================================
static bool g_streak_tables_created = false;
static std::mutex g_streak_mutex;

static void ensure_streak_tables(Database* db) {
    if (g_streak_tables_created) return;
    std::lock_guard<std::mutex> lock(g_streak_mutex);
    if (g_streak_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS daily_streaks ("
        "  user_id BIGINT UNSIGNED PRIMARY KEY,"
        "  current_streak INT NOT NULL DEFAULT 0,"
        "  longest_streak INT NOT NULL DEFAULT 0,"
        "  last_claim_date DATE DEFAULT NULL,"
        "  total_claims INT NOT NULL DEFAULT 0,"
        "  total_streak_bonus BIGINT NOT NULL DEFAULT 0,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_streak_tables_created = true;
}

// ============================================================================
// DB helpers
// ============================================================================

struct UserStreak {
    int current_streak;
    int longest_streak;
    std::string last_claim_date;
    int total_claims;
    int64_t total_bonus;
};

static std::string get_today_est_streak() {
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    time_t est = tnow - 5 * 3600;
    tm est_tm = *gmtime(&est);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &est_tm);
    return std::string(buf);
}

static std::string get_yesterday_est() {
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    time_t est = tnow - 5 * 3600 - 86400; // yesterday EST
    tm est_tm = *gmtime(&est);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &est_tm);
    return std::string(buf);
}

static UserStreak get_user_streak(Database* db, uint64_t user_id) {
    UserStreak streak = {0, 0, "", 0, 0};
    std::string sql = "SELECT current_streak, longest_streak, last_claim_date, total_claims, total_streak_bonus "
                      "FROM daily_streaks WHERE user_id = " + std::to_string(user_id);
    MYSQL_RES* res = economy::db_select(db, sql);
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            streak.current_streak = row[0] ? std::stoi(row[0]) : 0;
            streak.longest_streak = row[1] ? std::stoi(row[1]) : 0;
            streak.last_claim_date = row[2] ? row[2] : "";
            streak.total_claims = row[3] ? std::stoi(row[3]) : 0;
            streak.total_bonus = row[4] ? std::stoll(row[4]) : 0;
        }
        mysql_free_result(res);
    }
    return streak;
}

// Called when user claims daily — updates streak
static int64_t update_streak_on_daily_claim(Database* db, uint64_t user_id) {
    ensure_streak_tables(db);
    
    std::string today = get_today_est_streak();
    std::string yesterday = get_yesterday_est();
    
    auto streak = get_user_streak(db, user_id);
    
    // Already claimed today
    if (streak.last_claim_date == today) return 0;
    
    int new_streak;
    if (streak.last_claim_date == yesterday) {
        // Consecutive — increment streak
        new_streak = streak.current_streak + 1;
    } else {
        // Streak broken or first claim
        new_streak = 1;
    }
    
    int new_longest = std::max(streak.longest_streak, new_streak);
    
    // Check if we hit a milestone
    int64_t milestone_bonus = 0;
    for (const auto& m : STREAK_MILESTONES) {
        if (new_streak == m.day) {
            milestone_bonus = m.bonus_coins;
            break;
        }
    }
    
    // Upsert streak data
    std::string sql = "INSERT INTO daily_streaks (user_id, current_streak, longest_streak, last_claim_date, total_claims, total_streak_bonus) "
                      "VALUES (" + std::to_string(user_id) + ", " + std::to_string(new_streak) + ", "
                      + std::to_string(new_longest) + ", '" + today + "', 1, " + std::to_string(milestone_bonus) + ") "
                      "ON DUPLICATE KEY UPDATE "
                      "current_streak = " + std::to_string(new_streak) + ", "
                      "longest_streak = " + std::to_string(new_longest) + ", "
                      "last_claim_date = '" + today + "', "
                      "total_claims = total_claims + 1, "
                      "total_streak_bonus = total_streak_bonus + " + std::to_string(milestone_bonus);
    economy::db_exec(db, sql);
    
    // Award milestone bonus
    if (milestone_bonus > 0) {
        db->update_wallet(user_id, milestone_bonus);
        log_balance_change(db, user_id, "streak milestone (" + std::to_string(new_streak) + " days) +$" + format_number(milestone_bonus));
    }
    
    return milestone_bonus;
}

// ============================================================================
// /streak command
// ============================================================================
inline Command* create_streak_command(Database* db) {
    static Command* cmd = new Command("streak", "view your daily login streak", "economy", {"streaks"}, true,
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_streak_tables(db);
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            auto streak = get_user_streak(db, user_id);
            
            // Check if streak is still active (not broken)
            std::string today = get_today_est_streak();
            std::string yesterday = get_yesterday_est();
            bool streak_active = (streak.last_claim_date == today || streak.last_claim_date == yesterday);
            int display_streak = streak_active ? streak.current_streak : 0;
            
            // Build embed
            std::string desc;
            
            // Streak fire visual
            if (display_streak > 0) {
                desc += "\xF0\x9F\x94\xA5 **" + std::to_string(display_streak) + "-day streak!**\n\n";
            } else {
                desc += "\xE2\x9D\x84\xEF\xB8\x8F **No active streak** \xE2\x80\x94 claim your `/daily` to start!\n\n";
            }
            
            // Streak stats
            desc += "\xF0\x9F\x93\x8A **Stats**\n";
            desc += "\xE2\x80\xA2 Current Streak: **" + std::to_string(display_streak) + " days**\n";
            desc += "\xE2\x80\xA2 Longest Streak: **" + std::to_string(streak.longest_streak) + " days**\n";
            desc += "\xE2\x80\xA2 Total Claims: **" + std::to_string(streak.total_claims) + "**\n";
            desc += "\xE2\x80\xA2 Total Streak Bonus: **$" + format_number(streak.total_bonus) + "**\n\n";
            
            // Next milestone
            desc += "\xF0\x9F\x8E\xAF **Milestones**\n";
            
            for (const auto& m : STREAK_MILESTONES) {
                std::string status;
                if (display_streak >= m.day) {
                    status = bronx::EMOJI_CHECK;
                } else {
                    status = "\xE2\xAC\x9C"; // ⬜
                }
                
                desc += status + " " + m.emoji + " **Day " + std::to_string(m.day) + "** — " + m.name;
                desc += " ($" + format_number(m.bonus_coins) + ")";
                
                // Highlight next milestone
                if (display_streak < m.day && (display_streak == 0 || display_streak < m.day)) {
                    int days_left = m.day - display_streak;
                    desc += " *(" + std::to_string(days_left) + " days away)*";
                    // Only show "next" for the very first uncompleted milestone
                    static bool shown_next = false;
                    if (!shown_next) {
                        desc += " \xE2\x97\x80\xEF\xB8\x8F NEXT";
                        shown_next = true;
                    }
                }
                desc += "\n";
            }
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\x94\xA5 Daily Streak");
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::maybe_add_support_link(embed);
            bronx::send_message(bot, event, embed);
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_streak_tables(db);
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);
            
            auto streak = get_user_streak(db, user_id);
            
            std::string today = get_today_est_streak();
            std::string yesterday = get_yesterday_est();
            bool streak_active = (streak.last_claim_date == today || streak.last_claim_date == yesterday);
            int display_streak = streak_active ? streak.current_streak : 0;
            
            std::string desc;
            if (display_streak > 0) {
                desc += "\xF0\x9F\x94\xA5 **" + std::to_string(display_streak) + "-day streak!**\n\n";
            } else {
                desc += "\xE2\x9D\x84\xEF\xB8\x8F **No active streak** \xE2\x80\x94 claim your `/daily` to start!\n\n";
            }
            
            desc += "\xF0\x9F\x93\x8A **Stats**\n";
            desc += "\xE2\x80\xA2 Current Streak: **" + std::to_string(display_streak) + " days**\n";
            desc += "\xE2\x80\xA2 Longest Streak: **" + std::to_string(streak.longest_streak) + " days**\n";
            desc += "\xE2\x80\xA2 Total Claims: **" + std::to_string(streak.total_claims) + "**\n";
            desc += "\xE2\x80\xA2 Total Streak Bonus: **$" + format_number(streak.total_bonus) + "**\n\n";
            
            desc += "\xF0\x9F\x8E\xAF **Milestones**\n";
            bool shown_next = false;
            for (const auto& m : STREAK_MILESTONES) {
                std::string status;
                if (display_streak >= m.day) {
                    status = bronx::EMOJI_CHECK;
                } else {
                    status = "\xE2\xAC\x9C";
                }
                
                desc += status + " " + m.emoji + " **Day " + std::to_string(m.day) + "** — " + m.name;
                desc += " ($" + format_number(m.bonus_coins) + ")";
                
                if (display_streak < m.day && !shown_next) {
                    int days_left = m.day - display_streak;
                    desc += " *(" + std::to_string(days_left) + " days away)* \xE2\x97\x80\xEF\xB8\x8F NEXT";
                    shown_next = true;
                }
                desc += "\n";
            }
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\x94\xA5 Daily Streak");
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            bronx::maybe_add_support_link(embed);
            event.reply(dpp::message().add_embed(embed));
        }
    );
    
    cmd->extended_description = "Track your daily login streak and earn milestone bonuses. "
                                "Claim `/daily` every day to keep your streak alive!";
    cmd->examples = {"b.streak"};
    
    return cmd;
}

} // namespace daily_challenges
} // namespace commands
