#pragma once
#include "../command.h"
#include "../database/core/database.h"
#include "economy_core.h"
#include "economy/money.h"
#include "gambling.h"
#include "fishing.h"
#include "economy/trading.h"
#include "economy/bazaar.h"
#include "economy/market.h"
#include "economy/shop.h"
#include "economy/rob.h"
#include "fishing/autofisher.h"
#include "economy/achievements_cmd.h"
#include "economy/support_server.h"
#include "economy/use.h"
#include "crafting.h"
#include "mastery.h"
#include "daily_challenges.h"
#include "skill_trees.h"
#include "pet_system.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {

// Aggregate all economy-related commands
::std::vector<Command*> get_economy_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Get commands from each category
    auto core = get_economy_core_commands(db);
    auto money = get_money_commands(db);
    auto gambling = get_gambling_commands(db);
    auto fishing = get_fishing_commands(db);
    auto trading = get_trading_commands(db);
    auto bazaar = get_bazaar_commands(db);
    auto shop = get_shop_commands(db);
    auto market = get_market_commands(db);
    auto rob = get_rob_commands(db);
    auto autofisher = get_autofisher_commands(db);
    auto titles = get_title_commands(db);
    auto support = support_server::get_support_server_commands(db);
    auto use_cmds = use_item::get_use_commands(db);
    auto crafting = get_crafting_commands(db);
    auto mastery_cmds = get_mastery_commands(db);
    auto daily_challenge_cmds = get_daily_challenge_commands(db);
    auto skill_tree_cmds = get_skill_tree_commands(db);
    auto pet_cmds = get_pet_commands(db);
    
    // Combine all commands
    cmds.insert(cmds.end(), core.begin(), core.end());
    cmds.insert(cmds.end(), money.begin(), money.end());
    cmds.insert(cmds.end(), gambling.begin(), gambling.end());
    cmds.insert(cmds.end(), fishing.begin(), fishing.end());
    cmds.insert(cmds.end(), trading.begin(), trading.end());
    cmds.insert(cmds.end(), bazaar.begin(), bazaar.end());
    cmds.insert(cmds.end(), market.begin(), market.end());
    cmds.insert(cmds.end(), shop.begin(), shop.end());
    cmds.insert(cmds.end(), rob.begin(), rob.end());
    cmds.insert(cmds.end(), autofisher.begin(), autofisher.end());
    cmds.insert(cmds.end(), titles.begin(), titles.end());
    cmds.insert(cmds.end(), support.begin(), support.end());
    cmds.insert(cmds.end(), use_cmds.begin(), use_cmds.end());
    cmds.insert(cmds.end(), crafting.begin(), crafting.end());
    cmds.insert(cmds.end(), mastery_cmds.begin(), mastery_cmds.end());
    cmds.insert(cmds.end(), daily_challenge_cmds.begin(), daily_challenge_cmds.end());
    cmds.insert(cmds.end(), skill_tree_cmds.begin(), skill_tree_cmds.end());
    cmds.insert(cmds.end(), pet_cmds.begin(), pet_cmds.end());
    
    // Add achievements command
    cmds.push_back(get_achievements_command(db));
    
    return cmds;
}

// Register economy interaction handlers (buttons, modals, etc.)
inline void register_economy_handlers(dpp::cluster& bot, Database* db) {
    economy::register_bank_handlers(bot, db);
    register_daily_challenge_interactions(bot, db);
    register_skill_tree_interactions(bot, db);
    register_pet_interactions(bot, db);
}

} // namespace commands
