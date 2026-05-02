#include "modmail_handler.h"
#include "../database/operations/moderation/modmail_operations.h"
#include "../utils/logger.h"
#include "../utils/colors.h"
#include <iostream>

namespace bronx {
namespace events {

void register_modmail_handlers(dpp::cluster& bot, db::Database* db) {
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
                // No active thread
                // TODO: Phase 6 - Implement guild selection
                event.reply("You don't have an active modmail thread. (Guild selection coming soon in Phase 6)");
            }
            return;
        }

        // 3. Handle Guild Messages (Staff -> User)
        // Check if this channel is a modmail thread
        auto thread = db::get_modmail_thread_by_id(db, event.msg.channel_id);
        if (thread && thread->status == "open") {
            // This is a modmail thread!
            // Relay message to user DM.
            
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
                } else {
                    event.reply("❌ Failed to close thread in database.");
                }
                return;
            }

            // Relay to user
            dpp::message dm;
            dm.set_content("**Staff:** " + event.msg.content);
            dm.set_channel_id(thread->user_id); // This is actually the user_id, DPP will handle opening DM
            
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
