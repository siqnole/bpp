#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <regex>
#include <algorithm>
#include "text_filter_config.h"  // for mirroring text filter emoji lists
#include "mod_log.h"

namespace commands {
namespace quiet_moderation {

// Structure to hold reaction filter settings for a guild
struct ReactionFilterConfig {
    bool enabled = false;
    ::std::set<::std::string> blocked_emoji_names;  // Emoji names to block (case-insensitive)
    ::std::set<::std::string> blocked_emoji_patterns;  // Regex patterns to match emoji names
    ::std::set<dpp::snowflake> whitelist_roles;    // Roles exempt from filter
    ::std::set<dpp::snowflake> whitelist_users;    // Users exempt from filter
    ::std::set<dpp::snowflake> blacklist_roles;
    ::std::set<dpp::snowflake> blacklist_users;
    ::std::set<dpp::snowflake> blacklist_channels;
    bool use_whitelist = true;
    bool use_blacklist = false;
    bool log_violations = true;
    dpp::snowflake log_channel = 0;

    // Primary action: "remove" (default) will remove the triggering reaction.
    // Other supported values: "none", "timeout", "kick", "ban"
    ::std::string action = "remove";
    uint32_t action_timeout_seconds = 0; // used when action == "timeout"

    // Backwards-compatible toggle (legacy); if true reactions are removed/deleted.
    // ...existing code...
    int max_violations_before_timeout = 3;       // 0 = no timeout
    uint32_t timeout_duration = 300;             // seconds (5 minutes default)

