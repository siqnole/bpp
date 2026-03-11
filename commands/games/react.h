#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy/helpers.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <thread>

using namespace bronx::db;

namespace commands {
namespace games {

struct ReactGame {
    uint64_t game_id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id;
    uint64_t host_id;
    
    bool is_open;  // true for open games, false for 1v1
    ::std::set<uint64_t> players;  // all players in the game
    ::std::map<uint64_t, int> scores;  // player_id -> score
    
    bool active = false;  // game started
    bool accepting_joins = true;  // can still join
    
    int64_t bet_amount = 0;  // 0 means friendly game
    bool bet_escrowed = false;
    
    int target_score = 3;  // first to 3 wins
    int current_round = 0;
    int click_button = -1;  // which button is the "click me" button (0-8)
    std::string interaction_token;  // latest interaction token for webhook edits
};

static ::std::map<uint64_t, ReactGame> active_react_games;
static uint64_t next_react_game_id = 1;

// Create button grid for reaction game
static dpp::component create_react_buttons(const ReactGame& game) {
    dpp::component rows[3];
    
    for (int i = 0; i < 9; i++) {
        int row_idx = i / 3;
        ::std::string label = "-";
        dpp::component_style style = dpp::cos_secondary;
        bool disabled = (game.click_button < 0);
        
        // If this is the target button and game is showing it
        if (game.click_button >= 0 && i == game.click_button) {
            style = dpp::cos_success;  // Turn green
        }
        
        rows[row_idx].add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label(label)
            .set_style(style)
            .set_id("react_btn_" + ::std::to_string(game.game_id) + "_" + ::std::to_string(i))
            .set_disabled(disabled));
    }
    
    dpp::component container;
    for (auto& row : rows) container.add_component(row);
    return container;
}

// Update game message
static void update_react_message(dpp::cluster& bot, ReactGame& game, const ::std::string& status_text = "") {
    dpp::embed embed;
    ::std::string description;
    
    if (!game.active && game.accepting_joins) {
        // Waiting for players
        if (game.is_open) {
            description = "**Open react**\n\n";
            description += "Click **Join** to participate!\n";
            if (game.bet_amount > 0) {
                description += "**Entry Fee:** $" + economy::format_number(game.bet_amount) + "\n";
            } else {
                description += "**Friendly game** (no entry fee)\n";
            }
            description += "\n**Players:** " + ::std::to_string(game.players.size()) + "\n";
            for (uint64_t player_id : game.players) {
                description += "• <@" + ::std::to_string(player_id) + ">\n";
            }
            description += "\nHost: <@" + ::std::to_string(game.host_id) + "> can start when ready!";
        } else {
            // 1v1 game
            description = "**react Challenge**\n\n";
            if (game.bet_amount > 0) {
                description += "**Wager:** $" + economy::format_number(game.bet_amount) + "\n\n";
            } else {
                description += "**Friendly game** (no wager)\n\n";
            }
            
            if (game.players.size() < 2) {
                description += "Waiting for opponent to accept...";
            } else {
                description += "Both players ready! Starting soon...";
            }
        }
        
        embed = dpp::embed()
            .set_color(0xFFD700)
            .set_title("REACT")
            .set_description(description);
    } else if (game.active) {
        // Game in progress
        description = "**Round " + ::std::to_string(game.current_round + 1) + "** • First to " + ::std::to_string(game.target_score) + " wins!\n\n";
        
        // Show scores
        ::std::vector<::std::pair<uint64_t, int>> sorted_scores(game.scores.begin(), game.scores.end());
        ::std::sort(sorted_scores.begin(), sorted_scores.end(), 
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (const auto& [player_id, score] : sorted_scores) {
            description += "<@" + ::std::to_string(player_id) + ">: **" + ::std::to_string(score) + "** points\n";
        }
        
        if (!status_text.empty()) {
            description += "\n" + status_text;
        } else {
            description += "\nGet ready...";
        }
        
        embed = dpp::embed()
            .set_color(0x1E90FF)
            .set_title("REACT")
            .set_description(description);
    }
    
    dpp::message msg(game.channel_id, embed);
    msg.id = game.message_id;
    
    // Add buttons
    if (!game.active && game.accepting_joins) {
        if (game.is_open) {
            // Join and Start buttons for open games
            dpp::component action_row;
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Join")
                .set_style(dpp::cos_success)
                .set_id("react_join_" + ::std::to_string(game.game_id)));
            
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Start Game")
                .set_style(dpp::cos_primary)
                .set_id("react_start_" + ::std::to_string(game.game_id))
                .set_disabled(game.players.size() < 2));
            
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Cancel")
                .set_style(dpp::cos_danger)
                .set_id("react_cancel_" + ::std::to_string(game.game_id)));
            
            msg.add_component(action_row);
        } else {
            // Accept/Decline for 1v1
            dpp::component action_row;
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Accept")
                .set_style(dpp::cos_success)
                .set_id("react_accept_" + ::std::to_string(game.game_id)));
            
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Decline")
                .set_style(dpp::cos_danger)
                .set_id("react_decline_" + ::std::to_string(game.game_id)));
            
            msg.add_component(action_row);
        }
    } else if (game.active) {
        // Game buttons (will be enabled when showing "CLICK ME")
        auto buttons = create_react_buttons(game);
        for (const auto& row : buttons.components) {
            msg.add_component(row);
        }
    }
    
    if (!game.interaction_token.empty()) {
        bot.interaction_response_edit(game.interaction_token, msg);
    } else {
        bronx::safe_message_edit(bot, msg);
    }
}

