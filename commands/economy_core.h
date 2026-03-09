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
        cmds.push_back(economy::create_balance_command(db));
        cmds.push_back(economy::create_bank_command(db));
        cmds.push_back(economy::create_withdraw_command(db));
        cmds.push_back(economy::create_pay_command(db));
        cmds.push_back(economy::create_prestige_command(db));
        cmds.push_back(economy::create_rebirth_command(db));
        initialized = true;
    }
    
    return cmds;
}

} // namespace commands
