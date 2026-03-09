#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>
#include <algorithm>
#include <sstream>
#include <set>

using namespace bronx::db;

namespace commands {
namespace gambling {

struct MinesweeperGame {
    uint64_t user_id;
    uint64_t message_id;
    uint64_t channel_id;
    int64_t initial_bet;
    int rows; // y
    int cols; // x
    int total_mines;
    int safe_cells_revealed;
    double multiplier_per_safe;
    ::std::set<int> mine_positions; // positions of mines (row * cols + col)
    ::std::set<int> revealed_positions; // positions that have been revealed
    bool active;
    bool game_over;
    bool won;
};

static ::std::map<uint64_t, MinesweeperGame> active_minesweeper_games;

// Helper to convert difficulty to mine count
inline int difficulty_to_mines(const ::std::string& diff, int total_cells) {
    ::std::string difficulty = diff;
    ::std::transform(difficulty.begin(), difficulty.end(), difficulty.begin(), ::tolower);
    
    if (difficulty == "easy" || difficulty == "e") {
        return ::std::max(1, (int)(total_cells * 0.15)); // 15% mines
    } else if (difficulty == "medium" || difficulty == "med" || difficulty == "m") {
        return ::std::max(1, (int)(total_cells * 0.25)); // 25% mines
    } else if (difficulty == "hard" || difficulty == "h") {
        return ::std::max(1, (int)(total_cells * 0.35)); // 35% mines
    } else if (difficulty == "impossible" || difficulty == "imp" || difficulty == "i") {
        return ::std::max(1, (int)(total_cells * 0.50)); // 50% mines
    }
    
    // Try to parse as number
    try {
        return ::std::stoi(difficulty);
    } catch (...) {
        return -1; // invalid
    }
}

// Generate mine positions
inline ::std::set<int> generate_mines(int total_cells, int num_mines) {
    ::std::random_device rd;
    ::std::mt19937 gen(rd());
    ::std::uniform_int_distribution<> dis(0, total_cells - 1);
    
    ::std::set<int> mines;
    while (mines.size() < (size_t)num_mines) {
        mines.insert(dis(gen));
    }
    
    return mines;
}

// Calculate multiplier for minesweeper using conditional probability
// This ensures fair payouts: each cell revealed pays based on actual risk of hitting a mine
inline double calculate_multiplier_per_safe(int total_cells, int num_mines) {
    int safe_cells = total_cells - num_mines;
    if (safe_cells <= 0) return 0.0;
    
    // Use proper risk-based multiplier: for each cell revealed, the probability of 
    // surviving that pick is (remaining_safe / remaining_total).
    // Fair cumulative multiplier for N cells = product of (remaining_total / remaining_safe) for each pick.
    // We distribute this evenly so mult_per_safe gives an approximately fair game (~3% house edge).
    // Fair full-clear multiplier = total_cells! * (total_cells - num_mines - safe_cells)! / ((total_cells - safe_cells)! * safe_cells!)
    // Simplified: product of (total-i)/(safe-i) for i=0..safe-1 = C(total, mines)
    double fair_total_mult = 1.0;
    for (int i = 0; i < safe_cells; i++) {
        fair_total_mult *= (double)(total_cells - i) / (double)(safe_cells - i);
    }
    // Apply 3% house edge
    fair_total_mult *= 0.97;
    // Distribute evenly across safe cells: mult_per_safe = (fair_total - 1.0) / safe_cells
    double mult_per_safe = (fair_total_mult - 1.0) / safe_cells;
    
    return std::max(0.05, mult_per_safe); // minimum 5% per cell
}

// Create the game board display
inline ::std::string create_board_display(const MinesweeperGame& game, bool show_all = false) {
    ::std::stringstream ss;
    
    for (int row = 0; row < game.rows; row++) {
        for (int col = 0; col < game.cols; col++) {
            int pos = row * game.cols + col;
            
            if (show_all) {
                if (game.mine_positions.count(pos)) {
                    ss << "X ";
                } else if (game.revealed_positions.count(pos)) {
                    ss << "O ";
                } else {
                    ss << "? ";
                }
            } else {
                if (game.revealed_positions.count(pos)) {
                    if (game.mine_positions.count(pos)) {
                        ss << "X "; // Hit a mine
                    } else {
                        ss << "O "; // Safe cell
                    }
                } else {
                    ss << "? "; // Unrevealed
                }
            }
        }
        ss << "\n";
    }
    
    return ss.str();
}

// Create button grid for the minesweeper game
inline ::std::vector<dpp::component> create_minesweeper_buttons(const MinesweeperGame& game, bool show_all_mines = false) {
    ::std::vector<dpp::component> rows;
    
    // Discord allows max 5 buttons per row and 5 rows
    // We need to fit x * y buttons + cashout button
    int cells_per_row = game.cols;
    int total_rows = game.rows;
    
    // Limit grid to 5x5 due to Discord button limits
    if (cells_per_row > 5 || total_rows > 4) {
        // If grid is too large, we won't create buttons
        return rows;
    }
    
    for (int row = 0; row < total_rows; row++) {
        dpp::component action_row;
        action_row.set_type(dpp::cot_action_row);
        
        for (int col = 0; col < cells_per_row; col++) {
            int pos = row * game.cols + col;
            
            dpp::component btn;
            btn.set_type(dpp::cot_button);
            
            if (game.revealed_positions.count(pos)) {
                if (game.mine_positions.count(pos)) {
                    btn.set_label("X");
                    btn.set_style(dpp::cos_danger);
                    btn.set_disabled(true);
                } else {
                    btn.set_label("O");
                    btn.set_style(dpp::cos_success);
                    btn.set_disabled(true);
                }
            } else if (show_all_mines && game.mine_positions.count(pos)) {
                // Show hidden mines on game over
                btn.set_label("X");
                btn.set_style(dpp::cos_danger);
                btn.set_disabled(true);
            } else {
                btn.set_label(::std::to_string(row * game.cols + col + 1));
                btn.set_style(dpp::cos_secondary);
                if (!game.active) {
                    btn.set_disabled(true);
                }
            }
            
            btn.set_id("mine_reveal_" + ::std::to_string(pos) + "_" + ::std::to_string(game.user_id));
            action_row.add_component(btn);
        }
        
        rows.push_back(action_row);
    }
    
    // Add cashout button as last row
    dpp::component cashout_row;
    cashout_row.set_type(dpp::cot_action_row);
    
    dpp::component cashout_btn;
    cashout_btn.set_type(dpp::cot_button);
    cashout_btn.set_style(dpp::cos_success);
    cashout_btn.set_label("cash out");
    cashout_btn.set_id("mine_cashout_" + ::std::to_string(game.user_id));
    
    if (!game.active || game.safe_cells_revealed == 0) {
        cashout_btn.set_disabled(true);
    }
    
    cashout_row.add_component(cashout_btn);
    rows.push_back(cashout_row);
    
    return rows;
}

// Build the minesweeper message (for use with ir_update_message interaction replies)
inline dpp::message build_minesweeper_message(const MinesweeperGame& game, bool show_all_mines = false) {
    double current_multiplier = 1.0 + (game.safe_cells_revealed * game.multiplier_per_safe);
    int64_t current_payout = (int64_t)(game.initial_bet * current_multiplier);
    
    ::std::string content = "MINESWEEPER";
    if (game.game_over) {
        if (game.won) {
            content += " - YOU WIN!";
        } else {
            content += " - BOOM!";
        }
    }
    content += "\n\n";
    
    content += "**Bet:** $" + format_number(game.initial_bet) + "\n";
    content += "**Grid:** " + ::std::to_string(game.cols) + "x" + ::std::to_string(game.rows) + 
               " (" + ::std::to_string(game.total_mines) + " mines)\n";
    content += "**Current multiplier:** " + ::std::to_string(current_multiplier).substr(0, 4) + "x\n";
    content += "**Current payout:** $" + format_number(current_payout) + "\n\n";
    
    if (!game.game_over && game.active) {
        content += "**Click a cell to reveal it!**";
    }
    
    dpp::message msg;
    msg.set_content(content);
    
    auto buttons = create_minesweeper_buttons(game, show_all_mines);
    for (auto& row : buttons) {
        msg.add_component(row);
    }
    
    return msg;
}

inline Command* get_minesweeper_command(Database* db) {
    static Command* bomb = new Command("bomb", "play minesweeper - reveal safe cells without hitting mines!", "gambling", {"minesweeper", "mines"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "minesweeper", 3)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait a few seconds between games"));
                return;
            }
            
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: bomb <easy|medium|hard|impossible|number> <amount> [x=3] [y=3]\nexample: bomb easy 1000\nexample: bomb 5 500 4 4"));
                return;
            }
            
            ::std::string difficulty_input = args[0];
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            int64_t bet;
            try {
                bet = parse_amount(args[1], user->wallet);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("invalid bet amount"));
                return;
            }
            
            if (bet < 100) {
                bronx::send_message(bot, event, bronx::error("minimum bet is $100"));
                return;
            }
            
            if (bet > MAX_BET) {
                bronx::send_message(bot, event, bronx::error("maximum bet is $2,000,000,000"));
                return;
            }
            
            if (bet > user->wallet) {
                bronx::send_message(bot, event, bronx::error("you don't have that much"));
                return;
            }
            
            // Parse x and y (default 3x3)
            int x = 3; // cols
            int y = 3; // rows
            
            if (args.size() >= 3) {
                try {
                    x = ::std::stoi(args[2]);
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid x value"));
                    return;
                }
            }
            
            if (args.size() >= 4) {
                try {
                    y = ::std::stoi(args[3]);
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid y value"));
                    return;
                }
            }
            
            // Validate grid size (Discord button limits: max 5 cols, max 4 rows of cells + 1 for cashout)
            if (x < 2 || x > 5) {
                bronx::send_message(bot, event, bronx::error("x must be between 2 and 5"));
                return;
            }
            
            if (y < 2 || y > 4) {
                bronx::send_message(bot, event, bronx::error("y must be between 2 and 4"));
                return;
            }
            
            int total_cells = x * y;
            int num_mines = difficulty_to_mines(difficulty_input, total_cells);
            
            if (num_mines < 1) {
                bronx::send_message(bot, event, bronx::error("invalid difficulty or mine count\nuse: easy, medium, hard, impossible, or a number"));
                return;
            }
            
            if (num_mines >= total_cells) {
                bronx::send_message(bot, event, bronx::error("too many mines! must be less than " + ::std::to_string(total_cells)));
                return;
            }
            
            // Deduct bet
            db->update_wallet(event.msg.author.id, -bet);
            
            // Log minesweeper start to history
            int64_t start_balance = db->get_wallet(event.msg.author.id);
            bronx::db::history_operations::log_gambling(db, event.msg.author.id, "started minesweeper ($" + format_number(bet) + " bet)", -bet, start_balance);
            
            // Create game
            MinesweeperGame game;
            game.user_id = event.msg.author.id;
            game.channel_id = event.msg.channel_id;
            game.initial_bet = bet;
            game.rows = y;
            game.cols = x;
            game.total_mines = num_mines;
            game.safe_cells_revealed = 0;
            game.multiplier_per_safe = calculate_multiplier_per_safe(total_cells, num_mines);
            game.mine_positions = generate_mines(total_cells, num_mines);
            game.active = true;
            game.game_over = false;
            game.won = false;
            
