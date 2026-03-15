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

inline uint64_t jail_parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

// apply_jail takes a plain InfractionConfig (already unwrapped by callers)
inline void apply_jail(dpp::cluster& bot, bronx::db::Database* db,
                       uint64_t guild_id, uint64_t target_id, uint64_t mod_id,
                       uint64_t jail_role_id, uint32_t duration,
                       const std::string& reason, const bronx::db::InfractionConfig& config,
                       const std::string& guild_name,
                       const std::vector<dpp::snowflake>& current_roles,
                       std::function<void(bool, const std::string&, const std::optional<bronx::db::InfractionRow>&)> callback) {

    nlohmann::json meta;
    meta["stored_roles"] = nlohmann::json::array();
    for (auto& r : current_roles) {
        meta["stored_roles"].push_back(static_cast<uint64_t>(r));
    }
    meta["jail_role_id"] = jail_role_id;
    std::string metadata_str = meta.dump();

    for (auto& role_id : current_roles) {
        if (role_id == guild_id) continue;
        bot.guild_member_remove_role(guild_id, target_id, role_id,
            [](const dpp::confirmation_callback_t&) {});
    }

    bot.guild_member_add_role(guild_id, target_id, jail_role_id,
        [db, guild_id, target_id, mod_id, duration, reason, config, guild_name, metadata_str, callback, &bot](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                callback(false, "failed to add jail role: " + cb.get_error().message, std::nullopt);
                return;
            }

            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "jail", reason, config.point_mute, // plain member access — config is already unwrapped
                duration, metadata_str);

            if (!inf.has_value()) {
                callback(false, "jail applied but failed to create infraction record", std::nullopt);
                return;
            }

            schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "jail", target_id, duration, metadata_str);
            check_and_escalate(bot, db, guild_id, target_id, guild_name);
            send_mod_log(bot, db, guild_id, inf.value());
            callback(true, "", inf);
        });
}

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

            uint64_t target_id = jail_parse_mention(args[0]);
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

            // Unwrap config — if not configured, bail with a friendly error
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

            std::string guild_name = guild ? guild->name : "unknown server";

            if (config.dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "jail", reason, duration, config.point_mute);
            }

            std::vector<dpp::snowflake> current_roles;
            if (guild) {
                auto it = guild->members.find(target_id);
                if (it != guild->members.end()) current_roles = it->second.get_roles();
            }

            apply_jail(bot, db, guild_id, target_id, mod_id, config.jail_role_id,
                duration, reason, config, guild_name, current_roles,
                [&bot, &event, target_id, duration, reason](bool success, const std::string& err, const std::optional<bronx::db::InfractionRow>& inf) {
                    if (!success) { bronx::send_message(bot, event, bronx::error(err)); return; }

                    std::string desc = bronx::EMOJI_CHECK + " **jailed** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;
                    desc += "\n**roles stored:** " + std::to_string(inf->metadata.size() > 2 ? 1 : 0) + " roles saved for restoration";

                    auto embed = bronx::create_embed(desc, get_action_color("jail"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
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

            std::string guild_name = guild ? guild->name : "unknown server";

            if (config.dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "jail", reason, duration, config.point_mute);
            }

            std::vector<dpp::snowflake> current_roles;
            if (guild) {
                auto it = guild->members.find(target_id);
                if (it != guild->members.end()) current_roles = it->second.get_roles();
            }

            apply_jail(bot, db, guild_id, target_id, mod_id, config.jail_role_id,
                duration, reason, config, guild_name, current_roles,
                [&bot, event, target_id, duration, reason](bool success, const std::string& err, const std::optional<bronx::db::InfractionRow>& inf) {
                    if (!success) { event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral)); return; }

                    std::string desc = bronx::EMOJI_CHECK + " **jailed** <@" + std::to_string(target_id) + ">"
                        + "\n**duration:** " + format_duration(duration)
                        + "\n**case:** #" + std::to_string(inf->case_number);
                    if (!reason.empty()) desc += "\n**reason:** " + reason;

                    int roles_stored = 0;
                    try {
                        auto meta = nlohmann::json::parse(inf->metadata);
                        if (meta.contains("stored_roles") && meta["stored_roles"].is_array())
                            roles_stored = static_cast<int>(meta["stored_roles"].size());
                    } catch (...) {}
                    desc += "\n**roles stored:** " + std::to_string(roles_stored) + " roles saved for restoration";

                    auto embed = bronx::create_embed(desc, get_action_color("jail"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
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