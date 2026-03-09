#pragma once

#include "../../core/types.h"
#include <optional>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

class Database;

// Server economy settings structure
struct GuildEconomySettings {
    uint64_t guild_id;
    std::string economy_mode;  // "global" or "server"
    
    // Starting values
    int64_t starting_wallet;
    int64_t starting_bank_limit;
    double default_interest_rate;
    
    // Cooldowns
    int daily_cooldown;
    int work_cooldown;
    int beg_cooldown;
    int rob_cooldown;
    int fish_cooldown;
    
    // Multipliers
    double work_multiplier;
    double gambling_multiplier;
    double fishing_multiplier;
    
    // Feature toggles
    bool allow_gambling;
    bool allow_fishing;
    bool allow_trading;
    bool allow_robbery;
    
    // Economy limits
    std::optional<int64_t> max_wallet;
    std::optional<int64_t> max_bank;
    std::optional<int64_t> max_networth;
    
    // Tax system
    bool enable_tax;
    double transaction_tax_percent;
};

// Server-specific user data
struct ServerUserData {
    uint64_t guild_id;
    uint64_t user_id;
    int64_t wallet;
    int64_t bank;
    int64_t bank_limit;
    double interest_rate;
    int interest_level;
    std::optional<std::chrono::system_clock::time_point> last_interest_claim;
    std::optional<std::chrono::system_clock::time_point> last_daily;
    std::optional<std::chrono::system_clock::time_point> last_work;
    std::optional<std::chrono::system_clock::time_point> last_beg;
    int64_t total_gambled;
    int64_t total_won;
    int64_t total_lost;
    int commands_used;
};

namespace server_economy_operations {
    // Guild economy settings operations
    bool create_guild_economy(Database* db, uint64_t guild_id);
    std::optional<GuildEconomySettings> get_guild_economy_settings(Database* db, uint64_t guild_id);
    bool set_economy_mode(Database* db, uint64_t guild_id, const std::string& mode);
    bool update_economy_settings(Database* db, uint64_t guild_id, const GuildEconomySettings& settings);
    
    // Check if guild uses server economy
    bool is_server_economy(Database* db, uint64_t guild_id);
    
    // Server user operations
    bool ensure_server_user_exists(Database* db, uint64_t guild_id, uint64_t user_id);
    std::optional<ServerUserData> get_server_user(Database* db, uint64_t guild_id, uint64_t user_id);
    int64_t get_server_wallet(Database* db, uint64_t guild_id, uint64_t user_id);
    int64_t get_server_bank(Database* db, uint64_t guild_id, uint64_t user_id);
    int64_t get_server_bank_limit(Database* db, uint64_t guild_id, uint64_t user_id);
    int64_t get_server_networth(Database* db, uint64_t guild_id, uint64_t user_id);
    std::optional<int64_t> update_server_wallet(Database* db, uint64_t guild_id, uint64_t user_id, int64_t amount);
    std::optional<int64_t> update_server_bank(Database* db, uint64_t guild_id, uint64_t user_id, int64_t amount);
    TransactionResult transfer_server_money(Database* db, uint64_t guild_id, uint64_t from_user, uint64_t to_user, int64_t amount);
    
    // Unified operations that check economy mode and route accordingly
    bool ensure_user_exists_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id);
    int64_t get_wallet_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id);
    int64_t get_bank_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id);
    int64_t get_networth_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id);
    std::optional<int64_t> update_wallet_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id, int64_t amount);
    std::optional<int64_t> update_bank_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id, int64_t amount);
}

} // namespace db
} // namespace bronx
