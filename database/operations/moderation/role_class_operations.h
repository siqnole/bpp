#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "../../core/database.h"

namespace bronx {
namespace db {
namespace role_class_operations {

// CRUD for role classes
std::optional<RoleClass> create_role_class(Database* db, uint64_t guild_id,
    const std::string& name, int priority = 0, bool inherit_lower = false,
    const std::string& restrictions = "{}");
bool update_role_class(Database* db, uint32_t class_id,
    const std::string& name, int priority, bool inherit_lower,
    const std::string& restrictions);
bool delete_role_class(Database* db, uint32_t class_id);
std::vector<RoleClass> get_role_classes(Database* db, uint64_t guild_id);

// Role-to-class mapping
bool assign_role_to_class(Database* db, uint64_t guild_id, uint64_t role_id, uint32_t class_id);
bool remove_role_from_class(Database* db, uint64_t guild_id, uint64_t role_id);

// Get effective restrictions for a set of user roles
// Resolves class membership, merges restrictions respecting inherit_lower + priority
struct EffectiveRestrictions {
    std::vector<std::string> allowed_commands;
    std::vector<std::string> denied_commands;
    std::vector<std::string> allowed_modules;
    std::vector<std::string> denied_modules;
};
EffectiveRestrictions get_user_effective_restrictions(
    Database* db, uint64_t guild_id, const std::vector<uint64_t>& user_role_ids);

// Quick check: is a specific command allowed for the user's roles?
bool check_command_allowed(Database* db, uint64_t guild_id,
    const std::vector<uint64_t>& user_role_ids, const std::string& command_name);

} // namespace role_class_operations
} // namespace db
} // namespace bronx
