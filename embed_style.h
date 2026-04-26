#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <random>
#include <iostream>
#include <atomic>
#include <chrono>
#include <mutex>
#include <array>
#include <string_view>

namespace bronx {

    // ========================================================================
    // REST API Circuit Breaker
    // When Discord returns repeated 503 / upstream-overflow errors the bot
    // should *stop* making new REST calls for a short window.  This prevents
    // the retry-amplification loop that deepens the overload.
    // ========================================================================

    struct ApiCircuitBreaker {
        std::atomic<int>  consecutive_failures{0};
        std::atomic<int64_t> tripped_at_ms{0};
        std::atomic<int>  backoff_seconds{5};

        static constexpr int FAILURE_THRESHOLD = 3;
        static constexpr int MAX_BACKOFF       = 60;

        void record_failure() {
            int prev = consecutive_failures.fetch_add(1);
            if (prev + 1 == FAILURE_THRESHOLD) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                tripped_at_ms.store(now);
                int bo = backoff_seconds.load();
                std::cerr << "\033[1;33m⚠ API circuit breaker OPEN — "
                          << (prev + 1) << " consecutive 503s, backing off " << bo << "s\033[0m\n";
                if (bo < MAX_BACKOFF) backoff_seconds.store(std::min(bo * 2, MAX_BACKOFF));
            } else if (prev + 1 > FAILURE_THRESHOLD) {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                tripped_at_ms.store(now);
                int bo = backoff_seconds.load();
                if (bo < MAX_BACKOFF) backoff_seconds.store(std::min(bo * 2, MAX_BACKOFF));
            }
        }

        void record_success() {
            if (consecutive_failures.load() > 0) {
                consecutive_failures.store(0);
                backoff_seconds.store(5);
            }
        }

