#pragma once
#include <dpp/dpp.h>
#include "../../database/core/database.h"
#include "../../database/core/types.h"

namespace bronx {
namespace moderation {

/**
 * @brief Logs a moderation action according to guild settings (quiet mode, etc.)
 * 
 * @param bot Reference to the dpp::cluster
 * @param db Pointer to the database instance
 * @param guild_id The ID of the guild where the action occurred
 * @param inf The infraction data to log
 * @param channel_id The origin channel ID (for public notice)
 */
void log_mod_action(dpp::cluster& bot, db::Database* db, uint64_t guild_id, 
                   const db::InfractionRow& inf, uint64_t channel_id = 0);

/**
 * @brief Formats a duration in seconds to a human-readable string (e.g. 1d 2h)
 */
std::string format_duration(uint32_t seconds);

/**
 * @brief Returns the aesthetic color for a given action type
 */
uint32_t get_action_color(const std::string& type);

/**
 * @brief Parse a Discord @mention or raw snowflake string into a user ID
 * 
 * Handles formats: <@123>, <@!123>, 123
 * @return The parsed user ID, or 0 on failure
 */
uint64_t parse_mention(const std::string& s);

} // namespace moderation
} // namespace bronx
