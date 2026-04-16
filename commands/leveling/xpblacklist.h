#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/moderation/permission_operations.h"
#include "../database/operations/leveling/xp_blacklist_operations.h"
#include <dpp/dpp.h>
#include <sstream>

using namespace bronx::db;

namespace commands {

inline std::vector<Command*> get_xpblacklist_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    // xpblacklist command - manage XP blacklists
    static Command* xpblacklist_cmd = new Command("xpblacklist", "manage XP blacklists for channels, roles, and users", "leveling", {"xpbl", "xpban"}, true,
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
                bronx::send_message(bot, event, bronx::error("you need admin permissions to manage XP blacklists"));
                return;
            }
            
            // Parse subcommand
            if (args.empty()) {
                std::string desc = "**XP Blacklist Management**\n\n";
                desc += "**channels:**\n";
                desc += "`xpblacklist channel add <#channel> [reason]` - block XP in a channel\n";
                desc += "`xpblacklist channel remove <#channel>` - unblock a channel\n";
                desc += "`xpblacklist channel list` - list blacklisted channels\n\n";
                desc += "**roles:**\n";
                desc += "`xpblacklist role add <@role> [reason]` - block XP for a role\n";
                desc += "`xpblacklist role remove <@role>` - unblock a role\n";
                desc += "`xpblacklist role list` - list blacklisted roles\n\n";
                desc += "**users:**\n";
                desc += "`xpblacklist user add <@user> [reason]` - block XP for a user\n";
                desc += "`xpblacklist user remove <@user>` - unblock a user\n";
                desc += "`xpblacklist user list` - list blacklisted users\n";
                
                bronx::send_message(bot, event, bronx::create_embed(desc));
                return;
            }
            
