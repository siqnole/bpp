#pragma once
#include "../command.h"
#include "passive/fish_pond.h"
#include "passive/mining_claims.h"
#include "passive/commodity_market.h"
#include "passive/bank_interest.h"
#include <vector>

namespace commands {

// Aggregate all passive income commands
std::vector<Command*> get_passive_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::passive::get_pond_command(db));
        cmds.push_back(commands::passive::get_claim_command(db));
        cmds.push_back(commands::passive::get_market_overview_command(db));
        cmds.push_back(commands::passive::get_interest_command(db));
    }
    
    return cmds;
}

// Register passive income interaction handlers
inline void register_passive_interactions(dpp::cluster& bot, Database* db) {
    // Currently no button interactions for passive commands
    // (pond/claim/market/interest are all subcommand-based)
}

} // namespace commands
