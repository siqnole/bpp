#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <sstream>

namespace commands {
namespace utility {

// ============================================================================
// Auto-role: automatically assign roles to new members joining the server.
// - Manage Roles permission to edit
// - Administrator permission to manage (add/remove autoroles)
// - Cannot add roles higher than the invoker's top role
// - Cannot add roles with dangerous permissions (manage/ban/moderate/admin)
// ============================================================================

// In-memory cache of autoroles per guild (guild_id -> vector of role_ids)
static std::map<uint64_t, std::vector<uint64_t>> autorole_cache;
static std::mutex autorole_mutex;

// Optional database pointer
static bronx::db::Database* autorole_db = nullptr;
inline void set_autorole_db(bronx::db::Database* db) { autorole_db = db; }

// Dangerous permission bits — roles with any of these cannot be set as autoroles
static bool role_has_dangerous_perms(const dpp::role* role) {
    if (!role) return true;
    uint64_t perms = static_cast<uint64_t>(role->permissions);
    // Check for dangerous permission flags
    if (perms & static_cast<uint64_t>(dpp::p_administrator))        return true;
    if (perms & static_cast<uint64_t>(dpp::p_ban_members))          return true;
    if (perms & static_cast<uint64_t>(dpp::p_kick_members))         return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_guild))         return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_roles))         return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_channels))      return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_messages))      return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_webhooks))      return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_threads))       return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_events))        return true;
    if (perms & static_cast<uint64_t>(dpp::p_moderate_members))     return true;
    if (perms & static_cast<uint64_t>(dpp::p_manage_emojis_and_stickers)) return true;
    return false;
}

// Get the highest role position for a member
static int get_member_top_role_position(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    int top = -1;
    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (r && r->position > top) {
            top = r->position;
        }
    }
    return top;
}

// Check if member has Administrator permission
static bool member_has_administrator(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g && g->owner_id == member.user_id) return true;

    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (!r) continue;
        uint64_t perms = static_cast<uint64_t>(r->permissions);
        if (perms & static_cast<uint64_t>(dpp::p_administrator)) return true;
    }
    return false;
}

// Check if member has Manage Roles permission
static bool member_has_manage_roles(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g && g->owner_id == member.user_id) return true;

    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (!r) continue;
        uint64_t perms = static_cast<uint64_t>(r->permissions);
        if (perms & static_cast<uint64_t>(dpp::p_administrator)) return true;
        if (perms & static_cast<uint64_t>(dpp::p_manage_roles)) return true;
    }
    return false;
}

// Ensure database table exists
static void ensure_autorole_table() {
    if (!autorole_db) return;
    autorole_db->execute(
        "CREATE TABLE IF NOT EXISTS autoroles ("
        "  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  guild_id BIGINT UNSIGNED NOT NULL,"
        "  role_id BIGINT UNSIGNED NOT NULL,"
        "  added_by BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE KEY uk_guild_role (guild_id, role_id),"
        "  INDEX idx_guild (guild_id)"
        ") ENGINE=InnoDB;"
    );
}

// Load autoroles from DB into cache
static void load_autoroles_from_db() {
    if (!autorole_db) return;
    ensure_autorole_table();
    auto conn = autorole_db->get_pool()->acquire();
    if (!conn) return;

    const char* query = "SELECT guild_id, role_id FROM autoroles";
    if (mysql_query(conn->get(), query) == 0) {
        MYSQL_RES* result = mysql_store_result(conn->get());
        if (result) {
            std::lock_guard<std::mutex> lock(autorole_mutex);
            autorole_cache.clear();
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                uint64_t guild_id = std::stoull(row[0]);
                uint64_t role_id = std::stoull(row[1]);
                autorole_cache[guild_id].push_back(role_id);
            }
            mysql_free_result(result);
        }
    }
    autorole_db->get_pool()->release(conn);
}

// Add an autorole to DB + cache
static bool add_autorole(uint64_t guild_id, uint64_t role_id, uint64_t added_by) {
    if (!autorole_db) return false;
    ensure_autorole_table();
    auto conn = autorole_db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "INSERT IGNORE INTO autoroles (guild_id, role_id, added_by) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) { autorole_db->get_pool()->release(conn); return false; }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        mysql_stmt_close(stmt);
        autorole_db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &role_id;
    bind[1].is_unsigned = true;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = &added_by;
    bind[2].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    autorole_db->get_pool()->release(conn);

    if (ok && affected > 0) {
        std::lock_guard<std::mutex> lock(autorole_mutex);
        auto& roles = autorole_cache[guild_id];
        if (std::find(roles.begin(), roles.end(), role_id) == roles.end()) {
            roles.push_back(role_id);
        }
        return true;
    }
    return ok && affected == 0 ? false : ok; // affected==0 means duplicate
}

