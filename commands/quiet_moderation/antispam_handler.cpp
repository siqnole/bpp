#include "antispam_config.h"
#include "mod_log.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../moderation/infraction_engine.h"

namespace commands {
namespace quiet_moderation {

// Define storage (single TU)
::std::map<dpp::snowflake, AntiSpamConfig> guild_antispam_configs;
::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> antispam_violations;
::std::map<dpp::snowflake, ::std::map<dpp::snowflake, UserMessageData>> user_message_tracking;

// Register antispam event handler (uses helpers from antispam_config.h)
void register_antispam(dpp::cluster& bot) {
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (event.msg.author.is_bot()) return;
        if (event.msg.guild_id == 0) return;

        if (guild_antispam_configs.find(event.msg.guild_id) == guild_antispam_configs.end()) return;

        auto& config = guild_antispam_configs[event.msg.guild_id];
        if (!config.enabled) return;

        // Whitelist / blacklist handling (honour per-module toggles)
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

        // If whitelisting is enabled and the user/channel/role is whitelisted, skip checks
        // Whitelist overrides blacklist — if you're whitelisted, you're always exempt
        if (is_whitelisted) {
            return;
        }

        // Otherwise proceed to spam detection (blacklist entries are still subject to checks)
        auto& user_data = user_message_tracking[event.msg.guild_id][event.msg.author.id];
        SpamResult sres = detect_spam(event, config, user_data);

        if (!sres.is_spam) return;

        // Spam detected
        antispam_violations[event.msg.guild_id][event.msg.author.id]++;
        int violation_count = antispam_violations[event.msg.guild_id][event.msg.author.id];

        // Module-specific behavior (if configured)
        DetectionBehavior behavior;
        if (sres.module == "duplicates") behavior = config.duplicates_behavior;
        else if (sres.module == "mentions") behavior = config.mentions_behavior;
        else if (sres.module == "emojis") behavior = config.emojis_behavior;
        else if (sres.module == "caps") behavior = config.caps_behavior;
        else if (sres.module == "newlines") behavior = config.newlines_behavior;
        else behavior = config.ratelimit_behavior; // ratelimit or default

        // Delete message either by module action or global setting
        if (behavior.action == "delete" || config.delete_messages) {
            bot.message_delete(event.msg.id, event.msg.channel_id);
        }

        // Logging: per-module log flag overrides global log_violations
        bool should_log = (behavior.log || config.log_violations) && (config.log_channel != 0);
        if (should_log) {
            dpp::embed log_embed = bronx::info("Anti-Spam Violation");
            log_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
            log_embed.add_field("Channel", "<#" + ::std::to_string(event.msg.channel_id) + ">", true);
            log_embed.add_field("Module", sres.module, true);
            log_embed.add_field("Reason", sres.reason, true);
            log_embed.add_field("Violations", ::std::to_string(violation_count), true);
            log_embed.set_timestamp(time(0));

            commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, log_embed);
        }

        // Warn user (global behavior)
        if (config.warn_user && violation_count == 1) {
            bot.direct_message_create(event.msg.author.id,
                dpp::message().add_embed(
                    bronx::info("Spam Detected")
                        .set_description("Your message was removed for spam.\n**Reason:** " + sres.reason)
                )
            );
        }

        // Module-level immediate actions (if configured)
        if (behavior.action == "timeout" || behavior.action == "kick" || behavior.action == "ban") {
            std::string infraction_type = "auto_antispam";
            std::string reason = "Anti-spam — module action: " + sres.module + ", " + sres.reason;
            double points = 0;
            uint32_t duration = 0;
            if (behavior.action == "timeout") {
                points = 2.0;
                duration = behavior.timeout_seconds ? behavior.timeout_seconds : config.timeout_duration;
            } else if (behavior.action == "kick") {
                points = 3.0;
            } else if (behavior.action == "ban") {
                points = 5.0;
            }
            auto inf = bronx::db::infraction_operations::create_infraction(
                nullptr, event.msg.guild_id, event.msg.author.id, bot.me.id,
                infraction_type, reason, points, duration,
                "{\"automod\":true,\"module\":\"" + sres.module + "\",\"violations\":" + std::to_string(violation_count) + "}" );
            if (inf.has_value()) {
                commands::moderation::send_mod_log(bot, nullptr, event.msg.guild_id, inf.value());
            }
            // Execute Discord-side action
            if (behavior.action == "timeout") {
                bot.guild_member_timeout(event.msg.guild_id, event.msg.author.id,
                    time(0) + duration,
                    [](const dpp::confirmation_callback_t&) {});
            } else if (behavior.action == "kick") {
                bot.guild_member_delete(event.msg.guild_id, event.msg.author.id,
                    [](const dpp::confirmation_callback_t&) {});
            } else if (behavior.action == "ban") {
                bot.guild_ban_add(event.msg.guild_id, event.msg.author.id, 1,
                    [](const dpp::confirmation_callback_t&) {});
            }
        }

        // Global escalation (still applies based on cumulative violations)
        if (config.max_violations_before_timeout > 0 &&
            violation_count >= config.max_violations_before_timeout &&
            violation_count < config.max_violations_before_kick) {

            bot.guild_member_timeout(event.msg.guild_id, event.msg.author.id,
                                     time(0) + config.timeout_duration,
                [&bot, event, config, violation_count](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error() && config.log_channel != 0) {
                        dpp::embed timeout_embed = bronx::error("User Timed Out");
                        timeout_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
                        timeout_embed.add_field("Reason", "Anti-spam violations", true);
                        timeout_embed.add_field("Duration", ::std::to_string(config.timeout_duration) + "s", true);
                        timeout_embed.add_field("Violations", ::std::to_string(violation_count), true);

                        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, timeout_embed);
                    }
                }
            );
        }

        // Kick (global)
        if (config.max_violations_before_kick > 0 &&
            violation_count >= config.max_violations_before_kick &&
            violation_count < config.max_violations_before_ban) {

            bot.guild_member_delete(event.msg.guild_id, event.msg.author.id,
                [&bot, event, config, violation_count](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error() && config.log_channel != 0) {
                        dpp::embed kick_embed = bronx::error("User Kicked");
                        kick_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
                        kick_embed.add_field("Reason", "Excessive spam violations", true);
                        kick_embed.add_field("Violations", ::std::to_string(violation_count), true);

                        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, kick_embed);
                    }
                }
            );
        }

        // Ban (global)
        if (config.max_violations_before_ban > 0 && violation_count >= config.max_violations_before_ban) {
            bot.guild_ban_add(event.msg.guild_id, event.msg.author.id, 1,
                [&bot, event, config, violation_count](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error() && config.log_channel != 0) {
                        dpp::embed ban_embed = bronx::error("User Banned");
                        ban_embed.add_field("User", "<@" + ::std::to_string(event.msg.author.id) + ">", true);
                        ban_embed.add_field("Reason", "Excessive spam violations", true);
                        ban_embed.add_field("Violations", ::std::to_string(violation_count), true);

                        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel, ban_embed);
                    }
                }
            );
        }
    });
}

} // namespace quiet_moderation
} // namespace commands
