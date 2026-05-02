#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "timeout.h"
#include "mute.h"
#include "jail.h"
#include "kick.h"
#include "ban.h"
#include "warn.h"
#include "untimeout.h"
#include "unmute.h"
#include "unjail.h"
#include "unban.h"
#include "case_cmd.h"
#include "history_cmd.h"
#include "modstats.h"
#include "pardon.h"
#include "infractions_config.h"
#include "muterole.h"
#include "jailsetup.h"
#include "modlog_channel.h"
#include "quiet_config.h"
#include "purge.h"
#include "slowmode.h"
#include "note.h"
#include "reason.h"
#include "massban.h"
#include "masskick.h"
#include "massmute.h"
#include "masstimeout.h"
#include "lockdown.h"
#include "softban.h"
#include "raid_protection.h"
#include "duration_edit.h"
#include "modmail.h"
#include "../quiet_moderation/antispam_api.h"
#include "../quiet_moderation/url_guard.h"
#include "../quiet_moderation/text_filter_config.h"
#include "../quiet_moderation/reaction_filter.h"
#include "../quiet_moderation/automod_commands.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <unordered_map>

using namespace bronx::db;

namespace commands {
namespace moderation {

// Maps subcommand names to their moderation handlers
struct ModCommandInfo {
    std::string name;
    std::string description;
    std::function<Command*(Database*)> getter;
};

inline std::vector<ModCommandInfo> get_moderation_actions(Database* db) {
    return {
        // Punishment commands
        {"timeout", "timeout a user", get_timeout_command},
        {"mute", "mute a user", get_mute_command},
        {"jail", "jail a user to restricted channel", get_jail_command},
        {"kick", "kick a user from server", get_kick_command},
        {"ban", "ban a user from server", get_ban_command},
        {"warn", "warn a user", get_warn_command},
        {"purge", "delete multiple messages", get_purge_command},
        {"slowmode", "set channel slowmode", get_slowmode_command},
        {"note", "add a moderation note to a user", get_note_command},
        {"reason", "update the reason for a case", get_reason_command},
        {"duration", "update the duration for a case", get_duration_command},
        {"massban", "ban multiple users at once", get_massban_command},
        {"masskick", "kick multiple users at once", get_masskick_command},
        {"massmute", "mute multiple users at once", get_massmute_command},
        {"masstimeout", "timeout multiple users at once", get_masstimeout_command},
        {"lockdown", "lock a channel from messages", get_lockdown_command},
        {"unlock", "unlock a channel", get_unlock_command},
        {"softban", "ban and unban to clear messages", get_softban_command},
        
        // Unpunishment commands
        {"untimeout", "remove timeout from user", get_untimeout_command},
        {"unmute", "unmute a user", get_unmute_command},
        {"unjail", "unjail a user", get_unjail_command},
        {"unban", "unban a user", get_unban_command},
        
        // Info/management commands
        {"case", "view case details", get_case_command},
        {"history", "view user infraction history", get_history_command},
        {"modstats", "view moderation stats", get_modstats_command},
        {"pardon", "remove infraction from history", get_pardon_command},
        
        // Config commands
        {"infractions", "configure infraction settings", get_infractions_config_command},
        {"muterole", "set mute role", get_muterole_command},
        {"jailsetup", "configure jail channel", get_jailsetup_command},
        {"modlog", "set moderation log channel", get_modlog_channel_command},
        {"quiet", "configure quiet moderation", get_quiet_config_command},
        {"raid", "configure raid protection", create_raid_protection_command},
        {"modmail", "manage modmail threads", get_modmail_command},
    };
}

// Cache individual moderation commands
static std::unordered_map<std::string, Command*> g_mod_commands;

inline Command* create_moderation_parent_command(Database* db) {
    // Initialize moderation command cache on first use
    static bool initialized = false;
    if (!initialized) {
        // Add manual moderation commands
        g_mod_commands["timeout"] = get_timeout_command(db);
        g_mod_commands["mute"] = get_mute_command(db);
        g_mod_commands["jail"] = get_jail_command(db);
        g_mod_commands["kick"] = get_kick_command(db);
        g_mod_commands["ban"] = get_ban_command(db);
        g_mod_commands["warn"] = get_warn_command(db);
        g_mod_commands["untimeout"] = get_untimeout_command(db);
        g_mod_commands["unmute"] = get_unmute_command(db);
        g_mod_commands["unjail"] = get_unjail_command(db);
        g_mod_commands["unban"] = get_unban_command(db);
        g_mod_commands["case"] = get_case_command(db);
        g_mod_commands["history"] = get_history_command(db);
        g_mod_commands["modstats"] = get_modstats_command(db);
        g_mod_commands["pardon"] = get_pardon_command(db);
        g_mod_commands["infractions"] = get_infractions_config_command(db);
        g_mod_commands["muterole"] = get_muterole_command(db);
        g_mod_commands["jailsetup"] = get_jailsetup_command(db);
        g_mod_commands["modlog"] = get_modlog_channel_command(db);
        g_mod_commands["quiet"] = get_quiet_config_command(db);
        g_mod_commands["purge"] = get_purge_command(db);
        g_mod_commands["slowmode"] = get_slowmode_command(db);
        g_mod_commands["note"] = get_note_command(db);
        g_mod_commands["reason"] = get_reason_command(db);
        g_mod_commands["duration"] = get_duration_command(db);
        g_mod_commands["massban"] = get_massban_command(db);
        g_mod_commands["masskick"] = get_masskick_command(db);
        g_mod_commands["massmute"] = get_massmute_command(db);
        g_mod_commands["masstimeout"] = get_masstimeout_command(db);
        g_mod_commands["lockdown"] = get_lockdown_command(db);
        g_mod_commands["unlock"] = get_unlock_command(db);
        g_mod_commands["softban"] = get_softban_command(db);
        g_mod_commands["raid"] = create_raid_protection_command(db);
        g_mod_commands["modmail"] = get_modmail_command(db);
        
        // Add quiet moderation commands
        g_mod_commands["antispam"] = quiet_moderation::get_antispam_command();
        g_mod_commands["urlguard"] = quiet_moderation::get_url_guard_command();
        g_mod_commands["filter"] = quiet_moderation::get_text_filter_command();
        g_mod_commands["reactions"] = quiet_moderation::get_reaction_filter_command();
        g_mod_commands["automod"] = quiet_moderation::get_automod_command(db);
        
        initialized = true;
    }

    auto mod = new Command(
        "mod",
        "moderation tools and configuration",
        "moderation",
        {},
        true,  // is_slash_command

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto actions = get_moderation_actions(db);
                std::string action_list = "**Punishments:**\n";
                for (size_t i = 0; i < 6; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Reversals:**\n";
                for (size_t i = 6; i < 10; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Information:**\n";
                for (size_t i = 10; i < 14; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Configuration:**\n";
                for (size_t i = 14; i < actions.size(); i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("🛡️ Moderation Tools")
                    .set_color(0xFF0000);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string action_name = args[0];
            std::transform(action_name.begin(), action_name.end(), action_name.begin(), ::tolower);

            // Find the command
            auto it = g_mod_commands.find(action_name);
            if (it == g_mod_commands.end()) {
                auto actions = get_moderation_actions(db);
                std::string suggestions = "Valid actions: ";
                for (size_t i = 0; i < actions.size(); i++) {
                    suggestions += actions[i].name;
                    if (i < actions.size() - 1) suggestions += ", ";
                }
                bronx::send_message(bot, event,
                    bronx::error("unknown action '" + action_name + "'\n" + suggestions));
                return;
            }

            // Forward all args except the action name to the command handler
            std::vector<std::string> cmd_args(args.begin() + 1, args.end());
            
            // Call the text handler of the individual command
            auto cmd = it->second;
            if (cmd && cmd->text_handler) {
                cmd->text_handler(bot, event, cmd_args);
            }
        },

        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Get the selected action subcommand
            std::string action_name;
            
            // Parse subcommand from slash command structure
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) {
                action_name = ci_options[0].name;
            }

            if (action_name.empty()) {
                auto actions = get_moderation_actions(db);
                std::string action_list = "**Punishments:**\n";
                for (size_t i = 0; i < 6; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Reversals:**\n";
                for (size_t i = 6; i < 10; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Information:**\n";
                for (size_t i = 10; i < 14; i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                action_list += "\n**Configuration:**\n";
                for (size_t i = 14; i < actions.size(); i++) {
                    action_list += "• **" + actions[i].name + "**: " + actions[i].description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("🛡️ Moderation Tools")
                    .set_color(0xFF0000);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // Find the command
            auto it = g_mod_commands.find(action_name);
            if (it == g_mod_commands.end()) {
                event.reply(dpp::message().add_embed(
                    bronx::error("Unknown action: " + action_name)));
                return;
            }

            // Call the slash handler of the individual command
            auto cmd = it->second;
            if (cmd && cmd->slash_handler) {
                cmd->slash_handler(bot, event);
            }
        }
    );

    // Add subcommands for each moderation action
    auto actions = get_moderation_actions(db);
    for (const auto& action : actions) {
        dpp::command_option action_option(
            dpp::co_sub_command,
            action.name,
            action.description
        );
        
        // Copy options from the actual command if it exists in cache
        if (g_mod_commands.count(action.name)) {
            auto* sub_cmd = g_mod_commands[action.name];
            if (sub_cmd) {
                for (auto& opt : sub_cmd->options) {
                    action_option.add_option(opt);
                }
            }
        }
        
        mod->options.push_back(action_option);
    }

    return mod;
}

} // namespace moderation
} // namespace commands
