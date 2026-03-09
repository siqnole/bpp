#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <random>
#include <iostream>

namespace bronx {
    // Soft color palette
    const uint32_t COLOR_DEFAULT = 0xB4A7D6;    // Soft lavender
    const uint32_t COLOR_SUCCESS = 0xA8D5BA;    // Soft green
    const uint32_t COLOR_ERROR = 0xE5989B;      // Soft red
    const uint32_t COLOR_WARNING = 0xF4D9C6;    // Soft peach
    const uint32_t COLOR_INFO = 0xA7C7E7;       // Soft blue

    // Custom emojis - easily changeable
    const std::string EMOJI_CHECK   = "<:check:1476703556428890132>";
    const std::string EMOJI_DENY    = "<:deny:1476703341454168288>";
    const std::string EMOJI_WARNING = "<:warning:1476717080723063038>";
    const std::string EMOJI_STAR    = "<:star:1476703830656684093>";

    // Support server URL constant
    const std::string SUPPORT_SERVER_URL = "https://discord.gg/bronx";
    const std::string SUPPORT_SERVER_LINK = "[support](https://discord.gg/bronx)";

    // Create a styled embed with bronx aesthetic
    inline dpp::embed create_embed(const std::string& description, uint32_t color = COLOR_DEFAULT) {
        dpp::embed embed = dpp::embed()
            .set_description(description)
            .set_color(color)
            .set_timestamp(time(0));
        return embed;
    }

    // Add support server link to an embed (call this to add SEO support link)
    inline void add_support_link(dpp::embed& embed) {
        embed.add_field("", "questions? join " + SUPPORT_SERVER_LINK, false);
    }

