#pragma once
// ============================================================================
// snipe.h — /snipe command: view recently deleted messages.
// Supports filtering by channel, user, and time range.
// Paginated (1 message per page, up to 10 pages) with prev/next buttons.
// Requires Manage Messages permission by default (customizable via .command).
// ============================================================================

#include "../../command.h"
#include "../../embed_style.h"
#include "../../performance/snipe_cache.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace commands {
namespace utility {

// ── time parsing helper ─────────────────────────────────────────
// Parses strings like "2h", "24h", "7d", "30d", "all" into a unix timestamp.
// Returns 0 for "all" (no lower bound).
inline int64_t parse_time_filter(const std::string& input) {
    if (input.empty() || input == "2h") {
        // default: last 2 hours
        auto since = std::chrono::system_clock::now() - std::chrono::hours(2);
        return std::chrono::system_clock::to_time_t(since);
    }
    if (input == "all") return 0;

    // Parse "<number><unit>" where unit is h/d/w/m
    size_t idx = 0;
    int value = 0;
    try {
        value = std::stoi(input, &idx);
    } catch (...) {
        // fallback to 2h
        auto since = std::chrono::system_clock::now() - std::chrono::hours(2);
        return std::chrono::system_clock::to_time_t(since);
    }
    if (value <= 0) {
        auto since = std::chrono::system_clock::now() - std::chrono::hours(2);
        return std::chrono::system_clock::to_time_t(since);
    }

    char unit = (idx < input.size()) ? input[idx] : 'h';
    std::chrono::hours duration(2);  // default
    switch (unit) {
        case 'h': duration = std::chrono::hours(value); break;
        case 'd': duration = std::chrono::hours(value * 24); break;
        case 'w': duration = std::chrono::hours(value * 24 * 7); break;
        case 'm': duration = std::chrono::hours(value * 24 * 30); break;
        default:  duration = std::chrono::hours(value); break;
    }
    auto since = std::chrono::system_clock::now() - duration;
    return std::chrono::system_clock::to_time_t(since);
}

// ── build a snipe embed for a single deleted message ────────────
inline dpp::embed build_snipe_embed(
    const bronx::snipe::DeletedMessage& msg,
    int page,
    int total_pages,
    uint64_t target_channel_id)
{
    auto embed = bronx::create_embed("", bronx::COLOR_INFO);

    // Author line: who wrote the deleted message
    dpp::embed_author author;
    author.name = msg.author_tag;
    if (!msg.author_avatar.empty()) {
        author.icon_url = msg.author_avatar;
    }
    embed.set_author(author);

    // Message content
    std::string desc;
    if (!msg.content.empty()) {
        desc = msg.content;
        if (desc.size() > 2000) desc = desc.substr(0, 2000) + "...";
    } else {
        desc = "*no text content*";
    }
    embed.set_description(desc);

    // Attachment URLs
    if (!msg.attachment_urls.empty()) {
        std::ostringstream att_text;
        for (size_t i = 0; i < msg.attachment_urls.size(); ++i) {
            att_text << "[attachment " << (i + 1) << "](" << msg.attachment_urls[i] << ")";
            if (i + 1 < msg.attachment_urls.size()) att_text << "\n";
        }
        embed.add_field("attachments", att_text.str(), false);

        // If first attachment looks like an image, set as embed image
        const auto& first = msg.attachment_urls[0];
        if (first.find(".png") != std::string::npos ||
            first.find(".jpg") != std::string::npos ||
            first.find(".jpeg") != std::string::npos ||
            first.find(".gif") != std::string::npos ||
            first.find(".webp") != std::string::npos) {
            embed.set_image(first);
        }
    }

    // Embed summary (if the original message had embeds)
    if (!msg.embeds_summary.empty()) {
        std::string summary = msg.embeds_summary;
        if (summary.size() > 500) summary = summary.substr(0, 500) + "...";
        embed.add_field("embeds", summary, false);
    }

    // Channel mention (if sniping across channels)
    if (msg.channel_id != target_channel_id) {
        embed.add_field("channel", "<#" + std::to_string(msg.channel_id) + ">", true);
    }

    // Footer with page info; embed timestamp handles the "when" display
    auto deleted_unix = std::chrono::system_clock::to_time_t(msg.deleted_at);
    std::string footer_text = "deleted • page " + std::to_string(page + 1) + "/" + std::to_string(total_pages);
    embed.set_footer(dpp::embed_footer().set_text(footer_text));
    embed.set_timestamp(deleted_unix);

    return embed;
}

// ── build navigation buttons ────────────────────────────────────
// Button ID format: snipe_{prev|next}_{userid}_{page}_{channelfilter}_{authorfilter}_{timefilter}
inline dpp::component build_snipe_nav_buttons(
    uint64_t user_id,
    int current_page,
    int total_pages,
    uint64_t channel_filter,
    uint64_t author_filter,
    const std::string& time_filter)
{
    std::string suffix = "_" + std::to_string(user_id)
        + "_" + std::to_string(current_page)
        + "_" + std::to_string(channel_filter)
        + "_" + std::to_string(author_filter)
        + "_" + time_filter;

    dpp::component nav_row;
    nav_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_emoji("◀️")
            .set_style(dpp::cos_primary)
            .set_disabled(current_page == 0)
            .set_id("snipe_prev" + suffix)
    );
    nav_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_emoji("▶️")
            .set_style(dpp::cos_primary)
            .set_disabled(current_page >= total_pages - 1)
            .set_id("snipe_next" + suffix)
    );
    return nav_row;
}

