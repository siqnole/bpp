#pragma once
#include "../command.h"
#include "passive/passive_parent.h"
#include <vector>

namespace commands {

// Aggregate all passive income commands
inline std::vector<Command*> get_passive_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        // Replace 4 individual commands (pond, claim, market, interest)
        // with single /passive parent command
        cmds.push_back(passive::create_passive_parent_command(db));
    }
    
    return cmds;
}

// Register passive income interaction handlers
inline void register_passive_interactions(dpp::cluster& bot, Database* db) {
    // Currently no button interactions for passive commands
    // (pond/claim/market/interest are all subcommand-based)
}

} // namespace commands
