#include "text_filter_config.h"
#include "mod_log.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>

namespace commands {
namespace quiet_moderation {

// Define storage (single TU)
::std::map<dpp::snowflake, TextFilterConfig> guild_text_filters;
::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> text_filter_violations;

// Register text filter event handler
void register_text_filter(dpp::cluster& bot) {
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        // Ignore bots
        if (event.msg.author.is_bot()) return;

        // Ignore DMs
        if (event.msg.guild_id == 0) return;

        // Check if filtering is enabled for this guild
        if (guild_text_filters.find(event.msg.guild_id) == guild_text_filters.end()) return;

        auto& config = guild_text_filters[event.msg.guild_id];
        if (!config.enabled) return;

        // Check whitelist
        if (is_whitelisted(event.msg.guild_id, event.msg.author.id, event.msg.channel_id,
                          event.msg.member.get_roles(), config)) {
            return;
        }

        // Check message content
        auto [blocked, reason] = contains_blocked_content(event.msg.content, config);

        // Check attachments if enabled
        if (!blocked && config.check_attachments) {
            for (const auto& attachment : event.msg.attachments) {
                auto [att_blocked, att_reason] = contains_blocked_content(attachment.filename, config);
                if (att_blocked) {
                    blocked = true;
                    reason = "filename: " + att_reason;
                    break;
                }
            }
        }

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

} // namespace quiet_moderation
} // namespace commands
