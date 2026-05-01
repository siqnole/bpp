#pragma once
#include "../command.h"
#include "utility/utility_helpers.h"
#include "utility/ping.h"
#include "utility/userinfo.h"
#include "utility/serverinfo.h"
#include "utility/simple_commands.h"
#include "utility/poll.h"
#include "utility/cleanup.h"
#include "utility/reactionrole.h"
#include "utility/autopurge.h"
#include "utility/prefix.h"
#include "utility/suggestion.h"
#include "utility/bugreport.h"
#include "utility/status.h"
#include "utility/autorole.h"
#include "utility/serverconfig.h"
#include "utility/role.h"
#include "utility/giveaways.h"
#include "utility/privacy.h"
#include "utility/settings.h"
#include "utility/snipe.h"
#ifdef HAVE_LIBCURL
#include "utility/steal.h"
#include "utility/media.h"
#include "utility/media_edit.h"
#endif
#include "utility/interaction_roles.h"
#include "utility/tickets.h"
#include <vector>

namespace commands {

// Main entry point for all utility commands
inline ::std::vector<Command*> get_utility_commands(CommandHandler* handler, bronx::db::Database* db = nullptr, bronx::snipe::SnipeCache* snipe_cache = nullptr) {
    static ::std::vector<Command*> cmds;
    
    // Only initialize once
    if (cmds.empty()) {
        cmds.push_back(utility::get_ping_command());
        cmds.push_back(utility::get_userinfo_command());
        cmds.push_back(utility::get_avatar_command());           // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_banner_command());           // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_invite_command());           // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_serveravatar_command());     // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_serverbanner_command());     // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_dcme_command());             // OPTIMIZATION Note: Text-only (slash removed)
        cmds.push_back(utility::get_say_command());
        cmds.push_back(utility::get_serverinfo_command(db));
        cmds.push_back(utility::get_poll_command());
        cmds.push_back(utility::get_cleanup_command());
        cmds.push_back(utility::get_reactionrole_command());
        cmds.push_back(utility::get_rrsync_command());
        cmds.push_back(utility::get_autopurge_command());
        cmds.push_back(utility::get_prefix_command());
        // status commands require handler pointer
        cmds.push_back(utility::get_modules_command(handler, db));
        cmds.push_back(utility::get_commands_status_command(handler, db));
        if (db) {
            cmds.push_back(utility::get_suggestion_command(db));
            cmds.push_back(utility::get_settings_command(db));
            cmds.push_back(utility::get_serverconfig_command(db));
        }
        cmds.push_back(utility::get_bugreport_command());
        cmds.push_back(utility::get_autorole_command());
        cmds.push_back(utility::get_role_command());
#ifdef HAVE_LIBCURL
        cmds.push_back(utility::get_steal_command());           // OPTIMIZATION Note: Text-only (slash removed)
        // Media commands
        cmds.push_back(utility::get_ocr_command());
        cmds.push_back(utility::get_transcribe_command());
        cmds.push_back(utility::get_gif_command());
        cmds.push_back(utility::get_download_command());
        // Media edit commands
        for (auto* cmd : utility::get_media_edit_commands()) {
            cmds.push_back(cmd);
        }
#endif
        // Privacy command (requires db)
        if (db) {
            cmds.push_back(utility::get_privacy_command(db));   // OPTIMIZATION Note: Text-only (slash removed)
        }
        // Snipe command (requires snipe cache)
        if (snipe_cache) {
            cmds.push_back(utility::get_snipe_command(snipe_cache)); // OPTIMIZATION Note: Text-only (slash removed)
        }
        // Giveaway commands (require db)
        if (db) {
            auto giveaway_cmds = utility::get_giveaway_commands(db);
            for (auto* cmd : giveaway_cmds) {
                cmds.push_back(cmd);
            }
            
            cmds.push_back(utility::get_interaction_role_command(db));
            cmds.push_back(utility::get_ticket_setup_command(db));
        }
    }
    
    return cmds;
}

// Register utility interaction handlers
inline void register_utility_interactions(dpp::cluster& bot, bronx::db::Database* db = nullptr, bronx::snipe::SnipeCache* snipe_cache = nullptr) {
    utility::register_poll_interactions(bot);
    utility::register_reactionrole_interactions(bot);
    utility::register_status_interactions(bot);
    utility::register_bugreport_interactions(bot);
    if (db) {
        utility::register_giveaway_interactions(bot, db);
        utility::register_privacy_interactions(bot, db);
        utility::register_interaction_role_interactions(bot, db);
        utility::register_ticket_interactions(bot, db);
    }
    if (snipe_cache) {
        utility::register_snipe_interactions(bot, snipe_cache);
    }
}

// Register autorole event handler (call after bot is ready)
inline void register_autorole_handler(dpp::cluster& bot) {
    bot.on_guild_member_add([&bot](const dpp::guild_member_add_t& event) {
        utility::handle_autorole_member_join(bot, event);
    });
}

// Re-export helper functions for backward compatibility
using utility::get_build_version;
using utility::create_progress_bar;

} // namespace commands
