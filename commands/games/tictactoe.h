#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy/helpers.h"
#include <dpp/dpp.h>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <random>
#include <chrono>
#include <thread>

using namespace bronx::db;

namespace commands {
namespace games {

struct TicTacToeGame {
    uint64_t game_id;
    uint64_t player1_id;  // X (challenger)
    uint64_t player2_id;  // O (opponent)
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id;
    
    ::std::array<int, 9> board;  // 0 = empty, 1 = X, 2 = O, 3 = wildcard
    int current_turn;  // 1 = player1's turn, 2 = player2's turn
    bool active = true;
    bool accepted = false;  // whether opponent accepted the challenge
    
    int wild_card_pos = -1;  // -1 = none; position of the wildcard tile
    
    int best_of = 1;          // total games in series (1, 3, 5, …)
    int player1_wins = 0;     // round wins for player 1 (X)
    int player2_wins = 0;     // round wins for player 2 (O)
    int current_round = 1;    // current round number
    int series_winner = 0;    // 0 = ongoing, 1 = player1 won series, 2 = player2 won series
    
    int64_t bet_amount = 0;  // 0 means friendly game
    bool bet_escrowed = false;  // whether money has been taken from both players
    std::string interaction_token;  // latest interaction token for webhook edits
};

static ::std::map<uint64_t, TicTacToeGame> active_tictactoe_games;
static uint64_t next_game_id = 1;

// Check for win condition
// board values: 0 = empty, 1 = X, 2 = O, 3 = wildcard (counts for either side)
static int check_winner(const ::std::array<int, 9>& board) {
    const int patterns[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},  // rows
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},  // columns
        {0, 4, 8}, {2, 4, 6}              // diagonals
    };
    
    for (const auto& pattern : patterns) {
        int vals[3] = { board[pattern[0]], board[pattern[1]], board[pattern[2]] };
        
        // Line must be fully occupied (no empty cells)
        if (vals[0] == 0 || vals[1] == 0 || vals[2] == 0) continue;
        
        // Find which player owns the non-wild cells; wildcard (3) matches either
        int player = 0;
        bool conflict = false;
        for (int v : vals) {
            if (v == 3) continue;  // wildcard — skip
            if (player == 0) player = v;
            else if (player != v) { conflict = true; break; }
        }
        
        // If all cells agree (wildcards fill in), that player wins
        if (!conflict && player != 0) return player;
    }
    
    // Check for draw (board full, no winner)
    bool full = true;
    for (int cell : board) {
        if (cell == 0) { full = false; break; }
    }
    
    if (full) return -1;  // Draw
    return 0;  // Game continues
}

