#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_slowmode_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "slowmode", "set the slowmode for the current channel", "moderation",
        {"sm"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_manage_channels)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to manage channels"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `slowmode <duration|off>`"));
                return;
            }

            std::string dur_str = args[0];
            uint32_t duration = 0;
            if (dur_str != "off" && dur_str != "0") {
                duration = parse_duration(dur_str);
                if (duration == 0) {
                    bronx::send_message(bot, event, bronx::error("invalid duration format (e.g., 5s, 1m, 1h)"));
                    return;
                }
                if (duration > 21600) {
                    bronx::send_message(bot, event, bronx::error("slowmode cannot exceed 6 hours"));
                    return;
                }
            }

            auto* channel = dpp::find_channel(event.msg.channel_id);
            if (!channel) {
                bronx::send_message(bot, event, bronx::error("failed to fetch channel"));
                return;
            }

            dpp::channel updated = *channel;
            updated.rate_limit_per_user = duration;
            bot.channel_edit(updated, [event, duration, &bot](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    bronx::send_message(bot, event, bronx::error("failed to update channel slowmode"));
                } else {
                    std::string desc = duration == 0 ? "slowmode has been disabled" : "slowmode set to **" + format_duration(duration) + "**";
                    auto embed = bronx::create_embed(desc, get_action_color("mute"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                }
            });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_manage_channels)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to manage channels")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto dur_opt = event.get_parameter("duration");
            if (dur_opt.index() == 0) {
                dpp::interaction_modal_response modal("mod_slowmode_modal_" + std::to_string(event.command.channel_id), "Set Slowmode");
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("duration")
                        .set_label("Duration (e.g. 5s, 1m, off)")
                        .set_text_style(dpp::text_short)
                        .set_min_length(1)
                        .set_max_length(10)
                        .set_required(true)
                    )
                );
                event.dialog(modal);
                return;
            }

            std::string dur_str = std::get<std::string>(dur_opt);
            uint32_t duration = 0;
            if (dur_str != "off" && dur_str != "0") {
                duration = parse_duration(dur_str);
                if (duration == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid duration format (e.g., 5s, 1m, 1h)")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (duration > 21600) {
                    event.reply(dpp::message().add_embed(bronx::error("slowmode cannot exceed 6 hours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            event.thinking(false); // don't need ephemeral yet because result is public
            
            bot.channel_get(event.command.channel_id, [&bot, event, duration](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("failed to fetch channel")));
                    return;
                }
                dpp::channel ch = std::get<dpp::channel>(callback.value);
                ch.rate_limit_per_user = duration;
                
                bot.channel_edit(ch, [event, duration](const dpp::confirmation_callback_t& ccb) {
                    if (ccb.is_error()) {
                        event.edit_original_response(dpp::message().add_embed(bronx::error("failed to update channel slowmode")));
                    } else {
                        std::string desc = duration == 0 ? "slowmode has been disabled" : "slowmode set to **" + format_duration(duration) + "**";
                        auto embed = bronx::create_embed(desc, get_action_color("mute"));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.edit_original_response(dpp::message().add_embed(embed));
                    }
                });
            });
        },
        {
            dpp::command_option(dpp::co_string, "duration", "slowmode duration (5s, 1m, 1h, off)", false)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