// Remove an autorole from DB + cache
static bool remove_autorole(uint64_t guild_id, uint64_t role_id) {
    if (!autorole_db) return false;
    auto conn = autorole_db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "DELETE FROM autoroles WHERE guild_id = ? AND role_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) { autorole_db->get_pool()->release(conn); return false; }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        mysql_stmt_close(stmt);
        autorole_db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &role_id;
    bind[1].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    autorole_db->get_pool()->release(conn);

    if (ok && affected > 0) {
        std::lock_guard<std::mutex> lock(autorole_mutex);
        auto& roles = autorole_cache[guild_id];
        roles.erase(std::remove(roles.begin(), roles.end(), role_id), roles.end());
        if (roles.empty()) autorole_cache.erase(guild_id);
        return true;
    }
    return false;
}

// Clear all autoroles for a guild
static bool clear_autoroles(uint64_t guild_id) {
    if (!autorole_db) return false;
    auto conn = autorole_db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "DELETE FROM autoroles WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) { autorole_db->get_pool()->release(conn); return false; }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        mysql_stmt_close(stmt);
        autorole_db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    autorole_db->get_pool()->release(conn);

    if (ok) {
        std::lock_guard<std::mutex> lock(autorole_mutex);
        autorole_cache.erase(guild_id);
    }
    return ok;
}

// Get autoroles for a guild (from cache)
static std::vector<uint64_t> get_autoroles(uint64_t guild_id) {
    std::lock_guard<std::mutex> lock(autorole_mutex);
    auto it = autorole_cache.find(guild_id);
    if (it != autorole_cache.end()) return it->second;
    return {};
}

// Parse a role from text: accepts <@&ID>, @RoleName, plain ID, or role name
static uint64_t parse_role_id(const dpp::snowflake& guild_id, const std::string& input) {
    std::string s = input;
    // Trim whitespace
    s.erase(0, s.find_first_not_of(" \t\n\r"));
    s.erase(s.find_last_not_of(" \t\n\r") + 1);
    if (s.empty()) return 0;

    // <@&ID> mention format
    if (s.size() > 4 && s.substr(0, 3) == "<@&" && s.back() == '>') {
        std::string id_str = s.substr(3, s.size() - 4);
        try { return std::stoull(id_str); } catch (...) { return 0; }
    }

    // Pure numeric ID
    bool all_digits = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    if (all_digits && s.size() >= 17) {
        try { return std::stoull(s); } catch (...) { return 0; }
    }

    // Strip leading @ if present
    if (!s.empty() && s[0] == '@') s = s.substr(1);

    // Try name match (case-insensitive)
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g) {
        std::string lower_input = s;
        std::transform(lower_input.begin(), lower_input.end(), lower_input.begin(), ::tolower);
        for (const auto& rid : g->roles) {
            dpp::role* r = dpp::find_role(rid);
            if (!r) continue;
            std::string lower_name = r->name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            if (lower_name == lower_input) return r->id;
        }
    }

    return 0;
}

