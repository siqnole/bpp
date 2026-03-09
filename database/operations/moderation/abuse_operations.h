#pragma once

#include "../../core/types.h"
#include <vector>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {

// Forward declaration so header can refer to Database*
class Database;

namespace abuse_operations {
    // Global blacklist helpers
    bool add_global_blacklist(Database* db, uint64_t user_id, const std::string& reason = "");
    bool remove_global_blacklist(Database* db, uint64_t user_id);
    bool is_global_blacklisted(Database* db, uint64_t user_id);
    std::vector<BlacklistEntry> get_global_blacklist(Database* db);

    // Global whitelist helpers
    bool add_global_whitelist(Database* db, uint64_t user_id, const std::string& reason = "");
    bool remove_global_whitelist(Database* db, uint64_t user_id);
    bool is_global_whitelisted(Database* db, uint64_t user_id);
    std::vector<WhitelistEntry> get_global_whitelist(Database* db);
}

} // namespace db
} // namespace bronx
