#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_jailsetup_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("jailsetup", "set the jail role and channel", "moderation", {"jail-setup"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can configure jail settings"));
                return;
            }

            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: jailsetup <@role> <#channel>"));
                return;
            }

            std::string role_mention = args[0];
            uint64_t role_id = 0;
            if (role_mention.size() > 3 && role_mention[0] == '<' && role_mention[1] == '@' && role_mention[2] == '&') {
                std::string id_str = role_mention.substr(3);
                id_str = id_str.substr(0, id_str.find('>'));
                try { role_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { role_id = std::stoull(role_mention); } catch (...) {}
            }
            if (role_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid role — mention a role or provide a role id"));
                return;
            }

            std::string chan_mention = args[1];
            uint64_t channel_id = 0;
            if (chan_mention.size() > 3 && chan_mention[0] == '<' && chan_mention[1] == '#') {
                std::string id_str = chan_mention.substr(2);
                id_str = id_str.substr(0, id_str.find('>'));
                try { channel_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { channel_id = std::stoull(chan_mention); } catch (...) {}
            }
            if (channel_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid channel — mention a channel or provide a channel id"));
                return;
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.jail_role_id = role_id;
            config.jail_channel_id = channel_id;
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            bronx::send_message(bot, event, bronx::success(
                "jail configured\n**role:** <@&" + std::to_string(role_id) + ">\n**channel:** <#" + std::to_string(channel_id) + ">"
            ));
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can configure jail settings")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::snowflake role_id    = std::get<dpp::snowflake>(event.get_parameter("role"));
            dpp::snowflake channel_id = std::get<dpp::snowflake>(event.get_parameter("channel"));

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.jail_role_id = static_cast<uint64_t>(role_id);
            config.jail_channel_id = static_cast<uint64_t>(channel_id);
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            event.reply(dpp::message().add_embed(bronx::success(
                "jail configured\n**role:** <@&" + std::to_string(static_cast<uint64_t>(role_id))
                + ">\n**channel:** <#" + std::to_string(static_cast<uint64_t>(channel_id)) + ">"
            )));
        },
        {
            dpp::command_option(dpp::co_role, "role", "the role to use as the jail role", true),
            dpp::command_option(dpp::co_channel, "channel", "the channel to use as the jail channel", true)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands