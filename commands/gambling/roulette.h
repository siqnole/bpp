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
#include <map>
#include <set>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream> // debug logging
#include "../../log.h"

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

// Roulette wheel numbers (American roulette)
const ::std::vector<int> roulette_wheel = {0, 28, 9, 26, 30, 11, 7, 20, 32, 17, 5, 22, 34, 15, 3, 24, 36, 13, 1, 
                                          37, 27, 10, 25, 29, 12, 8, 19, 31, 18, 6, 21, 33, 16, 4, 23, 35, 14, 2};

// Red numbers
const ::std::set<int> red_numbers = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};

// Black numbers (all non-red, non-green)
const ::std::set<int> black_numbers = {2, 4, 6, 8, 10, 11, 13, 15, 17, 20, 22, 24, 26, 28, 29, 31, 33, 35};

struct PlayerBets {
    int64_t total_bet;
    ::std::map<::std::string, int64_t> bets; // bet_type -> amount
};

struct RouletteGame {
    uint64_t author_id;
    uint64_t message_id;
    uint64_t channel_id;
    ::std::map<uint64_t, PlayerBets> player_bets; // user_id -> PlayerBets
    bool active;
};

struct PlayerResult {
    uint64_t user_id;
    int64_t net_profit;
    ::std::string result_text;
};

static ::std::map<uint64_t, RouletteGame> active_roulette_games;
static ::std::map<uint64_t, ::std::pair<::std::vector<PlayerResult>, int>> roulette_pagination_data;

// Debounce state for roulette bet message edits — prevents rate-limiting
// when multiple bets land within a short window
static ::std::map<uint64_t, std::chrono::steady_clock::time_point> roulette_last_edit_time;
static ::std::map<uint64_t, bool> roulette_edit_pending;

::std::string get_number_color_emoji(int num) {
    if (num == 0 || num == 37) return "🟢"; // Green (0 and 00)
    if (red_numbers.count(num)) return "🔴";
    return "⚫";
}

::std::string get_number_display(int num) {
    if (num == 37) return "00";
    return ::std::to_string(num);
}

::std::string get_number_color_name(int num) {
    if (num == 0 || num == 37) return "green";
    if (red_numbers.count(num)) return "red";
    return "black";
}

::std::string get_roulette_display(int result, int offset = 0) {
    ::std::string display = "";
    
    // Show wheel animation context (5 numbers before and after)
    for (int i = -5; i <= 5; i++) {
        int idx = 0;
        for (size_t j = 0; j < roulette_wheel.size(); j++) {
            if (roulette_wheel[j] == result) {
                idx = j;
                break;
            }
        }
        
        int curr_idx = (idx + i + offset + roulette_wheel.size() * 10) % roulette_wheel.size();
        int num = roulette_wheel[curr_idx];
        
        if (i == 0 && offset == 0) {
            display += "**[" + get_number_color_emoji(num) + get_number_display(num) + "]** ";
        } else {
            display += get_number_color_emoji(num) + get_number_display(num) + " ";
        }
    }
    
    return display;
}

::std::string get_paginated_results(const ::std::vector<PlayerResult>& results, int page, int result_num, const ::std::string& wheel_display) {
    ::std::string description = "";
    
    if (page == 0) {
        // First page shows wheel result
        description += "**wheel result:**\n" + wheel_display + "\n\n";

        description += "**result: " + get_number_display(result_num) + " " + get_number_color_emoji(result_num) + "**\n\n";
        description += "**results:**\n";
        
        // Show first 2 results
        for (int i = 0; i < ::std::min(2, (int)results.size()); i++) {
            description += "<@" + ::std::to_string(results[i].user_id) + ">:\n";
            description += results[i].result_text;
            if (results[i].net_profit > 0) {
                description += "  **profit: +$" + format_number(results[i].net_profit) + "** 📈\n";
            } else if (results[i].net_profit == 0) {
                description += "  **break Even**\n";
            } else {
                description += "  **loss: -$" + format_number(::std::abs(results[i].net_profit)) + "** 📉\n";
            }
            description += "\n";
        }
    } else {
        // Subsequent pages show 4 results each
        int start_idx = 2 + (page - 1) * 4;
        int end_idx = ::std::min(start_idx + 4, (int)results.size());
        int shown_count = end_idx; // Total number of results shown up to this page
        
        description += "**results (continued - " + ::std::to_string(shown_count) + "/" + ::std::to_string((int)results.size()) + "):**\n";
        for (int i = start_idx; i < end_idx; i++) {
            description += "<@" + ::std::to_string(results[i].user_id) + ">:\n";
            description += results[i].result_text;
            if (results[i].net_profit > 0) {
                description += "  **profit: +$" + format_number(results[i].net_profit) + "** 📈\n";
            } else if (results[i].net_profit == 0) {
                description += "  **break even**\n";
            } else {
                description += "  **loss: -$" + format_number(::std::abs(results[i].net_profit)) + "** 📉\n";
            }
            description += "\n";
        }
    }
    
    // Add page indicator if multiple pages
    int total_pages = results.size() <= 2 ? 1 : 1 + ((results.size() - 2 + 3) / 4);
    if (total_pages > 1) {
        description += "\n*page " + ::std::to_string(page + 1) + "/" + ::std::to_string(total_pages) + "*";
    }
    
    return description;
}

