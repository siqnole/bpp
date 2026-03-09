#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// Cooldown operations extension for Database class
// These methods handle command cooldowns and timing restrictions
namespace cooldown_operations {
    bool is_on_cooldown(Database* db, uint64_t user_id, const std::string& command);
    bool set_cooldown(Database* db, uint64_t user_id, const std::string& command, int seconds);
    std::optional<std::chrono::system_clock::time_point> get_cooldown_expiry(Database* db, uint64_t user_id, const std::string& command);
    // Atomic cooldown claim - returns true if cooldown was successfully claimed (wasn't on cooldown)
    // Returns false if already on cooldown. This is race-condition safe.
    bool try_claim_cooldown(Database* db, uint64_t user_id, const std::string& command, int seconds);
}

} // namespace db
} // namespace bronx