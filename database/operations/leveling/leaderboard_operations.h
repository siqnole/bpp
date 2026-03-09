#pragma once

#include "../../core/types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// Leaderboard operations extension for Database class
// These methods handle various ranking systems and statistics
namespace leaderboard_operations {
    // Economy leaderboards
    std::vector<LeaderboardEntry> get_networth_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_wallet_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_bank_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_inventory_value_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    
    // Fishing leaderboards
    std::vector<LeaderboardEntry> get_fish_caught_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_fish_sold_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_most_valuable_fish_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_fishing_profit_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    
    // Gambling leaderboards
    std::vector<LeaderboardEntry> get_gambling_wins_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_gambling_losses_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_gambling_profit_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_slots_wins_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_coinflip_wins_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    
    // Activity leaderboards
    std::vector<LeaderboardEntry> get_commands_used_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_daily_streak_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_work_count_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    
    // Generic methods
    std::vector<LeaderboardEntry> get_leaderboard(Database* db, const std::string& type, int limit = 10);
    int get_user_rank(Database* db, uint64_t user_id, const std::string& type);
    void update_leaderboard_cache(Database* db);
    
    // Helper method for stats-based leaderboards
    std::vector<LeaderboardEntry> get_stats_leaderboard(Database* db, const std::string& stat_name, uint64_t guild_id, int limit, const std::string& emoji = "");
}

} // namespace db
} // namespace bronx