#pragma once

#include "../../core/types.h"
#include <vector>

namespace bronx {
namespace db {

// Forward declaration so header can refer to Database*
class Database;

// C-style wrappers for autopurge persistence
namespace autopurge_operations {
    // add a new autopurge entry, returns new row ID (0 on failure)
    uint64_t add_autopurge(Database* db, uint64_t user_id, uint64_t guild_id, uint64_t channel_id,
                           int interval_seconds, int message_limit);
    // remove an autopurge; only the owning user may delete
    bool remove_autopurge(Database* db, uint64_t autopurge_id, uint64_t user_id);
    std::vector<AutopurgeRow> get_all_autopurges(Database* db);
    std::vector<AutopurgeRow> get_autopurges_for_user(Database* db, uint64_t user_id);
}

} // namespace db
} // namespace bronx
