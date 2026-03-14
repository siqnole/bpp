#pragma once

#include "../../core/types.h"
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

class Database;

// NOTE: ServerFishCatch is now replaced by FishCatch with guild_id field.
// The "server_fishing_operations" namespace is retained for backward compat
// but all underlying queries hit the unified user_fish_catches table.

namespace server_fishing_operations {
    // Fish catch operations (guild_id selects server economy)
    bool create_fish_catch(Database* db, uint64_t guild_id, uint64_t user_id,
                           const std::string& rarity, const std::string& fish_name,
                           double weight, int64_t value,
                           const std::string& rod_id, const std::string& bait_id);
    
    std::vector<FishCatch> get_unsold_fish(Database* db, uint64_t guild_id, uint64_t user_id);
    int64_t sell_all_fish(Database* db, uint64_t guild_id, uint64_t user_id);
    
    // Active fishing gear (per guild)
    std::pair<std::string, std::string> get_active_gear(Database* db, uint64_t guild_id, uint64_t user_id);
    bool set_active_rod(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& rod_id);
    bool set_active_bait(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& bait_id);
    
    // Server inventory (delegates to unified user_inventory with guild_id)
    bool add_inventory_item(Database* db, uint64_t guild_id, uint64_t user_id,
                            const std::string& item_id, const std::string& item_type,
                            int quantity, int level);
    bool remove_inventory_item(Database* db, uint64_t guild_id, uint64_t user_id,
                               const std::string& item_id, int quantity);
    int get_item_quantity(Database* db, uint64_t guild_id, uint64_t user_id,
                          const std::string& item_id);
}

} // namespace db
} // namespace bronx
