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
    // Channel blacklist
    bool add_channel_blacklist(Database* db, uint64_t guild_id, uint64_t channel_id, uint64_t added_by, const std::string& reason = "");
    bool remove_channel_blacklist(Database* db, uint64_t guild_id, uint64_t channel_id);
    bool is_channel_blacklisted(Database* db, uint64_t guild_id, uint64_t channel_id);
    std::vector<XPBlacklistChannel> get_blacklisted_channels(Database* db, uint64_t guild_id);
    
    // Role blacklist
    bool add_role_blacklist(Database* db, uint64_t guild_id, uint64_t role_id, uint64_t added_by, const std::string& reason = "");
    bool remove_role_blacklist(Database* db, uint64_t guild_id, uint64_t role_id);
    bool is_role_blacklisted(Database* db, uint64_t guild_id, uint64_t role_id);
    std::vector<XPBlacklistRole> get_blacklisted_roles(Database* db, uint64_t guild_id);
    
    // User blacklist
    bool add_user_blacklist(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t added_by, const std::string& reason = "");
    bool remove_user_blacklist(Database* db, uint64_t guild_id, uint64_t user_id);
    bool is_user_blacklisted(Database* db, uint64_t guild_id, uint64_t user_id);
    std::vector<XPBlacklistUser> get_blacklisted_users(Database* db, uint64_t guild_id);
    
    // Combined check - returns true if XP should be blocked
    bool should_block_xp(Database* db, uint64_t guild_id, uint64_t channel_id, uint64_t user_id, const std::vector<uint64_t>& user_role_ids);
}

} // namespace db
} // namespace bronx
