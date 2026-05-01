#pragma once
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_jail_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "jail", "strip roles and assign jail role to a user", "moderation",
        {"j"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_moderate_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to jail members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `jail @user [duration] [reason]`"));
                return;
            }

            uint64_t target_id = parse_mention(args[0]);
            if (target_id == 0) { bronx::send_message(bot, event, bronx::error("invalid user mention")); return; }
            if (target_id == mod_id) { bronx::send_message(bot, event, bronx::error("you can't jail yourself")); return; }
            if (target_id == bot.me.id) { bronx::send_message(bot, event, bronx::error("you can't jail the bot")); return; }

            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't jail that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't jail that user — their role is higher than yours"));
                    return;
                }
            }

            // Unwrap config
            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value() || config_opt.value().jail_role_id == 0) {
                bronx::send_message(bot, event, bronx::error("jail role is not configured — use the setup command to set one"));
                return;
            }
            if (config_opt.value().jail_channel_id == 0) {
                bronx::send_message(bot, event, bronx::error("jail channel is not configured — use the setup command to set one"));
                return;
            }
            auto config = config_opt.value();

            uint32_t duration = 0;
            std::string reason;
            size_t reason_start = 1;

            if (args.size() > 1) {
                duration = parse_duration(args[1]);
                reason_start = duration > 0 ? 2 : 1;
            }
            if (duration == 0) duration = config.default_duration_mute;

            for (size_t i = reason_start; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            // If reason is empty, we trigger modal if it was slash, but for text we'll set default
            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_jail_modal_" + std::to_string(target_id) + "_" + std::to_string(duration), "jail member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the jail").set_min_length(1).set_max_length(512));
                reason = "no reason provided";
            }

            std::vector<dpp::snowflake> current_roles;
            if (guild) {
                auto it = guild->members.find(target_id);
                if (it != guild->members.end()) current_roles = it->second.get_roles();
            }

            apply_jail_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config, current_roles,
                [&bot, &event, target_id, duration, reason](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) return;
                    
                    std::string desc = bronx::EMOJI_CHECK + " **jailed** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    int roles_stored = 0;
                    try {
                        auto meta = nlohmann::json::parse(inf.metadata);
                        if (meta.contains("stored_roles") && meta["stored_roles"].is_array())
                            roles_stored = static_cast<int>(meta["stored_roles"].size());
                    } catch (...) {}
                    desc += "\n**roles stored:** " + std::to_string(roles_stored) + " roles saved for restoration";

                    auto embed = bronx::create_embed(desc, get_action_color("jail"));
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

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_moderate_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to jail members")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto user_param = event.get_parameter("user");
            if (!std::holds_alternative<dpp::snowflake>(user_param)) {
                event.reply(dpp::message().add_embed(bronx::error("please mention a user")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t target_id = std::get<dpp::snowflake>(user_param);

            if (target_id == mod_id) { event.reply(dpp::message().add_embed(bronx::error("you can't jail yourself")).set_flags(dpp::m_ephemeral)); return; }
            if (target_id == bot.me.id) { event.reply(dpp::message().add_embed(bronx::error("you can't jail the bot")).set_flags(dpp::m_ephemeral)); return; }

            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't jail that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't jail that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config_opt.has_value() || config_opt.value().jail_role_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("jail role is not configured — use the setup command to set one")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (config_opt.value().jail_channel_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("jail channel is not configured — use the setup command to set one")).set_flags(dpp::m_ephemeral));
                return;
            }
            auto config = config_opt.value();

            uint32_t duration = 0;
            auto dur_param = event.get_parameter("duration");
            if (std::holds_alternative<std::string>(dur_param)) {
                duration = parse_duration(std::get<std::string>(dur_param));
            }
            if (duration == 0) duration = config.default_duration_mute;

            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
            }

            // if no reason, trigger modal
            if (reason.empty()) {
                dpp::interaction_modal_response modal("mod_jail_modal_" + std::to_string(target_id) + "_" + std::to_string(duration), "jail member");
                modal.add_component(dpp::component().set_label("reason").set_id("reason").set_type(dpp::cot_text).set_placeholder("reason for the jail").set_min_length(1).set_max_length(512));
                event.dialog(modal);
                return;
            }

            std::vector<dpp::snowflake> current_roles;
            if (guild) {
                auto it = guild->members.find(target_id);
                if (it != guild->members.end()) current_roles = it->second.get_roles();
            }

            apply_jail_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config, current_roles,
                [&bot, event, target_id, duration](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("user jailed (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    std::string desc = bronx::EMOJI_CHECK + " **jailed** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    int roles_stored = 0;
                    try {
                        auto meta = nlohmann::json::parse(inf.metadata);
                        if (meta.contains("stored_roles") && meta["stored_roles"].is_array())
                            roles_stored = static_cast<int>(meta["stored_roles"].size());
                    } catch (...) {}
                    desc += "\n**roles stored:** " + std::to_string(roles_stored) + " roles saved for restoration";

                    auto embed = bronx::create_embed(desc, get_action_color("jail"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        },
        {
            dpp::command_option(dpp::co_user, "user", "the user to jail", true),
            dpp::command_option(dpp::co_string, "duration", "jail duration (e.g. 10m, 1h, 3d)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for jailing", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands