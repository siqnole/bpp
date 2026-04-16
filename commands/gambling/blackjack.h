#pragma once

// ============================================================================
// blackjack.h — DECLARATIONS ONLY
// Heavy implementation moved to blackjack.cpp to optimize compilation.
// ============================================================================

#include "../../command.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <map>

using namespace bronx::db;

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

    // Bets are tracked per hand so we can support splits.
    int64_t bet;   // first hand amount
    int64_t bet2;  // second hand amount (0 if not splitting)

    ::std::vector<Card> player_hand;
    ::std::vector<Card> player_hand2; // used when split
    ::std::vector<Card> dealer_hand;
    ::std::vector<Card> deck;

    bool active;
    bool player_stood;

    // Track which hand the player is currently playing (0 or 1).
    bool split;
    int current_hand;

    // Double‑down flags for each hand.
    bool doubled_down;
    bool doubled_down2;
};

extern std::map<uint64_t, BlackjackGame> active_blackjack_games;

// Export handlers for gambling_interactions.h
void handle_blackjack_hit(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id);
void handle_blackjack_stand(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id);
void handle_blackjack_double(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id);
void handle_blackjack_split(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t user_id);

// Export primary command factory
Command* get_blackjack_command(Database* db);

} // namespace gambling
} // namespace commands