void update_roulette_bet_message(dpp::cluster& bot, Database* db, uint64_t game_id) {
    auto it = active_roulette_games.find(game_id);
    if (it == active_roulette_games.end()) return;
    
    // PERFORMANCE FIX: Debounce rapid message edits to prevent rate-limiting.
    // Multiple bets landing within 2s are coalesced into a single edit.
    auto _now = std::chrono::steady_clock::now();
    auto _last_it = roulette_last_edit_time.find(game_id);
    if (_last_it != roulette_last_edit_time.end()) {
        auto _elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_now - _last_it->second).count();
        if (_elapsed_ms < 2000) {
            if (!roulette_edit_pending[game_id]) {
                roulette_edit_pending[game_id] = true;
                bot.start_timer([&bot, db, game_id](dpp::timer t) {
                    bot.stop_timer(t);
                    roulette_edit_pending.erase(game_id);
                    update_roulette_bet_message(bot, db, game_id);
                }, 2);
            }
            return;
        }
    }
    roulette_last_edit_time[game_id] = _now;
    
    auto& game = it->second;
    
    // Check if message_id is valid
    if (game.message_id == 0) {
        // Message hasn't been created yet, skip update
        return;
    }
    
    dpp::embed embed = dpp::embed()
        .set_color(0x6A0DAD) // Purple accent
        .set_title("🎰 ROULETTE - Place Your Bets");
    
    ::std::string description = "";
    
    // Show all player bets
    if (game.player_bets.empty()) {
        description += "*No bets placed yet. Use the buttons below to place bets!*\n\n";
    } else {
        // Create a vector of bet info for sorting
        struct BetInfo {
            uint64_t user_id;
            ::std::string bet_type;
            int64_t bet_amount;
            int64_t potential_win;
        };
        
        ::std::vector<BetInfo> all_bets;
        
        for (const auto& player : game.player_bets) {
            for (const auto& bet : player.second.bets) {
                BetInfo info;
                info.user_id = player.first;
                info.bet_type = bet.first;
                info.bet_amount = bet.second;
                
                // Calculate potential winnings based on bet type
                int payout_multiplier = 2; // Default for Red/Black/Even/Odd
                if (bet.first == "Green") {
                    payout_multiplier = 35;
                } else if (bet.first != "Red" && bet.first != "Black" && 
                           bet.first != "Even" && bet.first != "Odd") {
                    // It's a number bet
                    payout_multiplier = 35;
                }
                
                info.potential_win = bet.second * payout_multiplier;
                all_bets.push_back(info);
            }
        }
        
        // Sort by potential winnings (highest risk first)
        ::std::sort(all_bets.begin(), all_bets.end(), 
                  [](const BetInfo& a, const BetInfo& b) {
                      return a.potential_win > b.potential_win;
                  });
        
        description += "**Current Bets (sorted by risk):**\n";
        for (const auto& bet_info : all_bets) {
            description += "<@" + ::std::to_string(bet_info.user_id) + "> bet **$" + 
                          format_number(bet_info.bet_amount) + "** on **" + bet_info.bet_type + 
                          "** (wins : $" + format_number(bet_info.potential_win) + ")\n";
        }
        description += "\n";
    }
    
    description += "**Betting Options:**\n";
    description += "Red (2:1) | Black (2:1) | Green (35:1)\n";
    description += "Even (2:1) | Odd (2:1)\n";
    description += "Single Number (35:1)";
    
    embed.set_description(description);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Check if the author has placed a bet
    bool author_has_bet = game.player_bets.find(game.author_id) != game.player_bets.end();
    
    // Add buttons
    dpp::component row1, row2, row3;
    row1.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Red")
        .set_style(dpp::cos_danger)
        .set_id("roulette_bet_red_" + ::std::to_string(game_id)));
    
    row1.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Black")
        .set_style(dpp::cos_secondary)
        .set_id("roulette_bet_black_" + ::std::to_string(game_id)));
    
    row1.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Green")
        .set_style(dpp::cos_success)
        .set_id("roulette_bet_green_" + ::std::to_string(game_id)));
    
    row2.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Even")
        .set_style(dpp::cos_primary)
        .set_id("roulette_bet_even_" + ::std::to_string(game_id)));
    
    row2.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Odd")
        .set_style(dpp::cos_primary)
        .set_id("roulette_bet_odd_" + ::std::to_string(game_id)));
    
    row2.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Number")
        .set_style(dpp::cos_secondary)
        .set_id("roulette_bet_number_" + ::std::to_string(game_id)));
    
    ::std::string spin_label = author_has_bet ? "Spin!" : "Spin! (Author Only)";
    row3.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label(spin_label)
        .set_style(dpp::cos_success)
        .set_id("roulette_spin_" + ::std::to_string(game_id))
        .set_disabled(!author_has_bet));
    
    row3.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Cancel")
        .set_style(dpp::cos_danger)
        .set_id("roulette_cancel_" + ::std::to_string(game_id)));
    
    msg.add_component(row1);
    msg.add_component(row2);
    msg.add_component(row3);
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;
    
    std::cout << DBG_ROUL "update_roulette_bet_message game=" << game_id << " channel=" << game.channel_id << " msg=" << game.message_id << "\n";
    bot.message_edit(msg, [game_id](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            auto err = callback.get_error();
            std::cout << DBG_ROUL "roulette message_edit callback game=" << game_id
                      << " code=" << err.code << " msg=" << err.message << "\n";
            // still log 10062 so we can see when unknown interaction occurs
        }
    });
}

