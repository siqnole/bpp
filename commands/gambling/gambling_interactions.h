#pragma once
#include "../../command.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "roulette.h"
#include "blackjack.h"
#include "russian_roulette.h"
#include <dpp/dpp.h>
#include <sstream>
#include "../../log.h"
#include <thread>
#include <chrono>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

// Register all gambling button interactions (roulette, blackjack)
inline void register_roulette_blackjack_interactions(dpp::cluster& bot, Database* db) {
    // Roulette bet buttons
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        // Only handle gambling-related interactions
        if (custom_id.find("roulette_") != 0 && 
            custom_id.find("blackjack_") != 0 && 
            custom_id.find("russian_roulette_") != 0 &&
            custom_id.find("rr_") != 0 &&
            custom_id.find("minesweeper_") != 0 &&
            custom_id.find("frogger_") != 0 &&
            custom_id.find("coinflip_") != 0 &&
            custom_id.find("dice_") != 0 &&
            custom_id.find("lottery_") != 0) {
            return; // Let other handlers deal with non-gambling interactions
        }
        
        std::cout << DBG_GAMB "button_click user=" << event.command.get_issuing_user().id \
                  << " custom_id=" << custom_id << "\n";
        
        // Check for roulette bet buttons
        if (custom_id.find("roulette_bet_") == 0) {
            // Parse: roulette_bet_TYPE_gameid
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            auto it = active_roulette_games.find(game_id);
            if (it == active_roulette_games.end() || !it->second.active) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this roulette game is no longer active")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Determine bet type from button
            ::std::string bet_type;
            if (custom_id.find("_red_") != ::std::string::npos) bet_type = "Red";
            else if (custom_id.find("_black_") != ::std::string::npos) bet_type = "Black";
            else if (custom_id.find("_green_") != ::std::string::npos) bet_type = "Green";
            else if (custom_id.find("_even_") != ::std::string::npos) bet_type = "Even";
            else if (custom_id.find("_odd_") != ::std::string::npos) bet_type = "Odd";
            else if (custom_id.find("_number_") != ::std::string::npos) {
                // Show modal to get number and bet amount
                dpp::interaction_modal_response modal("roulette_number_modal_" + ::std::to_string(game_id), "Choose a Number");
                modal.add_component(
                    dpp::component()
                        .set_label("Number (0-36 or 00)")
                        .set_id("number_input")
                        .set_type(dpp::cot_text)
                        .set_placeholder("Enter 0-36 or 00")
                        .set_min_length(1)
                        .set_max_length(2)
                        .set_text_style(dpp::text_short)
                );
                modal.add_row();
                modal.add_component(
                    dpp::component()
                        .set_label("Bet Amount")
                        .set_id("bet_amount")
                        .set_type(dpp::cot_text)
                        .set_placeholder("Enter amount to bet")
                        .set_min_length(1)
                        .set_max_length(15)
                        .set_text_style(dpp::text_short)
                );
                event.dialog(modal);
                return;
            }
            
            // Show modal to get bet amount
            dpp::interaction_modal_response modal("roulette_bet_modal_" + bet_type + "_" + ::std::to_string(game_id), "Place Bet on " + bet_type);
            modal.add_component(
                dpp::component()
                    .set_label("Bet Amount")
                    .set_id("bet_amount")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Enter amount to bet (supports all, half, 50%, 1k)")
                    .set_min_length(1)
                    .set_max_length(15)
                    .set_text_style(dpp::text_short)
            );
            event.dialog(modal);
        }
        
        // Roulette spin button
        if (custom_id.find("roulette_spin_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            handle_roulette_spin(bot, db, event, game_id);
        }
        
        // Roulette cancel button
        if (custom_id.find("roulette_cancel_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            handle_roulette_cancel(bot, db, event, game_id);
        }
        
        // Roulette pagination buttons
        if (custom_id.find("roulette_page_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            auto game_it = active_roulette_games.find(game_id);
            if (game_it == active_roulette_games.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This game session has expired.")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            // Only author can navigate
            if (event.command.get_issuing_user().id != game_it->second.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Only the game author can navigate pages!")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            // Get pagination data
            auto data_it = roulette_pagination_data.find(game_id);
            if (data_it == roulette_pagination_data.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Pagination data not found.")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            auto& results = data_it->second.first;
            int result_num = data_it->second.second;
            int total_pages = results.size() <= 2 ? 1 : 1 + ((results.size() - 2 + 3) / 4);
            
            // Track current page in game state
            static ::std::map<uint64_t, int> current_pages;
            if (current_pages.find(game_id) == current_pages.end()) {
                current_pages[game_id] = 0;
            }
            
            int& current_page = current_pages[game_id];
            
            if (custom_id.find("_prev_") != ::std::string::npos) {
                if (current_page > 0) current_page--;
            } else if (custom_id.find("_next_") != ::std::string::npos) {
                if (current_page < total_pages - 1) current_page++;
            }
            
            // Update embed
            ::std::string wheel_display = get_roulette_display(result_num, 0);
            bool is_red = red_numbers.count(result_num);
            bool is_green = (result_num == 0 || result_num == 37);
            
            dpp::embed result_embed = dpp::embed()
                .set_color(is_green ? 0x00FF00 : (is_red ? 0xFF0000 : 0x000000))
                .set_title("🎰 ROULETTE RESULTS");
            
            result_embed.set_description(get_paginated_results(results, current_page, result_num, wheel_display));
            
            dpp::message result_msg;
            result_msg.add_embed(result_embed);
            
            // Update navigation buttons
            dpp::component nav_row;
            nav_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("◀ Previous")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_page_prev_" + ::std::to_string(game_id))
                .set_disabled(current_page == 0));
            
            nav_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Next ▶")
                .set_style(dpp::cos_secondary)
                .set_id("roulette_page_next_" + ::std::to_string(game_id))
                .set_disabled(current_page >= total_pages - 1));
            
            result_msg.add_component(nav_row);
            
            event.reply(dpp::ir_update_message, result_msg);
        }
        
        // Blackjack hit button
        if (custom_id.find("blackjack_hit_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t user_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            if (event.command.get_issuing_user().id != user_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            handle_blackjack_hit(bot, db, event, user_id);
        }
        
        // Blackjack stand button
        if (custom_id.find("blackjack_stand_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t user_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            if (event.command.get_issuing_user().id != user_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            handle_blackjack_stand(bot, db, event, user_id);
        }
        
        // Blackjack double down button
        if (custom_id.find("blackjack_double_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t user_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            if (event.command.get_issuing_user().id != user_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            handle_blackjack_double(bot, db, event, user_id);
        }

        // Blackjack split button
        if (custom_id.find("blackjack_split_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t user_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            if (event.command.get_issuing_user().id != user_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this is not your game!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            handle_blackjack_split(bot, db, event, user_id);
        }
        
        // ── Russian Roulette lobby buttons ──
        if (custom_id.find("russian_roulette_join_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            handle_russian_roulette_join(bot, db, event, game_id);
            return;
        }

        if (custom_id.find("russian_roulette_start_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            handle_russian_roulette_start(bot, event, game_id);
            return;
        }

        // ── Russian Roulette game buttons ──
        if (custom_id.find("rr_shoot_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(9));
            handle_rr_shoot(bot, db, event, game_id);
            return;
        }

        if (custom_id.find("rr_spin_") == 0) {
            uint64_t game_id = ::std::stoull(custom_id.substr(8));
            handle_rr_spin(bot, event, game_id);
            return;
        }
    });

    // Handle Russian Roulette player target dropdown
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        ::std::string custom_id = event.custom_id;
        if (custom_id.find("rr_target_") != 0) return;

        uint64_t game_id = ::std::stoull(custom_id.substr(10));
        handle_rr_target(bot, db, event, game_id);
    });
    
    // Handle roulette bet modals
    bot.on_form_submit([&bot, db](const dpp::form_submit_t& event) {
        try {
            ::std::string custom_id = event.custom_id;
            
            // Handle regular bet modal (Red, Black, Green, Even, Odd)
            if (custom_id.find("roulette_bet_modal_") == 0) {
            // Parse: roulette_bet_modal_TYPE_gameid
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            size_t second_last = custom_id.rfind('_', last_underscore - 1);
            ::std::string bet_type = custom_id.substr(second_last + 1, last_underscore - second_last - 1);
            
            auto it = active_roulette_games.find(game_id);
            if (it == active_roulette_games.end() || !it->second.active) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this roulette game is no longer active")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            uint64_t user_id = event.command.usr.id;
            auto user = db->get_user(user_id);
            if (!user) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("user not found")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (event.components.empty()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("no form data received")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            ::std::string bet_str;
            try {
                bet_str = ::std::get<::std::string>(event.components[0].value);
            } catch (const ::std::exception& e) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error(::std::string("failed to read bet amount: ") + e.what())).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            int64_t bet_amount;
            try {
                bet_amount = parse_amount(bet_str, user->wallet);
            } catch (const ::std::exception& e) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error(e.what())).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            if (bet_amount < 50) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("minimum bet is $50")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            if (bet_amount > MAX_BET) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("maximum bet is $2,000,000,000")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            if (bet_amount > user->wallet) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you don't have that much money!" )).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            // Deduct money and add bet
            db->update_wallet(user_id, -bet_amount);
            log_gambling(db, user_id, "bet $" + format_number(bet_amount) + " on roulette (" + bet_type + ")");;
            it->second.player_bets[user_id].bets[bet_type] += bet_amount;
            it->second.player_bets[user_id].total_bet += bet_amount;
            
            // Calculate potential winnings
            int64_t payout_multiplier = (bet_type == "Green") ? 35 : 2;
            int64_t potential_win = bet_amount * payout_multiplier;
            
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::success("Bet $" + format_number(bet_amount) + " on " + bet_type + " for a potential win of $" + format_number(potential_win))).set_flags(dpp::m_ephemeral),
                dpp::utility::log_error());
            
            // Update message after replying to avoid race condition
            update_roulette_bet_message(bot, db, game_id);
        }
        
        // Handle number bet modal
        if (custom_id.find("roulette_number_modal_") == 0) {
            size_t last_underscore = custom_id.rfind('_');
            uint64_t game_id = ::std::stoull(custom_id.substr(last_underscore + 1));
            
            auto it = active_roulette_games.find(game_id);
            if (it == active_roulette_games.end() || !it->second.active) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this roulette game is no longer active")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            uint64_t user_id = event.command.usr.id;
            auto user = db->get_user(user_id);
            if (!user) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("user not found")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if we have both components
            if (event.components.size() < 2) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("invalid form data")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            ::std::string number_str, bet_str;
            try {
                number_str = ::std::get<::std::string>(event.components[0].value);
                bet_str = ::std::get<::std::string>(event.components[1].value);
            } catch (const ::std::exception& e) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error(::std::string("failed to read form values: ") + e.what())).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            // Validate number
            int number = -1;
            if (number_str == "00") {
                number = 37;
            } else {
                try {
                    number = ::std::stoi(number_str);
                    if (number < 0 || number > 36) {
                        event.reply(dpp::ir_channel_message_with_source,
                            dpp::message().add_embed(bronx::error("invalid number! Must be 0-36 or 00")).set_flags(dpp::m_ephemeral),
                            dpp::utility::log_error());
                        return;
                    }
                } catch (...) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("invalid number!")).set_flags(dpp::m_ephemeral),
                        dpp::utility::log_error());
                    return;
                }
            }
            
            // Parse bet amount
            int64_t bet_amount;
            try {
                bet_amount = parse_amount(bet_str, user->wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("invalid bet amount")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            if (bet_amount < 50) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("minimum bet is $50")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            if (bet_amount > MAX_BET) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("maximum bet is $2,000,000,000")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            if (bet_amount > user->wallet) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you don't have that much money!")).set_flags(dpp::m_ephemeral),
                    dpp::utility::log_error());
                return;
            }
            
            // Deduct money and add bet
            db->update_wallet(user_id, -bet_amount);
            ::std::string display_number = (number == 37) ? "00" : ::std::to_string(number);
            log_gambling(db, user_id, "bet $" + format_number(bet_amount) + " on roulette (#" + display_number + ")");
            it->second.player_bets[user_id].bets[display_number] += bet_amount;
            it->second.player_bets[user_id].total_bet += bet_amount;
            
            update_roulette_bet_message(bot, db, game_id);
            
            int64_t potential_win = bet_amount * 35;
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::success("Bet $" + format_number(bet_amount) + " on " + display_number + " for a potential win of $" + format_number(potential_win))).set_flags(dpp::m_ephemeral),
                dpp::utility::log_error());
        }
        } catch (const ::std::exception& e) {
            // Catch any unhandled exceptions
            try {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error(::std::string("unexpected error: ") + e.what())).set_flags(dpp::m_ephemeral));
            } catch (...) {}
        } catch (...) {
            try {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an unexpected error occurred")).set_flags(dpp::m_ephemeral));
            } catch (...) {}
        }
    });
}

} // namespace gambling
} // namespace commands
