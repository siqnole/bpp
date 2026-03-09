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
#include <vector>
#include <iostream>  // debug logging
#include "../../log.h"

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

struct Card {
    ::std::string suit; // ♠ ♥ ♦ ♣
    ::std::string rank; // A, 2-10, J, Q, K
    int value;
    
    ::std::string display() const {
        return rank + suit;
    }
};

struct BlackjackGame {
    uint64_t user_id;
    uint64_t message_id;
    uint64_t channel_id = 0;

    // Bets are tracked per hand so we can support splits. `bet` is the
    // original (or first hand) wager. `bet2` will be populated when the
    // player splits; it is equal to `bet` (or modified if the player doubles
    // down on the second hand).
    int64_t bet;   // first hand amount
    int64_t bet2;  // second hand amount (0 if not splitting)

    ::std::vector<Card> player_hand;
    ::std::vector<Card> player_hand2; // used when split
    ::std::vector<Card> dealer_hand;
    ::std::vector<Card> deck;

    bool active;
    bool player_stood;

    // Track which hand the player is currently playing (0 or 1). If
    // `split` is false this value is ignored (always 0).
    bool split;
    int current_hand;

    // Double‑down flags for each hand. Doubling simply multiplies the
    // corresponding bet value and deducts the extra amount from the wallet.
    bool doubled_down;
    bool doubled_down2;
};

static ::std::map<uint64_t, BlackjackGame> active_blackjack_games;

Card create_card(const ::std::string& rank, const ::std::string& suit) {
    Card card;
    card.rank = rank;
    card.suit = suit;
    
    if (rank == "A") {
        card.value = 11; // Ace can be 1 or 11
    } else if (rank == "J" || rank == "Q" || rank == "K") {
        card.value = 10;
    } else {
        card.value = ::std::stoi(rank);
    }
    
    return card;
}

::std::vector<Card> create_deck() {
    ::std::vector<Card> deck;
    ::std::vector<::std::string> suits = {"♠", "♥", "♦", "♣"};
    ::std::vector<::std::string> ranks = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    
    // standard 52-card deck
    for (const auto& suit : suits) {
        for (const auto& rank : ranks) {
            deck.push_back(create_card(rank, suit));
        }
    }
    
    return deck;
}

void shuffle_deck(::std::vector<Card>& deck) {
    ::std::random_device rd;
    ::std::mt19937 gen(rd());
    ::std::shuffle(deck.begin(), deck.end(), gen);
}

int calculate_hand_value(const ::std::vector<Card>& hand) {
    int value = 0;
    int aces = 0;
    
    for (const auto& card : hand) {
        value += card.value;
        if (card.rank == "A") {
            aces++;
        }
    }
    
    // Adjust for aces (count as 1 instead of 11 if busting)
    while (value > 21 && aces > 0) {
        value -= 10;
        aces--;
    }
    
    return value;
}

::std::string hand_to_string(const ::std::vector<Card>& hand, bool hide_first = false) {
    ::std::string result = "";
    for (size_t i = 0; i < hand.size(); i++) {
        if (i == 0 && hide_first) {
            result += "🂠 ";
        } else {
            result += hand[i].display() + " ";
        }
    }
    return result;
}

bool is_blackjack(const ::std::vector<Card>& hand) {
    return hand.size() == 2 && calculate_hand_value(hand) == 21;
}

