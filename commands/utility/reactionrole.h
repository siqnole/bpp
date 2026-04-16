#pragma once

// ============================================================================
// reactionrole.h — DECLARATIONS ONLY
// All implementations are in reactionrole.cpp to avoid recompiling
// the entire project when reaction role logic changes (~1,900 lines).
// ============================================================================

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <string>
#include <map>
#include <vector>

namespace commands {
namespace utility {

// ── Data structures (needed by event handlers in main.cpp) ───────────
struct RREntry {
    uint64_t role_id;
    uint64_t emoji_id = 0;
    ::std::string emoji_str;
    ::std::string raw;
};

struct RRMessage {
    uint64_t channel_id = 0;
    ::std::vector<RREntry> entries;
};

// message_id -> RRMessage  (extern — defined in reactionrole.cpp)
extern ::std::map<uint64_t, RRMessage> reaction_roles;

// ── Public API (implementations in reactionrole.cpp) ─────────────────
void set_reactionrole_db(bronx::db::Database* db);
void sync_existing_reactions(dpp::cluster& bot, uint64_t message_id, uint64_t channel_id,
                             const std::string& emoji_reaction, uint64_t role_id,
                             uint64_t guild_id, uint64_t after_user = 0);
void load_persistent_reaction_roles(dpp::cluster& bot);
void handle_rr_check(dpp::cluster& bot, uint64_t channel_id, uint64_t message_id,
                     uint64_t guild_id, uint64_t user_id,
                     std::function<void(const dpp::embed&)> reply_embed,
                     std::function<void(dpp::message)> reply_msg);
Command* get_reactionrole_command();
void register_reactionrole_interactions(dpp::cluster& bot);

} // namespace utility
} // namespace commands