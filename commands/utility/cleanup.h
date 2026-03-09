#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <set>
#include <chrono>
#include <functional>

// we record any webhook IDs that the bot creates/uses so that cleanup can
// recognise messages generated via those webhooks as belonging to the bot.
#include "../../commands/quiet_moderation/mod_log.h"

// gambling games are tracked so cleanup avoids deleting active game messages
#include "../../commands/gambling/blackjack.h"
#include "../../commands/gambling/roulette.h"
#include "../../commands/gambling/russian_roulette.h"
#include "../../commands/gambling/gambling_helpers.h"

// prefix database for resolving user/guild prefixes
#include "prefix.h"

namespace commands {
namespace utility {

// Cleanup command (text only) NOW CAN CLEANUP WEBHOOK MESSAGES WITH -w FLAG
inline Command* get_cleanup_command() {
    static Command cleanup("cleanup", "delete recent bot messages and commands that invoked them", "utility", {"clear", "clean", "cu"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // only users with manage messages or administrator may run cleanup
            bool limited_user = false;
            if (event.msg.guild_id != 0) {
                // check cached guild to compute permissions
                if (auto g = dpp::find_guild(event.msg.guild_id)) {
                    dpp::permission perm = g->base_permissions(event.msg.member);
                    if (!(perm & dpp::p_manage_messages) && !(perm & dpp::p_administrator)) {
                        limited_user = true;
                    }
                } else {
                    // no cache; err on side of caution
                    limited_user = true;
                }
            }
            // Help command for flags
            // Help command for flags
            if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
                dpp::embed help_embed = bronx::info("Cleanup Command Help");
                help_embed.set_description("Flags for b.cu (cleanup):\n\n"
                    "-s, --silent, --quiet: Silent mode, no confirmation message\n"
                    "-i, --ignore, --onlyyou: Ignore all bot messages, only delete commands\n"
                    "-b, --bot, --botonly: Ignore commands, only delete bot messages\n"
                    "-w, --webhook: Also include third‑party webhook messages (bot webhooks are\n"
                    "               removed automatically by default)\n"
                    "<number>: Limit number of messages to check (default 50, max 100)\n"
                    "-h, --help: Show this help message");
                bot.message_create(dpp::message(event.msg.channel_id, help_embed));
                return;
            }
            // Default to 50 messages, max 100
            int limit = 50;
            bool silent_mode = false;
            bool ignore_other_bots = false;  // -i: ignore all bot messages, only delete commands
            bool bot_only = false;            // -b: ignore commands, only delete bot messages
            bool include_webhook = false;     // -w: also delete non‑bot webhook messages

            // Parse arguments like bash command line
            for (const auto& arg : args) {
                // Check if it's a flag (starts with -)
                if (!arg.empty() && arg[0] == '-') {
                    ::std::string flag = arg;
                    // Handle both single dash and double dash
                    if (flag == "-s" || flag == "--silent" || flag == "-silent" || 
                        flag == "--quiet" || flag == "-quiet") {
                        silent_mode = true;
                    }
                    else if (flag == "-i" || flag == "--ignore" || flag == "-ignore" ||
                             flag == "--onlyyou" || flag == "-onlyyou") {
                        ignore_other_bots = true;
                    }
                    else if (flag == "-b" || flag == "--bot" || flag == "-bot" ||
                             flag == "--botonly" || flag == "-botonly") {
                        bot_only = true;
                    }
                    else if (flag == "-w" || flag == "--webhook" || flag == "-webhook") {
                        include_webhook = true;
                    }
                    // Ignore unknown flags
                } else {
                    // Not a flag, try to parse as limit number
                    try {
                        limit = ::std::stoi(arg);
                        if (limit < 1) limit = 50;
                        if (limit > 100) limit = 100;
                    } catch (...) {
                        // Ignore invalid numeric arguments
                    }
                }
            }
            
            // Build list of prefixes to check (default + guild + user)
            ::std::vector<::std::string> valid_prefixes;
            valid_prefixes.push_back("b."); // default prefix
            if (prefix_db) {
                if (event.msg.guild_id != 0) {
                    auto gps = prefix_db->get_guild_prefixes(event.msg.guild_id);
                    valid_prefixes.insert(valid_prefixes.end(), gps.begin(), gps.end());
                }
                auto ups = prefix_db->get_user_prefixes(event.msg.author.id);
                valid_prefixes.insert(valid_prefixes.end(), ups.begin(), ups.end());
            }
            
            // Fetch recent messages
            bot.messages_get(event.msg.channel_id, 0, 0, 0, limit, [&bot, event, silent_mode, ignore_other_bots, bot_only, include_webhook, limited_user, valid_prefixes](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Failed to fetch messages for cleanup")));
                    return;
                }
                
                auto messages = callback.get<dpp::message_map>();
                ::std::set<dpp::snowflake> to_delete_set;

                // compile list of protected message ids for active gambling games
                ::std::set<dpp::snowflake> protected_ids;
                for (auto &p : commands::gambling::active_blackjack_games) {
                    if (p.second.active) protected_ids.insert(p.second.message_id);
                }
                for (auto &p : commands::gambling::active_roulette_games) {
                    if (p.second.active) protected_ids.insert(p.second.message_id);
                }
                for (auto &p : commands::gambling::active_russian_roulette_games) {
                    if (p.second.active) protected_ids.insert(p.second.message_id);
                }
                for (auto &p : commands::gambling::active_frogger_games) {
                    if (p.second.active) protected_ids.insert(p.second.message_id);
                }
                
                // Filter messages based on flags
                for (const auto& [msg_id, msg] : messages) {
                    if (protected_ids.count(msg_id)) {
                        continue; // skip active gambling game message
                    }
                    bool is_this_bot = (msg.author.id == bot.me.id);
                    bool is_webhook = msg.webhook_id != 0;
                    bool is_our_webhook = is_webhook && commands::quiet_moderation::owned_webhooks.count(msg.webhook_id);
                    bool is_bot_message = is_this_bot || is_our_webhook;
                    
                    // Check if message starts with any valid prefix (case-insensitive)
                    bool is_command = false;
                    ::std::string content_lower = msg.content;
                    ::std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
                    for (const auto& pfx : valid_prefixes) {
                        if (pfx.empty()) continue;
                        ::std::string pfx_lower = pfx;
                        ::std::transform(pfx_lower.begin(), pfx_lower.end(), pfx_lower.begin(), ::tolower);
                        if (content_lower.size() >= pfx_lower.size() && 
                            content_lower.rfind(pfx_lower, 0) == 0) {
                            is_command = true;
                            break;
                        }
                    }

                    // if limited user, only allow deletions of their own messages or any bot message
                    if (limited_user) {
                        if (msg.author.id == event.msg.author.id) {
                            to_delete_set.insert(msg_id);
                            continue;
                        }
                        if (is_bot_message) {
                            to_delete_set.insert(msg_id);
                        }
                        continue;
                    }

                    // Apply filtering logic based on flags
                    if (ignore_other_bots && bot_only) {
                        // Both flags: only delete this bot's messages and user commands
                        if (is_bot_message || is_command || (include_webhook && is_webhook)) {
                            to_delete_set.insert(msg_id);
                        }
                    }
                    else if (ignore_other_bots) {
                        // Only -i: ignore all bots, only delete commands
                        if (is_command || (include_webhook && is_webhook)) {
                            to_delete_set.insert(msg_id);
                        }
                    }
                    else if (bot_only) {
                        // Only -b: ignore commands, only delete bot messages
                        if (is_bot_message || (include_webhook && is_webhook)) {
                            to_delete_set.insert(msg_id);
                        }
                    }
                    else {
                        // Default: delete bot messages and commands
                        if (is_bot_message || is_command || (include_webhook && is_webhook)) {
                            to_delete_set.insert(msg_id);
                        }
                    }
                }
                
                // Also delete the cleanup command itself
                to_delete_set.insert(event.msg.id);
                
                if (to_delete_set.empty()) {
                    if (!silent_mode) {
                        auto embed = bronx::info("No bot messages, commands, or webhook messages found to clean up");
                        bot.message_create(dpp::message(event.msg.channel_id, embed));
                    }
                    return;
                }
                
                // Filter messages for bulk delete (must be < 14 days old and max 100)
                // Discord only allows bulk delete for messages less than 2 weeks old
                auto now = ::std::chrono::system_clock::now();
                auto two_weeks_ago = now - ::std::chrono::hours(24 * 14);
                
                ::std::vector<dpp::snowflake> bulk_delete;
                
                for (const auto& msg_id : to_delete_set) {
                    // Get message creation time using DPP's method
                    auto msg_created = dpp::snowflake(msg_id).get_creation_time();
                    auto msg_time = ::std::chrono::system_clock::from_time_t(msg_created);
                    
                    // Only include messages less than 14 days old
                    if (msg_time > two_weeks_ago) {
                        bulk_delete.push_back(msg_id);
                    }
                }
                
                if (bulk_delete.empty()) {
                    if (!silent_mode) {
                        auto embed = bronx::info("No messages recent enough to delete (messages must be < 14 days old)");
                        bot.message_create(dpp::message(event.msg.channel_id, embed));
                    }
                    return;
                }
                
                // Limit to 100 messages max
                if (bulk_delete.size() > 100) {
                    bulk_delete.resize(100);
                }
                
                int delete_count = bulk_delete.size();
                
                // helper that actually issues a bulk-delete and retries on 429.
                // Stored in a shared_ptr so it can safely be captured by value in
                // both the async callback and any retry timers – avoids dangling refs.
                auto perform_bulk_ptr = std::make_shared<std::function<void(std::vector<dpp::snowflake>)>>();
                *perform_bulk_ptr = [&bot, event, delete_count, silent_mode, perform_bulk_ptr](std::vector<dpp::snowflake> ids) {
                    if (ids.empty()) return;
                    bot.message_delete_bulk(ids, event.msg.channel_id,
                        [&bot, event, delete_count, silent_mode, ids, perform_bulk_ptr](const dpp::confirmation_callback_t& del_callback) {
                            if (del_callback.is_error()) {
                                auto error = del_callback.get_error();
                                if (error.code == 429) {
                                    // rate limited; try again after a short delay
                                    bot.start_timer([&bot, ids, perform_bulk_ptr](dpp::timer timer) {
                                        (*perform_bulk_ptr)(ids);
                                        bot.stop_timer(timer);
                                    }, 3);
                                    return;
                                }
                                ::std::string error_msg = "Failed to delete messages: " + error.human_readable;
                                if (error.code == 50013) {
                                    error_msg = "Missing permissions! Bot needs **Manage Messages** permission.";
                                } else if (error.code == 50034) {
                                    error_msg = "Some messages are too old (> 14 days) to bulk delete.";
                                }
                                bot.message_create(dpp::message(event.msg.channel_id,
                                    bronx::error(error_msg)));
                                return;
                            }

                            // success; send confirmation if not silent
                            if (!silent_mode) {
                                ::std::string description = "cleaned **" + ::std::to_string(delete_count) + "** messages";
                                auto embed = bronx::create_embed(description);
                                embed.set_color(0x43B581);
                                bot.message_create(dpp::message(event.msg.channel_id, embed), [&bot, event](const dpp::confirmation_callback_t& msg_callback) {
                                    if (!msg_callback.is_error()) {
                                        auto sent_msg = msg_callback.get<dpp::message>();
                                        bot.start_timer([&bot, channel_id = event.msg.channel_id, msg_id = sent_msg.id](dpp::timer timer) {
                                            bot.message_delete(msg_id, channel_id);
                                            bot.stop_timer(timer);
                                        }, 5);
                                    }
                                });
                            }
                        });
                };
                
                // finally call the helper
                if (bulk_delete.size() >= 2) {
                    (*perform_bulk_ptr)(bulk_delete);
                } else if (bulk_delete.size() == 1) {
                    // Single message - delete individually
                    bot.message_delete(bulk_delete[0], event.msg.channel_id, [&bot, event, silent_mode](const dpp::confirmation_callback_t& del_callback) {
                        if (del_callback.is_error()) {
                            auto error = del_callback.get_error();
                            ::std::string error_msg = "Failed to delete message: " + error.human_readable;
                            if (error.code == 50013) {
                                error_msg = "Missing permissions! Bot needs **Manage Messages** permission.";
                            }
                            bot.message_create(dpp::message(event.msg.channel_id,
                                bronx::error(error_msg)));
                            return;
                        }
                        
                        // Send confirmation if not in silent mode
                        if (!silent_mode) {
                            ::std::string description = "cleaned **1** message";
                            auto embed = bronx::create_embed(description);
                            embed.set_color(0x43B581);
                            
                            bot.message_create(dpp::message(event.msg.channel_id, embed), [&bot, event](const dpp::confirmation_callback_t& msg_callback) {
                                if (!msg_callback.is_error()) {
                                    auto sent_msg = msg_callback.get<dpp::message>();
                                    // Delete the confirmation message after 5 seconds
                                    bot.start_timer([&bot, channel_id = event.msg.channel_id, msg_id = sent_msg.id](dpp::timer timer) {
                                        bot.message_delete(msg_id, channel_id);
                                        bot.stop_timer(timer);
                                    }, 5);
                                }
                            });
                        }
                    });
                }
            });
        });
    
    return &cleanup;
}

} // namespace utility
} // namespace commands
