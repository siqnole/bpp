#pragma once
#include "../command.h"
#include "social/heist.h"
#include <vector>

namespace commands {

// Aggregate all social/cooperative commands
inline std::vector<Command*> get_social_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::social::get_heist_command(db));
    }
    
    return cmds;
}

// Register social interaction handlers (heist buttons, etc.)
inline void register_social_interactions(dpp::cluster& bot, Database* db) {
    commands::social::register_heist_interactions(bot, db);
}

} // namespace commands
