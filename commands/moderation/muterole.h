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

inline Command* get_muterole_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("muterole", "set the mute role for this server", "moderation", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can set the mute role"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: muterole <@role>"));
                return;
            }

            std::string mention = args[0];
            uint64_t role_id = 0;
            if (mention.size() > 3 && mention[0] == '<' && mention[1] == '@' && mention[2] == '&') {
                std::string id_str = mention.substr(3);
                id_str = id_str.substr(0, id_str.find('>'));
                try { role_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { role_id = std::stoull(mention); } catch (...) {}
            }
            if (role_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid role — mention a role or provide a role id"));
                return;
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.mute_role_id = role_id;
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            bronx::send_message(bot, event, bronx::success("mute role set to <@&" + std::to_string(role_id) + ">"));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can set the mute role")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::snowflake role_id = std::get<dpp::snowflake>(event.get_parameter("role"));

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.mute_role_id = static_cast<uint64_t>(role_id);
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            event.reply(dpp::message().add_embed(bronx::success("mute role set to <@&" + std::to_string(static_cast<uint64_t>(role_id)) + ">")));
        },
        {
            dpp::command_option(dpp::co_role, "role", "the role to use as the mute role", true)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands