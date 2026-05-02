#pragma once

#include "../../core/types.h"
#include "../../core/database.h"
#include <map>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

struct UserSkillPointRow {
    std::string skill_id;
    int rank;
};

namespace skill_operations {
    // Table initialization (idempotent)
    bool ensure_tables(Database* db);

    // Retrieval
    std::map<std::string, int> get_user_skills(Database* db, uint64_t user_id);
    
    // Modification
    bool invest_skill_point(Database* db, uint64_t user_id, const std::string& skill_id);
    bool reset_all_skills(Database* db, uint64_t user_id);
}

} // namespace db
} // namespace bronx
