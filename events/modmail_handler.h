#pragma once

#include <dpp/dpp.h>
#include "../database/core/database.h"

namespace bronx {
namespace events {

/**
 * @brief Registers the modmail event handlers (on_message_create).
 * 
 * @param bot Reference to the bot cluster.
 * @param db Pointer to the database instance.
 */
void register_modmail_handlers(dpp::cluster& bot, db::Database* db);

} // namespace events
} // namespace bronx