// ── core snipe logic (shared between text + slash handlers) ─────
inline void execute_snipe(
    dpp::cluster& bot,
    bronx::snipe::SnipeCache* cache,
    uint64_t guild_id,
    uint64_t invoker_channel_id,
    uint64_t invoker_user_id,
    uint64_t channel_filter,  // 0 = guild-wide (use invoker channel)
    uint64_t author_filter,   // 0 = all authors
    const std::string& time_str,
    int page,
    std::function<void(dpp::message)> reply_fn)
{
    // Determine effective channel filter
    uint64_t effective_channel = channel_filter;
    bool guild_wide = false;
    if (channel_filter == 0) {
        effective_channel = invoker_channel_id;
    }
    // Special: if "server" scope was requested, set guild_wide
    // We pass channel_filter=1 as a sentinel for "server-wide" from text command
    // Actually, let's use channel_filter=0 for guild-wide and non-zero for specific channel
    // The caller should set channel_filter=0 for server-wide, invoker_channel for channel scope

    int64_t since_unix = parse_time_filter(time_str);
    int max_results = 10; // max pages

    // Get total count for pagination (checks both memory and DB)
    int total_count = std::min(cache->count(guild_id, effective_channel, author_filter, since_unix), max_results);
    int total_pages = std::max(total_count, 1);

    // Clamp page
    if (page >= total_pages) page = total_pages - 1;
    if (page < 0) page = 0;

    // Query a single result at the requested page offset
    auto results = cache->query(guild_id, effective_channel, author_filter, since_unix, 1, page);

    if (results.empty()) {
        std::string error_msg = "no deleted messages found";
        if (!time_str.empty() && time_str != "all") {
            error_msg += " in the last " + time_str;
        }
        reply_fn(dpp::message().add_embed(bronx::error(error_msg)));
        return;
    }

    auto embed = build_snipe_embed(results[0], page, total_pages, invoker_channel_id);
    auto nav = build_snipe_nav_buttons(invoker_user_id, page, total_pages,
                                        effective_channel, author_filter, time_str);

    dpp::message msg;
    msg.add_embed(embed);
    if (total_pages > 1) {
        msg.add_component(nav);
    }
    reply_fn(std::move(msg));
}

