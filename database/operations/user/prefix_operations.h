#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration so header can refer to Database*
class Database;

namespace prefix_operations {
    // user-specific prefixes
    bool add_user_prefix(Database* db, uint64_t user_id, const std::string& prefix);
    bool remove_user_prefix(Database* db, uint64_t user_id, const std::string& prefix);
    std::vector<std::string> get_user_prefixes(Database* db, uint64_t user_id);

    // guild-specific prefixes
    bool add_guild_prefix(Database* db, uint64_t guild_id, const std::string& prefix);
    bool remove_guild_prefix(Database* db, uint64_t guild_id, const std::string& prefix);
    std::vector<std::string> get_guild_prefixes(Database* db, uint64_t guild_id);
}

} // namespace db
} // namespace bronx
