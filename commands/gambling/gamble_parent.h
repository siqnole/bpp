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
namespace gambling {

// Forward declarations for individual game handlers
// These are implemented in their respective files
inline Command* get_slots_command(Database* db);
inline Command* get_coinflip_command(Database* db);
inline Command* get_dice_command(Database* db);
inline Command* get_frogger_command(Database* db);
inline Command* get_roulette_command(Database* db);
inline Command* get_blackjack_command(Database* db);
inline Command* get_lottery_command(Database* db);
inline Command* get_minesweeper_command(Database* db);
inline Command* get_crash_command(Database* db);
inline Command* get_poker_command(Database* db);
inline Command* get_jackpot_command(Database* db);
inline Command* get_stats_command(Database* db);

// Maps subcommand names to their individual command getters
struct GameCommandInfo {
    std::string name;
    std::string description;
    std::function<Command*(Database*)> getter;
};

inline std::vector<GameCommandInfo> get_gambling_games(Database* db) {
    return {
        {"slots", "spin the slot machine", get_slots_command},
        {"coinflip", "flip a coin and bet on the outcome", get_coinflip_command},
        {"dice", "roll two dice and bet on the outcome", get_dice_command},
        {"frogger", "hop over obstacles for prizes", get_frogger_command},
        {"roulette", "spin the roulette wheel", get_roulette_command},
        {"blackjack", "play blackjack vs the dealer", get_blackjack_command},
        {"lottery", "buy lottery tickets for jackpot wins", get_lottery_command},
        {"minesweeper", "clear the field and earn rewards", get_minesweeper_command},
        {"crash", "predict when the multiplier crashes", get_crash_command},
        {"poker", "play poker against the house", get_poker_command},
        {"jackpot", "try your luck at the jackpot machine", get_jackpot_command},
    };
}

// Cache individual game commands
static std::unordered_map<std::string, Command*> g_game_commands;

inline Command* create_gamble_parent_command(Database* db) {
    // Initialize game command cache on first use
    static bool initialized = false;
    if (!initialized) {
        auto games = get_gambling_games(db);
        for (const auto& game : games) {
            g_game_commands[game.name] = game.getter(db);
        }
        initialized = true;
    }

    auto gamble = new Command(
        "gamble",
        "play various gambling games",
        "gambling",
        {},
        true,  // is_slash_command

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto games = get_gambling_games(db);
                std::string game_list;
                for (const auto& game : games) {
                    game_list += "• **" + game.name + "**: " + game.description + "\n";
                }
                auto embed = bronx::create_embed(game_list)
                    .set_title("Available Games")
                    .set_color(0xFF6B00);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string game_name = args[0];
            std::transform(game_name.begin(), game_name.end(), game_name.begin(), ::tolower);

            // Find the game command
            auto it = g_game_commands.find(game_name);
            if (it == g_game_commands.end()) {
                // Check aliases
                if (game_name == "cf" || game_name == "flip") {
                    game_name = "coinflip";
                    it = g_game_commands.find(game_name);
                } else if (game_name == "slot") {
                    game_name = "slots";
                    it = g_game_commands.find(game_name);
                } else if (game_name == "roll") {
                    game_name = "dice";
                    it = g_game_commands.find(game_name);
                }
            }

            if (it == g_game_commands.end()) {
                auto games = get_gambling_games(db);
                std::string suggestions = "Valid games: ";
                for (size_t i = 0; i < games.size(); i++) {
                    suggestions += games[i].name;
                    if (i < games.size() - 1) suggestions += ", ";
                }
                bronx::send_message(bot, event,
                    bronx::error("unknown game '" + game_name + "'\n" + suggestions));
                return;
            }

            // Forward all args except the game name to the game handler
            std::vector<std::string> game_args(args.begin() + 1, args.end());
            
            // Call the text handler of the individual game command
            auto game_cmd = it->second;
            if (game_cmd && game_cmd->text_handler) {
                game_cmd->text_handler(bot, event, game_args);
            }
        },

        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Get the selected game subcommand
            std::string game_name;
            
            // Parse subcommand from slash command structure
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) {
                game_name = ci_options[0].name;
            }

            if (game_name.empty()) {
                auto games = get_gambling_games(db);
                std::string game_list;
                for (const auto& game : games) {
                    game_list += "• **" + game.name + "**: " + game.description + "\n";
                }
                auto embed = bronx::create_embed(game_list)
                    .set_title("Available Games")
                    .set_color(0xFF6B00);
                event.reply(dpp::message().add_embed(embed));
                return;
            }

            // Find the game command
            auto it = g_game_commands.find(game_name);
            if (it == g_game_commands.end()) {
                event.reply(dpp::message().add_embed(
                    bronx::error("Unknown game: " + game_name)));
                return;
            }

            // Call the slash handler of the individual game command
            auto game_cmd = it->second;
            if (game_cmd && game_cmd->slash_handler) {
                game_cmd->slash_handler(bot, event);
            }
        }
    );

    // Add subcommands for each game
    auto games = get_gambling_games(db);
    for (const auto& game : games) {
        dpp::command_option game_option(
            dpp::co_sub_command,
            game.name,
            game.description
        );

        // Add amount parameter to applicable games
        if (game.name != "stats") {
            game_option.add_option(
                dpp::command_option(dpp::co_integer, "amount", "Amount to bet", true)
                    .set_min_value(50)
                    .set_max_value(2000000000)
            );
        }

        // Add game-specific options
        if (game.name == "coinflip") {
            game_option.add_option(
                dpp::command_option(dpp::co_string, "choice", "Heads or Tails", false)
                    .add_choice(dpp::command_option_choice("heads", "heads"))
                    .add_choice(dpp::command_option_choice("tails", "tails"))
            );
        }

        gamble->options.push_back(game_option);
    }

    return gamble;
}

} // namespace gambling
} // namespace commands
