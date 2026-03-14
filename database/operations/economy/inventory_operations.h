#pragma once

#include "../../core/types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// Inventory operations extension for Database class
// These methods handle user item management and inventory tracking
namespace inventory_operations {
    // `level` parameter added to record item level when items are created or purchased
    bool add_item(Database* db, uint64_t user_id, const std::string& item_id, const std::string& item_type, int quantity, const std::string& metadata = "", int level = 1, uint64_t guild_id = 0);
    bool has_item(Database* db, uint64_t user_id, const std::string& item_id, int quantity = 1, uint64_t guild_id = 0);
    int get_item_quantity(Database* db, uint64_t user_id, const std::string& item_id, uint64_t guild_id = 0);
    std::vector<InventoryItem> get_inventory(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    bool remove_item(Database* db, uint64_t user_id, const std::string& item_id, int quantity, uint64_t guild_id = 0);
    bool update_item_metadata(Database* db, uint64_t user_id, const std::string& item_id, const std::string& new_metadata, uint64_t guild_id = 0);
}

} // namespace db
} // namespace bronx