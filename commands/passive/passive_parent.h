#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "fish_pond.h"
#include "mining_claims.h"
#include "commodity_market.h"
#include "bank_interest.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <unordered_map>

using namespace bronx::db;

namespace commands {
namespace passive {

// Maps subcommand names to their individual command getters
struct PassiveCommandInfo {
    std::string name;
    std::string description;
    std::function<Command*(Database*)> getter;
};

inline std::vector<PassiveCommandInfo> get_passive_actions(Database* db) {
    return {
        {"pond", "passive fishing pond", get_pond_command},
        {"claim", "claim mining rewards", get_claim_command},
        {"market", "commodity market overview", get_market_overview_command},
        {"interest", "check bank interest earnings", get_interest_command},
    };
}

// Cache individual passive commands
static std::unordered_map<std::string, Command*> g_passive_commands;

inline Command* create_passive_parent_command(Database* db) {
    // Initialize passive command cache on first use
    static bool initialized = false;
    if (!initialized) {
        g_passive_commands["pond"] = get_pond_command(db);
        g_passive_commands["claim"] = get_claim_command(db);
        g_passive_commands["market"] = get_market_overview_command(db);
        g_passive_commands["interest"] = get_interest_command(db);
        initialized = true;
    }

    auto passive = new Command(
        "passive",
        "manage passive income streams",
        "economy",
        {},
        true,  // is_slash_command

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto actions = get_passive_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("💰 Passive Income")
                    .set_color(0x00AA00);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string action_name = args[0];
            std::transform(action_name.begin(), action_name.end(), action_name.begin(), ::tolower);

            // Find the command
            auto it = g_passive_commands.find(action_name);
            if (it == g_passive_commands.end()) {
                auto actions = get_passive_actions(db);
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
                auto actions = get_passive_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("💰 Passive Income")
                    .set_color(0x00AA00);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // Find the command
            auto it = g_passive_commands.find(action_name);
            if (it == g_passive_commands.end()) {
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

    // Add subcommands for each passive action
    auto actions = get_passive_actions(db);
    for (const auto& action : actions) {
        dpp::command_option action_option(
            dpp::co_sub_command,
            action.name,
            action.description
        );
        passive->options.push_back(action_option);
    }

    return passive;
}

} // namespace passive
} // namespace commands
