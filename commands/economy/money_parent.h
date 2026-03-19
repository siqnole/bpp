#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <unordered_map>

using namespace bronx::db;

namespace commands {
namespace economy {

// Forward declarations for individual money handlers
// These are implemented in their respective files
inline Command* create_balance_command(Database* db);
inline Command* create_bank_command(Database* db);
inline Command* create_withdraw_command(Database* db);
inline Command* create_pay_command(Database* db);
inline Command* create_prestige_command(Database* db);
inline Command* create_rebirth_command(Database* db);

} // namespace economy
} // namespace commands

// These are in commands namespace, not commands::economy
namespace commands {
// From money.h - gets daily, weekly, work commands
::std::vector<Command*> get_money_commands(Database* db);

// From rob.h - gets rob command(s)
::std::vector<Command*> get_rob_commands(Database* db);
} // namespace commands

namespace commands {
namespace economy {

// Maps subcommand names to their individual command getters
struct MoneyCommandInfo {
    std::string name;
    std::string description;
    std::function<Command*(Database*)> getter;
};

inline std::vector<MoneyCommandInfo> get_money_actions(Database* db) {
    return {
        {"balance", "check your wallet, bank & net worth", create_balance_command},
        {"bank", "deposit/withdraw from your bank", create_bank_command},
        {"withdraw", "withdraw money from your bank", create_withdraw_command},
        {"pay", "send money to another user", create_pay_command},
        {"prestige", "prestige your account for bonuses", create_prestige_command},
        {"rebirth", "ultimate prestige beyond P20", create_rebirth_command},
        {"daily", "claim your daily reward", nullptr},  // Special handling in money.h
        {"weekly", "claim your weekly reward", nullptr},  // Special handling in money.h
        {"work", "work for some easy cash", nullptr},  // Special handling in money.h
        {"rob", "attempt to rob another user", nullptr},  // Special handling in rob.h
    };
}

// Cache individual money commands
static std::unordered_map<std::string, Command*> g_money_commands;

inline Command* create_money_parent_command(Database* db) {
    // Initialize money command cache on first use
    static bool initialized = false;
    if (!initialized) {
        // Add core economy commands
        g_money_commands["balance"] = create_balance_command(db);
        g_money_commands["bank"] = create_bank_command(db);
        g_money_commands["withdraw"] = create_withdraw_command(db);
        g_money_commands["pay"] = create_pay_command(db);
        g_money_commands["prestige"] = create_prestige_command(db);
        g_money_commands["rebirth"] = create_rebirth_command(db);
        
        // Add money commands (daily, weekly, work) - get_money_commands is in ::commands namespace
        auto money_cmds = ::commands::get_money_commands(db);
        for (auto cmd : money_cmds) {
            if (cmd && !cmd->name.empty()) {
                g_money_commands[cmd->name] = cmd;
            }
        }
        
        // Add rob command - get_rob_commands is in ::commands namespace
        auto rob_cmds = ::commands::get_rob_commands(db);
        if (!rob_cmds.empty() && rob_cmds[0]) {
            g_money_commands["rob"] = rob_cmds[0];
        }
        
        initialized = true;
    }

    auto money = new Command(
        "money",
        "manage your economy and finances",
        "economy",
        {},
        true,  // is_slash_command

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto actions = get_money_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("💰 Money Commands")
                    .set_color(0x00AA00);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string action_name = args[0];
            std::transform(action_name.begin(), action_name.end(), action_name.begin(), ::tolower);

            // Check aliases
            if (action_name == "bal") action_name = "balance";
            else if (action_name == "dep") action_name = "bank";
            else if (action_name == "w" || action_name == "with") action_name = "withdraw";

            // Find the command
            auto it = g_money_commands.find(action_name);
            if (it == g_money_commands.end()) {
                auto actions = get_money_actions(db);
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
                auto actions = get_money_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("💰 Money Commands")
                    .set_color(0x00AA00);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // Find the command
            auto it = g_money_commands.find(action_name);
            if (it == g_money_commands.end()) {
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

    // Add subcommands for each money action
    auto actions = get_money_actions(db);
    for (const auto& action : actions) {
        dpp::command_option action_option(
            dpp::co_sub_command,
            action.name,
            action.description
        );
        money->options.push_back(action_option);
    }

    return money;
}

} // namespace economy
} // namespace commands