// ── slash command factory ───────────────────────────────────────
inline Command* get_snipe_command(bronx::snipe::SnipeCache* cache) {
    static Command cmd("snipe",
        "view recently deleted messages in this channel",
        "utility",
        {"s"},  // alias
        true,   // is_slash_command
        // ── text handler ──
        [cache](dpp::cluster& bot, const dpp::message_create_t& event,
                const std::vector<std::string>& args)
        {
            // Permission check: require manage messages
            dpp::guild* g = dpp::find_guild(event.msg.guild_id);
            if (g) {
                auto perms = g->base_permissions(event.msg.member);
                if (!(perms & dpp::p_manage_messages) && !(perms & dpp::p_administrator)) {
                    bronx::send_message(bot, event, bronx::error("you need **manage messages** to use this"));
                    return;
                }
            }

            // Parse text args: snipe [channel|user|time] ...
            // Supported forms:
            //   b.snipe                     — last 2h in current channel
            //   b.snipe @user               — filter by mentioned user
            //   b.snipe #channel            — different channel
            //   b.snipe 24h                 — time range
            //   b.snipe all                 — all time
            //   b.snipe server              — guild-wide
            //   b.snipe @user 7d            — user + time
            uint64_t channel_filter = event.msg.channel_id;
            uint64_t author_filter = 0;
            std::string time_str = "2h";
            bool guild_wide = false;

            for (const auto& arg : args) {
                // Check for user mention
                if (!event.msg.mentions.empty() && arg.find("<@") == 0) {
                    author_filter = event.msg.mentions[0].first.id;
                }
                // Check for channel mention
                else if (arg.find("<#") == 0 && arg.back() == '>') {
                    try {
                        channel_filter = std::stoull(arg.substr(2, arg.size() - 3));
                    } catch (...) {}
                }
                // Check for "server" / "guild" keyword
                else if (arg == "server" || arg == "guild") {
                    guild_wide = true;
                }
                // Check for time filter
                else if (arg == "all" || (arg.size() <= 5 && (arg.back() == 'h' || arg.back() == 'd' || arg.back() == 'w' || arg.back() == 'm'))) {
                    time_str = arg;
                }
            }

            if (guild_wide) channel_filter = 0;

            execute_snipe(bot, cache, event.msg.guild_id, event.msg.channel_id,
                          event.msg.author.id, channel_filter, author_filter, time_str, 0,
                          [&bot, &event](dpp::message msg) {
                              msg.set_channel_id(event.msg.channel_id);
                              msg.set_guild_id(event.msg.guild_id);
                              bot.message_create(msg);
                          });
        },
        // ── slash handler ──
        [cache](dpp::cluster& bot, const dpp::slashcommand_t& event)
        {
            // Permission check
            auto perms = event.command.get_resolved_permission(event.command.usr.id);
            if (!(perms & dpp::p_manage_messages) && !(perms & dpp::p_administrator)) {
                event.reply(dpp::message()
                    .add_embed(bronx::error("you need **manage messages** to use this"))
                    .set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t channel_filter = event.command.channel_id;
            uint64_t author_filter = 0;
            std::string time_str = "2h";

            // Parse channel option
            auto ch_param = event.get_parameter("channel");
            if (std::holds_alternative<dpp::snowflake>(ch_param)) {
                channel_filter = std::get<dpp::snowflake>(ch_param);
            }

            // Parse user option
            auto user_param = event.get_parameter("user");
            if (std::holds_alternative<dpp::snowflake>(user_param)) {
                author_filter = std::get<dpp::snowflake>(user_param);
            }

            // Parse time option
            auto time_param = event.get_parameter("time");
            if (std::holds_alternative<std::string>(time_param)) {
                time_str = std::get<std::string>(time_param);
            }

            // Parse scope option (channel vs server)
            auto scope_param = event.get_parameter("scope");
            if (std::holds_alternative<std::string>(scope_param)) {
                std::string scope = std::get<std::string>(scope_param);
                if (scope == "server") {
                    channel_filter = 0; // guild-wide
                }
            }

            execute_snipe(bot, cache, event.command.guild_id, event.command.channel_id,
                          event.command.usr.id, channel_filter, author_filter, time_str, 0,
                          [&event](dpp::message msg) {
                              event.reply(msg);
                          });
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_channel, "channel", "channel to snipe (default: current)", false),
            dpp::command_option(dpp::co_user, "user", "filter by message author", false),
            dpp::command_option(dpp::co_string, "time", "time range: 2h, 24h, 7d, 30d, all (default: 2h)", false)
                .add_choice(dpp::command_option_choice("last 2 hours", std::string("2h")))
                .add_choice(dpp::command_option_choice("last 24 hours", std::string("24h")))
                .add_choice(dpp::command_option_choice("last 7 days", std::string("7d")))
                .add_choice(dpp::command_option_choice("last 30 days", std::string("30d")))
                .add_choice(dpp::command_option_choice("all time", std::string("all"))),
            dpp::command_option(dpp::co_string, "scope", "search scope", false)
                .add_choice(dpp::command_option_choice("this channel", std::string("channel")))
                .add_choice(dpp::command_option_choice("entire server", std::string("server")))
        }
    );

    // Extended help
    cmd.extended_description = "shows recently deleted messages. defaults to the last 2 hours in the current channel. "
                               "use the time option to search further back, or scope to search the entire server.";
    cmd.examples = {
        "b.snipe",
        "b.snipe @user",
        "b.snipe #general",
        "b.snipe server",
        "b.snipe all",
        "b.snipe @user 7d",
        "/snipe time:all scope:server"
    };
    cmd.notes = "requires manage messages permission. configurable via b.command.";

    return &cmd;
}

// ── button interaction handler for pagination ───────────────────
inline void register_snipe_interactions(dpp::cluster& bot, bronx::snipe::SnipeCache* cache) {
    bot.on_button_click([&bot, cache](const dpp::button_click_t& event) {
        std::string cid = event.custom_id;
        if (cid.find("snipe_prev_") != 0 && cid.find("snipe_next_") != 0) return;

        // Parse: snipe_{action}_{userid}_{page}_{channelfilter}_{authorfilter}_{timefilter}
        // Find action (prev or next)
        bool is_next = (cid.find("snipe_next_") == 0);
        std::string rest = cid.substr(is_next ? 11 : 11); // skip "snipe_prev_" or "snipe_next_"

        // Split by underscore
        std::vector<std::string> parts;
        std::istringstream iss(rest);
        std::string part;
        while (std::getline(iss, part, '_')) {
            parts.push_back(part);
        }

        if (parts.size() < 5) {
            event.reply(dpp::ir_update_message, event.command.msg);
            return;
        }

        uint64_t owner_id = 0;
        int current_page = 0;
        uint64_t channel_filter = 0;
        uint64_t author_filter = 0;
        std::string time_filter;

        try {
            owner_id = std::stoull(parts[0]);
            current_page = std::stoi(parts[1]);
            channel_filter = std::stoull(parts[2]);
            author_filter = std::stoull(parts[3]);
            time_filter = parts[4];
        } catch (...) {
            event.reply(dpp::ir_update_message, event.command.msg);
            return;
        }

        // Verify user
        if (event.command.usr.id != owner_id) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message(bronx::EMOJI_DENY + " this isn't yours to flip through")
                    .set_flags(dpp::m_ephemeral));
            return;
        }

        // Calculate new page
        int new_page = current_page + (is_next ? 1 : -1);
        if (new_page < 0) new_page = 0;

        int64_t since_unix = parse_time_filter(time_filter);
        int total_count = std::min(
            cache->count(event.command.guild_id, channel_filter, author_filter, since_unix), 10);
        int total_pages = std::max(total_count, 1);

        if (new_page >= total_pages) new_page = total_pages - 1;

        // Query the specific page
        auto results = cache->query(event.command.guild_id, channel_filter, author_filter,
                                     since_unix, 1, new_page);

        if (results.empty()) {
            event.reply(dpp::ir_update_message, event.command.msg);
            return;
        }

        auto embed = build_snipe_embed(results[0], new_page, total_pages, event.command.channel_id);
        auto nav = build_snipe_nav_buttons(owner_id, new_page, total_pages,
                                            channel_filter, author_filter, time_filter);

        dpp::message msg;
        msg.add_embed(embed);
        if (total_pages > 1) {
            msg.add_component(nav);
        }
        event.reply(dpp::ir_update_message, msg);
    });
}

} // namespace utility
} // namespace commands
