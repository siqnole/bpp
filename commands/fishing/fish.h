#pragma once

// ============================================================================
// fish.h — DECLARATIONS ONLY
// All heavy implementations moved to fish.cpp to optimize compilation times.
// ============================================================================

#include "../../command.h"
#include "../../database/core/database.h"
#include "fishing_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <unordered_map>
#include <string>

using namespace bronx::db;

namespace commands {
namespace fishing {

// how long the fishing command remains on cooldown (seconds)
static const int FISH_COOLDOWN_SECONDS = 30;

// Get cooldown adjusted by Quick Hands skill (fish_cooldown_reduction)
int get_adjusted_fish_cooldown(Database* db, uint64_t uid, int base_cd);

// pagination state stored per-user
struct FishReceiptState {
    std::string header;
    std::string footer;
    std::vector<CatchInfo> log;
    int current_page = 0;
};

// Global state variables needed by fishing.h (must be extern)
extern std::unordered_map<uint64_t, FishReceiptState> fish_states;

// build a message containing one page of the receipt for user uid
dpp::message build_fish_message(uint64_t uid);

// Handle the "reel in" button click
void handle_reel_in(dpp::cluster& bot, Database* db, uint64_t uid, uint64_t channel_id, const dpp::button_click_t& event);

// Primary fish command factory
Command* get_fish_command(Database* db);

} // namespace fishing
} // namespace commands
