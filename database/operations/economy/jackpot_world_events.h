#pragma once

#include "../../core/types.h"
#include <optional>
#include <vector>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// JackpotData, JackpotHistoryEntry, and WorldEventData are declared in types.h

namespace jackpot_operations {
    // Get the current jackpot pool amount
    int64_t get_jackpot_pool(Database* db);

    // Get full jackpot data
    std::optional<JackpotData> get_jackpot(Database* db);

    // Add to the jackpot pool (called on gambling losses)
    bool contribute_to_jackpot(Database* db, int64_t amount);

    // Attempt to win the jackpot — returns the amount won (0 if no win).
    // Automatically resets the pool and records history on win.
    int64_t try_win_jackpot(Database* db, uint64_t user_id);

    // Get recent jackpot wins
    std::vector<JackpotHistoryEntry> get_jackpot_history(Database* db, int limit = 10);
}

namespace world_event_operations {
    // Get the currently active event (if any)
    std::optional<WorldEventData> get_active_event(Database* db);

    // Start a new world event
    bool start_event(Database* db, const std::string& event_type, const std::string& event_name,
                     const std::string& description, const std::string& emoji,
                     const std::string& bonus_type, double bonus_value, int duration_minutes);

    // End the currently active event
    bool end_active_event(Database* db);

    // Expire events whose end time has passed
    int expire_events(Database* db);

    // Get bonus value for a specific bonus type from the active event (0.0 if no event or wrong type)
    double get_active_bonus(Database* db, const std::string& bonus_type);

    // Get recent event history
    std::vector<WorldEventData> get_event_history(Database* db, int limit = 10);
}

} // namespace db
} // namespace bronx