// Builds the current blackjack game message for user_id and returns it.
// Does NOT send or edit anything; callers are responsible for delivery.
dpp::message build_blackjack_message(Database* db, uint64_t user_id, bool game_over = false) {
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end()) return {};
    auto& game = it->second;

    int player_value = calculate_hand_value(game.player_hand);
    int dealer_value = calculate_hand_value(game.dealer_hand);

    dpp::embed embed = dpp::embed()
        .set_color(0x6A0DAD) // Purple accent
        .set_title("BLACKJACK");

    // Show bet information
    ::std::string description = "**Bet: $" + format_number(game.bet);
    if (game.split) {
        description += " (per hand)";
    }
    description += "**\n\n";

    // Show dealer hand
    if (game_over) {
        description += "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n";
        description += "**Dealer Value:** " + ::std::to_string(dealer_value) + "\n\n";
    } else {
        description += "**Dealer's Hand:** " + hand_to_string(game.dealer_hand, true) + "\n";
        description += "**Dealer Value:** ?\n\n";
    }

    // Show player hands. When split we render both and highlight the current one.
    if (!game.split) {
        description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
        description += "**Your Value:** " + ::std::to_string(player_value);
    } else {
        description += "**Your Hand (1)";
        if (game.current_hand == 0) description += " [current]";
        description += ":** " + hand_to_string(game.player_hand) + "\n";
        description += "**Value:** " + ::std::to_string(calculate_hand_value(game.player_hand)) + "\n\n";

        description += "**Your Hand (2)";
        if (game.current_hand == 1) description += " [current]";
        description += ":** " + hand_to_string(game.player_hand2) + "\n";
        description += "**Value:** " + ::std::to_string(calculate_hand_value(game.player_hand2));

        player_value = (game.current_hand == 0 ? calculate_hand_value(game.player_hand)
                                               : calculate_hand_value(game.player_hand2));
    }

    if (player_value == 21) {
        const ::std::vector<Card>& checkHand =
            (game.split && game.current_hand == 1) ? game.player_hand2 : game.player_hand;
        if (is_blackjack(checkHand)) {
            description += " **BLACKJACK!** 🎉";
        } else {
            description += " ✨";
        }
    } else if (player_value > 21) {
        description += " **BUST!** 💥";
    }

    embed.set_description(description);

    dpp::message msg;
    msg.add_embed(embed);

    if (!game_over && player_value <= 21) {
        dpp::component row;
        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("hit")
            .set_style(dpp::cos_success)
            .set_id("blackjack_hit_" + ::std::to_string(user_id))
            .set_emoji("🎴"));

        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("stand")
            .set_style(dpp::cos_primary)
            .set_id("blackjack_stand_" + ::std::to_string(user_id))
            .set_emoji("✋"));

        if (!game.split) {
            if (game.player_hand.size() == 2 && !game.doubled_down) {
                auto user_obj = db->get_user(user_id);
                if (user_obj && user_obj->wallet >= game.bet) {
                    row.add_component(dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("Double Down")
                        .set_style(dpp::cos_secondary)
                        .set_id("blackjack_double_" + ::std::to_string(user_id))
                        .set_emoji("💰"));
                }
            }
        } else {
            if (game.current_hand == 0 && game.player_hand.size() == 2 && !game.doubled_down) {
                auto user_obj = db->get_user(user_id);
                if (user_obj && user_obj->wallet >= game.bet) {
                    row.add_component(dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("Double Down")
                        .set_style(dpp::cos_secondary)
                        .set_id("blackjack_double_" + ::std::to_string(user_id))
                        .set_emoji("💰"));
                }
            } else if (game.current_hand == 1 && game.player_hand2.size() == 2 && !game.doubled_down2) {
                auto user_obj = db->get_user(user_id);
                if (user_obj && user_obj->wallet >= game.bet2) {
                    row.add_component(dpp::component()
                        .set_type(dpp::cot_button)
                        .set_label("Double Down")
                        .set_style(dpp::cos_secondary)
                        .set_id("blackjack_double_" + ::std::to_string(user_id))
                        .set_emoji("💰"));
                }
            }
        }

        if (!game.split && game.player_hand.size() == 2 &&
            game.player_hand[0].value == game.player_hand[1].value) {
            auto user_obj = db->get_user(user_id);
            if (user_obj && user_obj->wallet >= game.bet) {
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("split")
                    .set_style(dpp::cos_secondary)
                    .set_id("blackjack_split_" + ::std::to_string(user_id))
                    .set_emoji("🔀"));
            }
        }

        msg.add_component(row);
    }
    return msg;
}

// Edits the original channel message directly (used for initial game display
// and for async paths that don't have a live interaction to reply to).
void update_blackjack_message(dpp::cluster& bot, Database* db, uint64_t user_id, bool game_over = false) {
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end()) return;
    auto& game = it->second;
    // if we haven't yet received the message create callback the IDs will be
    // zero; don't attempt edits until we know where the game message lives
    if (game.message_id == 0 || game.channel_id == 0) return;

    dpp::message msg = build_blackjack_message(db, user_id, game_over);
    if (msg.embeds.empty() && msg.content.empty()) return;
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;

    std::cout << DBG_BJ "update_blackjack_message user=" << user_id << " game_over=" << game_over << "\n";
    bot.message_edit(msg, [user_id](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            std::cout << DBG_BJ "blackjack message_edit failed for user " << user_id
                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
        }
    });
}

// Responds to a button interaction with the current game state using
// ir_update_message (a single direct reply, no deferred-update path).
// This avoids the DPP 10.1.x variant exception that fires when processing
// Discord's 204 response to ir_deferred_update_message.
void reply_blackjack_update(Database* db, const dpp::button_click_t& event,
                             uint64_t user_id, bool game_over = false) {
    dpp::message msg = build_blackjack_message(db, user_id, game_over);
    if (msg.embeds.empty() && msg.content.empty()) return;
    std::cout << DBG_BJ "reply_blackjack_update user=" << user_id << " game_over=" << game_over << "\n";
    event.reply(dpp::ir_update_message, msg,
                [user_id](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        std::cout << DBG_BJ "reply_blackjack_update failed for user " << user_id
                                  << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                    }
                });
}

