#pragma once

#include "../../core/types.h"
#include "../../core/database.h"
#include <vector>
#include <cstdint>
#include <string>
#include <optional>

namespace bronx {
namespace db {

struct UserPetRow {
    int64_t id;
    uint64_t user_id;
    std::string species_id;
    std::string nickname;
    int level;
    int xp;
    int hunger;
    bool equipped;
    std::string adopted_at;
    std::string last_fed;
};

namespace pet_operations {
    // Table initialization (idempotent)
    bool ensure_tables(Database* db);

    // CRUD operations
    std::vector<UserPetRow> get_user_pets(Database* db, uint64_t user_id);
    bool adopt_pet(Database* db, uint64_t user_id, const std::string& species_id, const std::string& nickname, bool equipped);
    bool feed_pet(Database* db, int64_t pet_id);
    bool equip_pet(Database* db, uint64_t user_id, int64_t pet_id);
    bool release_pet(Database* db, uint64_t user_id, int64_t pet_id);
    bool rename_pet(Database* db, int64_t pet_id, const std::string& new_name);
    
    // XP and leveling
    bool update_pet_stats(Database* db, int64_t pet_id, int new_level, int new_xp);
    
    // Existence checks
    int count_user_pets(Database* db, uint64_t user_id);
}

} // namespace db
} // namespace bronx
