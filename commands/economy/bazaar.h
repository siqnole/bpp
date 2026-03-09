#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

::std::vector<Command*> get_bazaar_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // TODO: Implement bazaar commands
    
    return cmds;
}

} // namespace commands
