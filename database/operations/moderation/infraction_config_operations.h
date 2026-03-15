#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "../../core/database.h"

namespace bronx {
namespace db {
namespace infraction_config_operations {

// Get infraction config (returns defaults if no row exists)
std::optional<InfractionConfig> get_infraction_config(Database* db, uint64_t guild_id);

// Upsert infraction config
bool upsert_infraction_config(Database* db, const InfractionConfig& config);

// Get automod config (returns defaults if no row exists)
std::optional<AutomodConfig> get_automod_config(Database* db, uint64_t guild_id);

// Upsert automod config
bool upsert_automod_config(Database* db, const AutomodConfig& config);

} // namespace infraction_config_operations
} // namespace db
} // namespace bronx
