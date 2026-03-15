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

inline Command* get_unjail_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("unjail", "remove a user from jail and restore their roles", "moderation", {}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to use this command"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: unjail <@user> [reason]"));
                return;
            }

            // parse user mention
            std::string mention = args[0];
            uint64_t user_id = 0;
            if (mention.size() > 3 && mention[0] == '<' && mention[1] == '@') {
                std::string id_str = mention.substr(mention.find_first_of("0123456789"));
                id_str = id_str.substr(0, id_str.find('>'));
                try { user_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { user_id = std::stoull(mention); } catch (...) {}
            }
            if (user_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user"));
                return;
            }

            std::string reason = "no reason provided";
            if (args.size() > 1) {
                reason.clear();
                for (size_t i = 1; i < args.size(); i++) {
                    if (!reason.empty()) reason += " ";
                    reason += args[i];
                }
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config.has_value() || config.value().jail_role_id == 0) {
                bronx::send_message(bot, event, bronx::error("no jail role configured — use `/jailsetup` to set one"));
                return;
            }

            // find the most recent active jail infraction to get stored roles
            auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
            std::vector<uint64_t> stored_roles;
            for (auto& inf : infractions) {
                if (inf.type == "jail" && inf.active && !inf.pardoned) {
                    try {
                        auto meta = nlohmann::json::parse(inf.metadata);
                        if (meta.contains("stored_roles") && meta["stored_roles"].is_array()) {
                            for (auto& r : meta["stored_roles"]) {
                                stored_roles.push_back(r.get<uint64_t>());
                            }
                        }
                    } catch (...) {}
                    break; // use the first (most recent) match
                }
            }

            // remove jail role
            bot.guild_member_remove_role(guild_id, user_id, config.value().jail_role_id,
                [&bot, db, guild_id, user_id, mod_id, reason, stored_roles, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to remove jail role: " + cb.get_error().message));
                        return;
                    }

                    // restore stored roles
                    for (auto role_id : stored_roles) {
                        bot.guild_member_add_role(guild_id, user_id, role_id,
                            [](const dpp::confirmation_callback_t&) {});
                    }

                    // pardon active jail infractions
                    auto infs = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infs) {
                        if (inf.type == "jail" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    bronx::send_message(bot, event, bronx::success(
                        "unjailed <@" + std::to_string(user_id) + ">"
                        + (stored_roles.empty() ? "" : " — restored **" + std::to_string(stored_roles.size()) + "** role(s)")
                        + (pardoned_count > 0 ? " — pardoned **" + std::to_string(pardoned_count) + "** infraction(s)" : "")
                    ));
                });
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_mod(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to use this command")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::snowflake user_id = std::get<dpp::snowflake>(event.get_parameter("user"));
            std::string reason = "no reason provided";
            try { reason = std::get<std::string>(event.get_parameter("reason")); } catch (...) {}

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            if (!config.has_value() || config.value().jail_role_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("no jail role configured — use `/jailsetup` to set one")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto infractions = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
            std::vector<uint64_t> stored_roles;
            for (auto& inf : infractions) {
                if (inf.type == "jail" && inf.active && !inf.pardoned) {
                    try {
                        auto meta = nlohmann::json::parse(inf.metadata);
                        if (meta.contains("stored_roles") && meta["stored_roles"].is_array()) {
                            for (auto& r : meta["stored_roles"]) {
                                stored_roles.push_back(r.get<uint64_t>());
                            }
                        }
                    } catch (...) {}
                    break;
                }
            }

            bot.guild_member_remove_role(guild_id, user_id, config.value().jail_role_id,
                [&bot, db, guild_id, user_id, mod_id, reason, stored_roles, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to remove jail role: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        return;
                    }

                    for (auto role_id : stored_roles) {
                        bot.guild_member_add_role(guild_id, user_id, role_id,
                            [](const dpp::confirmation_callback_t&) {});
                    }

                    auto infs = bronx::db::infraction_operations::get_user_infractions(db, guild_id, user_id, true);
                    int pardoned_count = 0;
                    for (auto& inf : infs) {
                        if (inf.type == "jail" && inf.active && !inf.pardoned) {
                            bronx::db::infraction_operations::pardon_infraction(db, guild_id, inf.case_number, mod_id, reason);
                            pardoned_count++;
                        }
                    }

                    event.reply(dpp::message().add_embed(bronx::success(
                        "unjailed <@" + std::to_string(static_cast<uint64_t>(user_id)) + ">"
                        + (stored_roles.empty() ? "" : " — restored **" + std::to_string(stored_roles.size()) + "** role(s)")
                        + (pardoned_count > 0 ? " — pardoned **" + std::to_string(pardoned_count) + "** infraction(s)" : "")
                    )));
                });
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_user, "user", "the user to unjail", true),
            dpp::command_option(dpp::co_string, "reason", "reason for unjailing", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