// Reset board with a random wildcard tile for tie-breaker round
static void reset_with_wildcard(TicTacToeGame& game) {
    static ::std::mt19937 rng(static_cast<uint32_t>(::std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    ::std::uniform_int_distribution<int> dist(0, 8);
    
    game.board.fill(0);
    int wc_pos = dist(rng);
    game.board[wc_pos] = 3;  // wildcard
    game.wild_card_pos = wc_pos;
    game.current_turn = 1;   // challenger (X) goes first in each new round
}

// Convert board to emoji display
// Values: 0=empty, 1=X, 2=O, 3=wildcard
static ::std::string board_to_string(const ::std::array<int, 9>& board) {
    ::std::string result;
    const ::std::string symbols[] = {"⬛", "❌", "⭕", "⚡"};
    
    for (int i = 0; i < 9; i++) {
        result += symbols[board[i]];
        if (i % 3 == 2) {
            result += "\n";
        } else {
            result += " ";
        }
    }
    
    return result;
}

// Create button grid for the board
// Values: 0=empty, 1=X, 2=O, 3=wildcard
static dpp::component create_board_buttons(const TicTacToeGame& game) {
    dpp::component rows[3];
    
    for (int i = 0; i < 9; i++) {
        int row_idx = i / 3;
        ::std::string label;
        dpp::component_style style;
        bool disabled = !game.active || !game.accepted;
        
        int val = game.board[i];
        if (val == 0) {
            label = "\xC2\xB7";  // middle dot
            style = dpp::cos_secondary;
        } else if (val == 1) {
            label = "X";
            style = dpp::cos_danger;
            disabled = true;
        } else if (val == 2) {
            label = "O";
            style = dpp::cos_primary;
            disabled = true;
        } else {  // val == 3: wildcard
            label = "W";
            style = dpp::cos_success;
            disabled = true;
        }
        
        rows[row_idx].add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label(label)
            .set_style(style)
            .set_id("ttt_" + ::std::to_string(game.game_id) + "_" + ::std::to_string(i))
            .set_disabled(disabled));
    }
    
    dpp::component container;
    for (auto& row : rows) container.add_component(row);
    return container;
}

// Update the game message
static void update_game_message(dpp::cluster& bot, TicTacToeGame& game) {
    dpp::embed embed;
    
    if (!game.accepted) {
        // Challenge pending
        ::std::string description = "<@" + ::std::to_string(game.player1_id) + "> challenged <@" + ::std::to_string(game.player2_id) + "> to Tic-Tac-Toe!\n\n";
        
        if (game.bet_amount > 0) {
            description += "**Wager:** $" + economy::format_number(game.bet_amount) + "\n\n";
        } else {
            description += "**Friendly game** (no wager)\n\n";
        }
        
        description += "<@" + ::std::to_string(game.player2_id) + ">, click Accept to play!";
        
        embed = dpp::embed()
            .set_color(0xFFD700)
            .set_title("⭕ TIC-TAC-TOE Challenge")
            .set_description(description);
    } else {
        // Game in progress or finished
        ::std::string description = board_to_string(game.board) + "\n";
        
        // Series-over display (board already reset to empty for next round, so check series_winner)
        if (game.series_winner > 0) {
            uint64_t winner_id = (game.series_winner == 1) ? game.player1_id : game.player2_id;
            description = "Score: " + ::std::to_string(game.player1_wins) + " - " + ::std::to_string(game.player2_wins) + "\n\n";
            description += "\n🏆 **<@" + ::std::to_string(winner_id) + "> wins the series!**\n";
            if (game.best_of > 1) {
                description += "*Best of " + ::std::to_string(game.best_of) + "*\n";
            }
            if (game.bet_amount > 0) {
                description += "**Winnings:** $" + economy::format_number(game.bet_amount * 2);
            }
            embed = dpp::embed()
                .set_color(0x00FF00)
                .set_title("⭕ TIC-TAC-TOE - Series Over")
                .set_description(description);
        } else {
        
        int winner = check_winner(game.board);
        
        // Score line shown when best_of > 1
        ::std::string score_line;
        if (game.best_of > 1) {
            score_line = "Round " + ::std::to_string(game.current_round) + "/" + ::std::to_string(game.best_of)
                       + " — Score: " + ::std::to_string(game.player1_wins)
                       + " - " + ::std::to_string(game.player2_wins) + "\n";
        }
        
        if (winner > 0) {
            game.active = false;
            uint64_t winner_id = (winner == 1) ? game.player1_id : game.player2_id;
            description += "\n🎉 **<@" + ::std::to_string(winner_id) + "> wins!**\n";
            
            if (game.bet_amount > 0) {
                description += "**Winnings:** $" + economy::format_number(game.bet_amount * 2);
            }
            
            embed = dpp::embed()
                .set_color(0x00FF00)
                .set_title("⭕ TIC-TAC-TOE - Game Over")
                .set_description(description);
        } else if (winner == -1) {
            // Draw — reset board with a wildcard and continue!
            reset_with_wildcard(game);
            description = board_to_string(game.board) + "\n";
            
            uint64_t current_player = (game.current_turn == 1) ? game.player1_id : game.player2_id;
            ::std::string symbol = (game.current_turn == 1) ? "❌" : "⭕";
            description += "\n⚡ **It's a tie! A wildcard has been placed!**\n";
            description += "The ⚡ **(W)** tile counts for **both** players.\n\n";
            description += symbol + " **<@" + ::std::to_string(current_player) + ">'s turn**\n";
            
            if (!score_line.empty()) description += "\n" + score_line;
            if (game.bet_amount > 0) {
                description += "\n**Wager:** $" + economy::format_number(game.bet_amount) + " each";
            }
            
            embed = dpp::embed()
                .set_color(0xFFAA00)
                .set_title("⚡ TIC-TAC-TOE - Wildcard Round!")
                .set_description(description);
        } else {
            // Game continues
            uint64_t current_player = (game.current_turn == 1) ? game.player1_id : game.player2_id;
            ::std::string symbol = (game.current_turn == 1) ? "❌" : "⭕";
            
            description += "\n" + symbol + " **<@" + ::std::to_string(current_player) + ">'s turn**\n";
            
            if (!score_line.empty()) description += "\n" + score_line;
            if (game.bet_amount > 0) {
                description += "\n**Wager:** $" + economy::format_number(game.bet_amount) + " each";
            }
            
            embed = dpp::embed()
                .set_color(0x1E90FF)
                .set_title("⭕ TIC-TAC-TOE")
                .set_description(description);
        }
        } // end series_winner == 0 block
    }
    
    dpp::message msg(game.channel_id, embed);
    msg.id = game.message_id;
    
    // Add buttons
    if (!game.accepted) {
        // Accept/Decline buttons
        dpp::component action_row;
        action_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("✅ Accept")
            .set_style(dpp::cos_success)
            .set_id("ttt_accept_" + ::std::to_string(game.game_id)));
        
        action_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("❌ Decline")
            .set_style(dpp::cos_danger)
            .set_id("ttt_decline_" + ::std::to_string(game.game_id)));
        
        msg.add_component(action_row);
    } else if (game.active) {
        // Game board buttons - need to add each row separately
        auto buttons = create_board_buttons(game);
        for (const auto& row : buttons.components) {
            msg.add_component(row);
        }
    }
    
    if (!game.interaction_token.empty()) {
        bot.interaction_response_edit(game.interaction_token, msg);
    } else {
        bot.message_edit(msg);
    }
}

