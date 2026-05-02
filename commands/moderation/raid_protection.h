#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/raid_operations.h"
#include "infraction_engine.h"
#include <dpp/dpp.h>
#include <string>
#include <vector>

namespace commands {
namespace moderation {

using namespace ::bronx::db;

inline std::string join_gate_to_string(JoinGateLevel level) {
    switch (level) {
        case JoinGateLevel::OFF:    return "OFF";
        case JoinGateLevel::LOW:    return "LOW (Age Check)";
        case JoinGateLevel::MEDIUM: return "MEDIUM (Velocity)";
        case JoinGateLevel::HIGH:   return "HIGH (Lockdown - Kick All)";
        case JoinGateLevel::MAX:    return "MAX (Banned Joins)";
        default:                    return "UNKNOWN";
    }
}

inline Command* create_raid_protection_command(Database* db) {
    static Command* cmd = new Command("raid", "configure raid protection and join gating", "moderation", {"raidmode", "lockdown_joins"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Text command implementation (minimal)
            uint64_t guild_id = event.msg.guild_id;
            auto settings = raid_operations::get_settings(db, guild_id);
            
            if (args.empty() || args[0] == "status") {
                std::string desc = "**Raid Protection Status**\n"
                                   "Mode: `" + join_gate_to_string(settings.join_gate_level) + "`\n"
                                   "Min Age: `" + std::to_string(settings.min_account_age_days) + " days`\n"
                                   "Velocity: `" + std::to_string(settings.join_velocity_threshold) + " joins/min`\n"
                                   "Alerts: `" + (settings.alert_on_raid ? "ENABLED" : "DISABLED") + "`";
                ::bronx::send_message(bot, event, ::bronx::create_embed(desc));
                return;
            }
            
            ::bronx::send_message(bot, event, ::bronx::error("please use the slash command `/raid` for full configuration."));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            auto settings = raid_operations::get_settings(db, guild_id);
            
            auto subcommand = event.command.get_command_interaction().options[0];

            if (subcommand.name == "status") {
                std::string desc = "🛡️ **Raid Protection Dashboard**\n\n"
                                   "> **Join Gate:** `" + join_gate_to_string(settings.join_gate_level) + "`\n"
                                   "> **Min Account Age:** `" + std::to_string(settings.min_account_age_days) + " days`\n"
                                   "> **Join Threshold:** `" + std::to_string(settings.join_velocity_threshold) + " joins/min`\n"
                                   "> **Notify Channel:** " + (settings.notify_channel_id ? "<#" + std::to_string(*settings.notify_channel_id) + ">" : "`None`") + "\n"
                                   "> **Alerts:** `" + (settings.alert_on_raid ? "ON" : "OFF") + "`";
                
                event.reply(dpp::message().add_embed(::bronx::create_embed(desc)));
                return;
            }

            if (subcommand.name == "mode") {
                int level = (int)std::get<int64_t>(event.get_parameter("level"));
                settings.join_gate_level = static_cast<JoinGateLevel>(level);
                raid_operations::update_settings(db, settings);
                
                event.reply(dpp::message().add_embed(::bronx::create_embed("Raid mode set to **" + join_gate_to_string(settings.join_gate_level) + "**.", ::bronx::COLOR_SUCCESS)));
                return;
            }

            if (subcommand.name == "thresholds") {
                auto age_it = event.get_parameter("age");
                auto vel_it = event.get_parameter("velocity");
                
                if (age_it.index() != 0) settings.min_account_age_days = (int)std::get<int64_t>(age_it);
                if (vel_it.index() != 0) settings.join_velocity_threshold = (int)std::get<int64_t>(vel_it);
                
                raid_operations::update_settings(db, settings);
                event.reply(dpp::message().add_embed(::bronx::create_embed("Raid thresholds updated.", ::bronx::COLOR_SUCCESS)));
                return;
            }

            if (subcommand.name == "notify") {
                auto chan_it = event.get_parameter("channel");
                if (chan_it.index() != 0) {
                    settings.notify_channel_id = (uint64_t)std::get<dpp::snowflake>(chan_it);
                } else {
                    settings.notify_channel_id = std::nullopt;
                }
                raid_operations::update_settings(db, settings);
                event.reply(dpp::message().add_embed(::bronx::create_embed("Notification channel updated.", ::bronx::COLOR_SUCCESS)));
                return;
            }
        },
        {
            dpp::command_option(dpp::co_sub_command, "status", "view current raid protection settings"),
            dpp::command_option(dpp::co_sub_command, "mode", "set the join-gate level")
                .add_option(dpp::command_option(dpp::co_integer, "level", "protection level", true)
                    .add_choice(dpp::command_option_choice("OFF", (int64_t)0))
                    .add_choice(dpp::command_option_choice("LOW (Age Check)", (int64_t)1))
                    .add_choice(dpp::command_option_choice("MEDIUM (Velocity)", (int64_t)2))
                    .add_choice(dpp::command_option_choice("HIGH (Lockdown)", (int64_t)3))
                    .add_choice(dpp::command_option_choice("MAX (Ban All)", (int64_t)4))),
            dpp::command_option(dpp::co_sub_command, "thresholds", "configure velocity and age thresholds")
                .add_option(dpp::command_option(dpp::co_integer, "age", "min account age in days", false))
                .add_option(dpp::command_option(dpp::co_integer, "velocity", "joins per minute threshold", false)),
            dpp::command_option(dpp::co_sub_command, "notify", "configure alert channel")
                .add_option(dpp::command_option(dpp::co_channel, "channel", "alert channel", false))
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