void finish_blackjack_game(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id) {
    std::cout << DBG_BJ "finish_blackjack_game called for user " << user_id << "\n";
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end() || !it->second.active) return;
    
    auto& game = it->second;
    game.active = false;
    
    int dealer_value = calculate_hand_value(game.dealer_hand);
    // Dealer draws until 17 or higher
    while (dealer_value < 17) {
        game.dealer_hand.push_back(game.deck.back());
        game.deck.pop_back();
        dealer_value = calculate_hand_value(game.dealer_hand);
    }

    // Helper to evaluate one hand against the dealer
    auto evaluate_hand = [&](const ::std::vector<Card>& hand, int64_t bet_amount) {
        int value = calculate_hand_value(hand);
        bool blackjack = is_blackjack(hand);
        bool bust = value > 21;
        bool dealer_blackjack = is_blackjack(game.dealer_hand);
        bool dealer_bust = dealer_value > 21;

        int64_t win = 0;
        ::std::string txt;
        if (bust) {
            txt = "**You busted!** 💥\nYou lost $" + format_number(bet_amount);
        } else if (dealer_bust) {
            win = bet_amount * 2;
            txt = "**Dealer busted!** 🎉\nYou won $" + format_number(bet_amount) + "!";
        } else if (blackjack && !dealer_blackjack) {
            win = static_cast<int64_t>(bet_amount * 2.5);
            txt = "**BLACKJACK!** 🎰\nYou won $" + format_number(static_cast<int64_t>(bet_amount * 1.5)) + "!";
        } else if (dealer_blackjack && !blackjack) {
            txt = "**Dealer has blackjack!** 😔\nYou lost $" + format_number(bet_amount);
        } else if (value > dealer_value) {
            win = bet_amount * 2;
            txt = "**You win!** 🎉\nYou won $" + format_number(bet_amount) + "!";
        } else if (dealer_value > value) {
            txt = "**Dealer wins!** 😔\nYou lost $" + format_number(bet_amount);
        } else {
            win = bet_amount;
            txt = "**Push!** 🤝\nYour bet has been returned.";
        }
        return std::make_pair(win, txt);
    };

    int64_t total_winnings = 0;
    ::std::string result_text;
    dpp::embed embed;

    if (!game.split) {
        // original single-hand flow
        auto [win, txt] = evaluate_hand(game.player_hand, game.bet);
        total_winnings = win;
        result_text = txt;
    } else {
        // evaluate both hands and accumulate results
        ::std::string part1;
        auto [win1, txt1] = evaluate_hand(game.player_hand, game.bet);
        part1 = "**Hand 1:** " + txt1;
        ::std::string part2;
        auto [win2, txt2] = evaluate_hand(game.player_hand2, game.bet2);
        part2 = "\n**Hand 2:** " + txt2;
        total_winnings = win1 + win2;
        result_text = part1 + part2;
    }

    db->update_wallet(user_id, total_winnings);
    
    // Track gambling stats
    int64_t total_bet = game.split ? (game.bet + game.bet2) : game.bet;
    bool won = total_winnings > total_bet;
    if (total_winnings == 0) {
        // Lost everything
        db->increment_stat(user_id, "gambling_losses", total_bet);
    } else if (total_winnings > total_bet) {
        // Won profit
        int64_t profit = total_winnings - total_bet;
        db->increment_stat(user_id, "gambling_profit", profit);
        // Check gambling profit achievements
        track_gambling_profit(bot, db, event.command.channel_id, user_id);
    }
    // Push: no stat change
    
    // Track milestone
    track_gambling_result(bot, db, event.command.channel_id, user_id, won, total_winnings - total_bet);
    
    // Log gambling history
    if (total_winnings == 0) {
        log_gambling(db, user_id, "lost blackjack for $" + format_number(total_bet));
    } else if (total_winnings == total_bet) {
        log_gambling(db, user_id, "pushed blackjack (returned $" + format_number(total_bet) + ")");
    } else {
        int64_t profit = total_winnings - total_bet;
        log_gambling(db, user_id, "won blackjack for $" + format_number(profit));
    }

    ::std::string description = "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n";
    description += "**Dealer Value:** " + ::std::to_string(dealer_value) + "\n\n";
    if (!game.split) {
        int player_value = calculate_hand_value(game.player_hand);
        description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
        description += "**Your Value:** " + ::std::to_string(player_value) + "\n\n";
    } else {
        description += "**Your Hand (1):** " + hand_to_string(game.player_hand) + "\n";
        description += "**Value:** " + ::std::to_string(calculate_hand_value(game.player_hand)) + "\n\n";
        description += "**Your Hand (2):** " + hand_to_string(game.player_hand2) + "\n";
        description += "**Value:** " + ::std::to_string(calculate_hand_value(game.player_hand2)) + "\n\n";
    }
    description += result_text;
    
    auto user_obj = db->get_user(user_id);
    if (user_obj) {
        description += "\n\n**New Balance:** $" + format_number(user_obj->wallet);
    }
    
    // choose embed colour/title based on aggregated result
    if (!game.split) {
        // reuse previous logic for embed type
        bool player_blackjack = is_blackjack(game.player_hand);
        bool dealer_blackjack = is_blackjack(game.dealer_hand);
        int player_value = calculate_hand_value(game.player_hand);
        bool player_bust = player_value > 21;
        bool dealer_bust = dealer_value > 21;
        if (player_bust) embed = bronx::error("🃏 BLACKJACK - BUST!");
        else if (dealer_bust) embed = bronx::success("🃏 BLACKJACK - YOU WIN!");
        else if (player_blackjack && !dealer_blackjack) embed = bronx::success("🃏 BLACKJACK!");
        else if (dealer_blackjack && !player_blackjack) embed = bronx::error("🃏 DEALER BLACKJACK");
        else if (player_value > dealer_value) embed = bronx::success("🃏 BLACKJACK - YOU WIN!");
        else if (dealer_value > player_value) embed = bronx::error("🃏 BLACKJACK - DEALER WINS");
        else embed = dpp::embed().set_color(0xFFFF00).set_title("🃏 BLACKJACK - PUSH");
    } else {
        // simple generic embed for split results
        embed = dpp::embed()
            .set_color(0x6A0DAD)
            .set_title("🃏 BLACKJACK - SPLIT RESULTS");
    }

    embed.set_description(description);
    
    std::cout << DBG_BJ "finish_blackjack_game replying for user " << user_id << "\n";
    // Reply directly with ir_update_message — avoids the deferred-update path
    // that triggers a DPP 10.1.x variant exception on the 204 response.
    event.reply(dpp::ir_update_message, dpp::message().add_embed(embed),
                [user_id](const dpp::confirmation_callback_t& cb){
                    if (cb.is_error()) {
                        std::cout << DBG_BJ "finish_blackjack_game reply failed for user " << user_id
                                  << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                    }
                });
    
    // Clean up
    active_blackjack_games.erase(user_id);
}
void handle_blackjack_hit(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id) {
    std::cout << DBG_BJ "handle_blackjack_hit for user " << user_id << "\n";
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end() || !it->second.active) {
        std::cout << DBG_BJ "hit handler inactive reply for user " << user_id << "\n";
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This blackjack game is no longer active.")).set_flags(dpp::m_ephemeral),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "blackjack inactive-reply failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        return;
    }
    
    auto& game = it->second;

    // Determine which hand to operate on
    auto& hand = (game.split && game.current_hand == 1) ? game.player_hand2 : game.player_hand;

    // Deal card to the appropriate hand
    hand.push_back(game.deck.back());
    game.deck.pop_back();
    
    int player_value = calculate_hand_value(hand);
    
    if (player_value > 21) {
        // Busted
        if (game.split && game.current_hand == 0) {
            // move to second hand
            game.current_hand = 1;
            reply_blackjack_update(db, event, user_id);
            return;
        }
        
        game.active = false;
        
        dpp::embed embed = bronx::error("🃏 BLACKJACK - BUST!");
        ::std::string description = "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n\n";
        if (!game.split) {
            description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
            description += "**Your Value:** " + ::std::to_string(player_value) + " **BUST!** 💥\n\n";
            description += "You lost $" + format_number(game.bet);
        } else {
            int64_t lostBet = (game.current_hand == 0 ? game.bet : game.bet2);
            description += "**Your Hand (" + ::std::to_string(game.current_hand + 1) + "):** " + hand_to_string(hand) + "\n";
            description += "**Your Value:** " + ::std::to_string(player_value) + " **BUST!** 💥\n\n";
            description += "You lost $" + format_number(lostBet);
        }
        
        auto user_obj = db->get_user(user_id);
        if (user_obj) {
            description += "\n\n**New Balance:** $" + format_number(user_obj->wallet);
        }
        
        embed.set_description(description);
        std::cout << DBG_BJ "hit handler bust reply for user " << user_id << "\n";
        event.reply(dpp::ir_update_message, dpp::message().add_embed(embed),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "blackjack reply after bust failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        
        active_blackjack_games.erase(user_id);
    } else if (player_value == 21) {
        if (game.split && game.current_hand == 0) {
            // automatically stand first hand and move to second
            game.current_hand = 1;
            reply_blackjack_update(db, event, user_id);
            return;
        } else {
            finish_blackjack_game(bot, db, event, user_id);
        }
    } else {
        // Continue playing current hand
        reply_blackjack_update(db, event, user_id);
        std::cout << DBG_BJ "hit handler update completed for user " << user_id << "\n";
    }
}

