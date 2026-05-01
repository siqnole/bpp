#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <deque>

namespace commands {
namespace gambling {

using namespace bronx::db;

// --- Poker Card ---
struct PokerCard {
    int rank; // 2–14 (14 = Ace high)
    int suit; // 0=♠ 1=♥ 2=♦ 3=♣
    std::string display() const;
};

std::vector<PokerCard> poker_make_deck();
void poker_shuffle(std::vector<PokerCard>& d);

// --- Hand Evaluation ---
using HandScore = std::vector<int>;
HandScore eval_5(std::vector<int> ranks, bool flush);
HandScore best_hand(const std::vector<PokerCard>& cards);
const char* hand_name(int cat);

// --- Game State ---
enum class PokerPhase { Lobby, Preflop, Flop, Turn, River, Showdown };

struct PokerPlayer {
    uint64_t    user_id;
    std::string display_name;
    bool        is_cpu      = false;
    std::vector<PokerCard> hole;
    int64_t  stack          = 0;
    int64_t  bet_street     = 0;
    bool     folded         = false;
    bool     all_in         = false;
};

struct PokerGame {
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t host_id;
    int64_t  min_bet   = 1000;
    int64_t  pot       = 0;
    int64_t  cur_bet   = 0;
    std::vector<PokerPlayer> players;
    std::vector<PokerCard>   community;
    std::vector<PokerCard>   deck;
    PokerPhase phase   = PokerPhase::Lobby;
    int dealer_idx     = 0;
    std::deque<int>    to_act;
    dpp::snowflake msg_id = 0;
    bool started = false;
};

extern std::map<uint64_t, PokerGame> g_poker_games;
extern std::mutex g_poker_mutex;

std::string pname(const PokerPlayer& p);
std::string cards_str(const std::vector<PokerCard>& cv);
int active_count(const PokerGame& g);

dpp::message build_poker_msg(const PokerGame& g);
void send_hole_cards(dpp::cluster& bot, const PokerGame& g);
void notify_current_player(dpp::cluster& bot, Database* db, const PokerGame& g, uint64_t chan_id);
void build_to_act_postflop(PokerGame& g);
void do_showdown(dpp::cluster& bot, Database* db, uint64_t chan_id);
void award_fold_win(dpp::cluster& bot, Database* db, uint64_t chan_id);
void advance_phase(dpp::cluster& bot, Database* db, uint64_t chan_id);

float cpu_hand_strength(const std::vector<PokerCard>& hole, const std::vector<PokerCard>& community);
std::string cpu_decide(float strength, int64_t to_call, int64_t stack);
void cpu_take_action(dpp::cluster& bot, Database* db, uint64_t chan_id);

void start_poker_game(dpp::cluster& bot, Database* db, uint64_t chan_id);
void process_action(dpp::cluster& bot, Database* db, uint64_t chan_id, const std::string& action, const dpp::button_click_t& event);

Command* get_poker_command(Database* db);
void register_poker_interactions(dpp::cluster& bot, Database* db);

} // namespace gambling
} // namespace commands