    // Maybe add support link based on probability (default 15% chance)
    inline void maybe_add_support_link(dpp::embed& embed, double probability = 0.15) {
        static std::mt19937 support_rng(static_cast<unsigned>(time(nullptr)));
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(support_rng) < probability) {
            add_support_link(embed);
        }
    }

    // Add footer with invoker information or a random bot message
    // Occasionally we replace the normal "invoked by" footer with some
    // flavour text: a fun fact about the bot, an invite prompt, or a
    // suggestion to try a random economy command.  This keeps replies a bit
    // more interesting.
    inline void add_invoker_footer(dpp::embed& embed, const dpp::user& invoker) {
        static std::mt19937 rng(static_cast<unsigned>(time(nullptr)));
        // available footer types: 0=invoker, 1=fun fact, 2=invite, 3=economy
        std::uniform_int_distribution<int> dist(0, 3);
        int choice = dist(rng);

        if (choice == 0) {
            // original behaviour
            std::string display_name = invoker.global_name.empty() ? invoker.username : invoker.global_name;
            embed.set_footer(dpp::embed_footer()
                .set_text("invoked by " + display_name)
                .set_icon(invoker.get_avatar_url()));
            return;
        }

        // potential fun facts about the bot
        static const std::vector<std::string> facts = {
            "i'm powered by C++ and DPP!",
            "i once counted to infinity... twice!",
            "i respond to both slash and text commands!",
            "use suggest if you have any ideas for new commands or features!",
            "use passive to toggle passive mode and avoid being robbed!",
            "i'm open source! check out the code on github: github.com/siqnole/bronxbot",
            "i have a custom database layer built on top of MySQL!",
            "i'm named after the bronx, the best borough!",
            "my creator's name is siqnole, but you can call them siq :)",
            "i'm always learning and improving, so expect new features and commands in the future!",
            "thanks for using my commands! <3",
            "need help? join our support server!"
        };
        // some economy commands we can suggest
        static const std::vector<std::string> econ_cmds = {
            "daily", "wallet", "bank", "slots", "fish", "sell", "trade"
        };

        std::string footer_text;
        if (choice == 1) {
            footer_text = facts[rng() % facts.size()];
        } else if (choice == 2) {
            // prefix is hardcoded here; change if you ever make it configurable
            footer_text = "invite me with b.invite";
        } else {
            footer_text = "try " + econ_cmds[rng() % econ_cmds.size()];
        }
        embed.set_footer(dpp::embed_footer().set_text(footer_text));
    }

    // Success embed
    inline dpp::embed success(const std::string& message) {
        return create_embed(EMOJI_CHECK + " " + message, COLOR_SUCCESS);
    }

    // Error embed
    inline dpp::embed error(const std::string& message) {
        return create_embed(EMOJI_DENY + " " + message, COLOR_ERROR);
    }

    // Info embed
    inline dpp::embed info(const std::string& message) {
        return create_embed(message, COLOR_INFO);
    }

    // ========================================================================
    // Permission error detection & DM fallback helpers
    // ========================================================================

    // Discord error codes related to send/permission failures
    inline bool is_permission_error(const dpp::confirmation_callback_t& callback) {
        if (!callback.is_error()) return false;
        auto err = callback.get_error();
        // 50001 = Missing Access, 50013 = Missing Permissions
        // 50008 = Cannot send messages in a non-text channel
        // 10003 = Unknown Channel
        return err.code == 50001 || err.code == 50013 || err.code == 50008 || err.code == 10003;
    }

    inline bool is_embed_permission_error(const dpp::confirmation_callback_t& callback) {
        if (!callback.is_error()) return false;
        auto err = callback.get_error();
        // 50013 often includes embed links permission being missing
        return err.code == 50013;
    }

    inline bool is_dm_closed_error(const dpp::confirmation_callback_t& callback) {
        if (!callback.is_error()) return false;
        auto err = callback.get_error();
        // 50007 = Cannot send messages to this user (DMs disabled)
        return err.code == 50007;
    }

    // Build a user-friendly DM message explaining a permission issue
    inline dpp::message build_permission_dm(uint64_t channel_id, const std::string& guild_name = "") {
        std::string desc = EMOJI_DENY + " **i'm missing permissions!**\n\n"
            "i tried to respond in a channel but i don't have the right permissions.\n\n"
            "**please ask a server admin to check that i have:**\n"
            "• `Send Messages`\n"
            "• `Embed Links`\n"
            "• `Use External Emojis`\n"
            "• `Attach Files`\n"
            "• `Read Message History`\n\n";
        if (!guild_name.empty()) {
            desc += "*this happened in* **" + guild_name + "**\n";
        }
        desc += "*channel id:* `" + std::to_string(channel_id) + "`";

        auto embed = create_embed(desc, COLOR_ERROR);
        embed.set_footer(dpp::embed_footer().set_text("tip: check channel-specific permission overrides too!"));
        dpp::message msg;
        msg.add_embed(embed);
        return msg;
    }

    // Try to DM the user about a permission issue; silently logs if DM also fails
    inline void dm_permission_error(dpp::cluster& bot, uint64_t user_id, uint64_t channel_id,
                                     const std::string& guild_name = "") {
        auto dm_msg = build_permission_dm(channel_id, guild_name);
        bot.direct_message_create(user_id, dm_msg, [user_id, channel_id](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                // DMs are likely closed — nothing more we can do
                std::cerr << "Permission error DM failed for user " << user_id
                          << " (channel " << channel_id << "): " << cb.get_error().message << "\n";
            }
        });
    }

    // Try to send a plain-text fallback (no embed) when embed permission is missing
    inline void try_plain_text_fallback(dpp::cluster& bot, uint64_t user_id, dpp::snowflake channel_id,
                                         const std::string& guild_name = "") {
        std::string plain = "⚠️ i'm missing the **Embed Links** permission in this channel! "
                            "please ask a server admin to grant it so i can respond properly.";
        dpp::message fallback(channel_id, plain);
        bot.message_create(fallback, [&bot, user_id, channel_id, guild_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                // Can't even send plain text — DM the user
                dm_permission_error(bot, user_id, channel_id, guild_name);
            }
        });
    }

    // ========================================================================
    // Enhanced send_message with permission-error handling
    // ========================================================================

    // Helper function to send message with reply fallback
    inline void send_message(dpp::cluster& bot, const dpp::message_create_t& event, const dpp::embed& embed) {
        dpp::message msg(event.msg.channel_id, embed);
        msg.set_reference(event.msg.id);
        uint64_t user_id = event.msg.author.id;
        dpp::snowflake chan_id = event.msg.channel_id;
        std::string guild_name;
        if (event.msg.guild_id) {
            auto* g = dpp::find_guild(event.msg.guild_id);
            if (g) guild_name = g->name;
        }

        bot.message_create(msg, [&bot, event, embed, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& callback) {
            if (callback.is_error()) {
                if (is_permission_error(callback)) {
                    // Try plain text first, then DM
                    try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    return;
                }
                // Reply failed (message probably deleted), send normally
                bot.message_create(dpp::message(event.msg.channel_id, embed),
                    [&bot, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& cb2) {
                        if (cb2.is_error() && is_permission_error(cb2)) {
                            try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                        }
                    });
            }
        });
    }

    // Helper function to send message with reply fallback (message overload)
    inline void send_message(dpp::cluster& bot, const dpp::message_create_t& event, const dpp::message& message) {
        // we make a copy so we can mutate the reference and channel id without touching
        // the original message object.
        dpp::message msg = message;
        msg.set_reference(event.msg.id);
        msg.channel_id = event.msg.channel_id;          // ensure we target the right channel
        uint64_t user_id = event.msg.author.id;
        dpp::snowflake chan_id = event.msg.channel_id;
        std::string guild_name;
        if (event.msg.guild_id) {
            auto* g = dpp::find_guild(event.msg.guild_id);
            if (g) guild_name = g->name;
        }

        bot.message_create(msg, [&bot, event, message, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& callback) {
            if (callback.is_error()) {
                if (is_permission_error(callback)) {
                    try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    return;
                }
                // the reply attempt failed (often because the original message was deleted or
                // references are not permitted). fall back to a plain send of the entire
                // message object so that embeds *and* components are preserved.
                std::cerr << "send_message: reply failed, falling back to normal send" << std::endl;
                dpp::message fallback = message;
                fallback.channel_id = event.msg.channel_id;
                bot.message_create(fallback, [&bot, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& cb2) {
                    if (cb2.is_error() && is_permission_error(cb2)) {
                        try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    }
                });
            }
        });
    }

    // ========================================================================
    // Slash command permission-error reply helper
    // ========================================================================

    // For slash commands: attempt event.reply, and if that fails due to permissions, DM the user
    inline void safe_slash_reply(dpp::cluster& bot, const dpp::slashcommand_t& event, const dpp::message& reply_msg) {
        uint64_t user_id = event.command.get_issuing_user().id;
        uint64_t chan_id = event.command.channel_id;
        std::string guild_name;
        if (event.command.guild_id) {
            auto* g = dpp::find_guild(event.command.guild_id);
            if (g) guild_name = g->name;
        }
        event.reply(reply_msg, [&bot, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error() && is_permission_error(cb)) {
                dm_permission_error(bot, user_id, chan_id, guild_name);
            }
        });
    }

    inline void safe_slash_reply(dpp::cluster& bot, const dpp::slashcommand_t& event, const dpp::embed& embed) {
        safe_slash_reply(bot, event, dpp::message().add_embed(embed));
    }
}