void handle_blackjack_stand(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id) {
    std::cout << DBG_BJ "handle_blackjack_stand for user " << user_id << "\n";
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end() || !it->second.active) {
        std::cout << DBG_BJ "stand handler inactive reply for user " << user_id << "\n";
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This blackjack game is no longer active.")).set_flags(dpp::m_ephemeral),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "stand inactive-reply failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        return;
    }
    auto& game = it->second;

    if (game.split && game.current_hand == 0) {
        // move to second hand instead of finishing
        game.current_hand = 1;
        reply_blackjack_update(db, event, user_id);
        std::cout << DBG_BJ "stand handler split transition for user " << user_id << "\n";
    } else {
        finish_blackjack_game(bot, db, event, user_id);
    }
}

void handle_blackjack_double(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id) {
    std::cout << DBG_BJ "handle_blackjack_double for user " << user_id << "\n";
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end() || !it->second.active) {
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This blackjack game is no longer active.")).set_flags(dpp::m_ephemeral),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "double inactive-reply failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        return;
    }
    
    auto& game = it->second;
    
    // Determine which hand is being doubled
    bool firstHand = !(game.split && game.current_hand == 1);
    int64_t& betRef = firstHand ? game.bet : game.bet2;
    bool& doubledRef = firstHand ? game.doubled_down : game.doubled_down2;
    auto& hand = firstHand ? game.player_hand : game.player_hand2;

    // Check if player has enough money
    auto user_obj = db->get_user(user_id);
    if (!user_obj || user_obj->wallet < betRef) {
        event.reply(dpp::ir_channel_message_with_source, 
            dpp::message().add_embed(bronx::error("You don't have enough to double down!")).set_flags(dpp::m_ephemeral),
            [user_id](const dpp::confirmation_callback_t& cb){
                if (cb.is_error()) {
                    std::cout << DBG_BJ "double insufficient-funds reply failed for user " << user_id
                              << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                }
            });
        return;
    }

    // Double the bet for the appropriate hand
    db->update_wallet(user_id, -betRef);
    betRef *= 2;
    doubledRef = true;
    
    // Deal one card and automatically stand
    hand.push_back(game.deck.back());
    game.deck.pop_back();
    
    int player_value = calculate_hand_value(hand);
    
    if (player_value > 21) {
        // Busted
        if (game.split && game.current_hand == 0) {
            // switch to second hand
            game.current_hand = 1;
            reply_blackjack_update(db, event, user_id);
            return;
        }
        
        game.active = false;
        
        dpp::embed embed = bronx::error("🃏 BLACKJACK - BUST!");
        ::std::string description = "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n\n";
        description += "**Your Hand:** " + hand_to_string(hand) + "\n";
        description += "**Your Value:** " + ::std::to_string(player_value) + " **BUST!** 💥\n\n";
        description += "You doubled down and lost $" + format_number(betRef);
        
        auto updated_user = db->get_user(user_id);
        if (updated_user) {
            description += "\n\n**New Balance:** $" + format_number(updated_user->wallet);
        }
        
        embed.set_description(description);
        event.reply(dpp::ir_update_message, dpp::message().add_embed(embed),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "double reply after bust failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        
        active_blackjack_games.erase(user_id);
    } else {
        // finish or move to next hand if split and first hand
        if (game.split && game.current_hand == 0) {
            game.current_hand = 1;
            reply_blackjack_update(db, event, user_id);
        } else {
            finish_blackjack_game(bot, db, event, user_id);
        }
    }
}

