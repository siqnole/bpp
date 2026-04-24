#pragma once
#include "../../core/database.h"
#include "../../core/types.h"
#include <optional>
#include <string>

namespace bronx {
namespace db {

// Guild Profile Operations
std::optional<GuildProfile> get_guild_profile_internal(Database& db, uint64_t guild_id);
bool set_guild_profile_internal(Database& db, const GuildProfile& profile);
bool update_guild_profile_field_internal(Database& db, uint64_t guild_id, const std::string& field, const std::string& value);
bool clear_guild_profile_field_internal(Database& db, uint64_t guild_id, const std::string& field);

} // namespace db
} // namespace bronx
