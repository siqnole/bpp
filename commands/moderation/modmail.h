#pragma once

#include "../../command.h"
#include "../../database/core/database.h"

namespace commands {
namespace moderation {

/**
 * @brief Returns the modmail command.
 * 
 * @param db Pointer to the database instance.
 */
Command* get_modmail_command(bronx::db::Database* db);

} // namespace moderation
} // namespace commands