// PERFORMANCE FIX: Animation + result processing extracted to run in a detached thread,
// avoiding blocking DPP's thread pool for 3.4s with sleep_for calls.
// Also adds early-exit when interaction token expires (10062) to avoid wasting REST calls.
void roulette_spin_worker(dpp::cluster& bot, Database* db, uint64_t game_id, int result,
                           std::string spin_token, uint64_t message_id, uint64_t channel_id,
                           ::std::map<uint64_t, PlayerBets> player_bets) {
    auto token_valid = std::make_shared<std::atomic<bool>>(true);
    auto check_edit = [token_valid](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            if (cb.get_error().code == 10062) {
                token_valid->store(false);
                std::cout << DBG_ROUL "interaction token expired, skipping remaining frames\n";
            } else {
                std::cout << "[ERROR] roulette anim edit: " << cb.get_error().message << std::endl;
            }
        }
    };

    // Frame 1 — initial spin
    dpp::message anim_msg;
    anim_msg.id = message_id;
    anim_msg.channel_id = channel_id;
    anim_msg.add_embed(dpp::embed()
        .set_color(0x1E90FF)
        .set_title("🎰 SPINNING THE WHEEL...")
        .set_description("The wheel is spinning!\n\n" + get_roulette_display(result, 15)));
    bot.interaction_response_edit(spin_token, anim_msg, check_edit);

    // Frame 2
    ::std::this_thread::sleep_for(::std::chrono::milliseconds(800));
    if (token_valid->load()) {
        anim_msg.embeds.clear();
        anim_msg.add_embed(dpp::embed()
            .set_color(0x1E90FF)
            .set_title("🎰 SPINNING THE WHEEL...")
            .set_description("The wheel is spinning!\n\n" + get_roulette_display(result, 10)));
        bot.interaction_response_edit(spin_token, anim_msg, check_edit);
    }

    // Frame 3
    ::std::this_thread::sleep_for(::std::chrono::milliseconds(800));
    if (token_valid->load()) {
        anim_msg.embeds.clear();
        anim_msg.add_embed(dpp::embed()
            .set_color(0x1E90FF)
            .set_title("🎰 SPINNING THE WHEEL...")
            .set_description("The wheel is spinning!\n\n" + get_roulette_display(result, 5)));
        bot.interaction_response_edit(spin_token, anim_msg, check_edit);
    }

    // Frame 4 — slowing down
    ::std::this_thread::sleep_for(::std::chrono::milliseconds(800));
    if (token_valid->load()) {
        anim_msg.embeds.clear();
        anim_msg.add_embed(dpp::embed()
            .set_color(0x1E90FF)
            .set_title("🎰 SLOWING DOWN...")
            .set_description("Almost there...\n\n" + get_roulette_display(result, 2)));
        bot.interaction_response_edit(spin_token, anim_msg, check_edit);
    }

    ::std::this_thread::sleep_for(::std::chrono::milliseconds(1000));

    // Determine result properties
    bool is_red = red_numbers.count(result);
    bool is_black = black_numbers.count(result);
    bool is_green = (result == 0 || result == 37);
    bool is_even = (result != 0 && result != 37 && result % 2 == 0);
    bool is_odd = (result != 0 && result != 37 && result % 2 == 1);

    // Calculate winnings for all players and create result objects
    ::std::vector<PlayerResult> player_results;

    for (auto& player_entry : player_bets) {
        uint64_t player_id = player_entry.first;
        auto& pbets = player_entry.second;

        int64_t total_winnings = 0;
        ::std::string result_text = "";

        for (const auto& bet : pbets.bets) {
            ::std::string bet_type = bet.first;
            int64_t amount = bet.second;
            bool won = false;
            int payout_multiplier = 0;

            if (bet_type == "Red" && is_red) {
                won = true;
                payout_multiplier = 2;
            } else if (bet_type == "Black" && is_black) {
                won = true;
                payout_multiplier = 2;
            } else if (bet_type == "Green" && is_green) {
                won = true;
                payout_multiplier = 35;
            } else if (bet_type == "Even" && is_even) {
                won = true;
                payout_multiplier = 2;
            } else if (bet_type == "Odd" && is_odd) {
                won = true;
                payout_multiplier = 2;
            } else if (bet_type == get_number_display(result)) {
                won = true;
                payout_multiplier = 35;
            }

            if (won) {
                total_winnings += amount * payout_multiplier;
                result_text += "  " + bronx::EMOJI_CHECK + " " + bet_type + ": +$" + format_number(amount * payout_multiplier) + "\n";
            } else {
                result_text += "  " + bronx::EMOJI_DENY + " " + bet_type + ": -$" + format_number(amount) + "\n";
            }
        }

        int64_t net_profit = total_winnings - pbets.total_bet;
        db->update_wallet(player_id, total_winnings);

        // Track gambling stats
        if (net_profit > 0) {
            db->increment_stat(player_id, "gambling_profit", net_profit);
            track_gambling_profit(bot, db, channel_id, player_id);
            track_gambling_result(bot, db, channel_id, player_id, true, net_profit);
        } else if (net_profit < 0) {
            db->increment_stat(player_id, "gambling_losses", -net_profit);
            track_gambling_result(bot, db, channel_id, player_id, false, net_profit);
        }

        // Log gambling history
        if (net_profit > 0) {
            log_gambling(db, player_id, "won roulette for $" + format_number(net_profit));
        } else if (net_profit < 0) {
            log_gambling(db, player_id, "lost roulette for $" + format_number(-net_profit));
        } else {
            log_gambling(db, player_id, "broke even in roulette");
        }

        PlayerResult pr;
        pr.user_id = player_id;
        pr.net_profit = net_profit;
        pr.result_text = result_text;
        player_results.push_back(pr);
    }

    // Sort: Winners first (by smallest win), then losers (by smallest loss)
    ::std::sort(player_results.begin(), player_results.end(), [](const PlayerResult& a, const PlayerResult& b) {
        if (a.net_profit > 0 && b.net_profit > 0) return a.net_profit < b.net_profit;
        if (a.net_profit < 0 && b.net_profit < 0) return a.net_profit > b.net_profit;
        if (a.net_profit > 0) return true;
        if (b.net_profit > 0) return false;
        return false;
    });

    // Create result embed with pagination if needed
    ::std::string wheel_display = get_roulette_display(result, 0);
    int total_pages = player_results.size() <= 2 ? 1 : 1 + ((player_results.size() - 2 + 3) / 4);

    dpp::embed result_embed = dpp::embed()
        .set_color(is_green ? 0x00FF00 : (is_red ? 0xFF0000 : 0x000000))
        .set_title("🎰 ROULETTE RESULTS");

    result_embed.set_description(get_paginated_results(player_results, 0, result, wheel_display));

    dpp::message result_msg;
    result_msg.id = message_id;
    result_msg.channel_id = channel_id;
    result_msg.add_embed(result_embed);

    // Add pagination buttons if needed
    if (total_pages > 1) {
        dpp::component nav_row;
        nav_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("◀ Previous")
            .set_style(dpp::cos_secondary)
            .set_id("roulette_page_prev_" + ::std::to_string(game_id))
            .set_disabled(true));

        nav_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Next ▶")
            .set_style(dpp::cos_secondary)
            .set_id("roulette_page_next_" + ::std::to_string(game_id)));

        result_msg.add_component(nav_row);

        // Store results in the global pagination map
        roulette_pagination_data[game_id] = {player_results, result};
    }

    bot.interaction_response_edit(spin_token, result_msg);

    // Clean up game state and debounce maps
    if (total_pages == 1) {
        active_roulette_games.erase(game_id);
    }
    roulette_last_edit_time.erase(game_id);
    roulette_edit_pending.erase(game_id);
}