// Handles splitting a hand when the initial two cards share the same value.
void handle_blackjack_split(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id) {
    std::cout << DBG_BJ "handle_blackjack_split for user " << user_id << "\n";
    auto it = active_blackjack_games.find(user_id);
    if (it == active_blackjack_games.end() || !it->second.active) {
        event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This blackjack game is no longer active.")).set_flags(dpp::m_ephemeral),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "split inactive-reply failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        return;
    }

    auto& game = it->second;
    if (game.split || game.player_hand.size() != 2 ||
        game.player_hand[0].value != game.player_hand[1].value) {
        event.reply(dpp::ir_update_message,
                    dpp::message().set_content("You cannot split this hand."),
                    [user_id](const dpp::confirmation_callback_t& cb){
                        if (cb.is_error()) {
                            std::cout << DBG_BJ "split invalid-reply failed for user " << user_id
                                      << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                        }
                    });
        return;
    }

    // make sure user can cover the additional bet
    auto user_obj = db->get_user(user_id);
    if (!user_obj || user_obj->wallet < game.bet) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::error("You don't have enough to split!")).set_flags(dpp::m_ephemeral),
            [user_id](const dpp::confirmation_callback_t& cb){
                if (cb.is_error()) {
                    std::cout << DBG_BJ "split insufficient funds reply failed for user " << user_id
                              << " code=" << cb.get_error().code << " msg=" << cb.get_error().message << "\n";
                }
            });
        return;
    }

    // Deduct extra bet and set up second hand
    db->update_wallet(user_id, -game.bet);
    game.bet2 = game.bet;
    game.split = true;
    game.current_hand = 0;
    game.doubled_down2 = false;

    // move second card to new hand and deal one new card to each
    game.player_hand2.push_back(game.player_hand[1]);
    game.player_hand.pop_back();
    game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
    game.player_hand2.push_back(game.deck.back()); game.deck.pop_back();

    reply_blackjack_update(db, event, user_id);
}