            // Create initial message
            ::std::string content = "MINESWEEPER\n\n";
            content += "**Bet:** $" + format_number(bet) + "\n";
            content += "**Grid:** " + ::std::to_string(x) + "x" + ::std::to_string(y) + 
                       " (" + ::std::to_string(num_mines) + " mines)\n";
            content += "**Current multiplier:** 1.00x\n";
            content += "**Current payout:** $" + format_number(bet) + "\n\n";
            
            content += "**Click a cell to reveal it!**";
            
            dpp::message msg(event.msg.channel_id, content);
            
            auto buttons = create_minesweeper_buttons(game);
            for (auto& row : buttons) {
                msg.add_component(row);
            }
            
            bot.message_create(msg, [game, event](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto sent_msg = ::std::get<dpp::message>(callback.value);
                    game.message_id = sent_msg.id;
                    active_minesweeper_games[event.msg.author.id] = game;
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "minesweeper", 3)) {
                event.reply(dpp::message().add_embed(bronx::error("slow down! wait a few seconds between games")));
                return;
            }
            
            auto diff_param = event.get_parameter("difficulty");
            ::std::string difficulty_input;
            if (std::holds_alternative<std::string>(diff_param)) {
                difficulty_input = std::get<std::string>(diff_param);
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide a difficulty")));
                return;
            }
            auto amount_param = event.get_parameter("amount");
            ::std::string amount_str;
            if (std::holds_alternative<std::string>(amount_param)) {
                amount_str = std::get<std::string>(amount_param);
            } else if (std::holds_alternative<int64_t>(amount_param)) {
                amount_str = std::to_string(std::get<int64_t>(amount_param));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide a bet amount")));
                return;
            }
            
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }
            
            int64_t bet;
            try {
                bet = parse_amount(amount_str, user->wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(bronx::error("invalid bet amount")));
                return;
            }
            
            if (bet < 100) {
                event.reply(dpp::message().add_embed(bronx::error("minimum bet is $100")));
                return;
            }
            
            if (bet > MAX_BET) {
                event.reply(dpp::message().add_embed(bronx::error("maximum bet is $2,000,000,000")));
                return;
            }
            
            if (bet > user->wallet) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that much")));
                return;
            }
            
            // Get optional x and y parameters
            int x = 3;
            int y = 3;
            
            try {
                if (event.get_parameter("x").index() != 0) {
                    x = ::std::get<int64_t>(event.get_parameter("x"));
                }
            } catch (...) {}
            
            try {
                if (event.get_parameter("y").index() != 0) {
                    y = ::std::get<int64_t>(event.get_parameter("y"));
                }
            } catch (...) {}
            
            if (x < 2 || x > 5) {
                event.reply(dpp::message().add_embed(bronx::error("x must be between 2 and 5")));
                return;
            }
            
            if (y < 2 || y > 4) {
                event.reply(dpp::message().add_embed(bronx::error("y must be between 2 and 4")));
                return;
            }
            
            int total_cells = x * y;
            int num_mines = difficulty_to_mines(difficulty_input, total_cells);
            
            if (num_mines < 1) {
                event.reply(dpp::message().add_embed(bronx::error("invalid difficulty or mine count\nuse: easy, medium, hard, impossible, or a number")));
                return;
            }
            
            if (num_mines >= total_cells) {
                event.reply(dpp::message().add_embed(bronx::error("too many mines! must be less than " + ::std::to_string(total_cells))));
                return;
            }
            
            db->update_wallet(event.command.get_issuing_user().id, -bet);
            
            MinesweeperGame game;
            game.user_id = event.command.get_issuing_user().id;
            game.channel_id = event.command.channel_id;
            game.initial_bet = bet;
            game.rows = y;
            game.cols = x;
            game.total_mines = num_mines;
            game.safe_cells_revealed = 0;
            game.multiplier_per_safe = calculate_multiplier_per_safe(total_cells, num_mines);
            game.mine_positions = generate_mines(total_cells, num_mines);
            game.active = true;
            game.game_over = false;
            game.won = false;
            
            ::std::string content = "MINESWEEPER\n\n";
            content += "**Bet:** $" + format_number(bet) + "\n";
            content += "**Grid:** " + ::std::to_string(x) + "x" + ::std::to_string(y) + 
                       " (" + ::std::to_string(num_mines) + " mines)\n";
            content += "**Current multiplier:** 1.00x\n";
            content += "**Current payout:** $" + format_number(bet) + "\n\n";
            
            content += "**Click a cell to reveal it!**";
            
            dpp::message msg;
            msg.set_content(content);
            
            auto buttons = create_minesweeper_buttons(game);
            for (auto& row : buttons) {
                msg.add_component(row);
            }
            
            event.reply(msg, [game](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto sent_msg = callback.get<dpp::message>();
                    game.message_id = sent_msg.id;
                    game.channel_id = sent_msg.channel_id;
                    active_minesweeper_games[game.user_id] = game;
                }
            });
        },
        {
            dpp::command_option(dpp::co_string, "difficulty", "easy/medium/hard/impossible or number of mines", true),
            dpp::command_option(dpp::co_string, "amount", "amount to bet (supports all, half, 50%, 1k)", true),
            dpp::command_option(dpp::co_integer, "x", "number of columns (2-5, default 3)", false),
            dpp::command_option(dpp::co_integer, "y", "number of rows (2-4, default 3)", false)
        }
    );
    
    return bomb;
}

