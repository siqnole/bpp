#pragma once
#include "../../command.h"
#include "mod_log.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <regex>
#include <algorithm>

namespace commands {
namespace quiet_moderation {

// Structure for URL/invite filter config
struct URLGuardConfig {
    bool enabled = false;
    bool block_all_links = false;
    bool block_discord_invites = true;
    bool block_external_invites = true;           // Other platform invites (twitch, etc)
    ::std::set<::std::string> whitelist_domains;      // Allowed domains (e.g., "youtube.com")
    ::std::set<::std::string> blacklist_domains;      // Explicitly blocked domains
    ::std::set<dpp::snowflake> whitelist_roles;
    ::std::set<dpp::snowflake> whitelist_users;
    ::std::set<dpp::snowflake> whitelist_channels;
    ::std::set<dpp::snowflake> blacklist_roles;
    ::std::set<dpp::snowflake> blacklist_users;
    ::std::set<dpp::snowflake> blacklist_channels;
    bool use_whitelist = true;
    bool use_blacklist = false;
    bool log_violations = true;
    dpp::snowflake log_channel = 0;
    bool delete_message = true;
    bool warn_user = false;
    int max_violations_before_timeout = 3;
    uint32_t timeout_duration = 600;
};

static ::std::map<dpp::snowflake, URLGuardConfig> guild_url_guards;
static ::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> url_guard_violations;

// Helper: Extract URLs from message
inline ::std::vector<::std::string> extract_urls(const ::std::string& content) {
    ::std::vector<::std::string> urls;
    
    // Regex for URLs (http/https)
    ::std::regex url_regex(R"((https?://[^\s<>\[\]]+))");
    ::std::smatch match;
    ::std::string text = content;
    
    while (::std::regex_search(text, match, url_regex)) {
        urls.push_back(match[1].str());
        text = match.suffix();
    }
    
    // Also catch URLs without protocol
    ::std::regex domain_regex(R"((?:^|\s)([a-zA-Z0-9-]+\.[a-zA-Z]{2,}(?:/[^\s]*)?))");
    text = content;
    while (::std::regex_search(text, match, domain_regex)) {
        ::std::string potential_url = match[1].str();
        // Avoid false positives like "file.txt"
        if (potential_url.find('/') != ::std::string::npos || 
            potential_url.find('.') != ::std::string::npos) {
            urls.push_back(potential_url);
        }
        text = match.suffix();
    }
    
    return urls;
}

// Helper: Check if URL is a Discord invite
inline bool is_discord_invite(const ::std::string& url) {
    ::std::string lower_url = url;
    ::std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    
    return (lower_url.find("discord.gg/") != ::std::string::npos ||
            lower_url.find("discord.com/invite/") != ::std::string::npos ||
            lower_url.find("discordapp.com/invite/") != ::std::string::npos);
}

// Helper: Check if URL is an external platform invite
inline bool is_external_invite(const ::std::string& url) {
    ::std::string lower_url = url;
    ::std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    
    return (lower_url.find("twitch.tv/") != ::std::string::npos ||
            lower_url.find("t.me/") != ::std::string::npos ||
            lower_url.find("telegram.me/") != ::std::string::npos ||
            lower_url.find("invite") != ::std::string::npos ||
            lower_url.find("join") != ::std::string::npos);
}

// Helper: Extract domain from URL
inline ::std::string get_domain(const ::std::string& url) {
    ::std::string domain = url;
    
    // Remove protocol
    size_t proto_pos = domain.find("://");
    if (proto_pos != ::std::string::npos) {
        domain = domain.substr(proto_pos + 3);
    }
    
    // Get just the domain part
    size_t slash_pos = domain.find('/');
    if (slash_pos != ::std::string::npos) {
        domain = domain.substr(0, slash_pos);
    }
    
    // Remove www.
    if (domain.substr(0, 4) == "www.") {
        domain = domain.substr(4);
    }
    
    ::std::transform(domain.begin(), domain.end(), domain.begin(), ::tolower);
    return domain;
}

// Helper: Check if URL should be blocked
inline ::std::pair<bool, ::std::string> should_block_url(const ::std::string& url, const URLGuardConfig& config) {
    // Check Discord invites
    if (config.block_discord_invites && is_discord_invite(url)) {
        return {true, "Discord invite link"};
    }
    
    // Check external invites
    if (config.block_external_invites && is_external_invite(url)) {
        return {true, "External invite link"};
    }
    
    ::std::string domain = get_domain(url);
    
    // Check blacklist
    for (const auto& blocked : config.blacklist_domains) {
        if (domain.find(blocked) != ::std::string::npos) {
            return {true, "Blacklisted domain: " + blocked};
        }
    }
    
    // If blocking all links, check whitelist
    if (config.block_all_links) {
        bool whitelisted = false;
        for (const auto& allowed : config.whitelist_domains) {
            if (domain.find(allowed) != ::std::string::npos) {
                whitelisted = true;
                break;
            }
        }
        if (!whitelisted) {
            return {true, "Link not whitelisted"};
        }
    }
    
    return {false, ""};
}

// Register URL guard event handler
inline void register_url_guard(dpp::cluster& bot) {
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (event.msg.author.is_bot()) return;
        if (event.msg.guild_id == 0) return;
        
        if (guild_url_guards.find(event.msg.guild_id) == guild_url_guards.end()) return;
        
        auto& config = guild_url_guards[event.msg.guild_id];
        if (!config.enabled) return;
        
        // Whitelist / blacklist handling (per-module toggles)
        bool is_whitelisted = false;
        bool is_blacklisted = false;

        if (config.use_blacklist) {
            if (config.blacklist_users.count(event.msg.author.id) > 0 ||
                config.blacklist_channels.count(event.msg.channel_id) > 0) {
                is_blacklisted = true;
            } else {
                for (const auto& role : event.msg.member.get_roles()) {
                    if (config.blacklist_roles.count(role) > 0) { is_blacklisted = true; break; }
                }
            }
        }

        if (config.use_whitelist) {
            if (config.whitelist_users.count(event.msg.author.id) > 0 ||
                config.whitelist_channels.count(event.msg.channel_id) > 0) {
                is_whitelisted = true;
            }
            for (const auto& role : event.msg.member.get_roles()) {
                if (config.whitelist_roles.count(role) > 0) { is_whitelisted = true; break; }
            }
        }

        if (is_whitelisted && !is_blacklisted) {
            return;
        }

        // Extract and check URLs
        auto urls = extract_urls(event.msg.content);
        if (urls.empty()) return;
        
        ::std::string blocked_url;
        ::std::string block_reason;
        bool should_block = false;
        
        for (const auto& url : urls) {
            auto [block, reason] = should_block_url(url, config);
            if (block) {
                should_block = true;
                blocked_url = url;
                block_reason = reason;
                break;
            }
        }
        
        if (!should_block) return;
        
        // Violation detected
        url_guard_violations[event.msg.guild_id][event.msg.author.id]++;
        int violation_count = url_guard_violations[event.msg.guild_id][event.msg.author.id];
        
        // Delete message
        if (config.delete_message) {
            bot.message_delete(event.msg.id, event.msg.channel_id);
        }
        
        // Log violation
        if (config.log_violations && config.log_channel != 0) {
            dpp::embed log_embed = bronx::info("URL Guard Violation");
            log_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
            log_embed.add_field("Channel", "<#" + ::std::to_string(event.msg.channel_id) + ">", true);
            log_embed.add_field("Reason", block_reason, true);
            log_embed.add_field("Violations", ::std::to_string(violation_count), true);
            
            ::std::string url_display = blocked_url;
            if (url_display.length() > 100) {
                url_display = url_display.substr(0, 100) + "...";
            }
            log_embed.add_field("Blocked URL", "`" + url_display + "`", false);
            
            log_embed.set_timestamp(time(0));
            commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, log_embed);
        }
        
