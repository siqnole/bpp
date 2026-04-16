#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "skill_tree/skill_tree.h"
#include <vector>

using namespace bronx::db;

namespace commands {

// Aggregate all skill tree commands
inline std::vector<Command*> get_skill_tree_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::skill_tree::create_skill_tree_command(db));
    }
    
    return cmds;
}

// Register skill tree interaction handlers (button-based skill selection)
inline void register_skill_tree_interactions(dpp::cluster& bot, Database* db) {
    // Skill tree uses button interactions for investing skill points
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        
        // Only handle skill tree buttons
        if (custom_id.find("skill_") != 0) return;
        
        commands::skill_tree::handle_skill_button(bot, event, db);
    });
}

} // namespace commands
