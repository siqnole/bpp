#pragma once
#include <dpp/dpp.h>
#include "../../database/core/database.h"
#include <mutex>
#include <map>

namespace commands {
namespace owner {

// --- Command History (Economy Audit) ---
struct CmdHistoryState {
    int current_page = 0;
    uint64_t target_user = 0;
    std::string filter = "";
};

extern std::map<uint64_t, CmdHistoryState> cmdhistory_states;
extern std::recursive_mutex cmdhistory_mutex;

dpp::message build_cmdhistory_message(bronx::db::Database* db, uint64_t owner_id);

// --- Suggestions Audit ---
struct SuggestState {
    int current_page = 0;
    std::string order_by = "submitted_at";
    bool asc = false;
};

extern std::map<uint64_t, SuggestState> suggest_states;
extern std::recursive_mutex suggest_mutex;

dpp::message build_suggestions_message(bronx::db::Database* db, uint64_t owner_id);

// --- Server List ---
struct ServerListState {
    int current_page = 0;
    std::string sort_by = "name";
    bool asc = true;
};

extern std::map<uint64_t, ServerListState> server_list_states;
extern std::recursive_mutex server_list_mutex;

dpp::message build_servers_message(dpp::cluster& bot, uint64_t owner_id);

/**
 * @brief Handles audit-related button, select menu, and modal interactions.
 * @return true if the interaction was handled.
 */
bool handle_audit_interaction(const dpp::interaction_create_t& event, dpp::cluster& bot, bronx::db::Database* db);

} // namespace owner
} // namespace commands
