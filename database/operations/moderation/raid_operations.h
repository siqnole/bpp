#pragma once
#include "../../core/database.h"
#include <string>
#include <cstdint>
#include <optional>

namespace bronx {
namespace db {

enum class JoinGateLevel : int {
    OFF = 0,
    LOW = 1,      // Account age check
    MEDIUM = 2,   // Join velocity check
    HIGH = 3,     // Lockdown (kick all)
    MAX = 4       // Extreme (ban all)
};

struct RaidSettings {
    uint64_t guild_id;
    JoinGateLevel join_gate_level = JoinGateLevel::OFF;
    int min_account_age_days = 1;
    int join_velocity_threshold = 10; // joins per minute
    std::optional<uint64_t> notify_channel_id;
    bool alert_on_raid = true;
};

namespace raid_operations {
    // Initialize the raid_settings table
    bool initialize_table(Database* db);

    // Get raid settings for a guild
    RaidSettings get_settings(Database* db, uint64_t guild_id);

    // Update raid settings for a guild
    bool update_settings(Database* db, const RaidSettings& settings);

    // Update specific fields
    bool set_gate_level(Database* db, uint64_t guild_id, JoinGateLevel level);
    bool set_notify_channel(Database* db, uint64_t guild_id, std::optional<uint64_t> channel_id);
}

} // namespace db
} // namespace bronx
