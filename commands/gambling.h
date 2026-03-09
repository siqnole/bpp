#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "gambling/slots.h"
#include "gambling/coinflip.h"
#include "gambling/dice.h"
#include "gambling/frogger.h"
#include "gambling/roulette.h"
#include "gambling/blackjack.h"
#include "gambling/lottery.h"
#include "gambling/minesweeper.h"
#include "gambling/stats.h"
#include "gambling/gambling_interactions.h"
#include "gambling/crash.h"
#include "gambling/poker.h"
#include "gambling/jackpot.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

::std::vector<Command*> get_gambling_commands(Database* db) {
    ::std::vector<Command*> cmds;
    
    cmds.push_back(gambling::get_slots_command(db));
    cmds.push_back(gambling::get_coinflip_command(db));
    cmds.push_back(gambling::get_dice_command(db));
    cmds.push_back(gambling::get_frogger_command(db));
    cmds.push_back(gambling::get_roulette_command(db));
    cmds.push_back(gambling::get_blackjack_command(db));
    cmds.push_back(gambling::get_lottery_command(db));
    cmds.push_back(gambling::get_minesweeper_command(db));
    cmds.push_back(gambling::get_stats_command(db));
    cmds.push_back(gambling::get_crash_command(db));
    cmds.push_back(gambling::get_poker_command(db));
    cmds.push_back(gambling::get_jackpot_command(db));
    
    return cmds;
}

// Register gambling interactions (frogger, roulette, blackjack, minesweeper)
inline void register_gambling_interactions(dpp::cluster& bot, Database* db) {
    gambling::register_gambling_interactions(bot, db);
    gambling::register_roulette_blackjack_interactions(bot, db);
    gambling::register_minesweeper_interactions(bot, db);
    gambling::register_crash_interactions(bot, db);
    gambling::register_poker_interactions(bot, db);
}

} // namespace commands
