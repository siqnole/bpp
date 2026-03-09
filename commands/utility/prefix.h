#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <algorithm>
#include <string>

namespace commands {
namespace utility {

static bronx::db::Database* prefix_db = nullptr;
inline void set_prefix_db(bronx::db::Database* db) { prefix_db = db; }

// helper for validating a prefix string
static bool valid_prefix(const std::string& p) {
    if (p.empty() || p.size() > 20) return false;
    // disallow whitespace
    for (char c : p) if (isspace((unsigned char)c)) return false;
    return true;
}

inline Command* get_prefix_command() {
    static Command cmd("prefix", "configure custom command prefixes (utility)", "utility", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            auto help_embed = bronx::info("Prefix Command Help");
            help_embed.set_description(
                "Usage:\n"
                "`b.prefix user add <prefix>` - add a personal prefix\n"
                "`b.prefix user remove <prefix>` - remove a personal prefix\n"
                "`b.prefix user list` - show your prefixes\n"
                "`b.prefix server add <prefix>` - admin: add a guild prefix\n"
                "`b.prefix server remove <prefix>` - admin: remove guild prefix\n"
                "`b.prefix server list` - list guild prefixes\n"
                "Example: `b.prefix user add !!` will allow only you to trigger commands with `!!cmd`"
            );

            if (args.empty()) {
                bronx::send_message(bot, event, help_embed);
                return;
            }
            std::string scope = args[0];
            std::transform(scope.begin(), scope.end(), scope.begin(), ::tolower);
            if (scope != "user" && scope != "server") {
                bronx::send_message(bot, event, bronx::error("First argument must be 'user' or 'server'."));
                return;
            }

            if (!prefix_db) {
                bronx::send_message(bot, event, bronx::error("database not configured"));
                return;
            }

            auto check_admin = [&]() -> bool {
                // only allow if user has manage server permission or is owner
                if (commands::is_owner(event.msg.author.id)) {
                    return true;
                }
                if (!event.msg.guild_id) return false;
                dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                if (!g) return false;
                dpp::guild_member member = event.msg.member;
                // if message came from anywhere, member may be default; ensure id is present
                if (member.user_id == 0) return false;
                return g->base_permissions(member).can(dpp::p_manage_guild);
            };

            if (scope == "user") {
                if (args.size() == 1) {
                    bronx::send_message(bot, event, help_embed);
                    return;
                }
                std::string action = args[1];
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                if (action == "list") {
                    auto list = prefix_db->get_user_prefixes(event.msg.author.id);
                    if (list.empty()) {
                        bronx::send_message(bot, event, bronx::info("You have no custom prefixes."));
                    } else {
                        std::string out;
                        for (auto &p : list) out += "`" + p + "` ";
                        dpp::embed eb = bronx::info("Your prefixes");
                        eb.set_description(out);
                        bot.message_create(dpp::message(event.msg.channel_id, eb));
                    }
                    return;
                }
                if (action != "add" && action != "remove") {
                    bronx::send_message(bot, event, bronx::error("Unknown action. Use add/remove/list."));
                    return;
                }
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("Prefix argument missing."));
                    return;
                }
                std::string value = args[2];
                if (!valid_prefix(value)) {
                    bronx::send_message(bot, event, bronx::error("Invalid prefix; must be 1-20 characters with no whitespace."));
                    return;
                }
                bool ok = false;
                if (action == "add") {
                    ok = prefix_db->add_user_prefix(event.msg.author.id, value);
                    if (ok) {
                        auto prefixes = prefix_db->get_user_prefixes(event.msg.author.id);
                        std::string plural = (prefixes.size() == 1) ? "prefix" : "prefixes";
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Added prefix `" + value + "`. You now have " + std::to_string(prefixes.size()) + " " + plural + ".")));
                    }
                } else {
                    ok = prefix_db->remove_user_prefix(event.msg.author.id, value);
                    if (ok) {
                        auto prefixes = prefix_db->get_user_prefixes(event.msg.author.id);
                        std::string plural = (prefixes.size() == 1) ? "prefix" : "prefixes";
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Removed prefix `" + value + "`. You now have " + std::to_string(prefixes.size()) + " " + plural + ".")));
                    }
                }
                if (!ok) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("operation failed (maybe prefix didn't exist?)")));
                }
                return;
            }

            // server scope
            if (!check_admin()) {
                bronx::send_message(bot, event, bronx::error("Administrator permission required."));
                return;
            }
            if (args.size() == 1) {
                bronx::send_message(bot, event, help_embed);
                return;
            }
            std::string action = args[1];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            if (action == "list") {
                uint64_t gid = event.msg.guild_id;
                auto list = prefix_db->get_guild_prefixes(gid);
                if (list.empty()) {
                    bronx::send_message(bot, event, bronx::info("This server has no custom prefixes."));
                } else {
                    std::string out;
                    for (auto &p : list) out += "`" + p + "` ";
                    dpp::embed eb = bronx::info("Server prefixes");
                    eb.set_description(out);
                    bot.message_create(dpp::message(event.msg.channel_id, eb));
                }
                return;
            }
            if (action != "add" && action != "remove") {
                bronx::send_message(bot, event, bronx::error("Unknown action. Use add/remove/list."));
                return;
            }
            if (args.size() < 3) {
                bronx::send_message(bot, event, bronx::error("Prefix argument missing."));
                return;
            }
            std::string value = args[2];
            if (!valid_prefix(value)) {
                bronx::send_message(bot, event, bronx::error("Invalid prefix; must be 1-20 characters with no whitespace."));
                return;
            }
            bool ok = false;
            uint64_t gid = event.msg.guild_id;
            if (action == "add") {
                ok = prefix_db->add_guild_prefix(gid, value);
                if (ok) {
                    auto prefixes = prefix_db->get_guild_prefixes(gid);
                    std::string plural = (prefixes.size() == 1) ? "prefix" : "prefixes";
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Added server prefix `" + value + "`. This server now has " + std::to_string(prefixes.size()) + " " + plural + ".")));
                }
            } else {
                ok = prefix_db->remove_guild_prefix(gid, value);
                if (ok) {
                    auto prefixes = prefix_db->get_guild_prefixes(gid);
                    std::string plural = (prefixes.size() == 1) ? "prefix" : "prefixes"; 
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Removed server prefix `" + value + "`. This server now has " + std::to_string(prefixes.size()) + " " + plural + ".")));
                }
            }
            if (!ok) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("operation failed (maybe prefix didn't exist?)")));
            }
        });
    return &cmd;
}

} // namespace utility
} // namespace commands
