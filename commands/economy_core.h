#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/economy/history_operations.h"
#include "economy/helpers.h"
#include "economy/balance.h"
#include "economy/bank.h"
#include "economy/withdraw.h"
#include "economy/pay.h"
#include "economy/prestige.h"
#include "economy/rebirth.h"
#include "economy/money_parent.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {

// Re-export helper functions for backward compatibility
using economy::parse_amount;
using economy::format_number;

inline ::std::vector<Command*> get_economy_core_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    static bool initialized = false;
    
    if (!initialized) {
        // Replace 10 individual commands (balance, bank, withdraw, pay, prestige, rebirth, daily, weekly, work, rob)
        // with single /money parent command
        cmds.push_back(economy::create_money_parent_command(db));
        initialized = true;
    }
    
    return cmds;
}

} // namespace commands
