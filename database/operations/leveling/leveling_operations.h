#pragma once

#include "../../core/types.h"
#include <optional>
#include <vector>
#include <cstdint>

namespace bronx {
namespace db {

class Database;

namespace leveling_operations {
    // User XP operations (guild_id=0 → global XP)
    std::optional<UserXP> get_user_xp(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    bool create_user_xp(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    bool add_xp(Database* db, uint64_t user_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up, uint64_t guild_id = 0);
    bool set_xp(Database* db, uint64_t user_id, uint64_t xp_amount, uint64_t guild_id = 0);
    bool reset_guild_xp(Database* db, uint64_t guild_id);
    bool reset_user_guild_xp(Database* db, uint64_t user_id, uint64_t guild_id);
    
    // Guild leveling configuration
    std::optional<GuildLevelingConfig> get_guild_config(Database* db, uint64_t guild_id);
    bool create_guild_config(Database* db, uint64_t guild_id);
    bool update_guild_config(Database* db, const GuildLevelingConfig& config);
    
    // Level roles
    std::vector<LevelRole> get_level_roles(Database* db, uint64_t guild_id);
    std::optional<LevelRole> get_level_role_at_level(Database* db, uint64_t guild_id, uint32_t level);
    bool create_level_role(Database* db, const LevelRole& role);
    bool delete_level_role(Database* db, uint64_t guild_id, uint32_t level);
    bool delete_level_role_by_id(Database* db, uint64_t id);
    
    // Level calculation helpers
    uint32_t calculate_level_from_xp(uint64_t xp);
    uint64_t calculate_xp_for_level(uint32_t level);
    uint64_t calculate_xp_for_next_level(uint32_t current_level);
    
    // Leaderboard queries
    std::vector<LeaderboardEntry> get_global_xp_leaderboard(Database* db, int limit = 10);
    std::vector<LeaderboardEntry> get_server_xp_leaderboard(Database* db, uint64_t guild_id, int limit = 10);
    int get_user_global_xp_rank(Database* db, uint64_t user_id);
    int get_user_server_xp_rank(Database* db, uint64_t user_id, uint64_t guild_id);
}

} // namespace db
} // namespace bronx
