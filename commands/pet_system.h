#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "pets/pets.h"
#include <vector>

using namespace bronx::db;

namespace commands {

// Aggregate all pet system commands
inline std::vector<Command*> get_pet_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    if (cmds.empty()) {
        cmds.push_back(commands::pets::create_pet_command(db));
    }
    
    return cmds;
}

// Register pet interaction handlers
inline void register_pet_interactions(dpp::cluster& bot, Database* db) {
    // Pet system button handlers (adopt confirmation, feed, etc.)
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        if (custom_id.find("pet_") != 0) return;
        commands::pets::handle_pet_button(bot, event, db);
    });
}

} // namespace commands
