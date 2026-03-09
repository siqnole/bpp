#pragma once

#include "../../core/types.h"
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace bronx {
namespace db {

class Database;


namespace market_operations {
    std::vector<MarketItem> get_market_items(Database* db, uint64_t guild_id);
    std::optional<MarketItem> get_market_item(Database* db, uint64_t guild_id, const std::string& item_id);
    bool create_market_item(Database* db, const MarketItem& item);
    bool update_market_item(Database* db, const MarketItem& item);
    bool delete_market_item(Database* db, uint64_t guild_id, const std::string& item_id);

    // adjust quantity by delta (negative to decrement).  Returns false if update failed
    bool adjust_market_item_quantity(Database* db, uint64_t guild_id, const std::string& item_id, int delta);
}

} // namespace db
} // namespace bronx
