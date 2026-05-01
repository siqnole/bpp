#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include <dpp/nlohmann/json.hpp>

namespace commands {
namespace moderation {

inline Command* get_quiet_config_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("quiet", "toggle quiet moderation settings", "moderation", {"qmod"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id   = event.msg.author.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can configure quiet moderation"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: quiet <on|off> [action]"));
                return;
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;

            std::string state = args[0];
            bool on = (state == "on" || state == "true" || state == "yes");

            if (args.size() == 1) {
                config.quiet_global = on;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                bronx::send_message(bot, event, bronx::success("global quiet moderation set to **" + std::string(on ? "on" : "off") + "**"));
            } else {
                std::string action = args[1];
                nlohmann::json overrides = nlohmann::json::parse(config.quiet_overrides.empty() ? "{}" : config.quiet_overrides);
                overrides[action] = on;
                config.quiet_overrides = overrides.dump();
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                bronx::send_message(bot, event, bronx::success("quiet mode for **" + action + "** set to **" + std::string(on ? "on" : "off") + "**"));
            }
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id   = event.command.usr.id;

            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can configure quiet moderation")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
            config.guild_id = guild_id;

            std::string state;
            auto state_param = event.get_parameter("state");
            if (std::holds_alternative<std::string>(state_param)) state = std::get<std::string>(state_param);
            
            bool on = (state == "on");

            std::string action;
            auto action_param = event.get_parameter("action");
            if (std::holds_alternative<std::string>(action_param)) action = std::get<std::string>(action_param);

            if (action.empty()) {
                config.quiet_global = on;
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                event.reply(dpp::message().add_embed(bronx::success("global quiet moderation set to **" + state + "**")));
            } else {
                nlohmann::json overrides = nlohmann::json::parse(config.quiet_overrides.empty() ? "{}" : config.quiet_overrides);
                overrides[action] = on;
                config.quiet_overrides = overrides.dump();
                bronx::db::infraction_config_operations::upsert_infraction_config(db, config);
                event.reply(dpp::message().add_embed(bronx::success("quiet mode for **" + action + "** set to **" + state + "**")));
            }
        },
        {
            dpp::command_option(dpp::co_string, "state", "turn quiet mode on or off", true)
                .add_choice(dpp::command_option_choice("on", std::string("on")))
                .add_choice(dpp::command_option_choice("off", std::string("off"))),
            dpp::command_option(dpp::co_string, "action", "per-action override (optional)", false)
                .add_choice(dpp::command_option_choice("warn", std::string("warn")))
                .add_choice(dpp::command_option_choice("timeout", std::string("timeout")))
                .add_choice(dpp::command_option_choice("mute", std::string("mute")))
                .add_choice(dpp::command_option_choice("kick", std::string("kick")))
                .add_choice(dpp::command_option_choice("ban", std::string("ban")))
                .add_choice(dpp::command_option_choice("slowmode", std::string("slowmode")))
                .add_choice(dpp::command_option_choice("purge", std::string("purge")))
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