// Handle bet escrow when game is accepted
static bool escrow_bets(Database* db, TicTacToeGame& game) {
    if (game.bet_amount <= 0 || game.bet_escrowed) return true;
    
    // Take money from both players
    auto player1_wallet = db->update_wallet(game.player1_id, -game.bet_amount);
    if (!player1_wallet || *player1_wallet < 0) {
        // Refund and cancel
        if (player1_wallet) {
            db->update_wallet(game.player1_id, game.bet_amount);
        }
        return false;
    }
    
    auto player2_wallet = db->update_wallet(game.player2_id, -game.bet_amount);
    if (!player2_wallet || *player2_wallet < 0) {
        // Refund player1 and cancel
        db->update_wallet(game.player1_id, game.bet_amount);
        return false;
    }
    
    game.bet_escrowed = true;
    return true;
}

// Handle game end payouts
static void handle_game_end(dpp::cluster& bot, Database* db, TicTacToeGame& game) {
    int winner = check_winner(game.board);
    
    if (winner > 0) {
        // Increment round wins
        if (winner == 1) game.player1_wins++;
        else             game.player2_wins++;
        
        int wins_needed = (game.best_of / 2) + 1;
        bool series_over = (game.player1_wins >= wins_needed || game.player2_wins >= wins_needed);
        
        if (series_over) {
            game.series_winner = (game.player1_wins >= wins_needed) ? 1 : 2;
            game.active = false;
            
            // Pay out bets on series winner
            if (game.bet_amount > 0 && game.bet_escrowed) {
                if (game.series_winner == 1)
                    db->update_wallet(game.player1_id, game.bet_amount * 2);
                else
                    db->update_wallet(game.player2_id, game.bet_amount * 2);
            }
        } else {
            // Start next round — clean board, player 1 (X) goes first
            game.current_round++;
            game.board.fill(0);
            game.wild_card_pos = -1;
            game.current_turn = 1;
        }
    }
    
    update_game_message(bot, game);
}

