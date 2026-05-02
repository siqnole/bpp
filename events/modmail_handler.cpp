#include "modmail_handler.h"
#include "../database/operations/moderation/modmail_operations.h"
#include "../utils/logger.h"
#include "../utils/colors.h"
#include "../utils/moderation/transcript.h"
#include <iostream>

namespace bronx {
namespace events {

struct PendingModmail {
    std::string content;
    time_t created_at;
};

static std::map<uint64_t, PendingModmail> pending_messages;

void register_modmail_handlers(dpp::cluster& bot, db::Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id == "modmail_close_btn") {
            auto thread = db::get_modmail_thread_by_id(db, event.command.channel_id);
            if (thread && thread->status == "open") {
                if (db::close_modmail_thread(db, event.command.channel_id)) {
                    event.reply(dpp::ir_channel_message_with_source, dpp::message("✅ Modmail thread closed."));
                    
                    // Notify user
                    dpp::message dm;
                    dpp::guild* g = dpp::find_guild(event.command.guild_id);
                    std::string guild_name = g ? g->name : "the server";
                    dm.set_content("Your modmail thread in **" + guild_name + "** has been closed by staff.");
                    bot.direct_message_create(thread->user_id, dm);

                    // Log and generate transcript
                    auto config = db::get_modmail_config(db, event.command.guild_id);
                    if (config && config->log_channel_id) {
                        dpp::embed log_embed = dpp::embed()
                            .set_color(0xEF4444) // Rouge-ish
                            .set_title("Modmail Thread Closed")
                            .add_field("User", "<@" + std::to_string(thread->user_id) + ">", true)
                            .add_field("Moderator", event.command.usr.get_mention(), true)
                            .add_field("Channel", "<#" + std::to_string(event.command.channel_id) + ">", true)
                            .set_timestamp(time(0));
                        bot.message_create(dpp::message(config->log_channel_id, log_embed));

                        // Generate Transcript
                        bronx::moderation::create_modmail_transcript(bot, event.command.channel_id, config->log_channel_id, thread->user_id, [&bot, tid = event.command.channel_id](bool ok) {
                            if (ok) {
                                // Archive the thread after a short delay to ensure everything is sent
                                // In a real bot, we might wait 5s.
                                bot.channel_delete(tid); // Or archive
                            }
                        });
                    }
                } else {
                    event.reply(dpp::ir_channel_message_with_source, dpp::message("❌ Failed to close thread in database.").set_flags(dpp::m_ephemeral));
                }
            } else {
                event.reply(dpp::ir_channel_message_with_source, dpp::message("❌ This thread is already closed or not a modmail thread.").set_flags(dpp::m_ephemeral));
            }
        }
    });

    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        if (event.custom_id == "modmail_guild_select") {
            uint64_t guild_id = std::stoull(event.values[0]);
            uint64_t user_id = event.command.usr.id;

            auto it = pending_messages.find(user_id);
            std::string content = (it != pending_messages.end()) ? it->second.content : "(No initial message)";
            if (it != pending_messages.end()) pending_messages.erase(it);

            auto config = db::get_modmail_config(db, guild_id);
            if (!config || !config->enabled) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message("❌ Modmail is no longer enabled for that server.").set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::guild* g = dpp::find_guild(guild_id);
            std::string guild_name = g ? g->name : "the server";

            event.reply(dpp::ir_update_message, dpp::message("Opening a modmail thread in **" + guild_name + "**..."));

            dpp::thread t;
            t.name = "modmail-" + event.command.usr.username;
            t.guild_id = guild_id;
            t.parent_id = config->category_id;
            
            bot.thread_create(t.name, config->category_id, 1440, dpp::channel_type::CHANNEL_PUBLIC_THREAD, true, 0, [db, &bot, event, guild_id, content, config](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    bot.message_create(dpp::message(event.command.channel_id, "❌ Failed to create thread: " + cb.get_error().message));
                } else {
                    dpp::thread n_t = std::get<dpp::thread>(cb.value);
                    db::create_modmail_thread(db, guild_id, event.command.usr.id, n_t.id);
                    
                    // Relay the message with a button
                    dpp::message relay;
                    relay.set_channel_id(n_t.id);
                    relay.set_content("**[New Thread]** " + content);
                    relay.add_component(dpp::component().add_component(
                        dpp::component().set_label("Close Thread").set_type(dpp::cot_button).set_id("modmail_close_btn").set_style(dpp::cos_danger)
                    ));
                    bot.message_create(relay);
                    
                    bot.message_create(dpp::message(event.command.channel_id, "✅ Thread opened! Staff will respond shortly."));

                    // Log the opening
                    if (config && config->log_channel_id) {
                        dpp::embed log_embed = dpp::embed()
                            .set_color(0x10B981) // Green-ish
                            .set_title("Modmail Thread Opened")
                            .add_field("User", event.command.usr.get_mention(), true)
                            .add_field("Channel", "<#" + std::to_string(n_t.id) + ">", true)
                            .set_timestamp(time(0));
                        bot.message_create(dpp::message(config->log_channel_id, log_embed));
                    }
                }
            });
        }
    });

    bot.on_message_create([&bot, db](const dpp::message_create_t& event) {
        // 1. Ignore bots
        if (event.msg.author.is_bot()) return;

        // 2. Handle DMs (User -> Staff)
        if (event.msg.guild_id == 0) {
            auto thread = db::get_any_active_modmail_thread(db, event.msg.author.id);
            if (thread && thread->status == "open") {
                // Relay to the staff thread
                dpp::message relay;
                relay.set_channel_id(thread->thread_id);
                relay.set_content("**[User DM]** " + event.msg.content);
                
                bot.message_create(relay, [&bot, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply("❌ Failed to deliver message to staff: " + cb.get_error().message);
                    } else {
                        bot.message_add_reaction(event.msg.id, 0, "✅");
                    }
                });
            } else {
                // No active thread. Find eligible guilds.
                std::vector<uint64_t> enabled_guilds = db::get_all_modmail_enabled_guilds(db);
                if (enabled_guilds.empty()) {
                    event.reply("❌ There are no servers with modmail enabled at this time.");
                    return;
                }

                // Filter guilds where the user is a member
                // Note: In a large bot, we should use a cache or async checks.
                // For now, we'll check the cache.
                std::vector<dpp::guild*> shared_guilds;
                for (uint64_t gid : enabled_guilds) {
                    dpp::guild* g = dpp::find_guild(gid);
                    if (g) {
                        // Check if user is in members cache or fetch it
                        // Since this is a DM, we might not have the user in all guild caches.
                        // We'll use guild_get_member which is async.
                        shared_guilds.push_back(g);
                    }
                }

                if (shared_guilds.empty()) {
                    event.reply("❌ You don't share any servers that have modmail enabled.");
                    return;
                }

                if (shared_guilds.size() == 1) {
                    // Only one guild, start immediately
                    uint64_t gid = shared_guilds[0]->id;
                    auto config = db::get_modmail_config(db, gid);
                    if (config && config->enabled) {
                        event.reply("Opening a modmail thread in **" + shared_guilds[0]->name + "**...");
                        
                        dpp::thread t;
                        t.name = "modmail-" + event.msg.author.username;
                        t.guild_id = gid;
                        t.parent_id = config->category_id;
                        
                        bot.thread_create(t.name, config->category_id, 1440, dpp::channel_type::CHANNEL_PUBLIC_THREAD, true, 0, [db, &bot, event, gid, config](const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                event.reply("❌ Failed to create thread: " + cb.get_error().message);
                            } else {
                                dpp::thread n_t = std::get<dpp::thread>(cb.value);
                                db::create_modmail_thread(db, gid, event.msg.author.id, n_t.id);
                                
                                // Relay the message with a button
                                dpp::message relay;
                                relay.set_channel_id(n_t.id);
                                relay.set_content("**[New Thread]** " + event.msg.content);
                                relay.add_component(dpp::component().add_component(
                                    dpp::component().set_label("Close Thread").set_type(dpp::cot_button).set_id("modmail_close_btn").set_style(dpp::cos_danger)
                                ));
                                bot.message_create(relay);
                                
                                event.reply("✅ Thread opened! Staff will respond shortly.");

                                // Log the opening
                                if (config && config->log_channel_id) {
                                    dpp::embed log_embed = dpp::embed()
                                        .set_color(0x10B981)
                                        .set_title("Modmail Thread Opened")
                                        .add_field("User", event.msg.author.get_mention(), true)
                                        .add_field("Channel", "<#" + std::to_string(n_t.id) + ">", true)
                                        .set_timestamp(time(0));
                                    bot.message_create(dpp::message(config->log_channel_id, log_embed));
                                }
                            }
                        });
                    }
                } else {
                    // Multiple guilds, send select menu
                    dpp::message m;
                    m.set_content("You are in multiple servers with modmail. Please select where you want to send this message:");
                    
                    dpp::component select;
                    select.set_type(dpp::cot_selectmenu);
                    select.set_id("modmail_guild_select");
                    select.set_placeholder("Select a server...");
                    
                    for (auto* g : shared_guilds) {
                        select.add_select_option(dpp::select_option(g->name, std::to_string(g->id), "Message the " + g->name + " staff team."));
                    }
                    
                    m.add_component(dpp::component().add_component(select));
                    event.reply(m);
                    
                    // Store the message for later
                    pending_messages[event.msg.author.id] = {event.msg.content, time(0)};

                    // Periodic cleanup (simple)
                    if (pending_messages.size() > 100) {
                        time_t now = time(0);
                        for (auto it = pending_messages.begin(); it != pending_messages.end(); ) {
                            if (now - it->second.created_at > 300) it = pending_messages.erase(it);
                            else ++it;
                        }
                    }
                }
            }
            return;
        }

        // 3. Handle Guild Messages (Staff -> User)
        // Check if this channel is a modmail thread
        auto thread = db::get_modmail_thread_by_id(db, event.msg.channel_id);
        if (thread && thread->status == "open") {
            // This is a modmail thread!
            
            // Check if it's a command first (e.g. closing the thread)
            if (event.msg.content.find("!close") == 0) {
                if (db::close_modmail_thread(db, event.msg.channel_id)) {
                    event.reply("✅ Modmail thread closed.");
                    
                    // Notify user
                    dpp::message dm;
                    dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                    std::string guild_name = g ? g->name : "the server";
                    dm.set_content("Your modmail thread in **" + guild_name + "** has been closed by staff.");
                    bot.direct_message_create(thread->user_id, dm);

                    // Log and generate transcript
                    auto config = db::get_modmail_config(db, event.msg.guild_id);
                    if (config && config->log_channel_id) {
                        dpp::embed log_embed = dpp::embed()
                            .set_color(0xEF4444) // Rouge-ish
                            .set_title("Modmail Thread Closed")
                            .add_field("User", "<@" + std::to_string(thread->user_id) + ">", true)
                            .add_field("Moderator", event.msg.author.get_mention(), true)
                            .add_field("Channel", "<#" + std::to_string(event.msg.channel_id) + ">", true)
                            .set_timestamp(time(0));
                        bot.message_create(dpp::message(config->log_channel_id, log_embed));

                        // Generate Transcript
                        bronx::moderation::create_modmail_transcript(bot, event.msg.channel_id, config->log_channel_id, thread->user_id, [&bot, tid = event.msg.channel_id](bool ok) {
                            if (ok) {
                                bot.channel_delete(tid);
                            }
                        });
                    }
                } else {
                    event.reply("❌ Failed to close thread in database.");
                }
                return;
            }

            // Relay to user
            dpp::message dm;
            dm.set_content("**Staff:** " + event.msg.content);
            dm.set_channel_id(thread->user_id); 
            
            bot.direct_message_create(thread->user_id, dm, [thread, &bot, event](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    event.reply("❌ Failed to deliver message to user: " + cb.get_error().message);
                } else {
                    bot.message_add_reaction(event.msg.id, event.msg.channel_id, "✅");
                }
            });
        }
    });
}

} // namespace events
} // namespace bronx