    // When true, reaction filter will also check the server's text filter blocked emoji names/patterns
    // instead of (or in addition to) its own lists.
    bool mirror_text_filter = false;
};

// Global storage for guild reaction filter configs
static ::std::map<dpp::snowflake, ReactionFilterConfig> guild_reaction_filters;
static ::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> user_violations;  // guild_id -> user_id -> count

// Helper: Check if emoji name matches any blocked patterns
inline bool is_emoji_blocked(const ::std::string& emoji_name, const ReactionFilterConfig& config) {
    ::std::string lower_name = emoji_name;
    ::std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    // Check exact name matches
    for (const auto& blocked : config.blocked_emoji_names) {
        ::std::string lower_blocked = blocked;
        ::std::transform(lower_blocked.begin(), lower_blocked.end(), lower_blocked.begin(), ::tolower);
        if (lower_name.find(lower_blocked) != ::std::string::npos) {
            return true;
        }
    }
    
    // Check regex patterns
    for (const auto& pattern : config.blocked_emoji_patterns) {
        try {
            ::std::regex regex_pattern(pattern, ::std::regex::icase);
            if (::std::regex_search(emoji_name, regex_pattern)) {
                return true;
            }
        } catch (...) {
            // Invalid regex, skip
        }
    }
    
    return false;
}

// Helper: Check if user is whitelisted
inline bool is_user_whitelisted(const dpp::snowflake& guild_id, const dpp::snowflake& user_id, 
                                 const ::std::vector<dpp::snowflake>& user_roles, 
                                 const ReactionFilterConfig& config) {
    if (!config.use_whitelist) return false;

    // Blacklist overrides whitelist
    if (config.use_blacklist) {
        if (config.blacklist_users.count(user_id) > 0) return false;
        for (const auto& role : user_roles) {
            if (config.blacklist_roles.count(role) > 0) return false;
        }
    }

    // Check user whitelist
    if (config.whitelist_users.count(user_id) > 0) {
        return true;
    }
    
    // Check role whitelist
    for (const auto& role : user_roles) {
        if (config.whitelist_roles.count(role) > 0) {
            return true;
        }
    }
    
    return false;
}

// Register reaction filter event handler
inline void register_reaction_filter(dpp::cluster& bot) {
    bot.on_message_reaction_add([&bot](const dpp::message_reaction_add_t& event) {
        // Check if filtering is enabled for this guild
        if (guild_reaction_filters.find(event.reacting_guild.id) == guild_reaction_filters.end()) {
            return;
        }
        
        auto& config = guild_reaction_filters[event.reacting_guild.id];
        if (!config.enabled) {
            return;
        }
        
        // Get emoji name
        ::std::string emoji_name;
        if (event.reacting_emoji.id != 0) {
            // Custom emoji - use its name
            emoji_name = event.reacting_emoji.name;
        } else {
            // Unicode emoji - use the unicode string as name
            emoji_name = event.reacting_emoji.name;
        }
        
        // Determine whether emoji is blocked by this config OR (if mirroring) by the text-filter lists
        bool blocked = is_emoji_blocked(emoji_name, config);
        if (!blocked && config.mirror_text_filter) {
            // consult text filter lists if available
            auto it = guild_text_filters.find(event.reacting_guild.id);
            if (it != guild_text_filters.end()) {
                const auto& tf = it->second;
                // check exact emoji name matches
                ::std::string lower_emoji = emoji_name;
                ::std::transform(lower_emoji.begin(), lower_emoji.end(), lower_emoji.begin(), ::tolower);
                for (const auto& b : tf.blocked_emoji_names) {
                    ::std::string lb = b;
                    ::std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
                    if (lower_emoji.find(lb) != ::std::string::npos) { blocked = true; break; }
                }
                // check patterns
                if (!blocked) {
                    for (const auto& pat : tf.blocked_patterns) {
                        try {
                            ::std::regex r(pat, ::std::regex::icase);
                            if (::std::regex_search(emoji_name, r)) { blocked = true; break; }
                        } catch (...) { }
                    }
                }
            }
        }

        if (!blocked) {
            return;
        }

        // Get user roles to check whitelist
        // copy config so we can safely capture in nested lambdas
        auto config_copy = config;
        bot.guild_get_member(event.reacting_guild.id, event.reacting_user.id, 
            [&bot, event, emoji_name, blocked, config_copy](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    return;
                }
                
                auto member = callback.get<dpp::guild_member>();
                
                // Check if user is whitelisted
                if (is_user_whitelisted(event.reacting_guild.id, event.reacting_user.id, 
                                       member.get_roles(), config_copy)) {
                    return;
                }
                
                // User violated the filter
                ::commands::quiet_moderation::user_violations[event.reacting_guild.id][event.reacting_user.id]++;
                int violation_count = ::commands::quiet_moderation::user_violations[event.reacting_guild.id][event.reacting_user.id];

                // Perform configured action (default = "remove")
                if (config_copy.action == "remove") {
                    // remove the user's reaction
                    bot.message_delete_reaction(event.message_id, event.channel_id, event.reacting_user.id, emoji_name);
                }
                else if (config_copy.action == "timeout") {
                    bot.guild_member_timeout(event.reacting_guild.id, event.reacting_user.id, time(0) + (config_copy.action_timeout_seconds ? config_copy.action_timeout_seconds : config_copy.timeout_duration));
                }
                else if (config_copy.action == "kick") {
                    bot.guild_member_delete(event.reacting_guild.id, event.reacting_user.id);
                }
                else if (config_copy.action == "ban") {
                    bot.guild_ban_add(event.reacting_guild.id, event.reacting_user.id, 0);
                }
                // else "none" -> only log/warn
                
                // Log violation
                if (config_copy.log_violations && config_copy.log_channel != 0) {
                    dpp::embed log_embed = bronx::info("Reaction Filter Violation");
                    log_embed.add_field("User", "<@" + ::std::to_string(event.reacting_user.id) + ">", true);
                    log_embed.add_field("Channel", "<#" + ::std::to_string(event.channel_id) + ">", true);
                    log_embed.add_field("Emoji", emoji_name, true);
                    log_embed.add_field("Violations", ::std::to_string(violation_count), true);
                    log_embed.set_timestamp(time(0));

                    ::commands::quiet_moderation::send_embed_via_webhook(bot, config_copy.log_channel, log_embed);
                }
                
                // Warn user via DM
                bot.direct_message_create(event.reacting_user.id, 
                    dpp::message().add_embed(
                        bronx::info("Reaction Blocked")
                            .set_description("Your reaction with emoji `" + emoji_name + 
                                           "` was removed as it violates server rules.")
                    )
                );
                
                // Apply timeout if threshold reached (per-config)
                if (config_copy.max_violations_before_timeout > 0 && 
                    violation_count >= config_copy.max_violations_before_timeout) {
                    bot.guild_member_timeout(event.reacting_guild.id, event.reacting_user.id, 
                        time(0) + config_copy.timeout_duration,
                        [&bot, event, config_copy, violation_count](const dpp::confirmation_callback_t& cb) {
                            if (!cb.is_error() && config_copy.log_channel != 0) {
                                dpp::embed timeout_embed = bronx::error("User Timed Out");
                                timeout_embed.add_field("User", "<@" + ::std::to_string(event.reacting_user.id) + ">", true);
                                timeout_embed.add_field("Reason", "Reaction filter violations", true);
                                timeout_embed.add_field("Duration", ::std::to_string(config_copy.timeout_duration) + "s", true);
                                timeout_embed.add_field("Total Violations", ::std::to_string(violation_count), true);
                                timeout_embed.set_timestamp(time(0));
                                ::commands::quiet_moderation::send_embed_via_webhook(bot, config_copy.log_channel, timeout_embed);
                            }
                        }
                    );
                    // Reset violation count after timeout
                    ::commands::quiet_moderation::user_violations[event.reacting_guild.id][event.reacting_user.id] = 0;
                }
            }
        );
    });
}

// Command to configure reaction filter
inline Command* get_reaction_filter_command() {
    static Command reaction_filter("reactionfilter", "Configure reaction filtering for offensive emojis", "moderation", 
        {"rf", "reactfilter"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Require administrator permission
            auto member = event.msg.member;
            bool is_admin = false;
            if (event.msg.guild_id != 0) {
                dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                if (g && g->owner_id == event.msg.author.id) is_admin = true;
            }
            for (const auto &rid : member.get_roles()) {
                dpp::role* r = dpp::find_role(rid);
                if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) { is_admin = true; break; }
            }
            if (!is_admin) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You need Administrator permission to use this command")));
                return;
            }
            
            if (args.empty()) {
                // Show current config
                auto& config = guild_reaction_filters[event.msg.guild_id];
                
                dpp::embed embed = bronx::info("Reaction Filter Configuration");
                embed.add_field("Status", config.enabled ? "🟢 Enabled" : "🔴 Disabled", true);
                embed.add_field("Action", config.action, true);
                // embed.add_field("Delete Reactions", "Yes", true); // Legacy field removed
                embed.add_field("Mirror Text Filter", config.mirror_text_filter ? "Yes" : "No", true);
                // embed.add_field("Warn Users", "Yes", true); // Legacy field removed
                
                ::std::string blocked_names = config.blocked_emoji_names.empty() ? "None" : "";
                for (const auto& name : config.blocked_emoji_names) {
                    blocked_names += "`" + name + "` ";
                }
                embed.add_field("Blocked Emoji Names", blocked_names, false);
                
                ::std::string patterns = config.blocked_emoji_patterns.empty() ? "None" : "";
                for (const auto& pattern : config.blocked_emoji_patterns) {
                    patterns += "`" + pattern + "` ";
                }
                embed.add_field("Blocked Patterns", patterns, false);
                
                embed.add_field("Log Channel", config.log_channel != 0 ? "<#" + ::std::to_string(config.log_channel) + ">" : "Not set", true);
                embed.add_field("Timeout After", config.max_violations_before_timeout > 0 ? ::std::to_string(config.max_violations_before_timeout) + " violations" : "Disabled", true);
                
                ::std::string usage = "**Usage:**\n";
                usage += "`b.reactionfilter enable` - Enable filter\n";
                usage += "`b.reactionfilter disable` - Disable filter\n";
                usage += "`b.reactionfilter add <emoji_name>` - Block emoji by name\n";
                usage += "`b.reactionfilter remove <emoji_name>` - Unblock emoji name\n";
                usage += "`b.reactionfilter addpattern <regex>` - Add regex pattern\n";
                usage += "`b.reactionfilter removepattern <regex>` - Remove regex pattern\n";
                usage += "`b.reactionfilter list` - List blocked emoji names\n";
                usage += "`b.reactionfilter listpatterns` - List regex patterns\n";
                usage += "`b.reactionfilter logchannel <#channel>` - Set log channel\n";
                usage += "`b.reactionfilter timeout <violations> <seconds>` - Set timeout threshold\n";
                usage += "`b.reactionfilter action <none|remove|timeout|kick|ban> [timeout_seconds]` - Set post-detection action (default: remove)\n";
                usage += "`b.reactionfilter delete on/off` - Auto-delete reactions (legacy toggle)\n";
                usage += "`b.reactionfilter mirrortext on/off` - Mirror the server text-filter's emoji lists\n";
                usage += "`b.reactionfilter warn on/off` - DM users warnings\n";
                usage += "`b.reactionfilter whitelist <role/user> <@mention>`\n";
                usage += "`b.reactionfilter blacklist <role/user> <@mention>`\n";
                usage += "`b.reactionfilter usewhitelist on/off`\n";
                usage += "`b.reactionfilter useblacklist on/off`\n";
                usage += "`b.reactionfilter reset` - Clear all violations";
                embed.add_field("Commands", usage, false);
                
                ::std::string whitelist_info = "";
                whitelist_info += "Roles: " + ::std::to_string(config.whitelist_roles.size()) + " ";
                whitelist_info += "Users: " + ::std::to_string(config.whitelist_users.size());
                embed.add_field("Whitelisted", whitelist_info, false);
                
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            auto& config = guild_reaction_filters[event.msg.guild_id];
            ::std::string subcommand = args[0];
            ::std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "enable") {
                config.enabled = true;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Reaction filter enabled")));
            }
            else if (subcommand == "disable") {
                config.enabled = false;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Reaction filter disabled")));
            }
            else if (subcommand == "add" && args.size() > 1) {
                config.blocked_emoji_names.insert(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Added `" + args[1] + "` to blocked emoji names")));
            }
            else if (subcommand == "remove" && args.size() > 1) {
                config.blocked_emoji_names.erase(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Removed `" + args[1] + "` from blocked emoji names")));
            }
            else if (subcommand == "addpattern" && args.size() > 1) {
                config.blocked_emoji_patterns.insert(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Added regex pattern `" + args[1] + "`")));
            }
            else if (subcommand == "removepattern" && args.size() > 1) {
                config.blocked_emoji_patterns.erase(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Removed regex pattern `" + args[1] + "`")));
            }
            else if (subcommand == "list") {
                ::std::string list;
                for (const auto& name : config.blocked_emoji_names) {
                    list += "`" + name + "` ";
                }
                if (list.empty()) list = "No blocked emoji names";
                
                dpp::embed embed = bronx::info("Blocked Emoji Names (" + ::std::to_string(config.blocked_emoji_names.size()) + ")");
                embed.set_description(list);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }
            else if (subcommand == "listpatterns") {
                ::std::string list;
                for (const auto& pattern : config.blocked_emoji_patterns) {
                    list += "`" + pattern + "` ";
                }
                if (list.empty()) list = "No patterns";
                
                dpp::embed embed = bronx::info("Blocked Patterns (" + ::std::to_string(config.blocked_emoji_patterns.size()) + ")");
                embed.set_description(list);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }
            else if (subcommand == "logchannel" && args.size() > 1) {
                try {
                    ::std::string channel_str = args[1];
                    // Remove <# and > if present
                    if (channel_str.size() > 3 && channel_str.substr(0, 2) == "<#") {
                        channel_str = channel_str.substr(2, channel_str.size() - 3);
                    }
                    config.log_channel = ::std::stoull(channel_str);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Log channel set to <#" + channel_str + ">")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid channel ID")));
                }
            }
            else if (subcommand == "timeout" && args.size() > 2) {
                try {
                    config.max_violations_before_timeout = ::std::stoi(args[1]);
                    config.timeout_duration = ::std::stoul(args[2]);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Timeout set to " + args[1] + " violations, " + args[2] + " seconds duration")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid timeout values")));
                }
            }
            else if (subcommand == "action" && args.size() > 1) {
                ::std::string act = args[1];
                ::std::transform(act.begin(), act.end(), act.begin(), ::tolower);
                if (act == "remove" || act == "none" || act == "timeout" || act == "kick" || act == "ban") {
                    config.action = act;
                    // optional timeout seconds when action == timeout
                    if (act == "timeout" && args.size() > 2) {
                        try { config.action_timeout_seconds = ::std::stoul(args[2]); } catch(...) { config.action_timeout_seconds = 0; }
                    }
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Action set to: " + config.action)));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Unsupported action. Use none/remove/timeout/kick/ban")));
                }
            }
            else if (subcommand == "delete" && args.size() > 1) {
                // Legacy toggle removed; no-op
            }
            else if (subcommand == "mirrortext" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.mirror_text_filter = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success(::std::string("Mirror text filter: ") + (config.mirror_text_filter ? "ON" : "OFF"))));
            }
            else if (subcommand == "warn" && args.size() > 1) {
                // Legacy toggle removed; no-op
            }
            else if (subcommand == "whitelist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                
                if (id_str.find("<@") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.whitelist_roles.insert(id);
                    else if (type == "user") config.whitelist_users.insert(id);
                    
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Added to whitelist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "unwhitelist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                if (id_str.find("<@") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.whitelist_roles.erase(id);
                    else if (type == "user") config.whitelist_users.erase(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Removed from whitelist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "blacklist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                if (id_str.find("<@") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.blacklist_roles.insert(id);
                    else if (type == "user") config.blacklist_users.insert(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Added to blacklist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "unblacklist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                if (id_str.find("<@") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.blacklist_roles.erase(id);
                    else if (type == "user") config.blacklist_users.erase(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Removed from blacklist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "listblacklist") {
                ::std::string out = "Roles: " + ::std::to_string(config.blacklist_roles.size()) + "\n";
                out += "Users: " + ::std::to_string(config.blacklist_users.size());
                dpp::embed eb = bronx::info("Reaction Filter Blacklist");
                eb.set_description(out);
                bot.message_create(dpp::message(event.msg.channel_id, eb));
            }
            else if (subcommand == "usewhitelist" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.use_whitelist = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success(::std::string("use_whitelist: ") + (config.use_whitelist ? "ON" : "OFF"))));
            }
            else if (subcommand == "useblacklist" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.use_blacklist = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success(::std::string("use_blacklist: ") + (config.use_blacklist ? "ON" : "OFF"))));
            }
            else if (subcommand == "reset") {
                user_violations[event.msg.guild_id].clear();
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("All violations cleared")));
            }
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid subcommand. Use `b.reactionfilter` to see usage")));
            }
        }
    );
    
    return &reaction_filter;
}

} // namespace quiet_moderation
} // namespace commands