            std::string type = args[0]; // channel, role, or user
            
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: `xpblacklist <channel|role|user> <add|remove|list>`"));
                return;
            }
            
            std::string action = args[1]; // add, remove, or list
            
            // ===== CHANNEL BLACKLIST =====
            if (type == "channel") {
                if (action == "add") {
                    if (event.msg.message_reference.channel_id == 0 && event.msg.mentions.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist channel add <#channel> [reason]`"));
                        return;
                    }
                    
                    uint64_t channel_id = 0;
                    if (!event.msg.mentions.empty() && !event.msg.mention_channels.empty()) {
                        channel_id = event.msg.mention_channels[0].id;
                    }
                    
                    if (channel_id == 0) {
                        bronx::send_message(bot, event, bronx::error("please mention a channel with #channel"));
                        return;
                    }
                    
                    std::string reason;
                    if (args.size() > 2) {
                        for (size_t i = 2; i < args.size(); i++) {
                            if (i > 2) reason += " ";
                            reason += args[i];
                        }
                    }
                    
                    bool success = xp_blacklist_operations::add_blacklist(db, guild_id, "channel", channel_id, event.msg.author.id, reason);
                    
                    if (success) {
                        std::string msg = "<#" + std::to_string(channel_id) + "> is now blacklisted from XP";
                        if (!reason.empty()) msg += "\n**reason:** " + reason;
                        bronx::send_message(bot, event, bronx::success(msg));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to blacklist channel"));
                    }
                    
                } else if (action == "remove") {
                    if (event.msg.mention_channels.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist channel remove <#channel>`"));
                        return;
                    }
                    
                    uint64_t channel_id = event.msg.mention_channels[0].id;
                    bool success = xp_blacklist_operations::remove_blacklist(db, guild_id, "channel", channel_id);
                    
                    if (success) {
                        bronx::send_message(bot, event, bronx::success("<#" + std::to_string(channel_id) + "> is no longer blacklisted"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("channel not found in blacklist"));
                    }
                    
                } else if (action == "list") {
                    auto blacklist = xp_blacklist_operations::get_blacklist(db, guild_id, "channel");
                    
                    if (blacklist.empty()) {
                        bronx::send_message(bot, event, bronx::create_embed("no channels are blacklisted from XP"));
                        return;
                    }
                    
                    std::ostringstream desc;
                    desc << "**blacklisted channels** (" << blacklist.size() << ")\n\n";
                    
                    for (const auto& entry : blacklist) {
                        desc << "<#" << entry.target_id << ">";
                        if (!entry.reason.empty()) {
                            desc << " — " << entry.reason;
                        }
                        desc << "\n";
                    }
                    
                    bronx::send_message(bot, event, bronx::create_embed(desc.str()));
                }
                
            // ===== ROLE BLACKLIST =====
            } else if (type == "role") {
                if (action == "add") {
                    if (event.msg.mention_roles.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist role add <@role> [reason]`"));
                        return;
                    }
                    
                    uint64_t role_id = event.msg.mention_roles[0];
                    
                    std::string reason;
                    if (args.size() > 2) {
                        for (size_t i = 2; i < args.size(); i++) {
                            if (i > 2) reason += " ";
                            reason += args[i];
                        }
                    }
                    
                    bool success = xp_blacklist_operations::add_blacklist(db, guild_id, "role", role_id, event.msg.author.id, reason);
                    
                    if (success) {
                        std::string msg = "<@&" + std::to_string(role_id) + "> is now blacklisted from XP";
                        if (!reason.empty()) msg += "\n**reason:** " + reason;
                        bronx::send_message(bot, event, bronx::success(msg));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to blacklist role"));
                    }
                    
                } else if (action == "remove") {
                    if (event.msg.mention_roles.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist role remove <@role>`"));
                        return;
                    }
                    
                    uint64_t role_id = event.msg.mention_roles[0];
                    bool success = xp_blacklist_operations::remove_blacklist(db, guild_id, "role", role_id);
                    
                    if (success) {
                        bronx::send_message(bot, event, bronx::success("<@&" + std::to_string(role_id) + "> is no longer blacklisted"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("role not found in blacklist"));
                    }
                    
                } else if (action == "list") {
                    auto blacklist = xp_blacklist_operations::get_blacklist(db, guild_id, "role");
                    
                    if (blacklist.empty()) {
                        bronx::send_message(bot, event, bronx::create_embed("no roles are blacklisted from XP"));
                        return;
                    }
                    
                    std::ostringstream desc;
                    desc << "**blacklisted roles** (" << blacklist.size() << ")\n\n";
                    
                    for (const auto& entry : blacklist) {
                        desc << "<@&" << entry.target_id << ">";
                        if (!entry.reason.empty()) {
                            desc << " — " << entry.reason;
                        }
                        desc << "\n";
                    }
                    
                    bronx::send_message(bot, event, bronx::create_embed(desc.str()));
                }
                
            // ===== USER BLACKLIST =====
            } else if (type == "user") {
                if (action == "add") {
                    if (event.msg.mentions.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist user add <@user> [reason]`"));
                        return;
                    }
                    
                    uint64_t target_id = event.msg.mentions[0].first.id;
                    
                    std::string reason;
                    if (args.size() > 2) {
                        for (size_t i = 2; i < args.size(); i++) {
                            if (i > 2) reason += " ";
                            reason += args[i];
                        }
                    }
                    
                    bool success = xp_blacklist_operations::add_blacklist(db, guild_id, "user", target_id, event.msg.author.id, reason);
                    
                    if (success) {
                        std::string msg = "<@" + std::to_string(target_id) + "> is now blacklisted from XP";
                        if (!reason.empty()) msg += "\n**reason:** " + reason;
                        bronx::send_message(bot, event, bronx::success(msg));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to blacklist user"));
                    }
                    
                } else if (action == "remove") {
                    if (event.msg.mentions.empty()) {
                        bronx::send_message(bot, event, bronx::error("usage: `xpblacklist user remove <@user>`"));
                        return;
                    }
                    
                    uint64_t target_id = event.msg.mentions[0].first.id;
                    bool success = xp_blacklist_operations::remove_blacklist(db, guild_id, "user", target_id);
                    
                    if (success) {
                        bronx::send_message(bot, event, bronx::success("<@" + std::to_string(target_id) + "> is no longer blacklisted"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("user not found in blacklist"));
                    }
                    
                } else if (action == "list") {
                    auto blacklist = xp_blacklist_operations::get_blacklist(db, guild_id, "user");
                    
                    if (blacklist.empty()) {
                        bronx::send_message(bot, event, bronx::create_embed("no users are blacklisted from XP"));
                        return;
                    }
                    
                    std::ostringstream desc;
                    desc << "**blacklisted users** (" << blacklist.size() << ")\n\n";
                    
                    for (const auto& entry : blacklist) {
                        desc << "<@" << entry.target_id << ">";
                        if (!entry.reason.empty()) {
                            desc << " — " << entry.reason;
                        }
                        desc << "\n";
                    }
                    
                    bronx::send_message(bot, event, bronx::create_embed(desc.str()));
                }
                
            } else {
                bronx::send_message(bot, event, bronx::error("invalid type. use `channel`, `role`, or `user`"));
            }
        },
        nullptr, // no slash command
        {}
    );
    
    cmds.push_back(xpblacklist_cmd);
    
    return cmds;
}

} // namespace commands
