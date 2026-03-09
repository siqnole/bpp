#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "mastery/mastery_helpers.h"
#include "mastery/mastery.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

std::vector<Command*> get_mastery_commands(Database* db) {
    static std::vector<Command*> cmds;
    if (cmds.empty()) {
        cmds.push_back(commands::mastery::create_mastery_command(db));
    }
    return cmds;
}

} // namespace commands
