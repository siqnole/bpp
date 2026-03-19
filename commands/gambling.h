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
#include "gambling/gamble_parent.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

::std::vector<Command*> get_gambling_commands(Database* db) {
    ::std::vector<Command*> cmds;
    
    // CONSOLIDATED: All gambling games now use the /gamble parent command with subcommands
    // This replaces the 11 individual commands (slots, coinflip, dice, frogger, roulette,
    // blackjack, lottery, minesweeper, crash, poker, jackpot) with a single command
    // that provides subcommands for each game.
    // Savings: 10 slash commands
    cmds.push_back(gambling::create_gamble_parent_command(db));
    
    // KEPT SEPARATE: Stats command remains as individual command (shows aggregate stats)
    cmds.push_back(gambling::get_stats_command(db));
    
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
