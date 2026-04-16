#pragma once

#include "../../core/types.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {

class Database;

namespace logging_operations {
    // Set or update a logging webhook config
    bool set_log_config(Database* db, const LogConfig& config);
    
    // Retrieve a specific log config
    std::optional<LogConfig> get_log_config(Database* db, uint64_t guild_id, const std::string& log_type);
    
    // Get all configured logs for a guild
    std::vector<LogConfig> get_all_log_configs(Database* db, uint64_t guild_id);
    
    // Delete a specific log config
    bool delete_log_config(Database* db, uint64_t guild_id, const std::string& log_type);
    
    // Delete all log configs for a guild (e.g. for complete AIO wipe/reset)
    bool clear_all_log_configs(Database* db, uint64_t guild_id);
    
    // Beta Testing Access
    bool is_guild_beta_tester(Database* db, uint64_t guild_id);
    bool set_guild_beta_tester(Database* db, uint64_t guild_id, bool is_beta);
}

} // namespace db
} // namespace bronx