void handle_roulette_spin(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t game_id) {
    std::cout << DBG_ROUL "handle_roulette_spin game=" << game_id << " user=" << event.command.get_issuing_user().id << "\n";
    auto it = active_roulette_games.find(game_id);
    if (it == active_roulette_games.end() || !it->second.active) {
        std::cout << DBG_ROUL "roulette spin inactive game_id=" << game_id << "\n";
        event.reply(dpp::ir_update_message, dpp::message().set_content("This roulette game is no longer active."), dpp::utility::log_error());
        return;
    }
    
    auto& game = it->second;
    
    // Check if the user is the author
    if (event.command.get_issuing_user().id != game.author_id) {
        std::cout << DBG_ROUL "roulette spin unauthorized user=" << event.command.get_issuing_user().id << " game_id=" << game_id << "\n";
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("Only the game author can start the spin!")).set_flags(dpp::m_ephemeral),
            dpp::utility::log_error());
        return;
    }

    // defer the interaction right away so we have plenty of time for the animation
    event.reply(dpp::ir_deferred_update_message, ::std::string(), dpp::utility::log_error());

    game.active = false;
    
    // Spin the wheel - use static generator for better randomness
    static ::std::random_device rd;
    static ::std::mt19937 gen(rd());
    ::std::uniform_int_distribution<> dis(0, 37); // 0-36 + 37 for 00
    int result = dis(gen);
    
    // Debug log the result for tracking distribution
    std::cout << DBG_ROUL "roulette_spin result=" << result 
              << " display=" << get_number_display(result)
              << " color=" << get_number_color_name(result) << std::endl;
    
    // Initial spinning embed (edit after defer using interaction webhook to avoid 50013)
    // PERFORMANCE FIX: Run animation + result processing in a detached thread to
    // avoid blocking DPP's thread pool for 3.4s with sleep_for calls.
    std::thread([&bot, db, game_id, result,
                 spin_token = std::string(event.command.token),
                 message_id = game.message_id,
                 channel_id = game.channel_id,
                 player_bets = game.player_bets]() {
        roulette_spin_worker(bot, db, game_id, result, spin_token, message_id, channel_id, player_bets);
    }).detach();
}

