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
#include "moderation/infraction_engine.h"
#include <dpp/dpp.h>
#include <vector>

namespace commands {

// Get all manual moderation commands
inline std::vector<Command*> get_manual_moderation_commands(bronx::db::Database* db) {
    return {
        moderation::get_timeout_command(db),
        moderation::get_mute_command(db),
        moderation::get_jail_command(db),
        moderation::get_kick_command(db),
        moderation::get_ban_command(db),
        moderation::get_warn_command(db),
        moderation::get_untimeout_command(db),
        moderation::get_unmute_command(db),
        moderation::get_unjail_command(db),
        moderation::get_unban_command(db),
        moderation::get_case_command(db),
        moderation::get_history_command(db),
        moderation::get_modstats_command(db),
        moderation::get_pardon_command(db),
        moderation::get_infractions_config_command(db),
        moderation::get_muterole_command(db),
        moderation::get_jailsetup_command(db),
        moderation::get_modlog_channel_command(db),
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
