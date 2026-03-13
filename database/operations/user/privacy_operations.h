#pragma once

#include "../../core/types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace bronx {
namespace db {

// Forward declaration
class Database;

namespace privacy_operations {
    // Opt-out management
    bool set_opted_out(Database* db, uint64_t user_id, bool opted_out);
    bool is_opted_out(Database* db, uint64_t user_id);

    // Bulk delete all user data across tables when they opt out
    bool delete_all_user_data(Database* db, uint64_t user_id);

    // Encrypted identity cache (30-day TTL)
    bool cache_encrypted_identity(Database* db, uint64_t user_id,
                                  const std::string& username,
                                  const std::string& nickname,
                                  const std::string& avatar_hash);
    
    struct DecryptedIdentity {
        std::string username;
        std::string nickname;
        std::string avatar_hash;
    };
    std::optional<DecryptedIdentity> get_cached_identity(Database* db, uint64_t user_id);
    
    // Purge expired identity cache entries (called periodically)
    int purge_expired_identities(Database* db);

    // Get list of all opted-out user IDs (for hot-path caching)
    std::vector<uint64_t> get_all_opted_out_users(Database* db);
}

} // namespace db
} // namespace bronx