// Register minesweeper interactions
inline void register_minesweeper_interactions(dpp::cluster& bot, Database* db) {
    // Handle minesweeper cell reveal
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.find("mine_reveal_") != 0) return;
        
        // Parse: mine_reveal_POS_USERID
        ::std::string custom_id = event.custom_id;
        size_t first_underscore = custom_id.find('_');
        size_t second_underscore = custom_id.find('_', first_underscore + 1);
        size_t third_underscore = custom_id.find('_', second_underscore + 1);
        
        int pos = ::std::stoi(custom_id.substr(second_underscore + 1, third_underscore - second_underscore - 1));
        uint64_t user_id = ::std::stoull(custom_id.substr(third_underscore + 1));
        
        if (event.command.get_issuing_user().id != user_id) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (active_minesweeper_games.find(user_id) == active_minesweeper_games.end()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game not found or expired")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        MinesweeperGame& game = active_minesweeper_games[user_id];
        
        if (!game.active) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game is already over")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Check if already revealed
        if (game.revealed_positions.count(pos)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this cell is already revealed")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Reveal the cell
        game.revealed_positions.insert(pos);
        
        // Check if it's a mine
        if (game.mine_positions.count(pos)) {
            // Hit a mine - game over, lose
            game.active = false;
            game.game_over = true;
            game.won = false;
            
            // Track gambling stats
            db->increment_stat(user_id, "gambling_losses", game.initial_bet);
            
            // Track milestone (loss)
            track_gambling_result(bot, db, event.command.channel_id, user_id, false);
            
            // Log minesweeper loss to history
            int64_t balance_after = db->get_wallet(user_id);
            bronx::db::history_operations::log_gambling(db, user_id, "lost minesweeper ($" + format_number(game.initial_bet) + ")", 0, balance_after);
            
            event.reply(dpp::ir_update_message, build_minesweeper_message(game, true));
            
            // Remove game from active games after a delay
            ::std::thread([user_id]() {
                ::std::this_thread::sleep_for(::std::chrono::seconds(30));
                active_minesweeper_games.erase(user_id);
            }).detach();
        } else {
            // Safe cell - increment counter
            game.safe_cells_revealed++;
            
            // Check if all safe cells revealed (auto-win)
            int total_safe = (game.rows * game.cols) - game.total_mines;
            if (game.safe_cells_revealed >= total_safe) {
                // All safe cells revealed - auto cashout
                game.active = false;
                game.game_over = true;
                game.won = true;
                
                double final_multiplier = 1.0 + (game.safe_cells_revealed * game.multiplier_per_safe);
                int64_t payout = (int64_t)(game.initial_bet * final_multiplier);
                
                db->update_wallet(user_id, payout);
                
                // Track gambling stats
                int64_t profit = payout - game.initial_bet;
                if (profit > 0) {
                    db->increment_stat(user_id, "gambling_profit", profit);
                    // Check gambling profit achievements
                    track_gambling_profit(bot, db, event.command.channel_id, user_id);
                }
                
                // Track milestone (win)
                track_gambling_result(bot, db, event.command.channel_id, user_id, profit > 0, profit);
                
                // Log minesweeper auto-win to history
                int64_t balance_after = db->get_wallet(user_id);
                bronx::db::history_operations::log_gambling(db, user_id, "won minesweeper ($" + format_number(profit) + " profit)", payout, balance_after);
                
                event.reply(dpp::ir_update_message, build_minesweeper_message(game, true));
                
                // Remove game from active games after a delay
                ::std::thread([user_id]() {
                    ::std::this_thread::sleep_for(::std::chrono::seconds(30));
                    active_minesweeper_games.erase(user_id);
                }).detach();
            } else {
                // Continue game
                event.reply(dpp::ir_update_message, build_minesweeper_message(game, false));
            }
        }
    });
    
    // Handle minesweeper cashout
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.find("mine_cashout_") != 0) return;
        
        ::std::string custom_id = event.custom_id;
        size_t last_underscore = custom_id.rfind('_');
        uint64_t user_id = ::std::stoull(custom_id.substr(last_underscore + 1));
        
        if (event.command.get_issuing_user().id != user_id) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (active_minesweeper_games.find(user_id) == active_minesweeper_games.end()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game not found or expired")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        MinesweeperGame& game = active_minesweeper_games[user_id];
        
        if (!game.active) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game is not active")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (game.safe_cells_revealed == 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you need to reveal at least one safe cell before cashing out")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Calculate winnings
        double final_multiplier = 1.0 + (game.safe_cells_revealed * game.multiplier_per_safe);
        int64_t payout = (int64_t)(game.initial_bet * final_multiplier);
        
        db->update_wallet(user_id, payout);
        
        // Track gambling stats
        int64_t profit = payout - game.initial_bet;
        if (profit > 0) {
            db->increment_stat(user_id, "gambling_profit", profit);
            // Check gambling profit achievements
            track_gambling_profit(bot, db, event.command.channel_id, user_id);
        }
        
        // Track milestone (win on cashout)
        track_gambling_result(bot, db, event.command.channel_id, user_id, profit > 0, profit);
        
        // Log minesweeper cashout to history
        int64_t balance_after = db->get_wallet(user_id);
        bronx::db::history_operations::log_gambling(db, user_id, "cashed out minesweeper ($" + format_number(profit) + " profit)", payout, balance_after);
        
        game.active = false;
        game.game_over = true;
        game.won = true;
        
        ::std::string content = "MINESWEEPER - CASHED OUT\n\n";
        content += "multiplier: **" + ::std::to_string(final_multiplier).substr(0, 4) + "x**\n\n";
        content += "bet: **$" + format_number(game.initial_bet) + "**\n";
        content += "payout: **$" + format_number(payout) + "**\n";
        content += "profit: **$" + format_number(payout - game.initial_bet) + "**";
        
        dpp::message msg;
        msg.set_content(content);
        
        event.reply(dpp::ir_update_message, msg);
        
        // Remove game from active games after a delay
        ::std::thread([user_id]() {
            ::std::this_thread::sleep_for(::std::chrono::seconds(30));
            active_minesweeper_games.erase(user_id);
        }).detach();
    });
}

} // namespace gambling
} // namespace commands