void handle_roulette_cancel(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t game_id) {
    std::cout << DBG_ROUL "handle_roulette_cancel game=" << game_id << " user=" << event.command.get_issuing_user().id << "\n";
    auto it = active_roulette_games.find(game_id);
    if (it == active_roulette_games.end()) {
        std::cout << DBG_ROUL "roulette cancel inactive game_id=" << game_id << "\n";
        event.reply(dpp::ir_update_message, dpp::message().set_content("This roulette game is no longer active."), dpp::utility::log_error());
        return;
    }
    
    auto& game = it->second;
    
    // Only author can cancel
    if (event.command.get_issuing_user().id != game.author_id) {        std::cout << DBG_ROUL "roulette cancel unauthorized user=" << event.command.get_issuing_user().id << " game_id=" << game_id << "\n";        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("Only the game author can cancel!")).set_flags(dpp::m_ephemeral),
            dpp::utility::log_error());
        return;
    }

    // defer so we won't hit interaction timeout if refund loop takes a bit
    event.reply(dpp::ir_deferred_update_message, ::std::string(), dpp::utility::log_error());
    
    // Refund all players
    for (const auto& player_entry : game.player_bets) {
        db->update_wallet(player_entry.first, player_entry.second.total_bet);
        log_gambling(db, player_entry.first, "refunded $" + format_number(player_entry.second.total_bet) + " from cancelled roulette");
    }
    
    dpp::embed embed = dpp::embed()
        .set_color(0x808080)
        .set_title("🎰 Roulette Cancelled")
        .set_description("All bets have been refunded to all players.");
    
    event.edit_response(dpp::message().add_embed(embed), dpp::utility::log_error());
    
    active_roulette_games.erase(game_id);
}

