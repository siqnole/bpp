#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "../../core/database.h"

namespace bronx {
namespace db {
namespace infraction_operations {

// Create a new infraction with auto-assigned case_number
// Returns the created infraction or nullopt on failure
std::optional<InfractionRow> create_infraction(
    Database* db, uint64_t guild_id, uint64_t user_id, uint64_t moderator_id,
    const std::string& type, const std::string& reason, double points,
    uint32_t duration_seconds = 0, const std::string& metadata = "{}");

// Get a single infraction by guild + case number
std::optional<InfractionRow> get_infraction(Database* db, uint64_t guild_id, uint32_t case_number);

// Get infractions for a user in a guild (optionally only active ones)
std::vector<InfractionRow> get_user_infractions(
    Database* db, uint64_t guild_id, uint64_t user_id,
    bool active_only = false, int limit = 25, int offset = 0);

// Sum active points for a user within a lookback window (days)
double get_user_active_points(Database* db, uint64_t guild_id, uint64_t user_id, int within_days = 365);

// Pardon a single infraction
bool pardon_infraction(Database* db, uint64_t guild_id, uint32_t case_number,
                       uint64_t pardoned_by, const std::string& reason = "");

// Pardon all active infractions for a user, returns count pardoned
int bulk_pardon_user(Database* db, uint64_t guild_id, uint64_t user_id,
                     uint64_t pardoned_by, const std::string& reason = "");

// Mark expired infractions as inactive (called periodically)
int expire_infractions(Database* db);

// Get infractions administered by a specific moderator
std::vector<InfractionRow> get_moderator_actions(
    Database* db, uint64_t guild_id, uint64_t moderator_id,
    int limit = 25, int offset = 0);

// Get recent infractions for a guild with optional type filter
std::vector<InfractionRow> get_recent_infractions(
    Database* db, uint64_t guild_id, int limit = 25, int offset = 0,
    const std::string& type_filter = "");

// Get all active timed infractions (for timer restoration on bot restart)
std::vector<InfractionRow> get_active_timed_infractions(Database* db);

// Update the reason on an existing infraction
bool update_infraction_reason(Database* db, uint64_t guild_id, uint32_t case_number, const std::string& reason);

// Update the duration on an existing timed infraction
bool update_infraction_duration(Database* db, uint64_t guild_id, uint32_t case_number, uint32_t new_duration_seconds);

// Count infractions for a user
InfractionCounts count_infractions(Database* db, uint64_t guild_id, uint64_t user_id);

} // namespace infraction_operations
} // namespace db
} // namespace bronx
