#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "quiet_moderation/antispam_api.h"
#include "quiet_moderation/url_guard.h"
#include "quiet_moderation/text_filter_config.h"
#include "quiet_moderation/reaction_filter.h"
#include "quiet_moderation/account_guard.h"
#include "quiet_moderation/avatar_guard.h"
#include "quiet_moderation/mutual_guard.h"
#include "quiet_moderation/nickname_guard.h"
#include "quiet_moderation/automod_commands.h"
#include <dpp/dpp.h>
#include <vector>

namespace commands {

// Get all quiet moderation commands
// NOTE: All moderation commands (manual + quiet + automod) are now consolidated in /mod parent
// This function returns empty since commands are handled in moderation_commands.h
inline ::std::vector<Command*> get_moderation_commands() {
    return {};
}

// Register all quiet moderation event handlers
inline void register_moderation_handlers(dpp::cluster& bot) {
    // Register antispam handler
    quiet_moderation::register_antispam(bot);
    
    // Register URL guard handler
    quiet_moderation::register_url_guard(bot);
    
    // Register text filter handler
    quiet_moderation::register_text_filter(bot);
    
    // Register reaction filter handler
    quiet_moderation::register_reaction_filter(bot);
}

// Get automod configuration commands
// NOTE: All moderation commands are now consolidated in /mod parent
// This function returns empty since the parent command handles everything
inline std::vector<Command*> get_automod_commands(bronx::db::Database* db) {
    return {};
}

// Register all automod guard event handlers
inline void register_automod_handlers(dpp::cluster& bot, bronx::db::Database* db) {
    quiet_moderation::register_account_guard(bot, db);
    quiet_moderation::register_avatar_guard(bot, db);
    quiet_moderation::register_mutual_guard(bot, db);
    quiet_moderation::register_nickname_guard(bot, db);
}

} // namespace commands
