#pragma once

#include "../../core/types.h"
#include <vector>

namespace bronx {
namespace db {

// Forward declaration so header can refer to Database*
class Database;

// Optional C-style wrappers for reaction-role persistence
namespace reaction_role_operations {
    bool add_reaction_role(Database* db, uint64_t guild_id, uint64_t message_id, uint64_t channel_id, const std::string& emoji_raw, uint64_t emoji_id, uint64_t role_id);
    bool remove_reaction_role(Database* db, uint64_t message_id, const std::string& emoji_raw);
    std::vector<ReactionRoleRow> get_all_reaction_roles(Database* db);
}

} // namespace db
} // namespace bronx
