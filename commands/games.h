#pragma once
#include "command.h"
#include "games/blacktea.h"
#include "games/tictactoe.h"
#include "games/react.h"
#include "games/heist.h"
#include "database/core/database.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

::std::vector<Command*> get_games_commands(Database* db) {
    ::std::vector<Command*> cmds;
    cmds.push_back(games::get_blacktea_command());
    cmds.push_back(games::get_tictactoe_command(db));
    cmds.push_back(games::get_react_command(db));
    cmds.push_back(games::get_heist_command(db));
    return cmds;
}

// Register message/reaction handlers for games
inline void register_games_handlers(dpp::cluster& bot, Database* db) {
    games::register_blacktea_handlers(bot);
    games::register_tictactoe_handlers(bot, db);
    games::register_react_handlers(bot, db);
    games::register_heist_handlers(bot, db);
}

} // namespace commands
