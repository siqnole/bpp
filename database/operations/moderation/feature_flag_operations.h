#pragma once

#include "../../core/database.h"
#include <vector>
#include <tuple>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {
namespace feature_flag_operations {

    // Set or update a feature flag
    bool set_feature_flag(Database* db, const std::string& feature, const std::string& mode, const std::string& reason = "");

    // Get all feature flags as (name, mode, reason) tuples
    std::vector<std::tuple<std::string, std::string, std::string>> get_all_feature_flags(Database* db);

    // Delete a feature flag
    bool delete_feature_flag(Database* db, const std::string& feature);

    // Whitelist operations
    bool add_whitelist(Database* db, const std::string& feature, uint64_t guild_id);
    bool remove_whitelist(Database* db, const std::string& feature, uint64_t guild_id);
    std::vector<std::pair<std::string, uint64_t>> get_all_whitelists(Database* db);

} // namespace feature_flag_operations
} // namespace db
} // namespace bronx
