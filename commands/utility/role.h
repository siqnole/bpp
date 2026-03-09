#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <set>

namespace commands {
namespace utility {

// ============================================================================
// Role command: add/remove roles for multiple users at once.
//
// Usage:
//   b.role user1,user2,123123,<@123> role1,role2,<@&456>,@role3
//   b.r user1,user2 role1,role2
//
// Requires Manage Roles permission.
// Cannot assign roles above your top role or roles with dangerous perms.
// ============================================================================

// Parse a user from text: accepts <@ID>, <@!ID>, plain ID, or username
static uint64_t parse_user_id_from_token(const dpp::snowflake& guild_id, const std::string& input) {
    std::string s = input;
    s.erase(0, s.find_first_not_of(" \t\n\r"));
    s.erase(s.find_last_not_of(" \t\n\r") + 1);
    if (s.empty()) return 0;

    // <@ID> or <@!ID> mention format
    if (s.size() > 3 && s[0] == '<' && s[1] == '@' && s.back() == '>') {
        std::string id_str;
        if (s[2] == '!') {
            id_str = s.substr(3, s.size() - 4);
        } else {
            id_str = s.substr(2, s.size() - 3);
        }
        try { return std::stoull(id_str); } catch (...) { return 0; }
    }

    // Pure numeric ID (snowflakes are 17+ digits)
    bool all_digits = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    if (all_digits && s.size() >= 17) {
        try { return std::stoull(s); } catch (...) { return 0; }
    }

    // Try username match in guild cache (case-insensitive)
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g) {
        std::string lower_input = s;
        std::transform(lower_input.begin(), lower_input.end(), lower_input.begin(), ::tolower);
        // Strip leading @ if present
        if (!lower_input.empty() && lower_input[0] == '@') lower_input = lower_input.substr(1);

        for (const auto& [uid, member] : g->members) {
            // Check display name / username
            if (member.get_user()) {
                std::string uname = member.get_user()->username;
                std::transform(uname.begin(), uname.end(), uname.begin(), ::tolower);
                if (uname == lower_input) return uid;

                std::string gname = member.get_user()->global_name;
                std::transform(gname.begin(), gname.end(), gname.begin(), ::tolower);
                if (!gname.empty() && gname == lower_input) return uid;
            }
            // Check server nickname
            std::string nick = member.get_nickname();
            std::string lower_nick = nick;
            std::transform(lower_nick.begin(), lower_nick.end(), lower_nick.begin(), ::tolower);
            if (!lower_nick.empty() && lower_nick == lower_input) return uid;
        }
    }
    return 0;
}

// Parse role from token — reuse parse_role_id from autorole.h
// If autorole.h is not included, define a local version
#ifndef AUTOROLE_PARSE_ROLE_DEFINED
static uint64_t parse_role_id_for_cmd(const dpp::snowflake& guild_id, const std::string& input) {
    std::string s = input;
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

    // Name match (case-insensitive)
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
#endif

// Check if a role has dangerous permissions (same logic as autorole)
static bool role_cmd_has_dangerous_perms(const dpp::role* role) {
    if (!role) return true;
    uint64_t perms = static_cast<uint64_t>(role->permissions);
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

// Get member top role position
static int role_cmd_get_member_top_pos(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    int top = -1;
    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (r && r->position > top) top = r->position;
    }
    return top;
}

// Check Manage Roles
static bool role_cmd_has_manage_roles(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
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

// Split a string by comma, trimming each token
static std::vector<std::string> split_by_comma(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim
        token.erase(0, token.find_first_not_of(" \t\n\r"));
        token.erase(token.find_last_not_of(" \t\n\r") + 1);
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

inline Command* get_role_command() {
    static Command role("role", "add roles to multiple users at once", "utility",
        {"r", "giverole", "addrole"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (event.msg.guild_id == 0) {
                bronx::send_message(bot, event, bronx::error("this command can only be used in a server"));
                return;
            }

            // ---- No args: show help ----
            if (args.empty()) {
                auto embed = bronx::create_embed("", bronx::COLOR_INFO);
                embed.set_title("role");
                embed.set_description(
                    "add roles to multiple users at once.\n\n"
                    "**usage:**\n"
                    "`role <users> <roles>`\n\n"
                    "**examples:**\n"
                    "`role @user1,@user2 @role1,@role2`\n"
                    "`role user1,123456789012345678 Member,VIP`\n"
                    "`role <@123>,user2 <@&456>,rolename`\n\n"
                    "separate users and roles with **commas** (no spaces around commas).\n"
                    "the first group is users, the second group is roles.\n\n"
                    "**permissions:** Manage Roles\n"
                    "**safety:** can't assign roles above your top role or with dangerous perms"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            // Permission check
            dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
            if (!guild) {
                bronx::send_message(bot, event, bronx::error("failed to find guild in cache"));
                return;
            }
            dpp::guild_member invoker;
            auto mit = guild->members.find(event.msg.author.id);
            if (mit != guild->members.end()) {
                invoker = mit->second;
            } else {
                bronx::send_message(bot, event, bronx::error("failed to find your member data"));
                return;
            }

            if (!role_cmd_has_manage_roles(event.msg.guild_id, invoker)) {
                bronx::send_message(bot, event, bronx::error("you need **Manage Roles** to use this command"));
                return;
            }

            // Need at least 2 args: users-group and roles-group
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error(
                    "usage: `role <users> <roles>`\n"
                    "example: `role @user1,@user2 @role1,@role2`"
                ));
                return;
            }

            // Parse users (first arg, comma-separated)
            auto user_tokens = split_by_comma(args[0]);
            // Parse roles (remaining args joined, comma-separated)
            // This allows role names with spaces if they don't contain commas
            std::string roles_raw;
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) roles_raw += " ";
                roles_raw += args[i];
            }
            auto role_tokens = split_by_comma(roles_raw);

            if (user_tokens.empty()) {
                bronx::send_message(bot, event, bronx::error("no users specified"));
                return;
            }
            if (role_tokens.empty()) {
                bronx::send_message(bot, event, bronx::error("no roles specified"));
                return;
            }

            // Resolve users
            std::vector<uint64_t> user_ids;
            std::vector<std::string> failed_users;
            for (const auto& tok : user_tokens) {
                uint64_t uid = parse_user_id_from_token(event.msg.guild_id, tok);
                if (uid != 0) {
                    user_ids.push_back(uid);
                } else {
                    failed_users.push_back(tok);
                }
            }

            // Resolve roles
            struct ResolvedRole {
                uint64_t id;
                std::string name;
            };
            std::vector<ResolvedRole> resolved_roles;
            std::vector<std::string> failed_roles;
            std::vector<std::string> blocked_roles; // dangerous perms
            std::vector<std::string> hierarchy_roles; // above invoker

            int invoker_top = role_cmd_get_member_top_pos(event.msg.guild_id, invoker);
            bool is_owner = (guild->owner_id == event.msg.author.id);

            // Get bot's top role position
            int bot_top = -1;
            auto bot_it = guild->members.find(bot.me.id);
            if (bot_it != guild->members.end()) {
                bot_top = role_cmd_get_member_top_pos(event.msg.guild_id, bot_it->second);
            }

            for (const auto& tok : role_tokens) {
                uint64_t rid = parse_role_id_for_cmd(event.msg.guild_id, tok);
                if (rid == 0) {
                    failed_roles.push_back(tok);
                    continue;
                }
                dpp::role* r = dpp::find_role(rid);
                if (!r || r->guild_id != event.msg.guild_id) {
                    failed_roles.push_back(tok);
                    continue;
                }
                // Can't be @everyone
                if (r->id == event.msg.guild_id) {
                    failed_roles.push_back("@everyone");
                    continue;
                }
                // Dangerous perms check
                if (role_cmd_has_dangerous_perms(r)) {
                    blocked_roles.push_back(r->name);
                    continue;
                }
                // Hierarchy check (invoker)
                if (!is_owner && r->position >= invoker_top) {
                    hierarchy_roles.push_back(r->name);
                    continue;
                }
                // Hierarchy check (bot)
                if (r->position >= bot_top) {
                    hierarchy_roles.push_back(r->name + " (above bot)");
                    continue;
                }
                resolved_roles.push_back({rid, r->name});
            }

            // Deduplicate
            {
                std::set<uint64_t> seen;
                std::vector<uint64_t> deduped;
                for (uint64_t uid : user_ids) {
                    if (seen.insert(uid).second) deduped.push_back(uid);
                }
                user_ids = deduped;
            }
            {
                std::set<uint64_t> seen;
                std::vector<ResolvedRole> deduped;
                for (const auto& rr : resolved_roles) {
                    if (seen.insert(rr.id).second) deduped.push_back(rr);
                }
                resolved_roles = deduped;
            }

            if (user_ids.empty()) {
                bronx::send_message(bot, event, bronx::error("couldn't resolve any users"));
                return;
            }
            if (resolved_roles.empty()) {
                std::string detail;
                if (!blocked_roles.empty()) {
                    detail += "**blocked (dangerous perms):** " + blocked_roles[0];
                    for (size_t i = 1; i < blocked_roles.size(); i++) detail += ", " + blocked_roles[i];
                    detail += "\n";
                }
                if (!hierarchy_roles.empty()) {
                    detail += "**blocked (hierarchy):** " + hierarchy_roles[0];
                    for (size_t i = 1; i < hierarchy_roles.size(); i++) detail += ", " + hierarchy_roles[i];
                    detail += "\n";
                }
                if (!failed_roles.empty()) {
                    detail += "**not found:** " + failed_roles[0];
                    for (size_t i = 1; i < failed_roles.size(); i++) detail += ", " + failed_roles[i];
                }
                bronx::send_message(bot, event, bronx::error("no valid roles to assign\n" + detail));
                return;
            }

            // Limit: max 10 users × 10 roles = 100 API calls
            if (user_ids.size() > 10) {
                bronx::send_message(bot, event, bronx::error("maximum **10** users per command"));
                return;
            }
            if (resolved_roles.size() > 10) {
                bronx::send_message(bot, event, bronx::error("maximum **10** roles per command"));
                return;
            }

            // Build a summary and execute
            size_t total_ops = user_ids.size() * resolved_roles.size();

            // Use a shared counter to track completion
            struct OpState {
                std::atomic<size_t> completed{0};
                std::atomic<size_t> success{0};
                std::atomic<size_t> failed{0};
                std::atomic<size_t> added{0};
                std::atomic<size_t> removed{0};
                size_t total;
                dpp::snowflake channel_id;
                dpp::snowflake msg_id;
                uint64_t guild_id;
                std::vector<std::string> errors;
                std::mutex err_mutex;
            };
            auto state = std::make_shared<OpState>();
            state->total = total_ops;
            state->channel_id = event.msg.channel_id;
            state->msg_id = event.msg.id;
            state->guild_id = event.msg.guild_id;

            // Send initial "working" message
            std::string role_list;
            for (size_t i = 0; i < resolved_roles.size(); i++) {
                if (i > 0) role_list += ", ";
                role_list += "<@&" + std::to_string(resolved_roles[i].id) + ">";
            }
            std::string user_list;
            for (size_t i = 0; i < user_ids.size(); i++) {
                if (i > 0) user_list += ", ";
                user_list += "<@" + std::to_string(user_ids[i]) + ">";
            }

            // Build warnings string
            std::string warnings;
            if (!failed_users.empty()) {
                warnings += "\n" + bronx::EMOJI_WARNING + " **unresolved users:** ";
                for (size_t i = 0; i < failed_users.size(); i++) {
                    if (i > 0) warnings += ", ";
                    warnings += "`" + failed_users[i] + "`";
                }
            }
            if (!failed_roles.empty()) {
                warnings += "\n" + bronx::EMOJI_WARNING + " **unresolved roles:** ";
                for (size_t i = 0; i < failed_roles.size(); i++) {
                    if (i > 0) warnings += ", ";
                    warnings += "`" + failed_roles[i] + "`";
                }
            }
            if (!blocked_roles.empty()) {
                warnings += "\n" + bronx::EMOJI_WARNING + " **blocked (dangerous perms):** ";
                for (size_t i = 0; i < blocked_roles.size(); i++) {
                    if (i > 0) warnings += ", ";
                    warnings += "`" + blocked_roles[i] + "`";
                }
            }
            if (!hierarchy_roles.empty()) {
                warnings += "\n" + bronx::EMOJI_WARNING + " **blocked (hierarchy):** ";
                for (size_t i = 0; i < hierarchy_roles.size(); i++) {
                    if (i > 0) warnings += ", ";
                    warnings += "`" + hierarchy_roles[i] + "`";
                }
            }

            // Execute role toggles: add if missing, remove if already has
            for (uint64_t uid : user_ids) {
                for (const auto& rr : resolved_roles) {
                    // Check if user already has this role
                    bool has_role = false;
                    auto mem_it = guild->members.find(uid);
                    if (mem_it != guild->members.end()) {
                        for (const auto& rid : mem_it->second.get_roles()) {
                            if (rid == rr.id) { has_role = true; break; }
                        }
                    }

                    auto callback = [&bot, state, uid, rr_id = rr.id, rr_name = rr.name,
                         role_list, user_list, warnings, total_ops, has_role]
                        (const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                state->failed++;
                                std::lock_guard<std::mutex> lock(state->err_mutex);
                                state->errors.push_back(
                                    "<@" + std::to_string(uid) + "> " + (has_role ? "- " : "+ ") + rr_name + ": " + cb.get_error().message
                                );
                            } else {
                                state->success++;
                                if (has_role) state->removed++;
                                else state->added++;
                            }
                            size_t done = ++state->completed;
                            
                            // When all ops complete, send the result
                            if (done >= state->total) {
                                std::string desc;
                                if (state->failed == 0) {
                                    // Build a concise summary
                                    std::string action;
                                    if (state->added > 0 && state->removed > 0) {
                                        action = "added " + std::to_string(state->added.load()) + ", removed " + std::to_string(state->removed.load());
                                    } else if (state->removed > 0) {
                                        action = "removed " + role_list + " from " + user_list;
                                    } else {
                                        action = "assigned " + role_list + " to " + user_list;
                                    }
                                    desc = bronx::EMOJI_CHECK + " " + action +
                                           "\n\n**" + std::to_string(state->success.load()) + "/" +
                                           std::to_string(state->total) + "** operations succeeded";
                                } else {
                                    desc = bronx::EMOJI_WARNING + " role update partially completed\n\n"
                                           "**" + std::to_string(state->success.load()) + "/" +
                                           std::to_string(state->total) + "** succeeded, **" +
                                           std::to_string(state->failed.load()) + "** failed";
                                    
                                    // Show first few errors
                                    std::lock_guard<std::mutex> lock(state->err_mutex);
                                    size_t show = std::min(state->errors.size(), (size_t)5);
                                    if (!state->errors.empty()) {
                                        desc += "\n\n**errors:**\n";
                                        for (size_t i = 0; i < show; i++) {
                                            desc += "• " + state->errors[i] + "\n";
                                        }
                                        if (state->errors.size() > 5) {
                                            desc += "• ... and " + std::to_string(state->errors.size() - 5) + " more\n";
                                        }
                                    }
                                }

                                desc += warnings;

                                uint32_t color = (state->failed == 0) ? bronx::COLOR_SUCCESS :
                                                 (state->success > 0) ? bronx::COLOR_WARNING : bronx::COLOR_ERROR;
                                auto embed = bronx::create_embed(desc, color);

                                dpp::message msg(state->channel_id, embed);
                                msg.set_reference(state->msg_id);
                                bot.message_create(msg, [&bot, state](const dpp::confirmation_callback_t& cb2) {
                                    if (cb2.is_error()) {
                                        // Fallback without reference
                                        auto fallback_embed = bronx::success(
                                            std::to_string(state->success.load()) + " role(s) updated successfully"
                                        );
                                        bot.message_create(dpp::message(state->channel_id, fallback_embed));
                                    }
                                });
                            }
                        };

                    if (has_role) {
                        bot.guild_member_remove_role(event.msg.guild_id, uid, rr.id, callback);
                    } else {
                        bot.guild_member_add_role(event.msg.guild_id, uid, rr.id, callback);
                    }
                }
            }
        });
    return &role;
}

} // namespace utility
} // namespace commands
