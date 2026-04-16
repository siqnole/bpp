#pragma once
#include "../command.h"
#include "mining/mining_helpers.h"
#include "mining/mine.h"
#include "mining/inventory.h"
#include <vector>

namespace commands {

// Main entry point for all mining commands
inline std::vector<Command*> get_mining_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::mining::get_mine_command(db));
        cmds.push_back(commands::mining::get_minv_command(db));
        cmds.push_back(commands::mining::get_sellore_command(db));
    }
    
    return cmds;
}

// Register mining interaction handlers
inline void register_mining_interactions(dpp::cluster& bot, Database* db) {
    commands::mining::register_mining_interactions(bot, db);
    commands::mining::register_minv_interactions(bot, db);
}

} // namespace commands
