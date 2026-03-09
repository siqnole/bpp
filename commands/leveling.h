#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/moderation/permission_operations.h"
#include <dpp/dpp.h>
#include <random>
#include <chrono>

using namespace bronx::db;

namespace commands {

std::vector<Command*> get_leveling_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    // rank/level command to show user's current level
    static Command* rank_cmd = new Command("rank", "view your or someone else's level and XP", "leveling", {"level", "xp"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t target_id = event.msg.author.id;
            
            // Check if a user was mentioned
            if (!event.msg.mentions.empty()) {
                target_id = event.msg.mentions[0].first.id;
            }
            
            // Get global XP
            auto global_xp = db->get_user_xp(target_id);
            if (!global_xp) {
                db->create_user_xp(target_id);
                global_xp = db->get_user_xp(target_id);
            }
            
            std::string description = "**Global Level**: " + std::to_string(global_xp->level) + "\n";
            description += "**Total XP**: " + format_number(global_xp->total_xp) + "\n";
            
            uint64_t xp_for_next = db->calculate_xp_for_next_level(global_xp->level);
            uint64_t xp_for_current = db->calculate_xp_for_level(global_xp->level);
            uint64_t xp_progress = global_xp->total_xp - xp_for_current;
            uint64_t xp_needed = xp_for_next - xp_for_current;
            
            description += "**Progress to Level " + std::to_string(global_xp->level + 1) + "**: ";
            description += format_number(xp_progress) + "/" + format_number(xp_needed) + "\n";
            
            // Calculate progress bar
            int bar_length = 20;
            int filled = static_cast<int>((static_cast<double>(xp_progress) / xp_needed) * bar_length);
            std::string bar = "[";
            for (int i = 0; i < bar_length; i++) {
                bar += (i < filled) ? "▓" : "░";
            }
            bar += "]";
            description += bar + "\n\n";
            
            // Get global rank
            int global_rank = db->get_user_global_xp_rank(target_id);
            if (global_rank > 0) {
                description += "**Global Rank**: #" + std::to_string(global_rank) + "\n\n";
            }
            
            // Get server XP if in a guild
            if (event.msg.guild_id) {
                auto server_xp = db->get_server_xp(target_id, event.msg.guild_id);
                if (!server_xp) {
                    db->create_server_xp(target_id, event.msg.guild_id);
                    server_xp = db->get_server_xp(target_id, event.msg.guild_id);
                }
                
                description += "**Server Level**: " + std::to_string(server_xp->server_level) + "\n";
                description += "**Server XP**: " + format_number(server_xp->server_xp) + "\n";
                
                uint64_t s_xp_for_next = db->calculate_xp_for_next_level(server_xp->server_level);
                uint64_t s_xp_for_current = db->calculate_xp_for_level(server_xp->server_level);
                uint64_t s_xp_progress = server_xp->server_xp - s_xp_for_current;
                uint64_t s_xp_needed = s_xp_for_next - s_xp_for_current;
                
                description += "**Progress to Level " + std::to_string(server_xp->server_level + 1) + "**: ";
                description += format_number(s_xp_progress) + "/" + format_number(s_xp_needed) + "\n";
                
                int s_filled = static_cast<int>((static_cast<double>(s_xp_progress) / s_xp_needed) * bar_length);
                std::string s_bar = "[";
                for (int i = 0; i < bar_length; i++) {
                    s_bar += (i < s_filled) ? "▓" : "░";
                }
                s_bar += "]";
                description += s_bar + "\n\n";
                
                int server_rank = db->get_user_server_xp_rank(target_id, event.msg.guild_id);
                if (server_rank > 0) {
                    description += "**Server Rank**: #" + std::to_string(server_rank) + "\n";
                }
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_title("📊 Rank Info");
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t target_id = event.command.get_issuing_user().id;
            
            // Get global XP
            auto global_xp = db->get_user_xp(target_id);
            if (!global_xp) {
                db->create_user_xp(target_id);
                global_xp = db->get_user_xp(target_id);
            }
            
            std::string description = "**Global Level**: " + std::to_string(global_xp->level) + "\n";
            description += "**Total XP**: " + format_number(global_xp->total_xp) + "\n";
            
            uint64_t xp_for_next = db->calculate_xp_for_next_level(global_xp->level);
            uint64_t xp_for_current = db->calculate_xp_for_level(global_xp->level);
            uint64_t xp_progress = global_xp->total_xp - xp_for_current;
            uint64_t xp_needed = xp_for_next - xp_for_current;
            
            description += "**Progress to Level " + std::to_string(global_xp->level + 1) + "**: ";
            description += format_number(xp_progress) + "/" + format_number(xp_needed) + "\n";
            
            // Get global rank
            int global_rank = db->get_user_global_xp_rank(target_id);
            if (global_rank > 0) {
                description += "**Global Rank**: #" + std::to_string(global_rank) + "\n\n";
            }
            
            // Get server XP if in a guild
            if (event.command.guild_id) {
                auto server_xp = db->get_server_xp(target_id, event.command.guild_id);
                if (!server_xp) {
                    db->create_server_xp(target_id, event.command.guild_id);
                    server_xp = db->get_server_xp(target_id, event.command.guild_id);
                }
                
                description += "**Server Level**: " + std::to_string(server_xp->server_level) + "\n";
                description += "**Server XP**: " + format_number(server_xp->server_xp) + "\n";
                
                int server_rank = db->get_user_server_xp_rank(target_id, event.command.guild_id);
                if (server_rank > 0) {
                    description += "**Server Rank**: #" + std::to_string(server_rank) + "\n";
                }
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_title("📊 Rank Info");
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {}
    );
    cmds.push_back(rank_cmd);
    
    // levelconfig command for admins to configure leveling
    static Command* levelconfig_cmd = new Command("levelconfig", "configure server leveling settings", "leveling", {"lvlcfg"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            
            uint64_t guild_id = event.msg.guild_id;
            
            // Check admin permission
            bool is_allowed = permission_operations::is_admin(db, event.msg.author.id, guild_id);
            if (!is_allowed) {
                // Check Discord administrator permission
                dpp::guild* g = dpp::find_guild(guild_id);
                if (g && g->base_permissions(event.msg.member).can(dpp::p_administrator)) {
                    is_allowed = true;
                }
            }
            if (!is_allowed) {
                bronx::send_message(bot, event, bronx::error("administrator permission required"));
                return;
            }
            
            // Get or create config
            auto config = db->get_server_leveling_config(guild_id);
            if (!config) {
                db->create_server_leveling_config(guild_id);
                config = db->get_server_leveling_config(guild_id);
            }
            
            if (args.empty()) {
                // Show current configuration
                std::string desc = "**Leveling Configuration**\n\n";
                desc += "**Enabled**: " + std::string(config->enabled ? "Yes" : "No") + "\n";
                desc += "**Reward Coins**: " + std::string(config->reward_coins ? "Yes" : "No") + "\n";
                desc += "**Coins Per Message**: $" + std::to_string(config->coins_per_message) + "\n";
                desc += "**XP Per Message**: " + std::to_string(config->min_xp_per_message) + "-" + std::to_string(config->max_xp_per_message) + "\n";
                desc += "**Min Message Length**: " + std::to_string(config->min_message_chars) + " characters\n";
                desc += "**XP Cooldown**: " + std::to_string(config->xp_cooldown_seconds) + " seconds\n";
                desc += "**Announce Level-ups**: " + std::string(config->announce_levelup ? "Yes" : "No") + "\n";
                if (config->announcement_channel.has_value()) {
                    desc += "**Announcement Channel**: <#" + std::to_string(*config->announcement_channel) + ">\n";
                }
                desc += "\n**Commands**:\n";
                desc += "`levelconfig enable/disable` - toggle leveling\n";
                desc += "`levelconfig coins enable/disable` - toggle coin rewards\n";
                desc += "`levelconfig coinamount <amount>` - set coins per message\n";
                desc += "`levelconfig xp <min> <max>` - set XP range\n";
                desc += "`levelconfig minchars <number>` - set minimum message length\n";
                desc += "`levelconfig cooldown <seconds>` - set XP cooldown\n";
                desc += "`levelconfig announce enable/disable` - toggle level-up announcements\n";
                desc += "`levelconfig channel <#channel>` - set announcement channel\n";
                desc += "`levelconfig reset` - reset all server XP (admin only)\n";
                
                auto embed = bronx::create_embed(desc);
                embed.set_title("⚙️ Leveling Configuration");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "enable") {
                config->enabled = true;
                db->update_server_leveling_config(*config);
                bronx::send_message(bot, event, bronx::success("leveling enabled"));
            } else if (sub == "disable") {
                config->enabled = false;
                db->update_server_leveling_config(*config);
                bronx::send_message(bot, event, bronx::success("leveling disabled"));
            } else if (sub == "coins") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig coins enable/disable`"));
                    return;
                }
                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);
                if (sub2 == "enable") {
                    config->reward_coins = true;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("coin rewards enabled"));
                } else if (sub2 == "disable") {
                    config->reward_coins = false;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("coin rewards disabled"));
                }
            } else if (sub == "coinamount") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig coinamount <amount>`"));
                    return;
                }
                try {
                    int amount = std::stoi(args[1]);
                    if (amount < 0) amount = 0;
                    config->coins_per_message = amount;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("set coins per message to $" + std::to_string(amount)));
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid number"));
                }
            } else if (sub == "xp") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig xp <min> <max>`"));
                    return;
                }
                try {
                    int min_xp = std::stoi(args[1]);
                    int max_xp = std::stoi(args[2]);
                    if (min_xp < 1) min_xp = 1;
                    if (max_xp < min_xp) max_xp = min_xp;
                    config->min_xp_per_message = min_xp;
                    config->max_xp_per_message = max_xp;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("set XP range to " + std::to_string(min_xp) + "-" + std::to_string(max_xp)));
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid numbers"));
                }
            } else if (sub == "minchars") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig minchars <number>`"));
                    return;
                }
                try {
                    int chars = std::stoi(args[1]);
                    if (chars < 0) chars = 0;
                    config->min_message_chars = chars;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("set minimum message length to " + std::to_string(chars) + " characters"));
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid number"));
                }
            } else if (sub == "cooldown") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig cooldown <seconds>`"));
                    return;
                }
                try {
                    int seconds = std::stoi(args[1]);
                    if (seconds < 0) seconds = 0;
                    config->xp_cooldown_seconds = seconds;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("set XP cooldown to " + std::to_string(seconds) + " seconds"));
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid number"));
                }
            } else if (sub == "announce") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig announce enable/disable`"));
                    return;
                }
                std::string sub2 = args[1];
                std::transform(sub2.begin(), sub2.end(), sub2.begin(), ::tolower);
                if (sub2 == "enable") {
                    config->announce_levelup = true;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("level-up announcements enabled"));
                } else if (sub2 == "disable") {
                    config->announce_levelup = false;
                    db->update_server_leveling_config(*config);
                    bronx::send_message(bot, event, bronx::success("level-up announcements disabled"));
                }
            } else if (sub == "channel") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelconfig channel <#channel|channel-name|channel-id>`"));
                    return;
                }
                
                uint64_t channel_id = 0;
                
                // Check if it's a channel mention
                if (!event.msg.mention_channels.empty()) {
                    channel_id = event.msg.mention_channels[0].id;
                } else {
                    // Try to parse as channel ID or find by name
                    std::string input = args[1];
                    
                    // Try parsing as numeric ID
                    try {
                        channel_id = std::stoull(input);
                        // Verify it exists
                        if (!dpp::find_channel(channel_id)) {
                            channel_id = 0;
                        }
                    } catch (...) {
                        // Try to find channel by name
                        // Use async API to get channels for this guild
                        bot.channels_get(guild_id, [&event, &bot, db, input, guild_id](const dpp::confirmation_callback_t& callback) {
                            if (callback.is_error()) {
                                bronx::send_message(bot, event, bronx::error("failed to retrieve server channels"));
                                return;
                            }
                            
                            std::string input_lower = input;
                            std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
                            
                            // Remove # prefix if present
                            if (!input_lower.empty() && input_lower[0] == '#') {
                                input_lower = input_lower.substr(1);
                            }
                            
                            auto channels = std::get<dpp::channel_map>(callback.value);
                            dpp::snowflake channel_id = 0;
                            
                            for (const auto& [ch_id, channel] : channels) {
                                std::string ch_name = channel.name;
                                std::transform(ch_name.begin(), ch_name.end(), ch_name.begin(), ::tolower);
                                if (ch_name == input_lower) {
                                    channel_id = ch_id;
                                    break;
                                }
                            }
                            
                            if (channel_id == 0) {
                                bronx::send_message(bot, event, bronx::error("channel not found - use a channel mention (#general), channel name (general), or channel ID"));
                                return;
                            }
                            
                            // Update the config
                            auto config = db->get_server_leveling_config(guild_id);
                            if (!config) {
                                bronx::send_message(bot, event, bronx::error("failed to retrieve configuration"));
                                return;
                            }
                            
                            config->announcement_channel = channel_id;
                            db->update_server_leveling_config(*config);
                            bronx::send_message(bot, event, bronx::success("set announcement channel to <#" + std::to_string(channel_id) + ">"));
                        });
                        return;
                    }
                }
                
                if (channel_id == 0) {
                    bronx::send_message(bot, event, bronx::error("channel not found - use a channel mention (#general), channel name (general), or channel ID"));
                    return;
                }
                
                config->announcement_channel = channel_id;
                db->update_server_leveling_config(*config);
                bronx::send_message(bot, event, bronx::success("set announcement channel to <#" + std::to_string(channel_id) + ">"));
            } else if (sub == "reset") {
                if (db->reset_server_xp(guild_id)) {
                    bronx::send_message(bot, event, bronx::success("reset all server XP"));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to reset server XP"));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("unknown subcommand. Use `levelconfig` to see available commands"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("use message mode for configuration commands")));
        },
        {}
    );
    cmds.push_back(levelconfig_cmd);
    
    // levelroles command to manage role rewards
    static Command* levelroles_cmd = new Command("levelroles", "manage level role rewards", "leveling", {"lvlroles"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            
            uint64_t guild_id = event.msg.guild_id;
            
            // Check admin permission
            bool is_allowed = permission_operations::is_admin(db, event.msg.author.id, guild_id);
            if (!is_allowed) {
                // Check Discord administrator permission
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
                // List all level roles
                auto level_roles = db->get_level_roles(guild_id);
                
                std::string desc = "**Level Role Rewards**\n\n";
                
                if (level_roles.empty()) {
                    desc += "*No level roles configured yet*\n\n";
                } else {
                    for (const auto& lr : level_roles) {
                        desc += "**Level " + std::to_string(lr.level) + "**: <@&" + std::to_string(lr.role_id) + ">";
                        if (lr.remove_previous) desc += " *(removes previous)*";
                        desc += "\n";
                        if (!lr.description.empty()) {
                            desc += "*" + lr.description + "*\n";
                        }
                        desc += "\n";
                    }
                }
                
                desc += "**Commands**:\n";
                desc += "`levelroles add <level> <@role> [description]` - add a level role\n";
                desc += "`levelroles remove <level>` - remove a level role\n";
                
                auto embed = bronx::create_embed(desc);
                embed.set_title("🏆 Level Roles");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "add") {
                if (args.size() < 3 || event.msg.mention_roles.empty()) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelroles add <level> <@role> [description]`"));
                    return;
                }
                
                try {
                    uint32_t level = std::stoul(args[1]);
                    uint64_t role_id = event.msg.mention_roles[0];
                    
                    dpp::role* role_ptr = dpp::find_role(role_id);
                    std::string role_name = role_ptr ? role_ptr->name : "Unknown Role";
                    
                    std::string description;
                    if (args.size() > 3) {
                        for (size_t i = 3; i < args.size(); i++) {
                            if (i > 3) description += " ";
                            description += args[i];
                        }
                    }
                    
                    LevelRole lr;
                    lr.guild_id = guild_id;
                    lr.level = level;
                    lr.role_id = role_id;
                    lr.role_name = role_name;
                    lr.description = description;
                    lr.remove_previous = false;
                    
                    if (db->create_level_role(lr)) {
                        bronx::send_message(bot, event, bronx::success("added level role for level " + std::to_string(level)));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to add level role (level may already have a role)"));
                    }
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid level number"));
                }
            } else if (sub == "remove") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `levelroles remove <level>`"));
                    return;
                }
                
                try {
                    uint32_t level = std::stoul(args[1]);
                    if (db->delete_level_role(guild_id, level)) {
                        bronx::send_message(bot, event, bronx::success("removed level role for level " + std::to_string(level)));
                    } else {
                        bronx::send_message(bot, event, bronx::error("no level role found for that level"));
                    }
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid level number"));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("unknown subcommand. Use `levelroles` to see available commands"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("use message mode for level role management")));
        },
        {}
    );
    cmds.push_back(levelroles_cmd);
    
    return cmds;
}

} // namespace commands