inline Command* get_blackjack_command(Database* db) {
    static Command* blackjack = new Command("blackjack", "play blackjack against the dealer", "gambling", {"bj", "21"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "blackjack", 3)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait a few seconds between games"));
                return;
            }
            
            if (args.size() < 1) {
                bronx::send_message(bot, event, bronx::error("usage: blackjack <amount>"));
                return;
            }

            auto user = db->get_user(event.msg.author.id);
            if (!user) return;

            int64_t bet;
            try {
                bet = parse_amount(args[0], user->wallet);
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

            // Deduct bet
            db->update_wallet(event.msg.author.id, -bet);
            log_gambling(db, event.msg.author.id, "started blackjack bet $" + format_number(bet));

            // Create deck and shuffle
            ::std::vector<Card> deck = create_deck();
            shuffle_deck(deck);

            // Create game
            BlackjackGame game;
            game.user_id = event.msg.author.id;
            game.bet = bet;
            game.bet2 = 0;
            game.deck = deck;
            game.active = true;
            game.player_stood = false;
            game.doubled_down = false;
            game.doubled_down2 = false;
            game.split = false;
            game.current_hand = 0;

            // Deal initial cards
            game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();

            int player_value = calculate_hand_value(game.player_hand);
            int dealer_value = calculate_hand_value(game.dealer_hand);

            // Check for immediate blackjacks
            bool player_blackjack = is_blackjack(game.player_hand);
            bool dealer_blackjack = is_blackjack(game.dealer_hand);

            if (player_blackjack || dealer_blackjack) {
                game.active = false;
                int64_t winnings = 0;
                ::std::string result_text = "";
                dpp::embed embed;
                if (player_blackjack && dealer_blackjack) {
                    winnings = bet;
                    result_text = "**Both have blackjack! Push!** 🤝\nYour bet has been returned.";
                    embed = dpp::embed().set_color(0xFFFF00).set_title("🃏 BLACKJACK - PUSH");
                } else if (player_blackjack) {
                    winnings = static_cast<int64_t>(bet * 2.5);
                    result_text = "**BLACKJACK!** 🎰\nYou won $" + format_number(static_cast<int64_t>(bet * 1.5)) + "!";
                    embed = bronx::success("🃏 BLACKJACK!");
                } else {
                    result_text = "**Dealer has blackjack!** 😔\nYou lost $" + format_number(bet);
                    embed = bronx::error("🃏 DEALER BLACKJACK");
                }
                db->update_wallet(event.msg.author.id, winnings);
                
                // Track gambling stats
                if (winnings == 0) {
                    db->increment_stat(event.msg.author.id, "gambling_losses", bet);
                } else if (winnings > bet) {
                    int64_t profit = winnings - bet;
                    db->increment_stat(event.msg.author.id, "gambling_profit", profit);
                    // Check gambling profit achievements
                    track_gambling_profit(bot, db, event.msg.channel_id, event.msg.author.id);
                }
                
                // Track milestone
                track_gambling_result(bot, db, event.msg.channel_id, event.msg.author.id, winnings > bet, winnings - bet);
                
                // Log gambling history
                if (winnings == 0) {
                    log_gambling(db, event.msg.author.id, "lost blackjack for $" + format_number(bet));
                } else if (winnings == bet) {
                    log_gambling(db, event.msg.author.id, "pushed blackjack (returned $" + format_number(bet) + ")");
                } else {
                    int64_t profit = winnings - bet;
                    log_gambling(db, event.msg.author.id, "won blackjack for $" + format_number(profit));
                }

                ::std::string description = "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n";
                description += "**Dealer Value:** " + ::std::to_string(dealer_value) + "\n\n";
                description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
                description += "**Your Value:** " + ::std::to_string(player_value) + "\n\n";
                description += result_text;

                auto updated_user = db->get_user(event.msg.author.id);
                if (updated_user) {
                    description += "\n\n**New Balance:** $" + format_number(updated_user->wallet);
                }

                embed.set_description(description);
                bronx::send_message(bot, event, embed);
                return;
            }

            // Create interactive message
            dpp::embed embed = dpp::embed()
                .set_color(0x6A0DAD) // Purple accent
                .set_title("🎴 BLACKJACK");

            ::std::string description = "**Bet: $" + format_number(bet) + "**\n\n";
            description += "**Dealer's Hand:** " + hand_to_string(game.dealer_hand, true) + "\n";
            description += "**Dealer Value:** ?\n\n";
            description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
            description += "**Your Value:** " + ::std::to_string(player_value);

            embed.set_description(description);

            dpp::message msg;
            msg.add_embed(embed);

            // Add buttons
            dpp::component row;
            row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("hit")
                .set_style(dpp::cos_success)
                .set_id("blackjack_hit_" + ::std::to_string(event.msg.author.id))
                .set_emoji("🎴"));

            row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("stand")
                .set_style(dpp::cos_primary)
                .set_id("blackjack_stand_" + ::std::to_string(event.msg.author.id))
                .set_emoji("✋"));

            // Check if player can double down
            if (user->wallet >= bet) {
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Double Down")
                    .set_style(dpp::cos_secondary)
                    .set_id("blackjack_double_" + ::std::to_string(event.msg.author.id))
                    .set_emoji("💰"));
            }

            // Offer a split if value matches
            if (game.player_hand.size() == 2 &&
                game.player_hand[0].value == game.player_hand[1].value &&
                user->wallet >= bet) {
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("split")
                    .set_style(dpp::cos_secondary)
                    .set_id("blackjack_split_" + ::std::to_string(event.msg.author.id))
                    .set_emoji("🔀"));
            }

            msg.add_component(row);

            // store immediately so handlers see the game instance even if the
            // create callback hasn't fired yet
            active_blackjack_games[event.msg.author.id] = game;

            bot.message_create(msg.set_channel_id(event.msg.channel_id), [event](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) return;

                auto sent_msg = callback.get<dpp::message>();
                auto &stored = active_blackjack_games[event.msg.author.id];
                stored.message_id = sent_msg.id;
                stored.channel_id = sent_msg.channel_id;
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "blackjack", 3)) {
                event.reply(dpp::message().add_embed(bronx::error("slow down! wait a few seconds between games")));
                return;
            }
            
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
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
            
            // Deduct bet
            db->update_wallet(event.command.get_issuing_user().id, -bet);
            log_gambling(db, event.command.get_issuing_user().id, "started blackjack bet $" + format_number(bet));
            
            // Create deck and shuffle
            ::std::vector<Card> deck = create_deck();
            shuffle_deck(deck);
            
            // Create game
            BlackjackGame game;
            game.user_id = event.command.get_issuing_user().id;
            game.bet = bet;
            game.bet2 = 0;
            game.deck = deck;
            game.active = true;
            game.player_stood = false;
            game.doubled_down = false;
            game.doubled_down2 = false;
            game.split = false;
            game.current_hand = 0;
            
            // Deal initial cards
            game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
            game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();
            
            int player_value = calculate_hand_value(game.player_hand);
            int dealer_value = calculate_hand_value(game.dealer_hand);
            
            // Check for immediate blackjacks
            bool player_blackjack = is_blackjack(game.player_hand);
            bool dealer_blackjack = is_blackjack(game.dealer_hand);
            
            if (player_blackjack || dealer_blackjack) {
                game.active = false;
                
                int64_t winnings = 0;
                ::std::string result_text = "";
                dpp::embed embed;
                
                if (player_blackjack && dealer_blackjack) {
                    winnings = bet;
                    result_text = "**Both have blackjack! Push!** 🤝\nYour bet has been returned.";
                    embed = dpp::embed().set_color(0xFFFF00).set_title("🃏 BLACKJACK - PUSH");
                } else if (player_blackjack) {
                    winnings = static_cast<int64_t>(bet * 2.5);
                    result_text = "**BLACKJACK!** 🎰\nYou won $" + format_number(static_cast<int64_t>(bet * 1.5)) + "!";
                    embed = bronx::success("🃏 BLACKJACK!");
                } else {
                    result_text = "**Dealer has blackjack!** 😔\nYou lost $" + format_number(bet);
                    embed = bronx::error("🃏 DEALER BLACKJACK");
                }
                
                db->update_wallet(event.command.get_issuing_user().id, winnings);
                
                // Track gambling stats
                uint64_t uid = event.command.get_issuing_user().id;
                if (winnings == 0) {
                    db->increment_stat(uid, "gambling_losses", bet);
                } else if (winnings > bet) {
                    int64_t profit = winnings - bet;
                    db->increment_stat(uid, "gambling_profit", profit);
                    // Check gambling profit achievements
                    track_gambling_profit(bot, db, event.command.channel_id, uid);
                }
                
                // Track milestone
                track_gambling_result(bot, db, event.command.channel_id, uid, winnings > bet, winnings - bet);
                
                // Log gambling history
                if (winnings == 0) {
                    log_gambling(db, uid, "lost blackjack for $" + format_number(bet));
                } else if (winnings == bet) {
                    log_gambling(db, uid, "pushed blackjack (returned $" + format_number(bet) + ")");
                } else {
                    int64_t profit = winnings - bet;
                    log_gambling(db, uid, "won blackjack for $" + format_number(profit));
                }
                
                ::std::string description = "**Dealer's Hand:** " + hand_to_string(game.dealer_hand) + "\n";
                description += "**Dealer Value:** " + ::std::to_string(dealer_value) + "\n\n";
                description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
                description += "**Your Value:** " + ::std::to_string(player_value) + "\n\n";
                description += result_text;
                
                auto updated_user = db->get_user(event.command.get_issuing_user().id);
                if (updated_user) {
                    description += "\n\n**New Balance:** $" + format_number(updated_user->wallet);
                }
                
                embed.set_description(description);
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // Create interactive message
            dpp::embed embed = dpp::embed()
                .set_color(0x6A0DAD) // Purple accent
                .set_title("🎴 BLACKJACK");

            ::std::string description = "**Bet: $" + format_number(bet) + "**\n\n";
            description += "**Dealer's Hand:** " + hand_to_string(game.dealer_hand, true) + "\n";
            description += "**Dealer Value:** ?\n\n";
            description += "**Your Hand:** " + hand_to_string(game.player_hand) + "\n";
            description += "**Your Value:** " + ::std::to_string(player_value);

            embed.set_description(description);

            dpp::message msg;
            msg.add_embed(embed);

            // Add buttons
            dpp::component row;
            uint64_t uid = event.command.get_issuing_user().id;

            row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("hit")
                .set_style(dpp::cos_success)
                .set_id("blackjack_hit_" + ::std::to_string(uid))
                .set_emoji("🎴"));
            
            row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("stand")
                .set_style(dpp::cos_primary)
                .set_id("blackjack_stand_" + ::std::to_string(uid))
                .set_emoji("✋"));
            
            if (user->wallet >= bet) {
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Double Down")
                    .set_style(dpp::cos_secondary)
                    .set_id("blackjack_double_" + ::std::to_string(uid))
                    .set_emoji("💰"));
            }
            
            // split option for slash commands as well
            if (game.player_hand.size() == 2 &&
                game.player_hand[0].value == game.player_hand[1].value &&
                user->wallet >= bet) {
                row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("split")
                    .set_style(dpp::cos_secondary)
                    .set_id("blackjack_split_" + ::std::to_string(uid))
                    .set_emoji("🔀"));
            }
            
            msg.add_component(row);

            // store the game immediately so button handlers see it even if the
            // reply callback hasn't fired yet (avoids race conditions)
            active_blackjack_games[uid] = game;
            
            event.reply(msg, [uid](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) return;
                
                auto sent_msg = callback.get<dpp::message>();
                // update the existing game entry with the real
                // message/channel ids
                auto &stored = active_blackjack_games[uid];
                stored.message_id = sent_msg.id;
                stored.channel_id = sent_msg.channel_id;
            });
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to bet (supports all, half, 50%, 1k, etc)", true)
        });
    
    return blackjack;
}

} // namespace gambling
} // namespace commands
