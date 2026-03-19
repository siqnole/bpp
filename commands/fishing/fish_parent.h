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
namespace fishing {

// Forward declarations for individual fishing handlers
inline Command* get_fish_command(Database* db);
inline Command* get_finv_command(Database* db);
inline Command* get_sellfish_command(Database* db);
inline Command* get_lockfish_command(Database* db);
inline Command* get_finfo_command(Database* db);
inline Command* get_inv_command(Database* db);
inline Command* get_equip_command(Database* db);
inline Command* get_suggestfish_command(Database* db);

namespace crews {
    inline Command* get_crew_command(Database* db);
}

// Note: autofisher returns a vector of commands, handled separately

// Fishing action definitions
struct FishingActionInfo {
    std::string name;
    std::string description;
    std::function<Command*(Database*)> getter;
};

inline std::vector<FishingActionInfo> get_fishing_actions(Database* db) {
    return {
        {"cast", "cast your rod and fish", get_fish_command},
        {"inventory", "view your fish inventory", get_finv_command},
        {"sell", "sell fish for money", get_sellfish_command},
        {"info", "get info about a specific fish", get_finfo_command},
        {"equip", "equip a fish (cosmetic)", get_equip_command},
        {"suggest", "get suggestions on what to fish", get_suggestfish_command},
        {"lock", "lock fish so they won't be sold", get_lockfish_command},
        {"crew", "manage your fishing crew", crews::get_crew_command},
    };
}

// Cache individual commands
static std::unordered_map<std::string, Command*> g_fishing_commands;

inline Command* create_fish_parent_command(Database* db) {
    // Initialize command cache on first use
    static bool initialized = false;
    if (!initialized) {
        auto actions = get_fishing_actions(db);
        for (const auto& action : actions) {
            g_fishing_commands[action.name] = action.getter(db);
        }
        initialized = true;
    }

    auto fish_cmd = new Command(
        "fish",
        "fishing-related commands",
        "economy",
        {"f"},
        true,  // is_slash_command

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto actions = get_fishing_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("Fishing Commands")
                    .set_color(0x2E7D32);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string action_name = args[0];
            std::transform(action_name.begin(), action_name.end(), action_name.begin(), ::tolower);

            // Check for old command aliases
            if (action_name == "finv" || action_name == "inv") {
                action_name = "inventory";
            } else if (action_name == "sellfish") {
                action_name = "sell";
            } else if (action_name == "lockfish") {
                action_name = "lock";
            } else if (action_name == "finfo") {
                action_name = "info";
            } else if (action_name == "suggestfish") {
                action_name = "suggest";
            }

            auto it = g_fishing_commands.find(action_name);
            if (it == g_fishing_commands.end()) {
                auto actions = get_fishing_actions(db);
                std::string suggestions = "Valid actions: ";
                for (size_t i = 0; i < actions.size(); i++) {
                    suggestions += actions[i].name;
                    if (i < actions.size() - 1) suggestions += ", ";
                }
                bronx::send_message(bot, event,
                    bronx::error("unknown action '" + action_name + "'\n" + suggestions));
                return;
            }

            // Forward all args except the action name to the handler
            std::vector<std::string> handler_args(args.begin() + 1, args.end());
            
            auto cmd = it->second;
            if (cmd && cmd->text_handler) {
                cmd->text_handler(bot, event, handler_args);
            }
        },

        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string action_name;
            
            // Parse subcommand from slash command structure
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) {
                action_name = ci_options[0].name;
            }

            if (action_name.empty()) {
                auto actions = get_fishing_actions(db);
                std::string action_list;
                for (const auto& action : actions) {
                    action_list += "• **" + action.name + "**: " + action.description + "\n";
                }
                auto embed = bronx::create_embed(action_list)
                    .set_title("Fishing Commands")
                    .set_color(0x2E7D32);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            auto it = g_fishing_commands.find(action_name);
            if (it == g_fishing_commands.end()) {
                event.reply(dpp::message().add_embed(
                    bronx::error("Unknown action: " + action_name)));
                return;
            }

            auto cmd = it->second;
            if (cmd && cmd->slash_handler) {
                cmd->slash_handler(bot, event);
            }
        }
    );

    // Add subcommands for each action
    auto actions = get_fishing_actions(db);
    for (const auto& action : actions) {
        dpp::command_option action_option(
            dpp::co_sub_command,
            action.name,
            action.description
        );

        // Add action-specific options
        if (action.name == "cast") {
            action_option.add_option(
                dpp::command_option(dpp::co_string, "answer", "captcha answer for anti-macro verification", false)
            );
        } else if (action.name == "sell") {
            action_option.add_option(
                dpp::command_option(dpp::co_string, "fish_type", "Type of fish to sell", false)
            );
            action_option.add_option(
                dpp::command_option(dpp::co_integer, "quantity", "How many to sell", false)
            );
        } else if (action.name == "info") {
            action_option.add_option(
                dpp::command_option(dpp::co_string, "fish_name", "Fish to get info on", false)
            );
        } else if (action.name == "equip") {
            action_option.add_option(
                dpp::command_option(dpp::co_integer, "fish_id", "Fish ID to equip", false)
            );
        } else if (action.name == "lock") {
            action_option.add_option(
                dpp::command_option(dpp::co_integer, "fish_id", "Fish ID to lock", false)
            );
        } else if (action.name == "crew") {
            action_option.add_option(
                dpp::command_option(dpp::co_string, "action", "crew action", false)
                    .add_choice(dpp::command_option_choice("view", "view"))
                    .add_choice(dpp::command_option_choice("hire", "hire"))
                    .add_choice(dpp::command_option_choice("upgrade", "upgrade"))
            );
        }

        fish_cmd->options.push_back(action_option);
    }

    return fish_cmd;
}

} // namespace fishing
} // namespace commands
