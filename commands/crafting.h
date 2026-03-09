#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "crafting/crafting_helpers.h"
#include "crafting/craft.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

std::vector<Command*> get_crafting_commands(Database* db) {
    static std::vector<Command*> cmds;
    if (cmds.empty()) {
        cmds.push_back(commands::crafting::create_craft_command(db));
    }
    return cmds;
}

} // namespace commands