        bool is_healthy() const {
            if (consecutive_failures.load() < FAILURE_THRESHOLD) return true;
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t tripped = tripped_at_ms.load();
            int bo = backoff_seconds.load();
            return (now - tripped) > (bo * 1000LL);
        }
    };

    inline ApiCircuitBreaker& api_breaker() {
        static ApiCircuitBreaker instance;
        return instance;
    }

    // Soft color palette
    const uint32_t COLOR_DEFAULT = 0xB4A7D6;
    const uint32_t COLOR_SUCCESS = 0xA8D5BA;
    const uint32_t COLOR_ERROR   = 0xE5989B;
    const uint32_t COLOR_WARNING  = 0xF4D9C6;
    const uint32_t COLOR_INFO    = 0xA7C7E7;

    // Custom emojis (stored as string_view to avoid construction on each call)
    inline const std::string EMOJI_CHECK   = "<:check:1476703556428890132>";
    inline const std::string EMOJI_DENY    = "<:deny:1476703341454168288>";
    inline const std::string EMOJI_WARNING = "<:warning:1476717080723063038>";
    inline const std::string EMOJI_STAR    = "<:star:1476703830656684093>";

    // Support server constants
    inline constexpr std::string_view SUPPORT_SERVER_URL  = "https://discord.gg/bronx";
    inline constexpr std::string_view SUPPORT_SERVER_LINK = "[support](https://discord.gg/bronx)";

    // Utility: convert current time to Discord timestamp (ISO 8601)
    inline std::string current_iso_timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        return std::string(ctime(&tt));
    }

    // Create a styled embed with bronx aesthetic
    inline dpp::embed create_embed(const std::string& description, uint32_t color = COLOR_DEFAULT) {
        dpp::embed embed;
        embed.set_description(description);
        embed.set_color(color);
        embed.set_timestamp(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        return embed;
    }

    inline void add_support_link(dpp::embed& embed) {
        embed.add_field("", "questions? join " + std::string(SUPPORT_SERVER_LINK), false);
    }

    inline void maybe_add_support_link(dpp::embed& embed, double probability = 0.15) {
        static thread_local std::mt19937 support_rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(support_rng) < probability) {
            add_support_link(embed);
        }
    }

    // Add footer with invoker info or a random flavour text
    inline void add_invoker_footer(dpp::embed& embed, const dpp::user& invoker) {
        static thread_local std::mt19937 rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<int> dist(0, 3);
        int choice = dist(rng);

        if (choice == 0) {
            std::string display_name = invoker.global_name.empty() ? invoker.username : invoker.global_name;
            embed.set_footer(dpp::embed_footer()
                .set_text("invoked by " + display_name)
                .set_icon(invoker.get_avatar_url()));
            return;
        }

        static const std::array<std::string_view, 12> FUN_FACTS {
            "i'm powered by c++ and dpp!",
            "i once counted to infinity... twice!",
            "i respond to both slash and text commands!",
            "use suggest if you have ideas!",
            "use passive to avoid robbery!",
            "i'm open source! github.com/siqnole/bpp",
            "i have a custom database layer built on mysql!",
            "i'm named after the bronx, the best borough!",
            "my creator's name is siqnole, but call them siq :)",
            "i'm always learning, expect new features!",
            "thanks for using my commands! <3",
            "need help? join our support server!"
        };

        static const std::array<std::string_view, 7> ECONOMY_CMDS {"daily", "wallet", "bank", "slots", "fish", "sell", "trade"};

        std::string footer_text;
        if (choice == 1) {
            footer_text = FUN_FACTS.at(rng() % FUN_FACTS.size());
        } else if (choice == 2) {
            footer_text = "invite me with b.invite";
        } else {
            footer_text = std::string("try ") + std::string(ECONOMY_CMDS.at(rng() % ECONOMY_CMDS.size()));
        }
        embed.set_footer(dpp::embed_footer().set_text(footer_text));
    }

    // Simple embeds
    inline dpp::embed success(const std::string& message) {
        return create_embed(std::string(EMOJI_CHECK) + " " + message, COLOR_SUCCESS);
    }

    inline dpp::embed error(const std::string& message) {
        return create_embed(std::string(EMOJI_DENY) + " " + message, COLOR_ERROR);
    }

    inline dpp::embed info(const std::string& message) {
        return create_embed(message, COLOR_INFO);
    }

    // ========================================================================
    // Permission and error helpers
    // ========================================================================

    inline bool is_permission_error(const dpp::confirmation_callback_t& callback) {
        if (!callback.is_error()) return false;
        int code = callback.get_error().code;
        return code == 50001 || code == 50013 || code == 50008 || code == 10003;
    }

    inline bool is_transient_overload(const dpp::confirmation_callback_t& callback) {
        if (!callback.is_error()) {
            api_breaker().record_success();
            return false;
        }
        const auto& err = callback.get_error();
        bool overload = err.message.find("503") != std::string::npos ||
                        err.message.find("502") != std::string::npos ||
                        err.message.find("parse_error") != std::string::npos ||
                        err.message.find("upstream connect error") != std::string::npos;
        if (overload) api_breaker().record_failure();
        return overload;
    }

    inline bool is_embed_permission_error(const dpp::confirmation_callback_t& callback) {
        return callback.is_error() && callback.get_error().code == 50013;
    }

    inline bool is_dm_closed_error(const dpp::confirmation_callback_t& callback) {
        return callback.is_error() && callback.get_error().code == 50007;
    }

    // Build a DM message explaining a permission issue
    inline dpp::message build_permission_dm(uint64_t channel_id, const std::string& guild_name = "") {
        std::string desc = std::string(EMOJI_DENY) + " **i'm missing permissions!**\n\n"
            "i tried to respond in a channel but i don't have the right permissions.\n\n"
            "**please ask a server admin to check that i have:**\n"
            "• `send messages`\n"
            "• `embed links`\n"
            "• `use external emojis`\n"
            "• `attach files`\n"
            "• `read message history`\n\n";
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

    // DM the user for a permission problem
    inline void dm_permission_error(const dpp::cluster& bot, uint64_t user_id, uint64_t channel_id,
                                    const std::string& guild_name = "") {
        auto dm_msg = build_permission_dm(channel_id, guild_name);
        // dpp::cluster::direct_message_create is non-const; const_cast is safe here as we only need mutable access
        const_cast<dpp::cluster&>(bot).direct_message_create(user_id, dm_msg, [user_id, channel_id](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cerr << "Permission error DM failed for user " << user_id
                          << " (channel " << channel_id << "): " << cb.get_error().message << "\n";
            }
        });
    }

    // Fallback plain text if embed fails
    inline void try_plain_text_fallback(dpp::cluster& bot, uint64_t user_id, dpp::snowflake channel_id,
                                        const std::string& guild_name = "") {
        std::string plain = std::string(EMOJI_WARNING) + " i'm missing the **embed links** permission in this channel! "
                            "please ask a server admin to grant it so i can respond properly.";
        dpp::message fallback(channel_id, plain);
        bot.message_create(fallback, [&bot, user_id, channel_id, guild_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                if (is_transient_overload(cb)) return;
                dm_permission_error(bot, user_id, channel_id, guild_name);
            }
        });
    }

    // ========================================================================
    // Sending messages with permission fallbacks
    // ========================================================================

    inline void send_message(dpp::cluster& bot, const dpp::message_create_t& event, const dpp::embed& embed) {
        if (!api_breaker().is_healthy()) return;

        dpp::message msg(event.msg.channel_id, embed);
        msg.set_reference(event.msg.id);
        uint64_t user_id = event.msg.author.id;
        dpp::snowflake chan_id = event.msg.channel_id;
        std::string guild_name;
        if (event.msg.guild_id) {
            const auto* g = dpp::find_guild(event.msg.guild_id);
            if (g) guild_name = g->name;
        }

        auto start = std::chrono::steady_clock::now();
        bot.message_create(msg, [&bot, event, embed, user_id, chan_id, guild_name, start](const dpp::confirmation_callback_t& callback) {
            auto end = std::chrono::steady_clock::now();
            double rest_ms = std::chrono::duration<double, std::milli>(end - start).count();
            if (rest_ms > 500.0) {
                std::cerr << "\033[1;33m[rest-slow]\033[0m send_message REST round-trip: " << rest_ms << "ms (channel " << chan_id << ")\n";
            }
            if (callback.is_error()) {
                if (is_transient_overload(callback)) return;
                if (is_permission_error(callback)) {
                    try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    return;
                }
                bot.message_create(dpp::message(event.msg.channel_id, embed), [&bot, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& cb2) {
                    if (cb2.is_error() && is_permission_error(cb2)) {
                        try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    }
                });
            }
        });
    }

    inline void send_message(dpp::cluster& bot, const dpp::message_create_t& event, const dpp::message& message) {
        if (!api_breaker().is_healthy()) return;

        dpp::message msg = message;
        if (msg.file_data.empty()) {
            msg.set_reference(event.msg.id);
        }
        msg.channel_id = event.msg.channel_id;
        uint64_t user_id = event.msg.author.id;
        dpp::snowflake chan_id = event.msg.channel_id;
        std::string guild_name;
        if (event.msg.guild_id) {
            const auto* g = dpp::find_guild(event.msg.guild_id);
            if (g) guild_name = g->name;
        }

        auto start = std::chrono::steady_clock::now();
        bot.message_create(msg, [&bot, event, message, user_id, chan_id, guild_name, start](const dpp::confirmation_callback_t& callback) {
            auto end = std::chrono::steady_clock::now();
            double rest_ms = std::chrono::duration<double, std::milli>(end - start).count();
            if (rest_ms > 500.0) {
                std::cerr << "\033[1;33m[rest-slow]\033[0m send_message(msg) REST round-trip: " << rest_ms << "ms (channel " << chan_id << ")\n";
            }
            if (callback.is_error()) {
                if (is_transient_overload(callback)) {
                    std::cerr << "send_message: API overload, dropping retry (" << callback.get_error().message << ")\n";
                    return;
                }
                if (is_permission_error(callback)) {
                    try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                    return;
                }
                std::cerr << "send_message: reply failed (" << callback.get_error().message
                          << "), falling back to normal send\n";
                dpp::message fallback = message;
                fallback.channel_id = event.msg.channel_id;
                bot.message_create(fallback, [&bot, user_id, chan_id, guild_name](const dpp::confirmation_callback_t& cb2) {
                    if (cb2.is_error()) {
                        if (is_permission_error(cb2)) {
                            try_plain_text_fallback(bot, user_id, chan_id, guild_name);
                        } else if (!is_transient_overload(cb2)) {
                            std::cerr << "send_message: fallback also failed: " << cb2.get_error().message << "\n";
                        }
                    }
                });
            }
        });
    }

    // ========================================================================
    // Slash command permission-error reply helper
    // ========================================================================

    inline void safe_slash_reply(dpp::cluster& bot, const dpp::slashcommand_t& event, const dpp::message& reply_msg) {
        uint64_t user_id = event.command.get_issuing_user().id;
        uint64_t chan_id = event.command.channel_id;
        std::string guild_name;
        if (event.command.guild_id) {
            const auto* g = dpp::find_guild(event.command.guild_id);
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

    // ========================================================================
    // Safe message edit with error handling
    // ========================================================================

    inline void safe_message_edit(dpp::cluster& bot, const dpp::message& msg,
                                  std::function<void(bool)> callback = nullptr) {
        if (!api_breaker().is_healthy()) {
            if (callback) callback(false);
            return;
        }
        bot.message_edit(msg, [callback](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                is_transient_overload(cb);
                const auto& err = cb.get_error();
                std::cerr << "[safe_message_edit] Edit failed (code " << err.code << "): " << err.message << "\n";
                if (callback) callback(false);
            } else {
                api_breaker().record_success();
                if (callback) callback(true);
            }
        });
    }

    inline void safe_message_edit(dpp::cluster& bot, dpp::snowflake channel_id, dpp::snowflake message_id,
                                   const dpp::embed& embed, std::function<void(bool)> callback = nullptr) {
        dpp::message msg;
        msg.channel_id = channel_id;
        msg.id = message_id;
        msg.add_embed(embed);
        safe_message_edit(bot, msg, callback);
    }
}
