#pragma once
#include "leaderboard/leaderboard_helpers.h"
#include "leaderboard/leaderboard_cmd.h"
#include "leaderboard/leaderboard_interactions.h"
#include "../database/core/database.h"
#include "../command.h"
#include <vector>

namespace commands {

inline ::std::vector<Command*> get_leaderboard_commands(bronx::db::Database* db) {
    return leaderboard::create_leaderboard_commands(db);
}

inline void register_leaderboard_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    leaderboard::register_interactions(bot, db);
}

} // namespace commands
