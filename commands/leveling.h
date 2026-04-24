#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/moderation/permission_operations.h"
#include <dpp/dpp.h>
#include <random>
#include <chrono>
#include <algorithm>
#include "../utils/string_utils.h"
#include "../utils/colors.h"

using namespace bronx::db;

namespace commands {

inline std::vector<Command*> get_leveling_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    // rank/level command to show user's current level
    static Command* rank_cmd = new Command("rank", "view your or someone else's level and XP", "leveling", {"xp"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t target_id = event.msg.author.id;
            if (!event.msg.mentions.empty()) {
                target_id = event.msg.mentions.begin()->first.id;
            }
            
            auto xp_data = db->get_user_xp(target_id, event.msg.guild_id);
            if (!xp_data) {
                bronx::send_message(bot, event, bronx::error("could not find any XP data for that user."));
                return;
            }

            dpp::user* target_user = dpp::find_user(target_id);
            std::string username = target_user ? target_user->username : std::to_string(target_id);
            
            std::string desc = "📊 **rank summary**\n\n"
                               "✨ **level**: " + std::to_string(xp_data->level) + "\n"
                               "📈 **xp**: " + std::to_string(xp_data->total_xp) + " / " + std::to_string(db->calculate_xp_for_next_level(xp_data->level));
            
            auto embed = bronx::create_embed(desc);
            embed.set_title(username + "'s leveling progress");
            embed.set_color(bronx::COLOR_INFO);
            if (target_user) embed.set_thumbnail(target_user->get_avatar_url());
            
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            dpp::snowflake target_id = event.command.usr.id;
            auto it = event.get_parameter("user");
            if (it.index() != 0) {
                target_id = std::get<dpp::snowflake>(it);
            }

            auto xp_data = db->get_user_xp(target_id, event.command.guild_id);
            if (!xp_data) {
                event.reply(dpp::message().add_embed(bronx::error("could not find any XP data for that user.")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::user* target_user = dpp::find_user(target_id);
            std::string username = target_user ? target_user->username : std::to_string(target_id);
            
            std::string desc = "📊 **rank summary**\n\n"
                               "✨ **level**: " + std::to_string(xp_data->level) + "\n"
                               "📈 **xp**: " + std::to_string(xp_data->total_xp) + " / " + std::to_string(db->calculate_xp_for_next_level(xp_data->level));
            
            auto embed = bronx::create_embed(desc);
            embed.set_title(username + "'s leveling progress");
            embed.set_color(bronx::COLOR_INFO);
            if (target_user) embed.set_thumbnail(target_user->get_avatar_url());
            
            event.reply(dpp::message().add_embed(embed));
        },
        { dpp::command_option(dpp::co_user, "user", "the user to view rank for", false) }
    );
    cmds.push_back(rank_cmd);
    
    // Combined level command group
    static Command* level_cmd = new Command("level", "configure and manage server leveling", "leveling", {"lvl"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            
            uint64_t guild_id = event.msg.guild_id;
            
            // Check admin permission
            bool is_allowed = permission_operations::is_admin(db, event.msg.author.id, guild_id);
            if (!is_allowed) {
                dpp::guild* g = dpp::find_guild(guild_id);
                if (g && g->base_permissions(event.msg.member).can(dpp::p_administrator)) {
                    is_allowed = true;
                }
            }
            if (!is_allowed) {
                bronx::send_message(bot, event, bronx::error("administrator permission required"));
                return;
            }

            if (args.empty()) {
                std::string desc = "**leveling system management**\n\n"
                                   "use the subcommands below to manage settings and rewards.\n\n"
                                   "🛠️ `level config` — view and edit system settings\n"
                                   "🏆 `level roles` — manage role rewards\n\n"
                                   "💡 *tip: try `level config message` to customize level-up announcements!*";
                auto embed = bronx::create_embed(desc);
                embed.set_title("⚙️ leveling group");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            // --- Level Config Subcommand ---
            if (sub == "config" || sub == "cfg") {
                auto config = db->get_guild_leveling_config(guild_id);
                if (!config) {
                    db->create_guild_leveling_config(guild_id);
                    config = db->get_guild_leveling_config(guild_id);
                }
                if (!config) {
                    bronx::send_message(bot, event, bronx::error("failed to load leveling config — database error"));
                    return;
                }

                if (args.size() == 1) { // Show dashboard
                    std::string desc = "**leveling configuration dashboard**\n\n";
                    desc += "✨ **status**: " + std::string(config->enabled ? "✅ enabled" : "❌ disabled") + "\n";
                    desc += "💰 **coin rewards**: " + std::string(config->reward_coins ? "enabled ($" + std::to_string(config->coins_per_message) + "/msg)" : "disabled") + "\n";
                    desc += "📈 **xp gain**: " + std::to_string(config->min_xp_per_message) + "–" + std::to_string(config->max_xp_per_message) + " xp per message\n";
                    desc += "⏱️ **cooldown**: " + std::to_string(config->xp_cooldown_seconds) + " seconds\n";
                    desc += "📏 **min chars**: " + std::to_string(config->min_message_chars) + " characters\n\n";
                    
                    desc += "**announcements**\n";
                    desc += "📣 **enabled**: " + std::string(config->announce_levelup ? "yes" : "no") + "\n";
                    desc += "📍 **channel**: " + (config->announcement_channel.has_value() ? "<#" + std::to_string(*config->announcement_channel) + ">" : "*not set (uses current channel)*") + "\n";
                    desc += "✉️ **message**: `" + config->announcement_message + "`\n\n";
                    
                    desc += "**commands**\n";
                    desc += "• `level config enable/disable`\n";
                    desc += "• `level config xp <min> <max>`\n";
                    desc += "• `level config cooldown <seconds>`\n";
                    desc += "• `level config channel <#channel>`\n";
                    desc += "• `level config message <template>`\n";
                    desc += "• `level config preview` — preview announcement\n";
                    desc += "• `level config reset` — resets all server xp\n";
                    
                    auto embed = bronx::create_embed(desc);
                    embed.set_title("⚙️ server leveling dashboard");
                    embed.set_color(bronx::COLOR_INFO);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }

                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);

                if (sub2 == "enable") {
                    config->enabled = true;
                    db->update_guild_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("leveling system enabled"));
                } else if (sub2 == "disable") {
                    config->enabled = false;
                    db->update_guild_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("leveling system disabled"));
                } else if (sub2 == "message") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config message <text>`\n\n**placeholders:**\n`{name}` — user mention\n`{level}` — new level\n`{members}` — server members\n`{date}` — current date\n`{createdat}` — server creation date"));
                        return;
                    }
                    std::string new_msg;
                    for (size_t i = 2; i < args.size(); i++) {
                        if (i > 2) new_msg += " ";
                        new_msg += args[i];
                    }
                    if (new_msg.length() > 500) {
                        bronx::send_message(bot, event, bronx::error("message template too long (max 500 chars)"));
                        return;
                    }
                    config->announcement_message = new_msg;
                    db->update_guild_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("level-up message updated!"));
                } else if (sub2 == "preview") {
                    std::unordered_map<std::string, std::string> placeholders;
                    placeholders["name"] = event.msg.author.get_mention();
                    placeholders["level"] = "10";
                    placeholders["members"] = "420";
                    placeholders["date"] = "2026-04-22";
                    placeholders["createdat"] = "2024-01-01";

                    std::string preview = bronx::utils::replace_placeholders(config->announcement_message, placeholders);
                    
                    auto embed = bronx::create_embed("### 📣 announcement preview\n\n" + preview);
                    embed.set_title("✨ level up preview");
                    embed.set_footer(dpp::embed_footer().set_text("tip: placeholders are {name}, {level}, {members}, {date}, {createdat}"));
                    bronx::send_message(bot, event, embed);
                } else if (sub2 == "channel") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config channel <#channel | reset>`"));
                        return;
                    }
                    if (args[2] == "reset" || args[2] == "none") {
                        config->announcement_channel = std::nullopt;
                        db->update_guild_leveling_config(*config);
                        bronx::send_message(bot, event, bronx::success("announcement channel reset (now uses original channel)"));
                    } else if (!event.msg.mention_channels.empty()) {
                        config->announcement_channel = event.msg.mention_channels[0].id;
                        db->update_guild_leveling_config(*config);
                        bronx::send_message(bot, event, bronx::success("announcements set to <#" + std::to_string(*config->announcement_channel) + ">"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("please mention a valid channel (e.g. #logs)"));
                    }
                } else if (sub2 == "xp") {
                    if (args.size() < 4) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config xp <min> <max>`"));
                        return;
                    }
                    try {
                        int min_xp = std::stoi(args[2]);
                        int max_xp = std::stoi(args[3]);
                        if (min_xp < 1) min_xp = 1;
                        if (max_xp < min_xp) max_xp = min_xp;
                        config->min_xp_per_message = min_xp;
                        config->max_xp_per_message = max_xp;
                        db->update_guild_leveling_config(*config);
                        bronx::send_message(bot, event, bronx::success("xp range set to **" + std::to_string(min_xp) + "–" + std::to_string(max_xp) + "**"));
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid numbers")); }
                } else if (sub2 == "cooldown") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level config cooldown <seconds>`"));
                        return;
                    }
                    try {
                        int seconds = std::stoi(args[2]);
                        config->xp_cooldown_seconds = std::max(0, seconds);
                        db->update_guild_leveling_config(*config);
                        bronx::send_message(bot, event, bronx::success("xp cooldown set to **" + std::to_string(config->xp_cooldown_seconds) + " seconds**"));
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid number")); }
                } else if (sub2 == "reset") {
                    if (db->reset_guild_xp(guild_id)) {
                        bronx::send_message(bot, event, bronx::success("reset all server XP successfully"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to reset server XP"));
                    }
                } else {
                    bronx::send_message(bot, event, bronx::error("unknown config option. use `level config` to see all settings."));
                }
            } 
            // --- Level Roles Subcommand ---
            else if (sub == "roles" || sub == "reward") {
                if (args.size() == 1) { // List roles
                    auto level_roles = db->get_level_roles(guild_id);
                    std::string desc = "**level role rewards**\n\n";
                    if (level_roles.empty()) {
                        desc += "*no role rewards configured yet.*\n\n";
                    } else {
                        for (const auto& lr : level_roles) {
                            desc += "🏅 **level " + std::to_string(lr.level) + "**: <@&" + std::to_string(lr.role_id) + ">\n";
                        }
                        desc += "\n";
                    }
                    desc += "**commands**\n";
                    desc += "• `level roles add <level> <@role>`\n";
                    desc += "• `level roles remove <level>`\n";
                    
                    auto embed = bronx::create_embed(desc);
                    embed.set_title("🏆 level rewards");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }

                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);

                if (sub2 == "add") {
                    if (args.size() < 4 || event.msg.mention_roles.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `level roles add <level> <@role>`"));
                        return;
                    }
                    try {
                        uint32_t level = std::stoul(args[2]);
                        uint64_t role_id = event.msg.mention_roles[0];
                        dpp::role* role_ptr = dpp::find_role(role_id);
                        
                        LevelRole lr;
                        lr.guild_id = guild_id;
                        lr.level = level;
                        lr.role_id = role_id;
                        lr.role_name = role_ptr ? role_ptr->name : "unknown role";
                        lr.remove_previous = false;
                        
                        if (db->create_level_role(lr)) {
                            bronx::send_message(bot, event, bronx::success("added reward info: Level **" + std::to_string(level) + "** -> <@&" + std::to_string(role_id) + ">"));
                        } else {
                            bronx::send_message(bot, event, bronx::error("failed to add reward (level may already have a role)"));
                        }
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid level number")); }
                } else if (sub2 == "remove") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: `level roles remove <level>`"));
                        return;
                    }
                    try {
                        uint32_t level = std::stoul(args[2]);
                        if (db->delete_level_role(guild_id, level)) {
                            bronx::send_message(bot, event, bronx::success("removed reward for level **" + std::to_string(level) + "**"));
                        } else {
                            bronx::send_message(bot, event, bronx::error("no reward found for that level"));
                        }
                    } catch (...) { bronx::send_message(bot, event, bronx::error("invalid level number")); }
                } else {
                    bronx::send_message(bot, event, bronx::error("unknown role command. use `level roles` to see help."));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("unknown subcommand. use `level config` or `level roles`."));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("please use text commands for leveling configuration")));
        },
        {}
    );
    cmds.push_back(level_cmd);
    
    return cmds;
}

} // namespace commands
