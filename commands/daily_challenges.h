#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "daily_challenges/challenges.h"
#include "daily_challenges/streaks.h"
#include <vector>

using namespace bronx::db;

namespace commands {

// Aggregate all daily challenge + streak commands
std::vector<Command*> get_daily_challenge_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::daily_challenges::create_challenges_command(db));
        cmds.push_back(commands::daily_challenges::create_streak_command(db));
    }
    
    return cmds;
}

// Register interaction handlers for daily challenges (button clicks)
inline void register_daily_challenge_interactions(dpp::cluster& bot, Database* db) {
    // Button handler for claiming challenge rewards
    // Handled via on_button_click in main.cpp
}

} // namespace commands
