#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "moderation/timeout.h"
#include "moderation/mute.h"
#include "moderation/jail.h"
#include "moderation/kick.h"
#include "moderation/ban.h"
#include "moderation/warn.h"
#include "moderation/untimeout.h"
#include "moderation/unmute.h"
#include "moderation/unjail.h"
#include "moderation/unban.h"
#include "moderation/case_cmd.h"
#include "moderation/history_cmd.h"
#include "moderation/modstats.h"
#include "moderation/pardon.h"
#include "moderation/infractions_config.h"
#include "moderation/muterole.h"
#include "moderation/jailsetup.h"
#include "moderation/modlog_channel.h"
#include "moderation/mod_parent.h"
#include <dpp/dpp.h>
#include <vector>

namespace commands {

// Get all moderation commands (consolidated into /mod parent)
inline std::vector<Command*> get_manual_moderation_commands(bronx::db::Database* db) {
    // Replace 18 individual commands with single /mod parent command
    return {
        moderation::create_moderation_parent_command(db),
    };
}

// Start the infraction expiry sweep timer (call once at bot ready)
inline dpp::timer start_infraction_expiry_sweep(dpp::cluster& bot, bronx::db::Database* db) {
    return moderation::start_expiry_sweep(bot, db);
}

// Restore active punishment timers from DB (call once at bot ready)
inline void restore_infraction_timers(dpp::cluster& bot, bronx::db::Database* db) {
    moderation::restore_active_timers(bot, db);
}

} // namespace commands
