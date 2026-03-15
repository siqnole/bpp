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

inline Command* get_modlog_channel_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("modlog-channel", "set the moderation log channel", "moderation", {"modlogchannel", "modlog"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can set the mod log channel"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: modlog-channel <#channel>"));
                return;
            }

            std::string mention = args[0];
            uint64_t channel_id = 0;
            if (mention.size() > 3 && mention[0] == '<' && mention[1] == '#') {
                std::string id_str = mention.substr(2);
                id_str = id_str.substr(0, id_str.find('>'));
                try { channel_id = std::stoull(id_str); } catch (...) {}
            } else {
                try { channel_id = std::stoull(mention); } catch (...) {}
            }
            if (channel_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid channel — mention a channel or provide a channel id"));
                return;
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.log_channel_id = channel_id;
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            bronx::send_message(bot, event, bronx::success("mod log channel set to <#" + std::to_string(channel_id) + ">"));
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can set the mod log channel")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::snowflake channel_id = std::get<dpp::snowflake>(event.get_parameter("channel"));

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;
            config.log_channel_id = static_cast<uint64_t>(channel_id);
            bronx::db::infraction_config_operations::upsert_infraction_config(db, config);

            event.reply(dpp::message().add_embed(bronx::success("mod log channel set to <#" + std::to_string(static_cast<uint64_t>(channel_id)) + ">")));
        },
        {
            dpp::command_option(dpp::co_channel, "channel", "the channel for moderation logs", true)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands