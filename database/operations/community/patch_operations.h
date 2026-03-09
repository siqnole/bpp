#pragma once

#include "../../core/types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

namespace patch_operations {
    // Create a new patch note (version is auto-generated)
    bool create_patch_note(Database* db, const std::string& notes, uint64_t author_id);
    
    // Get the latest patch note
    std::optional<PatchNote> get_latest_patch(Database* db);
    
    // Get all patch notes (for pagination)
    std::vector<PatchNote> get_all_patches(Database* db, int limit = 50, int offset = 0);
    
    // Get total count of patches
    int get_patch_count(Database* db);
    
    // Get next version number (automatically increments patch version)
    std::string get_next_version(Database* db);
    
    // Delete a patch note by ID or version
    bool delete_patch_by_id(Database* db, uint32_t patch_id);
    bool delete_patch_by_version(Database* db, const std::string& version);
}

} // namespace db
} // namespace bronx
