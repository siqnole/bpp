#include "poker.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>

namespace commands {
namespace gambling {

std::map<uint64_t, PokerGame> g_poker_games;
std::mutex g_poker_mutex;

std::string PokerCard::display() const {
    static const char* R[] = {"","","2","3","4","5","6","7","8","9","10","J","Q","K","A"};
    static const char* S[] = {"♠","♥","♦","♣"};
    return std::string(R[rank]) + S[suit];
}

std::vector<PokerCard> poker_make_deck() {
    std::vector<PokerCard> d;
    for (int s = 0; s < 4; s++)
        for (int r = 2; r <= 14; r++)
            d.push_back({r, (int)s});
    return d;
}

void poker_shuffle(std::vector<PokerCard>& d) {
    std::mt19937 g(std::random_device{}());
    std::shuffle(d.begin(), d.end(), g);
}

HandScore eval_5(std::vector<int> ranks, bool flush) {
    std::sort(ranks.rbegin(), ranks.rend());
    std::map<int, int> freq;
    for (int r : ranks) freq[r]++;

    std::vector<int> quads, trips, pairs, singles;
    for (auto& [r, c] : freq) {
        if (c == 4) quads.push_back(r);
        else if (c == 3) trips.push_back(r);
        else if (c == 2) pairs.push_back(r);
        else singles.push_back(r);
    }
    std::sort(quads.rbegin(), quads.rend());
    std::sort(trips.rbegin(), trips.rend());
    std::sort(pairs.rbegin(), pairs.rend());
    std::sort(singles.rbegin(), singles.rend());

    bool straight = false; int sh = 0;
    if ((int)std::set<int>(ranks.begin(), ranks.end()).size() == 5) {
        if (ranks[0] - ranks[4] == 4) { straight = true; sh = ranks[0]; }
        if (ranks[0]==14 && ranks[1]==5 && ranks[2]==4 && ranks[3]==3 && ranks[4]==2)
            { straight = true; sh = 5; }
    }

    if (straight && flush)  return {8, sh};
    if (!quads.empty())     return {7, quads[0], singles.empty() ? 0 : singles[0]};
    if (!trips.empty() && !pairs.empty()) return {6, trips[0], pairs[0]};
    if (flush)              return {5, ranks[0], ranks[1], ranks[2], ranks[3], ranks[4]};
    if (straight)           return {4, sh};
    if (!trips.empty())     return {3, trips[0],
                                   singles.size()>0?singles[0]:0, singles.size()>1?singles[1]:0};
    if (pairs.size() >= 2)  return {2, pairs[0], pairs[1], singles.empty()?0:singles[0]};
    if (pairs.size() == 1)  return {1, pairs[0],
                                   singles.size()>0?singles[0]:0,
                                   singles.size()>1?singles[1]:0,
                                   singles.size()>2?singles[2]:0};
    return {0, ranks[0], ranks[1], ranks[2], ranks[3], ranks[4]};
}

HandScore best_hand(const std::vector<PokerCard>& cards) {
    HandScore best;
    int n = (int)cards.size();
    if (n < 5) return best;
    for (int a = 0; a < n-4; a++)
      for (int b = a+1; b < n-3; b++)
        for (int c = b+1; c < n-2; c++)
          for (int d = c+1; d < n-1; d++)
            for (int e = d+1; e < n; e++) {
                std::vector<int> ranks = {cards[a].rank, cards[b].rank,
                                          cards[c].rank, cards[d].rank, cards[e].rank};
                bool fl = (cards[a].suit==cards[b].suit && cards[b].suit==cards[c].suit &&
                           cards[c].suit==cards[d].suit && cards[d].suit==cards[e].suit);
                HandScore s = eval_5(ranks, fl);
                if (s > best) best = s;
            }
    return best;
}

const char* hand_name(int cat) {
    switch(cat) {
        case 8: return "Straight Flush";
        case 7: return "Four of a Kind";
        case 6: return "Full House";
        case 5: return "Flush";
        case 4: return "Straight";
        case 3: return "Three of a Kind";
        case 2: return "Two Pair";
        case 1: return "One Pair";
        default: return "High Card";
    }
}

std::string pname(const PokerPlayer& p) {
    return p.is_cpu ? p.display_name : "<@" + std::to_string(p.user_id) + ">";
}

std::string cards_str(const std::vector<PokerCard>& cv) {
    std::string s;
    for (auto& c : cv) s += c.display() + " ";
    return s.empty() ? "*(none)*" : s;
}

int active_count(const PokerGame& g) {
    int n = 0;
    for (auto& p : g.players) if (!p.folded) n++;
    return n;
}

dpp::message build_poker_msg(const PokerGame& g) {
    static const char* phase_names[] = {"Lobby","Pre-Flop","Flop","Turn","River","Showdown"};

    std::string body = "**Community:** " + cards_str(g.community) + "\n"
        "**Pot:** $" + format_number(g.pot) + " | "
        "**Current Bet:** $" + format_number(g.cur_bet) + "\n\n"
        "**Players:**\n";

    int turn = g.to_act.empty() ? -1 : g.to_act.front();
    for (int i = 0; i < (int)g.players.size(); i++) {
        const auto& p = g.players[i];
        std::string status;
        if (p.folded) status = ::bronx::EMOJI_DENY + " folded";
        else if (p.all_in) status = "💥 all-in ($" + format_number(p.bet_street) + ")";
        else if (i == turn) status = "▶️ **ACTING**";
        else status = "$" + format_number(p.stack);
        body += (i == turn ? "**" : "") + pname(p)
            + (i == turn ? "**" : "") + " — " + status + "\n";
    }

    if (turn >= 0 && (int)g.players.size() > turn) {
        bool cpu_turn = g.players[turn].is_cpu;
        body += "\n*" + pname(g.players[turn]) + "'s turn!"
            + (cpu_turn ? " 🤖 thinking..." : "") + "*";
    }

    dpp::embed embed = ::bronx::info(body);
    embed.set_title("♠️ Texas Hold'em — " + std::string(phase_names[(int)g.phase]));

    dpp::message msg;
    msg.id         = g.msg_id;
    msg.channel_id = g.channel_id;
    msg.add_embed(embed);

    if (g.phase == PokerPhase::Lobby && !g.started) {
        dpp::component join_btn = dpp::component().set_type(dpp::cot_button)
            .set_label("🃏 Join").set_style(dpp::cos_success)
            .set_id("poker_join_" + std::to_string(g.channel_id));
        dpp::component start_btn = dpp::component().set_type(dpp::cot_button)
            .set_label("▶ Start").set_style(dpp::cos_primary)
            .set_id("poker_start_" + std::to_string(g.channel_id));
        dpp::component row;
        row.add_component(join_btn);
        row.add_component(start_btn);
        msg.add_component(row);
    } else if (g.phase != PokerPhase::Lobby && g.phase != PokerPhase::Showdown
               && !g.to_act.empty() && !g.players[g.to_act.front()].is_cpu) {
        bool can_check = (g.cur_bet == 0);
        int64_t to_call = 0;
        int turn_idx = g.to_act.front();
        if (turn_idx >= 0 && turn_idx < (int)g.players.size()) {
            to_call = std::min(g.players[turn_idx].stack,
                               g.cur_bet - g.players[turn_idx].bet_street);
        }
        std::string check_label = can_check
            ? "Check"
            : "Call $" + format_number(to_call);

        int64_t raise_to = std::max(g.min_bet * 2, g.cur_bet * 2);
        std::string raise_label = "⬆️ Raise to $" + format_number(raise_to);

        dpp::component row1;
        row1.add_component(dpp::component().set_type(dpp::cot_button)
            .set_label(check_label).set_style(dpp::cos_success)
            .set_emoji("check", 1476703556428890132)
            .set_id("poker_checkcall_" + std::to_string(g.channel_id)));
        row1.add_component(dpp::component().set_type(dpp::cot_button)
            .set_label(raise_label).set_style(dpp::cos_primary)
            .set_id("poker_raise_" + std::to_string(g.channel_id)));
        row1.add_component(dpp::component().set_type(dpp::cot_button)
            .set_label("💥 All-In").set_style(dpp::cos_danger)
            .set_id("poker_allin_" + std::to_string(g.channel_id)));
        row1.add_component(dpp::component().set_type(dpp::cot_button)
            .set_label("Fold").set_style(dpp::cos_secondary)
            .set_emoji("deny", 1476703341454168288)
            .set_id("poker_fold_" + std::to_string(g.channel_id)));

        dpp::component row2;
        row2.add_component(dpp::component().set_type(dpp::cot_button)
            .set_label("🃏 My Cards").set_style(dpp::cos_secondary)
            .set_id("poker_mycards_" + std::to_string(g.channel_id)));

        msg.add_component(row1);
        msg.add_component(row2);
    }
    return msg;
}

void send_hole_cards(dpp::cluster& bot, const PokerGame& g) {
    for (auto& p : g.players) {
        if (p.is_cpu) continue;
        std::string card_str = cards_str(p.hole);
        dpp::embed e = ::bronx::info("**Your hole cards:** " + card_str + "\n"
            "Stack: $" + format_number(p.stack));
        e.set_title("♠️ Poker — Your Hand");
        bot.direct_message_create(p.user_id, dpp::message().add_embed(e));
    }
}

void notify_current_player(dpp::cluster& bot, Database* db, const PokerGame& g, uint64_t chan_id) {
    if (g.to_act.empty()) return;
    const auto& p = g.players[g.to_act.front()];
    if (p.is_cpu) {
        std::thread([&bot, db, chan_id]() { cpu_take_action(bot, db, chan_id); }).detach();
        return;
    }
    std::string comm = cards_str(g.community);
    std::string hole_s = cards_str(p.hole);
    std::string note = "**Your cards:** " + hole_s + "\n"
        "**Community:** " + comm + "\n"
        "**Pot:** $" + format_number(g.pot) + " | "
        "**To call:** $" + format_number(std::max((int64_t)0, g.cur_bet - p.bet_street)) + "\n"
        "**Your stack:** $" + format_number(p.stack) + "\n\n"
        "*Go make your move!*";
    dpp::embed e = ::bronx::info(note);
    e.set_title("♠️ It's your turn!");
    bot.direct_message_create(p.user_id, dpp::message().add_embed(e));
}

void build_to_act_postflop(PokerGame& g) {
    g.to_act.clear();
    g.cur_bet = 0;
    for (auto& p : g.players) p.bet_street = 0;

    int n = (int)g.players.size();
    for (int i = 1; i <= n; i++) {
        int idx = (g.dealer_idx + i) % n;
        if (!g.players[idx].folded && !g.players[idx].all_in && g.players[idx].stack > 0)
            g.to_act.push_back(idx);
    }
}

void do_showdown(dpp::cluster& bot, Database* db, uint64_t chan_id) {
    std::unique_lock<std::mutex> lk(g_poker_mutex);
    auto it = g_poker_games.find(chan_id);
    if (it == g_poker_games.end()) return;
    PokerGame& g = it->second;
    g.phase = PokerPhase::Showdown;

    struct PlayerResult { int idx; HandScore score; };
    std::vector<PlayerResult> contenders;
    for (int i = 0; i < (int)g.players.size(); i++) {
        if (!g.players[i].folded) {
            std::vector<PokerCard> all = g.players[i].hole;
            all.insert(all.end(), g.community.begin(), g.community.end());
            contenders.push_back({i, best_hand(all)});
        }
    }

    HandScore top;
    for (auto& r : contenders) if (r.score > top) top = r.score;

    std::vector<int> winners;
    for (auto& r : contenders) if (r.score == top) winners.push_back(r.idx);

    int64_t share = g.pot / (int64_t)winners.size();
    int64_t remainder = g.pot % (int64_t)winners.size();

    std::string result = "**🂠 Showdown — " + cards_str(g.community) + "**\n\n";
    for (auto& r : contenders) {
        std::string hand_str = (!r.score.empty()) ? hand_name(r.score[0]) : "?";
        result += pname(g.players[r.idx]) + " — "
            + cards_str(g.players[r.idx].hole) + "→ **" + hand_str + "**\n";
    }
    for (int i = 0; i < (int)g.players.size(); i++) {
        if (g.players[i].folded) {
            result += pname(g.players[i]) + " — folded\n";
        }
    }
    result += "\n**Winner";
    if (winners.size() > 1) result += "s (split pot)";
    result += ":**\n";
    for (int w : winners) {
        int64_t payout = share + ((&w == &winners[0]) ? remainder : 0);
        result += pname(g.players[w]) + " wins **$" + format_number(payout) + "**\n";
        if (!g.players[w].is_cpu) {
            db->update_wallet(g.players[w].user_id, payout);
            ::commands::gambling::track_gambling_result(bot, db, chan_id, g.players[w].user_id, true, payout);
        }
    }
    for (auto& p : g.players) {
        if (p.is_cpu) continue;
        if (p.stack > 0) db->update_wallet(p.user_id, p.stack);
        if (!p.folded) {
            bool won = std::find(winners.begin(), winners.end(),
                (int)(&p - &g.players[0])) != winners.end();
            if (!won)
                ::commands::gambling::track_gambling_result(bot, db, chan_id, p.user_id, false, -g.min_bet);
        }
    }

    dpp::message msg;
    dpp::embed e = ::bronx::success(result);
    e.set_title("♠️ Showdown");
    msg.add_embed(e);
    msg.channel_id = chan_id;
    msg.id = g.msg_id;
    lk.unlock();
    ::bronx::safe_message_edit(bot, msg);

    std::thread([chan_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::lock_guard<std::mutex> lk2(g_poker_mutex);
        g_poker_games.erase(chan_id);
    }).detach();
}

void award_fold_win(dpp::cluster& bot, Database* db, uint64_t chan_id) {
    std::unique_lock<std::mutex> lk(g_poker_mutex);
    auto it = g_poker_games.find(chan_id);
    if (it == g_poker_games.end()) return;
    PokerGame& g = it->second;

    int winner = -1;
    for (int i = 0; i < (int)g.players.size(); i++)
        if (!g.players[i].folded) { winner = i; break; }
    if (winner < 0) { g_poker_games.erase(it); return; }

    int64_t payout = g.pot + g.players[winner].stack;
    if (!g.players[winner].is_cpu) {
        db->update_wallet(g.players[winner].user_id, payout);
        ::commands::gambling::track_gambling_result(bot, db, chan_id, g.players[winner].user_id, true, g.pot);
    }

    for (auto& p : g.players) {
        if (p.folded && p.stack > 0 && !p.is_cpu) db->update_wallet(p.user_id, p.stack);
    }

    std::string desc = pname(g.players[winner]) + " wins **$"
        + format_number(g.pot) + "** (everyone else folded)\n"
        "(returned stack: $" + format_number(g.players[winner].stack) + ")";

    dpp::message msg;
    dpp::embed e = ::bronx::success(desc);
    e.set_title("♠️ Hand Over");
    msg.add_embed(e);
    msg.channel_id = chan_id;
    msg.id = g.msg_id;
    g.phase = PokerPhase::Showdown;
    lk.unlock();
    ::bronx::safe_message_edit(bot, msg);

    std::thread([chan_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::lock_guard<std::mutex> lk2(g_poker_mutex);
        g_poker_games.erase(chan_id);
    }).detach();
}

void advance_phase(dpp::cluster& bot, Database* db, uint64_t chan_id) {
    {
        std::lock_guard<std::mutex> lk(g_poker_mutex);
        auto it = g_poker_games.find(chan_id);
        if (it == g_poker_games.end()) return;
        PokerGame& g = it->second;

        if (active_count(g) <= 1) {
            lk.~lock_guard();
            award_fold_win(bot, db, chan_id);
            return;
        } else {
            switch (g.phase) {
                case PokerPhase::Preflop:
                    g.phase = PokerPhase::Flop;
                    for (int i = 0; i < 3; i++) { g.community.push_back(g.deck.back()); g.deck.pop_back(); }
                    build_to_act_postflop(g);
                    break;
                case PokerPhase::Flop:
                    g.phase = PokerPhase::Turn;
                    g.community.push_back(g.deck.back()); g.deck.pop_back();
                    build_to_act_postflop(g);
                    break;
                case PokerPhase::Turn:
                    g.phase = PokerPhase::River;
                    g.community.push_back(g.deck.back()); g.deck.pop_back();
                    build_to_act_postflop(g);
                    break;
                case PokerPhase::River:
                    lk.~lock_guard();
                    do_showdown(bot, db, chan_id);
                    return;
                default: break;
            }
            if (g.to_act.empty() && g.phase != PokerPhase::Showdown) {
                lk.~lock_guard();
                advance_phase(bot, db, chan_id);
                return;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_poker_mutex);
        auto it = g_poker_games.find(chan_id);
        if (it == g_poker_games.end()) return;
        if (active_count(it->second) <= 1) {
            lk.~lock_guard();
            award_fold_win(bot, db, chan_id);
            return;
        }
        bot.message_edit(build_poker_msg(it->second));
        notify_current_player(bot, db, it->second, chan_id);
    }
}

float cpu_hand_strength(const std::vector<PokerCard>& hole,
                                const std::vector<PokerCard>& community) {
    if (community.empty()) {
        int r1 = hole[0].rank, r2 = hole[1].rank;
        if (r1 == r2) return 0.72f;
        int hi = std::max(r1, r2), lo = std::min(r1, r2);
        float v = (hi >= 13 ? 0.55f : hi >= 10 ? 0.45f : 0.28f)
                + (lo >= 10 ? 0.10f : 0.0f)
                + (hi - lo <= 2 ? 0.04f : 0.0f)
                + (hole[0].suit == hole[1].suit ? 0.04f : 0.0f);
        return std::min(v, 0.90f);
    }
    std::vector<PokerCard> all = hole;
    all.insert(all.end(), community.begin(), community.end());
    HandScore s = best_hand(all);
    if (s.empty()) return 0.1f;
    return std::min(0.10f + s[0] * 0.11f, 0.99f);
}

std::string cpu_decide(float strength, int64_t to_call, int64_t stack) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> d(0.f, 1.f);
    float r = d(rng);

    if (to_call > stack / 2 && strength < 0.55f) return "fold";

    if (to_call == 0) {
        if (strength > 0.72f) return r < 0.65f ? "raise" : "checkcall";
        if (strength > 0.45f) return r < 0.22f ? "raise" : "checkcall";
        return r < 0.06f ? "raise" : "checkcall";
    }
    if (strength > 0.78f) { return r < 0.50f ? "raise" : "checkcall"; }
    if (strength > 0.55f) {
        if (r < 0.18f) return "raise";
        if (r < 0.88f) return "checkcall";
        return "fold";
    }
    if (strength > 0.35f) { return r < 0.45f ? "checkcall" : "fold"; }
    return r < 0.12f ? "checkcall" : "fold";
}

void cpu_take_action(dpp::cluster& bot, Database* db, uint64_t chan_id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    bool needs_advance = false;
    {
        std::lock_guard<std::mutex> lk(g_poker_mutex);
        auto it = g_poker_games.find(chan_id);
        if (it == g_poker_games.end() || !it->second.started
            || it->second.phase == PokerPhase::Showdown
            || it->second.to_act.empty()) return;
        PokerGame& g = it->second;

        int cur_idx = g.to_act.front();
        auto& p = g.players[cur_idx];
        if (!p.is_cpu) return;

        float strength = cpu_hand_strength(p.hole, g.community);
        int64_t to_call = std::max((int64_t)0, g.cur_bet - p.bet_street);
        std::string action = cpu_decide(strength, to_call, p.stack);

        if (action == "fold") {
            p.folded = true;
            g.to_act.pop_front();
        } else if (action == "checkcall") {
            if (to_call > 0) {
                int64_t actual = std::min(p.stack, to_call);
                p.stack      -= actual;
                p.bet_street += actual;
                g.pot        += actual;
                if (p.stack == 0) p.all_in = true;
            }
            g.to_act.pop_front();
        } else { // raise
            int64_t raise_to = std::max(g.min_bet * 2, g.cur_bet * 2);
            int64_t to_pay   = std::min(p.stack, raise_to - p.bet_street);
            if (to_pay <= 0) to_pay = p.stack;
            p.stack      -= to_pay;
            p.bet_street += to_pay;
            g.pot        += to_pay;
            if (p.stack == 0) p.all_in = true;
            g.cur_bet = std::max(g.cur_bet, p.bet_street);
            g.to_act.pop_front();
            std::deque<int> nq;
            int n2 = (int)g.players.size();
            for (int i = 1; i <= n2; i++) {
                int pi = (cur_idx + i) % n2;
                if (!g.players[pi].folded && !g.players[pi].all_in
                    && g.players[pi].stack > 0 && pi != cur_idx)
                    nq.push_back(pi);
            }
            g.to_act = nq;
        }

        if (active_count(g) <= 1 || g.to_act.empty()) {
            needs_advance = true;
        } else {
            bot.message_edit(build_poker_msg(g));
            notify_current_player(bot, db, g, chan_id);
        }
    }
    if (needs_advance) advance_phase(bot, db, chan_id);
}

void start_poker_game(dpp::cluster& bot, Database* db, uint64_t chan_id) {
    std::unique_lock<std::mutex> lk(g_poker_mutex);
    auto it = g_poker_games.find(chan_id);
    if (it == g_poker_games.end() || it->second.started) return;
    PokerGame& g = it->second;

    int human_count = 0;
    for (auto& p : g.players) if (!p.is_cpu) human_count++;
    if (human_count < 1 || (int)g.players.size() < 2) {
        for (auto& p : g.players) if (!p.is_cpu) db->update_wallet(p.user_id, p.stack);
        dpp::message msg;
        msg.channel_id = chan_id;
        msg.add_embed(::bronx::error("poker cancelled — need at least 2 players. entry refunded."));
        bot.message_create(msg);
        g_poker_games.erase(it);
        return;
    }

    g.started = true;
    g.phase   = PokerPhase::Preflop;

    g.deck = poker_make_deck();
    poker_shuffle(g.deck);
    for (auto& p : g.players) {
        p.hole.push_back(g.deck.back()); g.deck.pop_back();
        p.hole.push_back(g.deck.back()); g.deck.pop_back();
    }

    int n = (int)g.players.size();
    int sb = (g.dealer_idx + 1) % n;
    int bb = (g.dealer_idx + 2) % n;
    int utg = (g.dealer_idx + 3) % n;
    if (n == 2) { sb = g.dealer_idx; bb = (g.dealer_idx + 1) % n; utg = g.dealer_idx; }

    auto post_blind = [&](int idx, int64_t amt) {
        int64_t actual = std::min(g.players[idx].stack, amt);
        g.players[idx].stack     -= actual;
        g.players[idx].bet_street = actual;
        g.pot += actual;
        if (g.players[idx].stack == 0) g.players[idx].all_in = true;
        g.cur_bet = std::max(g.cur_bet, actual);
    };
    post_blind(sb, g.min_bet / 2);
    post_blind(bb, g.min_bet);

    g.to_act.clear();
    for (int i = 0; i < n; i++) {
        int p = (utg + i) % n;
        if (!g.players[p].folded && !g.players[p].all_in && g.players[p].stack > 0)
            g.to_act.push_back(p);
    }

    send_hole_cards(bot, g);
    notify_current_player(bot, db, g, chan_id);
    bot.message_edit(build_poker_msg(g));
}

void process_action(dpp::cluster& bot, Database* db, uint64_t chan_id,
                           const std::string& action, const dpp::button_click_t& event) {
    uint64_t actor = event.command.get_issuing_user().id;
    bool needs_advance = false;

    {
        std::lock_guard<std::mutex> lk(g_poker_mutex);
        auto it = g_poker_games.find(chan_id);
        if (it == g_poker_games.end() || !it->second.started || it->second.phase == PokerPhase::Showdown) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("no active hand")).set_flags(dpp::m_ephemeral));
            return;
        }
        PokerGame& g = it->second;
        if (g.to_act.empty()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("waiting for next phase")).set_flags(dpp::m_ephemeral));
            return;
        }

        int cur_idx = g.to_act.front();
        if (g.players[cur_idx].is_cpu) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::info("🤖 waiting for CPU to act...")).set_flags(dpp::m_ephemeral));
            return;
        }
        if (g.players[cur_idx].user_id != actor) {
            if (action == "mycards") {
                std::string cs = cards_str([&]() {
                    for (auto& p : g.players) if (p.user_id == actor) return p.hole;
                    return std::vector<PokerCard>{};
                }());
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::info("**Your cards:** " + cs))
                        .set_flags(dpp::m_ephemeral));
            } else {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::error("it's not your turn!")).set_flags(dpp::m_ephemeral));
            }
            return;
        }

        auto& p = g.players[cur_idx];

        if (action == "mycards") {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::info("**Your cards:** " + cards_str(p.hole)))
                    .set_flags(dpp::m_ephemeral));
            return;
        }

        if (action == "fold") {
            p.folded = true;
            g.to_act.pop_front();
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("you folded")).set_flags(dpp::m_ephemeral));

        } else if (action == "checkcall") {
            if (g.cur_bet == 0) {
                g.to_act.pop_front();
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::success("you checked")).set_flags(dpp::m_ephemeral));
            } else {
                int64_t to_call = std::min(p.stack, g.cur_bet - p.bet_street);
                p.stack     -= to_call;
                p.bet_street += to_call;
                g.pot        += to_call;
                if (p.stack == 0) p.all_in = true;
                g.to_act.pop_front();
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::success("called $" + format_number(to_call)))
                        .set_flags(dpp::m_ephemeral));
            }

        } else if (action == "raise") {
            int64_t raise_to = std::max(g.min_bet * 2, g.cur_bet * 2);
            int64_t to_pay   = std::min(p.stack, raise_to - p.bet_street);
            if (to_pay <= 0 || to_pay >= p.stack) {
                to_pay = p.stack;
            }
            p.stack      -= to_pay;
            p.bet_street += to_pay;
            g.pot        += to_pay;
            if (p.stack == 0) p.all_in = true;
            g.cur_bet = std::max(g.cur_bet, p.bet_street);
            g.to_act.pop_front();
            std::deque<int> new_q;
            int n = (int)g.players.size();
            for (int i = 1; i <= n; i++) {
                int pi = (cur_idx + i) % n;
                if (!g.players[pi].folded && !g.players[pi].all_in
                    && g.players[pi].stack > 0 && pi != cur_idx)
                    new_q.push_back(pi);
            }
            g.to_act = new_q;
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::success("⬆️ raised to $" + format_number(p.bet_street)))
                    .set_flags(dpp::m_ephemeral));

        } else if (action == "allin") {
            int64_t amt = p.stack;
            p.bet_street += amt;
            p.stack       = 0;
            p.all_in      = true;
            g.pot        += amt;
            bool reopens = (p.bet_street > g.cur_bet);
            g.cur_bet     = std::max(g.cur_bet, p.bet_street);
            g.to_act.pop_front();
            if (reopens) {
                std::deque<int> new_q;
                int n2 = (int)g.players.size();
                for (int i = 1; i <= n2; i++) {
                    int pi = (cur_idx + i) % n2;
                    if (!g.players[pi].folded && !g.players[pi].all_in
                        && g.players[pi].stack > 0 && pi != cur_idx)
                        new_q.push_back(pi);
                }
                g.to_act = new_q;
            }
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::success("💥 ALL-IN! ($" + format_number(amt) + ")"))
                    .set_flags(dpp::m_ephemeral));
        }

        if (active_count(g) <= 1 || g.to_act.empty()) needs_advance = true;

        if (!needs_advance)
            bot.message_edit(build_poker_msg(g));
        if (!g.to_act.empty() && !needs_advance)
            notify_current_player(bot, db, g, chan_id);
    }

    if (needs_advance) advance_phase(bot, db, chan_id);
}

