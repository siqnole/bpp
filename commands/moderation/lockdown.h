#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_lockdown_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "lockdown", "lock a channel to prevent users from sending messages", "moderation",
        {"lock"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;
            uint64_t channel_id = event.msg.channel_id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_manage_channels)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to manage channels"));
                    return;
                }
            }

            std::string reason;
            for (const auto& arg : args) {
                if (!reason.empty()) reason += " ";
                reason += arg;
            }
            if (reason.empty()) reason = "no reason provided";

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_lockdown_internal(bot, db, guild_id, channel_id, mod_id, true, reason, config,
                [&bot, &event, channel_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) return;
                    auto embed = bronx::create_embed(bronx::EMOJI_CHECK + " channel <#" + std::to_string(channel_id) + "> has been **locked**", get_action_color("lockdown"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                },
                [&bot, &event](const std::string& err) {
                    bronx::send_message(bot, event, bronx::error(err));
                });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;
            uint64_t channel_id = event.command.channel_id;

            auto ch_param = event.get_parameter("channel");
            if (std::holds_alternative<dpp::snowflake>(ch_param)) {
                channel_id = std::get<dpp::snowflake>(ch_param);
            }

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_manage_channels)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to manage channels")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }
            if (reason.empty()) reason = "no reason provided";

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_lockdown_internal(bot, db, guild_id, channel_id, mod_id, true, reason, config,
                [event, channel_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("channel locked (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    auto embed = bronx::create_embed(bronx::EMOJI_CHECK + " channel <#" + std::to_string(channel_id) + "> has been **locked**", get_action_color("lockdown"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        },
        {
            dpp::command_option(dpp::co_channel, "channel", "the channel to lock (optional, defaults to current)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for locking the channel", false)
        }
    );

    return cmd;
}

inline Command* get_unlock_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "unlock", "unlock a channel to allow users to send messages again", "moderation",
        {}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;
            uint64_t channel_id = event.msg.channel_id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_manage_channels)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to manage channels"));
                    return;
                }
            }

            std::string reason;
            for (const auto& arg : args) {
                if (!reason.empty()) reason += " ";
                reason += arg;
            }
            if (reason.empty()) reason = "no reason provided";

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_lockdown_internal(bot, db, guild_id, channel_id, mod_id, false, reason, config,
                [&bot, &event, channel_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) return;
                    auto embed = bronx::create_embed(bronx::EMOJI_CHECK + " channel <#" + std::to_string(channel_id) + "> has been **unlocked**", get_action_color("lockdown"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                },
                [&bot, &event](const std::string& err) {
                    bronx::send_message(bot, event, bronx::error(err));
                });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;
            uint64_t channel_id = event.command.channel_id;

            auto ch_param = event.get_parameter("channel");
            if (std::holds_alternative<dpp::snowflake>(ch_param)) {
                channel_id = std::get<dpp::snowflake>(ch_param);
            }

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_manage_channels)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to manage channels")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }
            if (reason.empty()) reason = "no reason provided";

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_lockdown_internal(bot, db, guild_id, channel_id, mod_id, false, reason, config,
                [event, channel_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("channel unlocked (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    auto embed = bronx::create_embed(bronx::EMOJI_CHECK + " channel <#" + std::to_string(channel_id) + "> has been **unlocked**", get_action_color("lockdown"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        },
        {
            dpp::command_option(dpp::co_channel, "channel", "the channel to unlock (optional, defaults to current)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for unlocking the channel", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
