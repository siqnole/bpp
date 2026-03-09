#pragma once

#include "../../core/types.h" // definition of Suggestion
#include "../../core/database.h" // need full Database definition for wrappers
#include <vector>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {


namespace suggestion_operations {
    inline bool add_suggestion(Database* db, uint64_t user_id, const std::string& text, int64_t networth) {
        return db->add_suggestion(user_id, text, networth);
    }
    inline std::vector<::bronx::db::Suggestion> fetch_suggestions(Database* db, const std::string& order_clause) {
        return db->fetch_suggestions(order_clause);
    }
    inline bool mark_read(Database* db, uint64_t suggestion_id) {
        return db->mark_suggestion_read(suggestion_id);
    }
    inline bool delete_suggestion(Database* db, uint64_t suggestion_id) {
        return db->delete_suggestion(suggestion_id);
    }
}

} // namespace db
} // namespace bronx