// Handle bet escrow
static bool escrow_bets(Database* db, ReactGame& game) {
    if (game.bet_amount <= 0 || game.bet_escrowed) return true;
    
    // Take money from all players
    for (uint64_t player_id : game.players) {
        auto wallet = db->update_wallet(player_id, -game.bet_amount);
        if (!wallet || *wallet < 0) {
            // Refund all previous players
            for (uint64_t refund_id : game.players) {
                if (refund_id == player_id) break;
                db->update_wallet(refund_id, game.bet_amount);
            }
            return false;
        }
    }
    
    game.bet_escrowed = true;
    return true;
}

// Start the game
static void start_game(dpp::cluster& bot, Database* db, ReactGame& game) {
    if (game.players.size() < 2) return;
    
    // Escrow bets
    if (game.bet_amount > 0 && !escrow_bets(db, game)) {
        // Failed to escrow - cancel game
        return;
    }
    
    game.active = true;
    game.accepting_joins = false;
    game.current_round = 0;
    
    // Initialize scores
    for (uint64_t player_id : game.players) {
        game.scores[player_id] = 0;
    }
    
    // Show "Get ready" message
    update_react_message(bot, game, "**Get ready...**");
    
    // After a random delay (2-5 seconds), show the click button
    auto game_id = game.game_id;
    ::std::thread([&bot, db, game_id]() {
        static ::std::mt19937 rng(static_cast<uint32_t>(::std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        ::std::uniform_int_distribution<int> delay_dist(2000, 5000);
        
        int delay_ms = delay_dist(rng);
        ::std::this_thread::sleep_for(::std::chrono::milliseconds(delay_ms));
        
        auto it = active_react_games.find(game_id);
        if (it == active_react_games.end() || !it->second.active) return;
        
        auto& game = it->second;
        
        // Show random click button
        ::std::uniform_int_distribution<int> button_dist(0, 8);
        game.click_button = button_dist(rng);
        
        update_react_message(bot, game, "**CLICK NOW!**");
    }).detach();
}

// Handle round win
static void handle_round_win(dpp::cluster& bot, Database* db, ReactGame& game, uint64_t winner_id) {
    game.scores[winner_id]++;
    game.click_button = -1;  // Reset click button
    
    // Check if someone won the game
    if (game.scores[winner_id] >= game.target_score) {
        // Game over!
        game.active = false;
        
        ::std::string description = "**<@" + ::std::to_string(winner_id) + "> WINS!**\n\n";
        
        // Show final scores
        ::std::vector<::std::pair<uint64_t, int>> sorted_scores(game.scores.begin(), game.scores.end());
        ::std::sort(sorted_scores.begin(), sorted_scores.end(), 
            [](const auto& a, const auto& b) { return a.second > b.second; });
        
        description += "**Final Scores:**\n";
        for (const auto& [player_id, score] : sorted_scores) {
            description += "<@" + ::std::to_string(player_id) + ">: **" + ::std::to_string(score) + "** points\n";
        }
        
        // Handle payouts
        if (game.bet_amount > 0 && game.bet_escrowed) {
            int64_t total_pot = game.bet_amount * game.players.size();
            db->update_wallet(winner_id, total_pot);
            description += "\n**Winnings:** $" + economy::format_number(total_pot);
        }
        
        dpp::embed embed = dpp::embed()
            .set_color(0x00FF00)
            .set_title("REACT - GAME OVER")
            .set_description(description);
        
        dpp::message msg(game.channel_id, embed);
        msg.id = game.message_id;
        
        if (!game.interaction_token.empty()) {
            bot.interaction_response_edit(game.interaction_token, msg);
        } else {
            bronx::safe_message_edit(bot, msg);
        }
        
        // Clean up after delay
        auto game_id = game.game_id;
        ::std::thread([game_id]() {
            ::std::this_thread::sleep_for(::std::chrono::minutes(5));
            active_react_games.erase(game_id);
        }).detach();
    } else {
        // Continue to next round
        game.current_round++;
        
        update_react_message(bot, game, "<@" + ::std::to_string(winner_id) + "> got the point!");
        
        // Start next round after short delay
        auto game_id = game.game_id;
        ::std::thread([&bot, db, game_id]() {
            ::std::this_thread::sleep_for(::std::chrono::seconds(2));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end() || !it->second.active) return;
            
            auto& game = it->second;
            
            // Show "Get ready" message
            update_react_message(bot, game, "**Get ready...**");
            
            // Random delay before showing click button
            static ::std::mt19937 rng(static_cast<uint32_t>(::std::chrono::high_resolution_clock::now().time_since_epoch().count()));
            ::std::uniform_int_distribution<int> delay_dist(2000, 5000);
            int delay_ms = delay_dist(rng);
            ::std::this_thread::sleep_for(::std::chrono::milliseconds(delay_ms));
            
            it = active_react_games.find(game_id);
            if (it == active_react_games.end() || !it->second.active) return;
            
            game = it->second;
            
            // Show random click button
            ::std::uniform_int_distribution<int> button_dist(0, 8);
            game.click_button = button_dist(rng);
            
            update_react_message(bot, game, "**CLICK NOW!**");
        }).detach();
    }
}

// Register button handlers
inline void register_react_handlers(dpp::cluster& bot, Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        // Only handle react buttons
        if (custom_id.find("react_") != 0) return;
        
        // Join button (open games)
        if (custom_id.find("react_join_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(11));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            if (!game.accepting_joins) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game is no longer accepting players.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (game.players.find(user_id) != game.players.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("You're already in this game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if user has enough money
            if (game.bet_amount > 0) {
                auto user = db->get_user(user_id);
                if (!user || user->wallet < game.bet_amount) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("You don't have enough money! Entry fee: $" + economy::format_number(game.bet_amount))).set_flags(dpp::m_ephemeral));
                    return;
                }
            }
            
            game.players.insert(user_id);
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            game.interaction_token = event.command.token;
            update_react_message(bot, game);
            return;
        }
        
        // Start button (open games, host only)
        if (custom_id.find("react_start_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(12));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            if (user_id != game.host_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the host can start the game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (game.players.size() < 2) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Need at least 2 players to start!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            start_game(bot, db, game);
            return;
        }
        
        // Cancel button (open games, host only)
        if (custom_id.find("react_cancel_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(13));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            if (user_id != game.host_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the host can cancel the game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFF0000)
                .set_title("REACT - Cancelled")
                .set_description("The game was cancelled by the host.");
            
            dpp::message msg(game.channel_id, embed);
            msg.id = game.message_id;
            
            event.reply(dpp::ir_update_message, msg);
            active_react_games.erase(game_id);
            return;
        }
        
        // Accept button (1v1 games)
        if (custom_id.find("react_accept_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(13));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            // Check if user is the opponent
            if (game.players.find(user_id) == game.players.end() || user_id == game.host_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the challenged player can accept!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if opponent has enough money
            if (game.bet_amount > 0) {
                auto user = db->get_user(user_id);
                if (!user || user->wallet < game.bet_amount) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("You don't have enough money to accept this bet!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                // Also check host still has money
                auto host = db->get_user(game.host_id);
                if (!host || host->wallet < game.bet_amount) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("The challenger doesn't have enough money anymore!")).set_flags(dpp::m_ephemeral));
                    game.active = false;
                    active_react_games.erase(game_id);
                    return;
                }
            }
            
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            game.interaction_token = event.command.token;
            start_game(bot, db, game);
            return;
        }
        
        // Decline button (1v1 games)
        if (custom_id.find("react_decline_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(14));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            if (game.players.find(user_id) == game.players.end() || user_id == game.host_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the challenged player can decline!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFF0000)
                .set_title("REACT - Challenge Declined")
                .set_description("<@" + ::std::to_string(user_id) + "> declined the challenge.");
            
            dpp::message msg(game.channel_id, embed);
            msg.id = game.message_id;
            
            event.reply(dpp::ir_update_message, msg);
            active_react_games.erase(game_id);
            return;
        }
        
        // Game button clicks (format: react_btn_GAMEID_POSITION)
        if (custom_id.find("react_btn_") == 0) {
            size_t first_underscore = custom_id.find('_', 10);
            if (first_underscore == ::std::string::npos) return;
            
            uint64_t game_id = ::std::stoull(custom_id.substr(10, first_underscore - 10));
            int position = ::std::stoi(custom_id.substr(first_underscore + 1));
            
            auto it = active_react_games.find(game_id);
            if (it == active_react_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            uint64_t user_id = event.command.get_issuing_user().id;
            
            if (!game.active) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game is not active.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (game.players.find(user_id) == game.players.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("You're not in this game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (game.click_button < 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Wait for the button to appear!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (position != game.click_button) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Wrong button!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // User clicked the correct button first!
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            game.interaction_token = event.command.token;
            handle_round_win(bot, db, game, user_id);
        }
    });
}

// Create the command
inline Command* get_react_command(Database* db) {
    static Command* react = new Command(
        "react",
        "Challenge someone to a reaction time game",
        "games",
        {},
        true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("Usage: .react <@user|open> [bet_amount]"));
                return;
            }
            
            uint64_t challenger_id = event.msg.author.id;
            bool is_open = (args[0] == "open");
            
            int64_t bet_amount = 0;
            int bet_arg_index = is_open ? 1 : 1;
            
            if (args.size() > bet_arg_index) {
                auto challenger = db->get_user(challenger_id);
                if (!challenger) {
                    bronx::send_message(bot, event, bronx::error("You need to be registered in the database first!"));
                    return;
                }
                
                try {
                    bet_amount = economy::parse_amount(args[bet_arg_index], challenger->wallet);
                    
                    if (bet_amount < 0) {
                        bronx::send_message(bot, event, bronx::error("Bet amount must be positive!"));
                        return;
                    }
                    
                    if (bet_amount > 0 && bet_amount < 100) {
                        bronx::send_message(bot, event, bronx::error("Minimum bet is $100"));
                        return;
                    }
                    
                    if (bet_amount > challenger->wallet) {
                        bronx::send_message(bot, event, bronx::error("You don't have enough money!"));
                        return;
                    }
                } catch (const ::std::exception& e) {
                    bronx::send_message(bot, event, bronx::error("Invalid bet amount: " + ::std::string(e.what())));
                    return;
                }
            }
            
            ReactGame game;
            game.game_id = next_react_game_id++;
            game.guild_id = event.msg.guild_id;
            game.channel_id = event.msg.channel_id;
            game.host_id = challenger_id;
            game.is_open = is_open;
            game.bet_amount = bet_amount;
            game.players.insert(challenger_id);
            
            if (!is_open) {
                // 1v1 game
                if (event.msg.mentions.empty()) {
                    bronx::send_message(bot, event, bronx::error("You must mention a user to challenge!"));
                    return;
                }
                
                uint64_t opponent_id = event.msg.mentions.begin()->first.id;
                
                if (opponent_id == challenger_id) {
                    bronx::send_message(bot, event, bronx::error("You can't play against yourself!"));
                    return;
                }
                
                if (event.msg.mentions.begin()->first.is_bot()) {
                    bronx::send_message(bot, event, bronx::error("You can't play against a bot!"));
                    return;
                }
                
                game.players.insert(opponent_id);
            }
            
            // Create initial message
            ::std::string description;
            if (is_open) {
                description = "**Open react**\n\n";
                description += "Click **Join** to participate!\n";
                if (bet_amount > 0) {
                    description += "**Entry Fee:** $" + economy::format_number(bet_amount) + "\n";
                } else {
                    description += "**Friendly game** (no entry fee)\n";
                }
                description += "\n**Players:** 1\n• <@" + ::std::to_string(challenger_id) + ">\n";
                description += "\nHost can start when ready!";
            } else {
                uint64_t opponent_id = *::std::next(game.players.begin());
                description = "**react Challenge**\n\n";
                description += "<@" + ::std::to_string(challenger_id) + "> challenged <@" + ::std::to_string(opponent_id) + ">!\n\n";
                if (bet_amount > 0) {
                    description += "**Wager:** $" + economy::format_number(bet_amount) + "\n\n";
                } else {
                    description += "**Friendly game** (no wager)\n\n";
                }
                description += "<@" + ::std::to_string(opponent_id) + ">, click Accept to play!";
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFFD700)
                .set_title("REACT")
                .set_description(description);
            
            dpp::message msg(event.msg.channel_id, embed);
            
            if (is_open) {
                dpp::component action_row;
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Join")
                    .set_style(dpp::cos_success)
                    .set_id("react_join_" + ::std::to_string(game.game_id)));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Start Game")
                    .set_style(dpp::cos_primary)
                    .set_id("react_start_" + ::std::to_string(game.game_id))
                    .set_disabled(true));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Cancel")
                    .set_style(dpp::cos_danger)
                    .set_id("react_cancel_" + ::std::to_string(game.game_id)));
                
                msg.add_component(action_row);
            } else {
                dpp::component action_row;
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Accept")
                    .set_style(dpp::cos_success)
                    .set_id("react_accept_" + ::std::to_string(game.game_id)));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Decline")
                    .set_style(dpp::cos_danger)
                    .set_id("react_decline_" + ::std::to_string(game.game_id)));
                
                msg.add_component(action_row);
            }
            
            bot.message_create(msg, [game](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto created_msg = ::std::get<dpp::message>(callback.value);
                    game.message_id = created_msg.id;
                    active_react_games[game.game_id] = game;
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto mode = event.get_parameter("mode");
            ::std::string mode_str = ::std::get<::std::string>(mode);
            
            uint64_t challenger_id = event.command.get_issuing_user().id;
            bool is_open = (mode_str == "open");
            
            int64_t bet_amount = 0;
            auto bet_param = event.get_parameter("bet");
            
            if (::std::holds_alternative<::std::string>(bet_param)) {
                ::std::string bet_str = ::std::get<::std::string>(bet_param);
                
                if (!bet_str.empty()) {
                    auto challenger = db->get_user(challenger_id);
                    if (!challenger) {
                        event.reply(dpp::message().add_embed(bronx::error("You need to be registered in the database first!")));
                        return;
                    }
                    
                    try {
                        bet_amount = economy::parse_amount(bet_str, challenger->wallet);
                        
                        if (bet_amount < 0) {
                            event.reply(dpp::message().add_embed(bronx::error("Bet amount must be positive!")));
                            return;
                        }
                        
                        if (bet_amount > 0 && bet_amount < 100) {
                            event.reply(dpp::message().add_embed(bronx::error("Minimum bet is $100")));
                            return;
                        }
                        
                        if (bet_amount > challenger->wallet) {
                            event.reply(dpp::message().add_embed(bronx::error("You don't have enough money!")));
                            return;
                        }
                    } catch (const ::std::exception& e) {
                        event.reply(dpp::message().add_embed(bronx::error("Invalid bet amount: " + ::std::string(e.what()))));
                        return;
                    }
                }
            }
            
            ReactGame game;
            game.game_id = next_react_game_id++;
            game.guild_id = event.command.guild_id;
            game.channel_id = event.command.channel_id;
            game.host_id = challenger_id;
            game.is_open = is_open;
            game.bet_amount = bet_amount;
            game.players.insert(challenger_id);
            
            uint64_t opponent_id = 0;
            
            if (!is_open) {
                auto opponent = event.get_parameter("opponent");
                opponent_id = ::std::get<dpp::snowflake>(opponent);
                
                if (opponent_id == challenger_id) {
                    event.reply(dpp::message().add_embed(bronx::error("You can't play against yourself!")));
                    return;
                }
                
                game.players.insert(opponent_id);
            }
            
            // Create initial message
            ::std::string description;
            if (is_open) {
                description = "**Open react**\n\n";
                description += "Click **Join** to participate!\n";
                if (bet_amount > 0) {
                    description += "**Entry Fee:** $" + economy::format_number(bet_amount) + "\n";
                } else {
                    description += "**Friendly game** (no entry fee)\n";
                }
                description += "\n**Players:** 1\n• <@" + ::std::to_string(challenger_id) + ">\n";
                description += "\nHost can start when ready!";
            } else {
                description = "**react Challenge**\n\n";
                description += "<@" + ::std::to_string(challenger_id) + "> challenged <@" + ::std::to_string(opponent_id) + ">!\n\n";
                if (bet_amount > 0) {
                    description += "**Wager:** $" + economy::format_number(bet_amount) + "\n\n";
                } else {
                    description += "**Friendly game** (no wager)\n\n";
                }
                description += "<@" + ::std::to_string(opponent_id) + ">, click Accept to play!";
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFFD700)
                .set_title("REACT")
                .set_description(description);
            
            dpp::message msg(event.command.channel_id, embed);
            
            if (is_open) {
                dpp::component action_row;
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Join")
                    .set_style(dpp::cos_success)
                    .set_id("react_join_" + ::std::to_string(game.game_id)));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Start Game")
                    .set_style(dpp::cos_primary)
                    .set_id("react_start_" + ::std::to_string(game.game_id))
                    .set_disabled(true));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Cancel")
                    .set_style(dpp::cos_danger)
                    .set_id("react_cancel_" + ::std::to_string(game.game_id)));
                
                msg.add_component(action_row);
            } else {
                dpp::component action_row;
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Accept")
                    .set_style(dpp::cos_success)
                    .set_id("react_accept_" + ::std::to_string(game.game_id)));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Decline")
                    .set_style(dpp::cos_danger)
                    .set_id("react_decline_" + ::std::to_string(game.game_id)));
                
                msg.add_component(action_row);
            }
            
            event.reply(msg, [&bot, game, token = event.command.token](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) return;
                // event.reply() returns confirmation, not message — fetch the original response to get the message ID
                bot.interaction_response_get_original(token, [game](const dpp::confirmation_callback_t& resp) mutable {
                    if (resp.is_error()) return;
                    auto created_msg = resp.get<dpp::message>();
                    game.message_id = created_msg.id;
                    active_react_games[game.game_id] = game;
                });
            });
        },
        {
            dpp::command_option(dpp::co_string, "mode", "Game mode: '1v1' or 'open'", true)
                .add_choice(dpp::command_option_choice("1v1 Challenge", ::std::string("1v1")))
                .add_choice(dpp::command_option_choice("Open Game", ::std::string("open"))),
            dpp::command_option(dpp::co_user, "opponent", "The user to challenge (for 1v1 mode)", false),
            dpp::command_option(dpp::co_string, "bet", "Optional bet/entry fee (100+)", false)
        }
    );
    
    return react;
}

} // namespace games
} // namespace commands