// Register button handlers
inline void register_tictactoe_handlers(dpp::cluster& bot, Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        // Only handle tic-tac-toe buttons
        if (custom_id.find("ttt_") != 0) return;
        
        // Handle accept button
        if (custom_id.find("ttt_accept_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(11));  // "ttt_accept_".length() = 11
            
            auto it = active_tictactoe_games.find(game_id);
            if (it == active_tictactoe_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            
            // Only the challenged player can accept
            if (event.command.get_issuing_user().id != game.player2_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the challenged player can accept!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if both players have enough money for bet
            if (game.bet_amount > 0) {
                auto player1 = db->get_user(game.player1_id);
                auto player2 = db->get_user(game.player2_id);
                
                if (!player1 || player1->wallet < game.bet_amount) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Challenger doesn't have enough money!")).set_flags(dpp::m_ephemeral));
                    game.active = false;
                    active_tictactoe_games.erase(game_id);
                    return;
                }
                
                if (!player2 || player2->wallet < game.bet_amount) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("You don't have enough money to accept this bet!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                // Escrow the bets
                if (!escrow_bets(db, game)) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Failed to escrow bets. Game cancelled.")).set_flags(dpp::m_ephemeral));
                    game.active = false;
                    active_tictactoe_games.erase(game_id);
                    return;
                }
            }
            
            game.accepted = true;
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            game.interaction_token = event.command.token;
            update_game_message(bot, game);
            return;
        }
        
        // Handle decline button
        if (custom_id.find("ttt_decline_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(12));  // "ttt_decline_".length() = 12
            
            auto it = active_tictactoe_games.find(game_id);
            if (it == active_tictactoe_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            
            // Only the challenged player can decline
            if (event.command.get_issuing_user().id != game.player2_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the challenged player can decline!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFF0000)
                .set_title("⭕ TIC-TAC-TOE - Challenge Declined")
                .set_description("<@" + ::std::to_string(game.player2_id) + "> declined the challenge.");
            
            dpp::message msg(game.channel_id, embed);
            msg.id = game.message_id;
            
            event.reply(dpp::ir_update_message, msg);
            active_tictactoe_games.erase(game_id);
            return;
        }
        
        // Handle board cell clicks (format: ttt_GAMEID_POSITION)
        if (custom_id.find("ttt_") == 0) {
            size_t first_underscore = custom_id.find('_', 4);
            if (first_underscore == ::std::string::npos) return;
            
            uint64_t game_id = ::std::stoull(custom_id.substr(4, first_underscore - 4));
            int position = ::std::stoi(custom_id.substr(first_underscore + 1));
            
            auto it = active_tictactoe_games.find(game_id);
            if (it == active_tictactoe_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game no longer exists.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& game = it->second;
            
            if (!game.active || !game.accepted) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game is not active.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if it's this player's turn
            uint64_t user_id = event.command.get_issuing_user().id;
            if ((game.current_turn == 1 && user_id != game.player1_id) ||
                (game.current_turn == 2 && user_id != game.player2_id)) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("It's not your turn!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if cell is empty
            if (game.board[position] != 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("That cell is already taken!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Make the move
            game.board[position] = game.current_turn;
            
            // Check for win or draw
            int result = check_winner(game.board);
            if (result > 0) {
                // Round win — handle_game_end manages active/series state
                handle_game_end(bot, db, game);
                event.reply(dpp::ir_deferred_update_message, dpp::message());
                game.interaction_token = event.command.token;
                
                // Clean up after a delay if series is now over
                if (!game.active) {
                    ::std::thread([game_id]() {
                        ::std::this_thread::sleep_for(::std::chrono::minutes(5));
                        active_tictactoe_games.erase(game_id);
                    }).detach();
                }
            } else if (result == -1) {
                // Draw — reset_with_wildcard is called inside update_game_message
                event.reply(dpp::ir_deferred_update_message, dpp::message());
                game.interaction_token = event.command.token;
                update_game_message(bot, game);
            } else {
                // Game continues — switch turns
                game.current_turn = (game.current_turn == 1) ? 2 : 1;
                event.reply(dpp::ir_deferred_update_message, dpp::message());
                game.interaction_token = event.command.token;
                update_game_message(bot, game);
            }
        }
    });
}

// Create the command
inline Command* get_tictactoe_command(Database* db) {
    static Command* tictactoe = new Command(
        "tictactoe",
        "Challenge someone to tic-tac-toe with optional bet",
        "games",
        {"ttt"},
        true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("Usage: .tictactoe @user [bet_amount]"));
                return;
            }
            
            // Parse opponent
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("You must mention a user to challenge!"));
                return;
            }
            
            uint64_t opponent_id = event.msg.mentions.begin()->first.id;
            uint64_t challenger_id = event.msg.author.id;
            
            if (opponent_id == challenger_id) {
                bronx::send_message(bot, event, bronx::error("You can't play against yourself!"));
                return;
            }
            
            if (event.msg.mentions.begin()->first.is_bot()) {
                bronx::send_message(bot, event, bronx::error("You can't play against a bot!"));
                return;
            }
            
            // Parse bet amount if provided
            int64_t bet_amount = 0;
            if (args.size() > 1) {
                auto challenger = db->get_user(challenger_id);
                if (!challenger) {
                    bronx::send_message(bot, event, bronx::error("You need to be registered in the database first!"));
                    return;
                }
                
                try {
                    bet_amount = economy::parse_amount(args[1], challenger->wallet);
                    
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
            
            // Parse best_of if provided
            int best_of = 1;
            if (args.size() > 2) {
                try {
                    best_of = ::std::stoi(args[2]);
                    if (best_of < 1 || best_of > 9 || best_of % 2 == 0) {
                        bronx::send_message(bot, event, bronx::error("Best-of must be an odd number between 1 and 9 (e.g. 1, 3, 5)"));
                        return;
                    }
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("Invalid best-of value"));
                    return;
                }
            }
            
            // Create game
            TicTacToeGame game;
            game.game_id = next_game_id++;
            game.player1_id = challenger_id;
            game.player2_id = opponent_id;
            game.guild_id = event.msg.guild_id;
            game.channel_id = event.msg.channel_id;
            game.board.fill(0);
            game.current_turn = 1;  // Player 1 (X) goes first
            game.bet_amount = bet_amount;
            game.best_of = best_of;
            
            // Create initial message
            ::std::string description = "<@" + ::std::to_string(challenger_id) + "> challenged <@" + ::std::to_string(opponent_id) + "> to Tic-Tac-Toe!\n\n";
            
            if (best_of > 1) {
                description += "**Best of " + ::std::to_string(best_of) + "**\n";
            }
            if (bet_amount > 0) {
                description += "**Wager:** $" + economy::format_number(bet_amount) + "\n";
            } else {
                description += "**Friendly game** (no wager)\n";
            }
            
            description += "<@" + ::std::to_string(opponent_id) + ">, click Accept to play!";
            
            dpp::embed embed = dpp::embed()
                .set_color(0xFFD700)
                .set_title("⭕ TIC-TAC-TOE Challenge")
                .set_description(description);
            
            dpp::message msg(event.msg.channel_id, embed);
            
            dpp::component action_row;
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("✅ Accept")
                .set_style(dpp::cos_success)
                .set_id("ttt_accept_" + ::std::to_string(game.game_id)));
            
            action_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("❌ Decline")
                .set_style(dpp::cos_danger)
                .set_id("ttt_decline_" + ::std::to_string(game.game_id)));
            
            msg.add_component(action_row);
            
            bot.message_create(msg, [&bot, db, game](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto created_msg = ::std::get<dpp::message>(callback.value);
                    game.message_id = created_msg.id;
                    active_tictactoe_games[game.game_id] = game;
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto opponent = event.get_parameter("opponent");
            uint64_t opponent_id = ::std::get<dpp::snowflake>(opponent);
            uint64_t challenger_id = event.command.get_issuing_user().id;
            
            if (opponent_id == challenger_id) {
                event.reply(dpp::message().add_embed(bronx::error("You can't play against yourself!")));
                return;
            }
            
            // Check if opponent is a bot
            bot.user_get(opponent_id, [&bot, db, event, challenger_id, opponent_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.reply(dpp::message().add_embed(bronx::error("Failed to get opponent information.")));
                    return;
                }
                
                dpp::user_identified opponent_user = ::std::get<dpp::user_identified>(callback.value);
                if (opponent_user.is_bot()) {
                    event.reply(dpp::message().add_embed(bronx::error("You can't play against a bot!")));
                    return;
                }
                
                // Parse bet amount if provided
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
                
                // Parse best_of if provided
                int best_of = 1;
                auto bo_param = event.get_parameter("best_of");
                if (::std::holds_alternative<int64_t>(bo_param)) {
                    int64_t bo_val = ::std::get<int64_t>(bo_param);
                    if (bo_val < 1 || bo_val > 9 || bo_val % 2 == 0) {
                        event.reply(dpp::message().add_embed(bronx::error("Best-of must be an odd number between 1 and 9 (e.g. 1, 3, 5)")));
                        return;
                    }
                    best_of = static_cast<int>(bo_val);
                }
                
                // Create game
                TicTacToeGame game;
                game.game_id = next_game_id++;
                game.player1_id = challenger_id;
                game.player2_id = opponent_id;
                game.guild_id = event.command.guild_id;
                game.channel_id = event.command.channel_id;
                game.board.fill(0);
                game.current_turn = 1;  // Player 1 (X) goes first
                game.bet_amount = bet_amount;
                game.best_of = best_of;
                
                // Create initial message
                ::std::string description = "<@" + ::std::to_string(challenger_id) + "> challenged <@" + ::std::to_string(opponent_id) + "> to Tic-Tac-Toe!\n\n";
                
                if (best_of > 1) {
                    description += "**Best of " + ::std::to_string(best_of) + "**\n";
                }
                if (bet_amount > 0) {
                    description += "**Wager:** $" + economy::format_number(bet_amount) + "\n";
                } else {
                    description += "**Friendly game** (no wager)\n";
                }
                
                description += "<@" + ::std::to_string(opponent_id) + ">, click Accept to play!";
                
                dpp::embed embed = dpp::embed()
                    .set_color(0xFFD700)
                    .set_title("⭕ TIC-TAC-TOE Challenge")
                    .set_description(description);
                
                dpp::message msg(event.command.channel_id, embed);
                
                dpp::component action_row;
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("✅ Accept")
                    .set_style(dpp::cos_success)
                    .set_id("ttt_accept_" + ::std::to_string(game.game_id)));
                
                action_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("❌ Decline")
                    .set_style(dpp::cos_danger)
                    .set_id("ttt_decline_" + ::std::to_string(game.game_id)));
                
                msg.add_component(action_row);
                
                event.reply(msg, [&bot, game, token = event.command.token](const dpp::confirmation_callback_t& callback) mutable {
                    if (callback.is_error()) return;
                    // event.reply() returns confirmation, not message — fetch the original response to get the message ID
                    bot.interaction_response_get_original(token, [game](const dpp::confirmation_callback_t& resp) mutable {
                        if (resp.is_error()) return;
                        auto created_msg = resp.get<dpp::message>();
                        game.message_id = created_msg.id;
                        active_tictactoe_games[game.game_id] = game;
                    });
                });
            });
        },
        {
            dpp::command_option(dpp::co_user, "opponent", "The user you want to challenge", true),
            dpp::command_option(dpp::co_string, "bet", "Optional bet amount (100+, supports all/half/1k/etc)", false),
            dpp::command_option(dpp::co_integer, "best_of", "Best out of N games (odd number 1-9, default 1)", false)
                .add_choice(dpp::command_option_choice("1 game", int64_t(1)))
                .add_choice(dpp::command_option_choice("Best of 3", int64_t(3)))
                .add_choice(dpp::command_option_choice("Best of 5", int64_t(5)))
                .add_choice(dpp::command_option_choice("Best of 7", int64_t(7)))
                .add_choice(dpp::command_option_choice("Best of 9", int64_t(9)))
        }
    );
    
    return tictactoe;
}

} // namespace games
} // namespace commands