inline Command* get_roulette_command(Database* db) {
    static Command* roulette = new Command("roulette", "start a roulette game - everyone can bet!", "gambling", {"rlt"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Create game
            RouletteGame game;
            game.author_id = event.msg.author.id;
            game.active = true;
            
            // Generate unique game ID (timestamp + author ID)
            uint64_t game_id = (uint64_t)::std::chrono::system_clock::now().time_since_epoch().count() + event.msg.author.id;
            
            // Create interactive message
            dpp::embed embed = dpp::embed()
                .set_color(0x1E90FF)
                .set_title("CLASSIC ROULETTE");
            
            ::std::string description = "";
            description += "*No bets placed yet. Use the buttons below to place bets!*\n\n";
            description += "**Betting Options:**\n";
            description += "Red (2:1) | Black (2:1) | Green (35:1)\n";
            description += "Even (2:1) | Odd (2:1)\n";
            description += "Single Number (35:1)";
            
            embed.set_description(description);
            
            dpp::message msg;
            msg.add_embed(embed);
            
            // Add buttons
            dpp::component row1, row2, row3;
            ::std::string game_id_str = ::std::to_string(game_id);
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Red")
                .set_style(dpp::cos_danger)
                .set_id("roulette_bet_red_" + game_id_str));
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Black")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_bet_black_" + game_id_str));
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Green")
                .set_style(dpp::cos_success)
                .set_id("roulette_bet_green_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Even")
                .set_style(dpp::cos_primary)
                .set_id("roulette_bet_even_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Odd")
                .set_style(dpp::cos_primary)
                .set_id("roulette_bet_odd_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Number")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_bet_number_" + game_id_str));
            
            row3.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Spin!")
                .set_style(dpp::cos_success)
                .set_id("roulette_spin_" + game_id_str)
                .set_disabled(true));
            
            row3.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Cancel")
                .set_style(dpp::cos_danger)
                .set_id("roulette_cancel_" + game_id_str));
            
            msg.add_component(row1);
            msg.add_component(row2);
            msg.add_component(row3);
            
            bot.message_create(msg.set_channel_id(event.msg.channel_id), [game, game_id, channel_id = event.msg.channel_id](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) return;
                
                auto sent_msg = callback.get<dpp::message>();
                game.message_id = sent_msg.id;
                game.channel_id = channel_id;
                active_roulette_games[game_id] = game;
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Create game
            RouletteGame game;
            game.author_id = event.command.get_issuing_user().id;
            game.active = true;
            
            // Generate unique game ID (timestamp + author ID)
            uint64_t game_id = (uint64_t)::std::chrono::system_clock::now().time_since_epoch().count() + event.command.get_issuing_user().id;
            
            dpp::embed embed = dpp::embed()
                .set_color(0x6A0DAD) // Purple accent
                .set_title("🎰 ROULETTE - Place Your Bets");
            
            ::std::string description = "";
            description += "*No bets placed yet. Use the buttons below to place bets!*\n\n";
            description += "**Betting Options:**\n";
            description += "Red (2:1) | Black (2:1) | Green (35:1)\n";
            description += "Even (2:1) | Odd (2:1)\n";
            description += "Single Number (35:1)";
            
            embed.set_description(description);
            
            dpp::message msg;
            msg.add_embed(embed);
            
            // Add buttons
            dpp::component row1, row2, row3;
            ::std::string game_id_str = ::std::to_string(game_id);
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Red")
                .set_style(dpp::cos_danger)
                .set_id("roulette_bet_red_" + game_id_str));
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Black")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_bet_black_" + game_id_str));
            
            row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Green")
                .set_style(dpp::cos_success)
                .set_id("roulette_bet_green_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Even")
                .set_style(dpp::cos_primary)
                .set_id("roulette_bet_even_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Odd")
                .set_style(dpp::cos_primary)
                .set_id("roulette_bet_odd_" + game_id_str));
            
            row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Number")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_bet_number_" + game_id_str));
            
            row3.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Spin!")
                .set_style(dpp::cos_success)
                .set_id("roulette_spin_" + game_id_str)
                .set_disabled(true));
            
            row3.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Cancel")
                .set_style(dpp::cos_danger)
                .set_id("roulette_cancel_" + game_id_str));
            
            msg.add_component(row1);
            msg.add_component(row2);
            msg.add_component(row3);
            
            // Store the game first with channel_id (message_id will be set after we fetch it)
            game.channel_id = event.command.channel_id;
            game.message_id = 0; // Will be set after we get the response
            active_roulette_games[game_id] = game;
            
            event.reply(msg, [&bot, game_id, token = event.command.token](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    active_roulette_games.erase(game_id);
                    return;
                }
                
                // Fetch the actual message to get its ID
                bot.interaction_response_get_original(token, [game_id](const dpp::confirmation_callback_t& resp_callback) {
                    if (resp_callback.is_error()) {
                        std::cout << DBG_ROUL "roulette interaction_response_get_original failed: " << resp_callback.get_error().message << "\n";
                        return;
                    }
                    auto msg = resp_callback.get<dpp::message>();
                    auto it = active_roulette_games.find(game_id);
                    if (it != active_roulette_games.end()) {
                        it->second.message_id = msg.id;
                        std::cout << DBG_ROUL "roulette slash command message_id set to " << msg.id << "\n";
                    }
                });
            });
        },
        {});
    
    return roulette;
}

} // namespace gambling
} // namespace commands
