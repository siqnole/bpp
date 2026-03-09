#pragma once

#include "text_filter_config.h"
#include "../../command.h"
#include "mod_log.h"

namespace commands {
namespace quiet_moderation {

// Forward declarations for the split implementation
Command* get_text_filter_command();
void register_text_filter(dpp::cluster& bot);

} // namespace quiet_moderation
} // namespace commands



        
        if (!blocked) return;
        
        // Violation detected
        text_filter_violations[event.msg.guild_id][event.msg.author.id]++;
        int violation_count = text_filter_violations[event.msg.guild_id][event.msg.author.id];
        
        // Delete message
        if (config.delete_message) {
            bot.message_delete(event.msg.id, event.msg.channel_id);
        }
        
        // Log violation
        if (config.log_violations && config.log_channel != 0) {
            dpp::embed log_embed = bronx::info("Text Filter Violation");
            log_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
            log_embed.add_field("Channel", "<#" + ::std::to_string(event.msg.channel_id) + ">", true);
            log_embed.add_field("Reason", reason, true);
            log_embed.add_field("Violations", ::std::to_string(violation_count), true);
            
            // Include message content (truncated if too long)
            ::std::string content_preview = event.msg.content;
            if (content_preview.length() > 200) {
                content_preview = content_preview.substr(0, 200) + "...";
            }
            if (!content_preview.empty()) {
                log_embed.add_field("Message Content", "```" + content_preview + "```", false);
            }
            
            log_embed.set_timestamp(time(0));
            commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, log_embed);
        }
        
        // Warn user
        if (config.warn_user) {
            bot.direct_message_create(event.msg.author.id,
                dpp::message().add_embed(
                    bronx::info("Message Blocked")
                        .set_description("Your message was removed as it violates server rules.\n**Reason:** " + reason)
                )
            );
        }
        
        // Apply timeout if threshold reached
        if (config.max_violations_before_timeout > 0 && 
            violation_count >= config.max_violations_before_timeout) {
            
            bot.guild_member_timeout(event.msg.guild_id, event.msg.author.id,
                                    time(0) + config.timeout_duration,
                [&bot, event, config, violation_count, reason](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error() && config.log_channel != 0) {
                        dpp::embed timeout_embed = bronx::error("User Timed Out");
                        timeout_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
                        timeout_embed.add_field("Reason", "Text filter violations", true);
                        timeout_embed.add_field("Duration", ::std::to_string(config.timeout_duration) + "s", true);
                        timeout_embed.add_field("Violations", ::std::to_string(violation_count), true);
                        timeout_embed.set_timestamp(time(0));
                        
                        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, timeout_embed);
                    }
                }
            );
            
            text_filter_violations[event.msg.guild_id][event.msg.author.id] = 0;
        }
    });
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
            else if (subcommand == "whitelist" && args.size() > 2) {
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
                    if (type == "role") config.whitelist_roles.insert(id);
                    else if (type == "user") config.whitelist_users.insert(id);
                    else if (type == "channel") config.whitelist_channels.insert(id);
                    
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
                
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
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
                    else if (type == "channel") config.whitelist_channels.erase(id);
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
                
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
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
                    else if (type == "channel") config.blacklist_channels.insert(id);
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
                dpp::embed eb = bronx::info("Text Filter Blacklist");
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
                text_filter_violations[event.msg.guild_id].clear();
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("All violations cleared")));
            }
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid subcommand")));
            }
        }
    );
    
    return &text_filter;
}

} // namespace quiet_moderation
} // namespace commands
