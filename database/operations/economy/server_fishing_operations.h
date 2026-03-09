#pragma once

#include "../../core/types.h"
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

class Database;

// Server-specific fish catch
struct ServerFishCatch {
    uint64_t id;
    uint64_t guild_id;
    uint64_t user_id;
    std::string rarity;
    std::string fish_name;
    double weight;
    int64_t value;
    std::chrono::system_clock::time_point caught_at;
    bool sold;
    std::string rod_id;
    std::string bait_id;
};

namespace server_fishing_operations {
    // Create fish catch in server economy
    bool create_server_fish_catch(Database* db, uint64_t guild_id, uint64_t user_id,
                                  const std::string& rarity, const std::string& fish_name,
                                  double weight, int64_t value,
                                  const std::string& rod_id, const std::string& bait_id);
    
    // Get user's unsold fish in server economy
    std::vector<ServerFishCatch> get_server_unsold_fish(Database* db, uint64_t guild_id, uint64_t user_id);
    
    // Sell all unsold fish in server economy
    int64_t sell_all_server_fish(Database* db, uint64_t guild_id, uint64_t user_id);
    
    // Get/set active fishing gear in server economy
    std::pair<std::string, std::string> get_server_active_gear(Database* db, uint64_t guild_id, uint64_t user_id);
    bool set_server_active_rod(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& rod_id);
    bool set_server_active_bait(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& bait_id);
    
    // Server inventory operations for fishing
    bool add_server_inventory_item(Database* db, uint64_t guild_id, uint64_t user_id,
                                   const std::string& item_id, const std::string& item_type,
                                   int quantity, int level);
    bool remove_server_inventory_item(Database* db, uint64_t guild_id, uint64_t user_id,
                                      const std::string& item_id, int quantity);
    int get_server_item_quantity(Database* db, uint64_t guild_id, uint64_t user_id,
                                 const std::string& item_id);
    
    // Unified fishing operations that check economy mode
    bool create_fish_catch_unified(Database* db, std::optional<uint64_t> guild_id, uint64_t user_id,
                                   const std::string& rarity, const std::string& fish_name,
                                   double weight, int64_t value,
                                   const std::string& rod_id, const std::string& bait_id);
    
    int64_t sell_all_fish_unified(Database* db, std::optional<uint64_t> guild_id, uint64_t user_id);
    
    std::pair<std::string, std::string> get_active_gear_unified(Database* db, 
                                                                std::optional<uint64_t> guild_id, 
                                                                uint64_t user_id);
    
    bool set_active_rod_unified(Database* db, std::optional<uint64_t> guild_id, 
                               uint64_t user_id, const std::string& rod_id);
    
    bool set_active_bait_unified(Database* db, std::optional<uint64_t> guild_id,
                                uint64_t user_id, const std::string& bait_id);
}

} // namespace db
} // namespace bronx
