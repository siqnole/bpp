#pragma once
#include <dpp/dpp.h>
#include "../../database/core/database.h"
#include <mutex>
#include <map>

namespace commands {
namespace owner {

struct OStatsState {
    int current_page = 0; 
};

extern std::map<uint64_t, OStatsState> ostats_states;
extern std::recursive_mutex ostats_mutex;
extern const int OSTATS_TOTAL_PAGES;

/**
 * @brief Builds the detailed statistics message for the ostats command.
 */
dpp::message build_ostats_message(dpp::cluster& bot, bronx::db::Database* db, uint64_t owner_id);

/**
 * @brief Handles ostats-related button and select menu interactions.
 */
bool handle_ostats_interaction(const dpp::interaction_create_t& event, dpp::cluster& bot, bronx::db::Database* db);

} // namespace owner
} // namespace commands
