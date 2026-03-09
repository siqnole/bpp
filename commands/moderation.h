#pragma once
#include "../command.h"
#include "quiet_moderation/antispam_api.h"
#include "quiet_moderation/url_guard.h"
#include "quiet_moderation/text_filter_config.h"
#include "quiet_moderation/reaction_filter.h"
#include <dpp/dpp.h>
#include <vector>

namespace commands {

// Get all quiet moderation commands
::std::vector<Command*> get_moderation_commands() {
    ::std::vector<Command*> cmds;
    
    cmds.push_back(quiet_moderation::get_antispam_command());
    cmds.push_back(quiet_moderation::get_url_guard_command());
    cmds.push_back(quiet_moderation::get_text_filter_command());
    cmds.push_back(quiet_moderation::get_reaction_filter_command());
    
    return cmds;
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

} // namespace commands