        // Warn user
        if (config.warn_user) {
            bot.direct_message_create(event.msg.author.id,
                dpp::message().add_embed(
                    bronx::info("Link Blocked")
                        .set_description("Your message containing a link was removed.\n**Reason:** " + block_reason)
                )
            );
        }
        
        // Apply timeout
        if (config.max_violations_before_timeout > 0 && 
            violation_count >= config.max_violations_before_timeout) {
            
            bot.guild_member_timeout(event.msg.guild_id, event.msg.author.id,
                                    time(0) + config.timeout_duration,
                [&bot, event, config, violation_count](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error() && config.log_channel != 0) {
                        dpp::embed timeout_embed = bronx::error("User Timed Out");
                        timeout_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
                        timeout_embed.add_field("Reason", "URL guard violations", true);
                        timeout_embed.add_field("Duration", ::std::to_string(config.timeout_duration) + "s", true);
                        timeout_embed.add_field("Violations", ::std::to_string(violation_count), true);
                        timeout_embed.set_timestamp(time(0));
                        
                        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, timeout_embed);
                    }
                }
            );
            
            url_guard_violations[event.msg.guild_id][event.msg.author.id] = 0;
        }
    });
}

// Command to configure URL guard
inline Command* get_url_guard_command() {
    static Command url_guard("urlguard", "Configure URL and invite link filtering", "moderation",
        {"ug", "linkfilter", "invitefilter"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
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
                auto& config = guild_url_guards[event.msg.guild_id];
                
                dpp::embed embed = bronx::info("URL Guard Configuration");
                embed.add_field("Status", config.enabled ? "🟢 Enabled" : "🔴 Disabled", true);
                embed.add_field("Block All Links", config.block_all_links ? "Yes" : "No", true);
                embed.add_field("Block Discord Invites", config.block_discord_invites ? "Yes" : "No", true);
                embed.add_field("Block External Invites", config.block_external_invites ? "Yes" : "No", true);
                
                ::std::string whitelist = ::std::to_string(config.whitelist_domains.size()) + " domain(s)";
                ::std::string blacklist = ::std::to_string(config.blacklist_domains.size()) + " domain(s)";
                
                embed.add_field("Whitelisted Domains", whitelist, true);
                embed.add_field("Blacklisted Domains", blacklist, true);
                embed.add_field("Log Channel", config.log_channel != 0 ? "<#" + ::std::to_string(config.log_channel) + ">" : "Not set", true);
                
                ::std::string usage = "**Usage:**\n";
                usage += "`b.urlguard enable/disable`\n";
                usage += "`b.urlguard blockall on/off` - Block all links\n";
                usage += "`b.urlguard invites on/off` - Block Discord invites\n";
                usage += "`b.urlguard external on/off` - Block external invites\n";
                usage += "`b.urlguard whitelist <domain>` - Allow domain\n";
                usage += "`b.urlguard blacklist <domain>` - Block domain\n";
                usage += "`b.urlguard removewhitelist <domain>` - Remove from whitelist\n";
                usage += "`b.urlguard removeblacklist <domain>` - Remove from blacklist\n";
                usage += "`b.urlguard listwhitelist` - Show allowed domains\n";
                usage += "`b.urlguard listblacklist` - Show blocked domains\n";
                usage += "`b.urlguard logchannel <#channel>`\n";
                usage += "`b.urlguard timeout <violations> <seconds>`\n";
                usage += "`b.urlguard delete on/off` - Delete violating messages\n";
                usage += "`b.urlguard warn on/off` - DM users warnings\n";
                usage += "`b.urlguard whitelistrole/user/channel <@mention>`\n";
                usage += "`b.urlguard reset` - Clear all violations";
                embed.add_field("Commands", usage, false);
                
                ::std::string whitelist_status = "";
                whitelist_status += "Roles: " + ::std::to_string(config.whitelist_roles.size()) + " ";
                whitelist_status += "Users: " + ::std::to_string(config.whitelist_users.size()) + " ";
                whitelist_status += "Channels: " + ::std::to_string(config.whitelist_channels.size());
                embed.add_field("Whitelisted", whitelist_status, false);
                
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            auto& config = guild_url_guards[event.msg.guild_id];
            ::std::string subcommand = args[0];
            ::std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "enable") {
                config.enabled = true;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("URL guard enabled")));
            }
            else if (subcommand == "disable") {
                config.enabled = false;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("URL guard disabled")));
            }
            else if (subcommand == "blockall" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.block_all_links = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Block all links: " + ::std::string(config.block_all_links ? "ON" : "OFF"))));
            }
            else if (subcommand == "invites" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.block_discord_invites = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Block Discord invites: " + ::std::string(config.block_discord_invites ? "ON" : "OFF"))));
            }
            else if (subcommand == "external" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.block_external_invites = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Block external invites: " + ::std::string(config.block_external_invites ? "ON" : "OFF"))));
            }
            else if (subcommand == "whitelist" && args.size() > 1) {
                config.whitelist_domains.insert(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Whitelisted domain: `" + args[1] + "`")));
            }
            else if (subcommand == "blacklist" && args.size() > 1) {
                config.blacklist_domains.insert(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Blacklisted domain: `" + args[1] + "`")));
            }
            else if (subcommand == "removewhitelist" && args.size() > 1) {
                config.whitelist_domains.erase(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Removed from whitelist: `" + args[1] + "`")));
            }
            else if (subcommand == "removeblacklist" && args.size() > 1) {
                config.blacklist_domains.erase(args[1]);
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Removed from blacklist: `" + args[1] + "`")));
            }
            else if (subcommand == "listwhitelist") {
                ::std::string list;
                for (const auto& domain : config.whitelist_domains) {
                    list += "`" + domain + "` ";
                }
                if (list.empty()) list = "No whitelisted domains";
                
                dpp::embed embed = bronx::info("Whitelisted Domains (" + ::std::to_string(config.whitelist_domains.size()) + ")");
                embed.set_description(list);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }
            else if (subcommand == "listblacklist") {
                ::std::string list;
                for (const auto& domain : config.blacklist_domains) {
                    list += "`" + domain + "` ";
                }
                if (list.empty()) list = "No blacklisted domains";
                
                dpp::embed embed = bronx::info("Blacklisted Domains (" + ::std::to_string(config.blacklist_domains.size()) + ")");
                embed.set_description(list);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
            }
            else if (subcommand == "logchannel" && args.size() > 1) {
                try {
                    ::std::string channel_str = args[1];
                    if (channel_str.size() > 3 && channel_str.substr(0, 2) == "<#") {
                        channel_str = channel_str.substr(2, channel_str.size() - 3);
                    }
                    config.log_channel = ::std::stoull(channel_str);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Log channel set")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid channel")));
                }
            }
            else if (subcommand == "timeout" && args.size() > 2) {
                try {
                    config.max_violations_before_timeout = ::std::stoi(args[1]);
                    config.timeout_duration = ::std::stoul(args[2]);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Timeout configured")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid values")));
                }
            }
            else if (subcommand == "delete" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.delete_message = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Delete messages: " + ::std::string(config.delete_message ? "ON" : "OFF"))));
            }
            else if (subcommand == "warn" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.warn_user = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Warn users: " + ::std::string(config.warn_user ? "ON" : "OFF"))));
            }
            else if ((subcommand == "whitelistrole" || subcommand == "whitelistuser" || 
                     subcommand == "whitelistchannel") && args.size() > 1) {
                ::std::string id_str = args[1];
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (subcommand == "whitelistrole") config.whitelist_roles.insert(id);
                    else if (subcommand == "whitelistuser") config.whitelist_users.insert(id);
                    else if (subcommand == "whitelistchannel") config.whitelist_channels.insert(id);
                    
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Added to whitelist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if ((subcommand == "blacklistrole" || subcommand == "blacklistuser" || subcommand == "blacklistchannel") && args.size() > 1) {
                ::std::string id_str = args[1];
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (subcommand == "blacklistrole") config.blacklist_roles.insert(id);
                    else if (subcommand == "blacklistuser") config.blacklist_users.insert(id);
                    else if (subcommand == "blacklistchannel") config.blacklist_channels.insert(id);
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
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
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
                    else if (type == "channel") config.blacklist_channels.erase(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Removed from blacklist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "listblacklist") {
                ::std::string out = "Roles: " + ::std::to_string(config.blacklist_roles.size()) + "\n";
                out += "Users: " + ::std::to_string(config.blacklist_users.size()) + "\n";
                out += "Channels: " + ::std::to_string(config.blacklist_channels.size());
                dpp::embed eb = bronx::info("URL Guard Blacklist");
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
                url_guard_violations[event.msg.guild_id].clear();
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("All violations cleared")));
            }
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid subcommand")));
            }
        }
    );
    
    return &url_guard;
}

} // namespace quiet_moderation
} // namespace commands