// ---- The autorole command ----
inline Command* get_autorole_command() {
    static Command autorole("autorole", "manage auto-assigned roles for new members", "utility",
        {"ar", "joinrole", "welcomerole"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (event.msg.guild_id == 0) {
                bronx::send_message(bot, event, bronx::error("this command can only be used in a server"));
                return;
            }

            // ---- No subcommand: show help ----
            if (args.empty()) {
                auto embed = bronx::create_embed("", bronx::COLOR_INFO);
                embed.set_title("autorole");
                embed.set_description(
                    "automatically assign roles to new members when they join.\n\n"
                    "**subcommands:**\n"
                    "`autorole add <role>` — add an autorole\n"
                    "`autorole remove <role>` — remove an autorole\n"
                    "`autorole list` — list current autoroles\n"
                    "`autorole clear` — remove all autoroles\n\n"
                    "**permissions:**\n"
                    "• `add/remove/clear` require **Administrator**\n"
                    "• `list` requires **Manage Roles**\n\n"
                    "**safety:**\n"
                    "• cannot assign roles higher than your top role\n"
                    "• cannot assign roles with dangerous permissions\n"
                    "  (administrator, manage, ban, kick, moderate)"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            // ---- LIST ----
            if (sub == "list" || sub == "ls" || sub == "show") {
                // Requires Manage Roles
                dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
                if (!guild) {
                    bronx::send_message(bot, event, bronx::error("failed to find guild in cache"));
                    return;
                }
                dpp::guild_member member;
                auto mit = guild->members.find(event.msg.author.id);
                if (mit != guild->members.end()) {
                    member = mit->second;
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to find your member data"));
                    return;
                }

                if (!member_has_manage_roles(event.msg.guild_id, member)) {
                    bronx::send_message(bot, event, bronx::error("you need **Manage Roles** to view autoroles"));
                    return;
                }

                auto roles = get_autoroles(event.msg.guild_id);
                if (roles.empty()) {
                    bronx::send_message(bot, event, bronx::info("no autoroles configured for this server"));
                    return;
                }

                std::string desc;
                for (size_t i = 0; i < roles.size(); i++) {
                    dpp::role* r = dpp::find_role(roles[i]);
                    std::string role_display = r ? r->name : std::to_string(roles[i]);
                    desc += "**" + std::to_string(i + 1) + ".** <@&" + std::to_string(roles[i]) + "> (" + role_display + ")\n";
                }

                auto embed = bronx::create_embed(desc, bronx::COLOR_INFO);
                embed.set_title("autoroles (" + std::to_string(roles.size()) + ")");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            // ---- ADD ----
            if (sub == "add" || sub == "set") {
                // Requires Administrator
                dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
                if (!guild) {
                    bronx::send_message(bot, event, bronx::error("failed to find guild in cache"));
                    return;
                }
                dpp::guild_member member;
                auto mit = guild->members.find(event.msg.author.id);
                if (mit != guild->members.end()) {
                    member = mit->second;
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to find your member data"));
                    return;
                }

                if (!member_has_administrator(event.msg.guild_id, member)) {
                    bronx::send_message(bot, event, bronx::error("you need **Administrator** to add autoroles"));
                    return;
                }

                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `autorole add <role>`\nrole can be a mention, ID, or name"));
                    return;
                }

                // Reconstruct the role argument (may contain spaces)
                std::string role_arg;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) role_arg += " ";
                    role_arg += args[i];
                }

                uint64_t role_id = parse_role_id(event.msg.guild_id, role_arg);
                if (role_id == 0) {
                    bronx::send_message(bot, event, bronx::error("couldn't find that role — use a mention, ID, or exact name"));
                    return;
                }

                dpp::role* role = dpp::find_role(role_id);
                if (!role) {
                    bronx::send_message(bot, event, bronx::error("that role doesn't exist in this server"));
                    return;
                }

                // Check: role must belong to this guild
                if (role->guild_id != event.msg.guild_id) {
                    bronx::send_message(bot, event, bronx::error("that role doesn't belong to this server"));
                    return;
                }

                // Check: can't be @everyone
                if (role->id == event.msg.guild_id) {
                    bronx::send_message(bot, event, bronx::error("you can't add @everyone as an autorole"));
                    return;
                }

                // Check: dangerous permissions
                if (role_has_dangerous_perms(role)) {
                    bronx::send_message(bot, event, bronx::error(
                        "**" + role->name + "** has dangerous permissions and cannot be set as an autorole\n\n"
                        "roles with any of these permissions are blocked:\n"
                        "administrator, manage guild/roles/channels/messages/webhooks/threads/events/emojis, "
                        "ban members, kick members, moderate members"
                    ));
                    return;
                }

                // Check: role must be below invoker's top role
                int invoker_top = get_member_top_role_position(event.msg.guild_id, member);
                if (role->position >= invoker_top && guild->owner_id != event.msg.author.id) {
                    bronx::send_message(bot, event, bronx::error(
                        "**" + role->name + "** is at or above your highest role — you can't assign it as an autorole"
                    ));
                    return;
                }

                // Check: role must be below bot's top role
                auto bot_member_it = guild->members.find(bot.me.id);
                if (bot_member_it != guild->members.end()) {
                    int bot_top = get_member_top_role_position(event.msg.guild_id, bot_member_it->second);
                    if (role->position >= bot_top) {
                        bronx::send_message(bot, event, bronx::error(
                            "**" + role->name + "** is at or above my highest role — i can't assign it"
                        ));
                        return;
                    }
                }

                // Check: max 10 autoroles per guild
                auto current = get_autoroles(event.msg.guild_id);
                if (current.size() >= 10) {
                    bronx::send_message(bot, event, bronx::error("maximum of **10** autoroles per server"));
                    return;
                }

                // Check: already exists
                if (std::find(current.begin(), current.end(), role_id) != current.end()) {
                    bronx::send_message(bot, event, bronx::error("<@&" + std::to_string(role_id) + "> is already an autorole"));
                    return;
                }

                if (add_autorole(event.msg.guild_id, role_id, event.msg.author.id)) {
                    bronx::send_message(bot, event, bronx::success(
                        "<@&" + std::to_string(role_id) + "> will now be assigned to new members"
                    ));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to add autorole — it may already exist"));
                }
                return;
            }

            // ---- REMOVE ----
            if (sub == "remove" || sub == "rm" || sub == "delete" || sub == "del") {
                // Requires Administrator
                dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
                if (!guild) {
                    bronx::send_message(bot, event, bronx::error("failed to find guild in cache"));
                    return;
                }
                dpp::guild_member member;
                auto mit = guild->members.find(event.msg.author.id);
                if (mit != guild->members.end()) {
                    member = mit->second;
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to find your member data"));
                    return;
                }

                if (!member_has_administrator(event.msg.guild_id, member)) {
                    bronx::send_message(bot, event, bronx::error("you need **Administrator** to remove autoroles"));
                    return;
                }

                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `autorole remove <role>`"));
                    return;
                }

                std::string role_arg;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) role_arg += " ";
                    role_arg += args[i];
                }

                uint64_t role_id = parse_role_id(event.msg.guild_id, role_arg);
                if (role_id == 0) {
                    bronx::send_message(bot, event, bronx::error("couldn't find that role"));
                    return;
                }

                if (remove_autorole(event.msg.guild_id, role_id)) {
                    bronx::send_message(bot, event, bronx::success(
                        "<@&" + std::to_string(role_id) + "> removed from autoroles"
                    ));
                } else {
                    bronx::send_message(bot, event, bronx::error("that role isn't currently an autorole"));
                }
                return;
            }

            // ---- CLEAR ----
            if (sub == "clear" || sub == "reset") {
                // Requires Administrator
                dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
                if (!guild) {
                    bronx::send_message(bot, event, bronx::error("failed to find guild in cache"));
                    return;
                }
                dpp::guild_member member;
                auto mit = guild->members.find(event.msg.author.id);
                if (mit != guild->members.end()) {
                    member = mit->second;
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to find your member data"));
                    return;
                }

                if (!member_has_administrator(event.msg.guild_id, member)) {
                    bronx::send_message(bot, event, bronx::error("you need **Administrator** to clear autoroles"));
                    return;
                }

                auto current = get_autoroles(event.msg.guild_id);
                if (current.empty()) {
                    bronx::send_message(bot, event, bronx::info("there are no autoroles to clear"));
                    return;
                }

                if (clear_autoroles(event.msg.guild_id)) {
                    bronx::send_message(bot, event, bronx::success(
                        "cleared **" + std::to_string(current.size()) + "** autorole(s)"
                    ));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to clear autoroles"));
                }
                return;
            }

            // Unknown subcommand
            bronx::send_message(bot, event, bronx::error(
                "unknown subcommand `" + sub + "`\nuse `autorole` for help"
            ));
        });
    return &autorole;
}