Command* get_poker_command(Database* db) {
    static Command* cmd = new Command(
        "poker",
        "play Texas Hold'em poker with other players",
        "gambling",
        {"holdem", "texas"},
        false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid     = event.msg.author.id;
            uint64_t chan_id = event.msg.channel_id;

            if (args.empty()) {
                ::bronx::send_message(bot, event, ::bronx::info(
                    "**♠️ Texas Hold'em Poker**\n\n"
                    "Classic Texas Hold'em for 2–6 players (vs humans or CPU bots).\n\n"
                    "**Commands:**\n"
                    "`poker start [min_bet] [cpus]` — open a lobby (default $1,000 BB)\n"
                    "> add `cpus` (1–5) to play solo vs bots, e.g. `poker start 5000 3`\n"
                    "`poker join` — join the lobby\n\n"
                    "**Blinds & Buy-in:**\n"
                    "Small Blind = min_bet / 2 | Big Blind = min_bet\n"
                    "Buy-in = 50 × Big Blind (deducted from wallet)\n\n"
                    "**Actions:** Check/Call · Raise 2x · All-In · Fold\n"
                    "Hole cards are sent to you via DM at the start of each hand."
                ));
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            if (sub == "start") {
                std::lock_guard<std::mutex> lk(g_poker_mutex);
                if (g_poker_games.count(chan_id)) {
                    ::bronx::send_message(bot, event, ::bronx::error("a poker game is already running in this channel"));
                    return;
                }

                int64_t min_bet  = 1000;
                int     cpu_count = 0;
                if (args.size() >= 2) {
                    try {
                        auto me = db->get_user(uid);
                        min_bet = parse_amount(args[1], me ? me->wallet : 0);
                    } catch (...) {}
                }
                if (args.size() >= 3) {
                    try { cpu_count = std::stoi(args[2]); } catch (...) {}
                }
                if (min_bet < 100) min_bet = 100;
                if (min_bet > 100000000LL) min_bet = 100000000LL;
                cpu_count = std::max(0, std::min(cpu_count, 5));
                cpu_count = std::min(cpu_count, 5);
                int64_t buy_in = min_bet * 50;

                auto me = db->get_user(uid);
                if (!me || me->wallet < buy_in) {
                    ::bronx::send_message(bot, event, ::bronx::error(
                        "you need $" + format_number(buy_in) + " to start (50x BB buy-in)"));
                    return;
                }
                db->update_wallet(uid, -buy_in);

                PokerGame g;
                g.channel_id = chan_id;
                g.guild_id   = event.msg.guild_id;
                g.host_id    = uid;
                g.min_bet    = min_bet;
                PokerPlayer host;
                host.user_id = uid;
                host.stack   = buy_in;
                g.players.push_back(host);

                for (int i = 0; i < cpu_count; i++) {
                    PokerPlayer cpu;
                    cpu.is_cpu       = true;
                    cpu.user_id      = (uint64_t)(i + 1);
                    cpu.display_name = "🤖 CPU " + std::to_string(i + 1);
                    cpu.stack        = buy_in;
                    g.players.push_back(cpu);
                }

                bool instant = (cpu_count > 0);
                g_poker_games[chan_id] = g;

                auto msg = build_poker_msg(g_poker_games[chan_id]);
                msg.channel_id = chan_id;
                bot.message_create(msg, [&bot, db, chan_id, instant](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error()) {
                        auto msg_obj = std::get<dpp::message>(cb.value);
                        std::lock_guard<std::mutex> lk2(g_poker_mutex);
                        auto it = g_poker_games.find(chan_id);
                        if (it != g_poker_games.end()) it->second.msg_id = msg_obj.id;
                    }
                    int delay_secs = instant ? 0 : 60;
                    std::thread([&bot, db, chan_id, delay_secs]() {
                        if (delay_secs > 0)
                            std::this_thread::sleep_for(std::chrono::seconds(delay_secs));
                        else
                            std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        start_poker_game(bot, db, chan_id);
                    }).detach();
                });
                return;
            }

            if (sub == "join") {
                std::lock_guard<std::mutex> lk(g_poker_mutex);
                auto it = g_poker_games.find(chan_id);
                if (it == g_poker_games.end()) {
                    ::bronx::send_message(bot, event, ::bronx::error("no active poker lobby in this channel"));
                    return;
                }
                if (it->second.started) {
                    ::bronx::send_message(bot, event, ::bronx::error("hand is already in progress"));
                    return;
                }
                for (auto& p : it->second.players) {
                    if (p.user_id == uid) {
                        ::bronx::send_message(bot, event, ::bronx::error("you're already in this game"));
                        return;
                    }
                }
                if ((int)it->second.players.size() >= 6) {
                    ::bronx::send_message(bot, event, ::bronx::error("table is full (max 6)"));
                    return;
                }
                int64_t buy_in = it->second.min_bet * 50;
                auto me = db->get_user(uid);
                if (!me || me->wallet < buy_in) {
                    ::bronx::send_message(bot, event, ::bronx::error("need $" + format_number(buy_in) + " to join"));
                    return;
                }
                db->update_wallet(uid, -buy_in);
                PokerPlayer pp; pp.user_id = uid; pp.stack = buy_in;
                it->second.players.push_back(pp);
                ::bronx::send_message(bot, event, ::bronx::success("you joined the table! buy-in: $" + format_number(buy_in)));
                bot.message_edit(build_poker_msg(it->second));
                return;
            }

            ::bronx::send_message(bot, event, ::bronx::error("unknown subcommand. use `poker` for help"));
        }
    );
    return cmd;
}

