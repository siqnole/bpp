#pragma once

#include "../../core/types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bronx {
namespace db {

// Forward declaration
class Database;

namespace xp_blacklist_operations {
    // Consolidated blacklist (target_type = "channel", "role", or "user")
    bool add_blacklist(Database* db, uint64_t guild_id, const std::string& target_type, uint64_t target_id, uint64_t added_by, const std::string& reason = "");
    bool remove_blacklist(Database* db, uint64_t guild_id, const std::string& target_type, uint64_t target_id);
    bool is_blacklisted(Database* db, uint64_t guild_id, const std::string& target_type, uint64_t target_id);
    std::vector<XPBlacklistEntry> get_blacklist(Database* db, uint64_t guild_id, const std::string& target_type = "");
    
    // Combined check - returns true if XP should be blocked
    bool should_block_xp(Database* db, uint64_t guild_id, uint64_t channel_id, uint64_t user_id, const std::vector<uint64_t>& user_role_ids);
}

} // namespace db
} // namespace bronx
