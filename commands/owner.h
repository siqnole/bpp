#pragma once

// ============================================================================
// owner.h — DECLARATIONS ONLY
// All implementations are in owner.cpp to avoid recompiling the entire
// project when owner commands change (~3,600 lines of code).
// ============================================================================

// Project headers
#include "../command.h"
#include "../command_handler.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../security/secure_config.h"

// Standard library headers
#include <set>
#include <string>
#include <vector>
#include <algorithm>

namespace commands {

// ── Owner identification ──────────────────────────────────────────────
// Owner IDs — loaded from BOT_OWNER_IDS env var (comma-separated).
// Falls back to hardcoded ID if env var is not set (for backward compat).
inline const std::set<uint64_t>& get_owner_ids() {
    static const std::set<uint64_t> ids = []() {
        auto env_ids = bronx::security::load_owner_ids();
        if (env_ids.empty()) {
            // Backward compatibility — remove this fallback once env is configured
            env_ids.insert(814226043924643880ULL);
            std::cerr << "\033[33m⚠ BOT_OWNER_IDS not set — using hardcoded fallback. Set BOT_OWNER_IDS env var.\033[0m\n";
        }
        return env_ids;
    }();
    return ids;
}

// Check if user is owner — now supports multiple admins
inline bool is_owner(uint64_t user_id) {
    return get_owner_ids().count(user_id) > 0;
}

// ── Utility helpers (used by other command headers) ───────────────────

// parse a mention or raw numeric string into a snowflake ID
inline uint64_t parse_snowflake(const std::string &s) {
    uint64_t id = 0;
    for (char c : s) {
        if (isdigit((unsigned char)c)) {
            id = id * 10 + (c - '0');
        }
    }
    return id;
}

// Extract scope type/id from argument list starting at idx.  On success
// returns true and fills scope_type/scope_id/exclusive; if no scope args present, it
// defaults to guild.  Returns false on malformed input.
inline bool parse_scope_args(const std::vector<std::string> &args,
                             size_t idx,
                             std::string &scope_type,
                             uint64_t &scope_id,
                             bool &exclusive) {
    scope_type = "guild";
    scope_id = 0;
    exclusive = false;
    if (idx >= args.size()) return true;

    // flag-style: scan all remaining args for -u/-r/-c/-e
    for (size_t i = idx; i < args.size(); ++i) {
        std::string flag = args[i];
        if (flag == "-e" || flag == "--exclusive") {
            exclusive = true;
        } else if (flag == "-u" || flag == "--user") {
            if (i + 1 >= args.size()) return false;
            scope_type = "user";
            scope_id = parse_snowflake(args[i + 1]);
            i++; // skip next arg (the ID)
        } else if (flag == "-r" || flag == "--role") {
            if (i + 1 >= args.size()) return false;
            scope_type = "role";
            scope_id = parse_snowflake(args[i + 1]);
            i++; // skip next arg (the ID)
        } else if (flag == "-c" || flag == "--channel") {
            if (i + 1 >= args.size()) return false;
            scope_type = "channel";
            scope_id = parse_snowflake(args[i + 1]);
            i++; // skip next arg (the ID)
        }
    }

    // Exclusive mode only makes sense with channel scope
    if (exclusive && scope_type != "channel") {
        return false;
    }

    // If no flags found, try positional-style: scope_type id
    if (scope_type == "guild" && idx < args.size()) {
        std::string t = args[idx];
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "guild") {
            scope_type = "guild";
            return true;
        }
        if (t != "channel" && t != "role" && t != "user") {
            return false;
        }
        scope_type = t;
        if (idx + 1 >= args.size()) return false;
        scope_id = parse_snowflake(args[idx + 1]);
    }
    
    return true;
}

// ── Heavy implementations (defined in owner.cpp) ─────────────────────
// These are the only two functions main.cpp calls from this module.
std::vector<Command*> get_owner_commands(CommandHandler* handler, bronx::db::Database* db);
void register_owner_interactions(dpp::cluster& bot, bronx::db::Database* db, CommandHandler* handler);

} // namespace commands