// ---- Event handler: assign autoroles on guild_member_add ----
inline void handle_autorole_member_join(dpp::cluster& bot, const dpp::guild_member_add_t& event) {
    // Don't assign roles to bots
    if (event.added.get_user() && event.added.get_user()->is_bot()) return;

    uint64_t guild_id = event.adding_guild.id;
    auto roles = get_autoroles(guild_id);
    if (roles.empty()) return;

    uint64_t user_id = event.added.user_id;
    for (uint64_t role_id : roles) {
        // Verify role still exists and is safe before assigning
        dpp::role* r = dpp::find_role(role_id);
        if (!r || role_has_dangerous_perms(r)) {
            // Role was deleted or gained dangerous perms — skip it
            continue;
        }

        bot.guild_member_add_role(guild_id, user_id, role_id,
            [guild_id, user_id, role_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[autorole] failed to add role " << role_id
                              << " to user " << user_id << " in guild " << guild_id
                              << ": " << cb.get_error().message << "\n";
                }
            });
    }
}

// Load persisted autoroles on startup
inline void load_autoroles(dpp::cluster& bot) {
    load_autoroles_from_db();
    std::lock_guard<std::mutex> lock(autorole_mutex);
    size_t total = 0;
    for (const auto& [gid, roles] : autorole_cache) total += roles.size();
    std::cout << "\033[32m✔ \033[0m" << "loaded " << total << " autorole(s) across "
              << autorole_cache.size() << " guild(s)\n";
}

} // namespace utility
} // namespace commands