void register_poker_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("poker_join_", 0) != 0) return;
        uint64_t chan_id = 0;
        try { chan_id = std::stoull(event.custom_id.substr(11)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        std::lock_guard<std::mutex> lk(g_poker_mutex);
        auto it = g_poker_games.find(chan_id);
        if (it == g_poker_games.end() || it->second.started) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("lobby no longer open")).set_flags(dpp::m_ephemeral));
            return;
        }
        for (auto& p : it->second.players) {
            if (p.user_id == uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::error("you're already seated")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        if ((int)it->second.players.size() >= 6) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("table is full")).set_flags(dpp::m_ephemeral));
            return;
        }
        int64_t buy_in = it->second.min_bet * 50;
        int64_t wallet = db->get_wallet(uid);
        if (wallet < buy_in) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(::bronx::error("need $" + format_number(buy_in) + " to join"))
                    .set_flags(dpp::m_ephemeral));
            return;
        }
        db->update_wallet(uid, -buy_in);
        PokerPlayer pp; pp.user_id = uid; pp.stack = buy_in;
        it->second.players.push_back(pp);
        event.reply(dpp::ir_update_message, build_poker_msg(it->second));
    });

    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("poker_start_", 0) != 0) return;
        uint64_t chan_id = 0;
        try { chan_id = std::stoull(event.custom_id.substr(12)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        {
            std::lock_guard<std::mutex> lk(g_poker_mutex);
            auto it = g_poker_games.find(chan_id);
            if (it == g_poker_games.end() || it->second.started) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::error("game not in lobby")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (it->second.host_id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(::bronx::error("only the host can start")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        event.reply(dpp::ir_deferred_update_message, dpp::message());
        start_poker_game(bot, db, chan_id);
    });

    auto make_action_handler = [db, &bot](const std::string& action_prefix, const std::string& action_key) {
        bot.on_button_click([db, &bot, action_prefix, action_key](const dpp::button_click_t& event) {
            if (event.custom_id.rfind(action_prefix, 0) != 0) return;
            uint64_t chan_id = 0;
            try { chan_id = std::stoull(event.custom_id.substr(action_prefix.size())); }
            catch (...) { return; }
            process_action(bot, db, chan_id, action_key, event);
        });
    };

    make_action_handler("poker_checkcall_", "checkcall");
    make_action_handler("poker_raise_",     "raise");
    make_action_handler("poker_allin_",     "allin");
    make_action_handler("poker_fold_",      "fold");
    make_action_handler("poker_mycards_",   "mycards");
}

} // namespace gambling
} // namespace commands
