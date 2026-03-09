#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>

using namespace bronx::db;

namespace commands {
namespace games {

// ============================================================================
// GUILD HEISTS — Cooperative multiplayer button game
// ============================================================================
// Up to 6 players team up to crack a vault in 3 timed phases.
//
// Flow:
//   heist start [entry_fee]  — host opens a lobby (default $5,000 entry)
//   players click "🔑 Join"  — up to 6 players, 60-second window
//   host clicks "▶ Start"    — optionally force-starts early
//
//   Phase 1 (Lockpick)  — 3 buttons, 1 correct, 15-second window
//   Phase 2 (Tunnel)    — same mechanic
//   Phase 3 (Hack)      — same mechanic
//   Payout              — vault_target × phases_cleared / members, split by contribution
//
// Each player's vault share is proportional to how many phase buttons
// they clicked correctly (0-3 contribution points).
// ============================================================================

struct HeistSession {
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t host_id;
    int64_t  entry_fee;       // coins to join
    int64_t  vault_target;    // total prize pool

    std::vector<uint64_t>           members;          // joined player IDs
    std::map<uint64_t, int>         contribution;     // player → correct clicks (0-3)
    std::set<uint64_t>              phase_clicked;    // who clicked this phase
    std::set<uint64_t>              paid_entry;       // who has paid entry fee

    bool started    = false;  // lobby closed, game in progress
    int  phase      = 0;      // 0=lobby, 1/2/3=rounds, 4=done
    int  correct_btn = 0;     // which button index (0, 1, or 2) is correct this phase

    dpp::snowflake lobby_msg_id = 0;
    dpp::snowflake phase_msg_id = 0;
};

static std::mutex g_heist_mutex;
static std::map<uint64_t, HeistSession> g_heist_sessions; // keyed by channel_id
static uint64_t g_next_heist_id = 1;

// ── Phase labels and button emojis ──────────────────────────────────────────
static const std::vector<std::string> PHASE_NAMES  = {"", "🔑 Lockpick", "⛏️ Tunnel", "💻 Hack"};
static const std::vector<std::string> PHASE_FLAVORS = {
    "",
    "crack the vault lock — quick!",
    "drill through the reinforced wall!",
    "override the security system!"
};

// Per-phase button sets: 3 options, 1 goal, 2 fakes
struct HeistPhaseButtons {
    std::string goal_label;     // correct button label
    std::string fake1_label;
    std::string fake2_label;
};

static const std::vector<HeistPhaseButtons> PHASE_BUTTONS = {
    {}, // phase 0 placeholder
    {"🔓 Click here!", "🔒 Locked!", "🚫 Wrong!"},
    {"⛏️ Dig here!", "🪨 Dead end", "💥 Collapse!"},
    {"✅ Firewall down", "⚠️ Alarm!", "❌ Rejected!"},
};

// ── Build lobby message ──────────────────────────────────────────────────────
static dpp::message build_lobby_message(const HeistSession& s) {
    std::string desc = "**🏦 Vault Heist — Lobby**\n\n"
        "entry fee: **$" + format_number(s.entry_fee) + "** per player\n"
        "vault target: **$" + format_number(s.vault_target) + "**\n\n"
        "**Players (" + std::to_string(s.members.size()) + "/6):**\n";
    for (uint64_t mid : s.members)
        desc += "• <@" + std::to_string(mid) + ">\n";
    desc += "\n*host: <@" + std::to_string(s.host_id) + "> can start early or wait 60 seconds*";

    dpp::component join_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_label("🔑 Join Heist")
        .set_style(dpp::cos_success)
        .set_id("heist_join_" + std::to_string(s.channel_id));

    dpp::component start_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_label("▶ Start Heist")
        .set_style(dpp::cos_primary)
        .set_id("heist_start_" + std::to_string(s.channel_id));

    dpp::component row;
    row.add_component(join_btn);
    row.add_component(start_btn);

    dpp::embed embed = bronx::info(desc);
    return dpp::message().add_embed(embed).add_component(row);
}

// ── Build phase message ──────────────────────────────────────────────────────
static dpp::message build_phase_message(const HeistSession& s) {
    const auto& pb = PHASE_BUTTONS[s.phase];
    std::string desc = "**🏦 Heist — " + PHASE_NAMES[s.phase] + "**\n\n"
        "*" + PHASE_FLAVORS[s.phase] + "*\n\n"
        "**15 seconds to click the right button!**\n\n"
        "Players who respond:\n";
    for (uint64_t mid : s.members)
        desc += (s.phase_clicked.count(mid) ? "✅" : "⏳") + std::string(" <@") + std::to_string(mid) + ">\n";

    // Build 3 buttons in random order, correct_btn position determines which is "correct"
    std::vector<std::pair<std::string, int>> btns = {
        {pb.goal_label, 0},
        {pb.fake1_label, 1},
        {pb.fake2_label, 2},
    };
    // s.correct_btn == 0 means first slot is correct (already set when phase starts)
    // We need to place the goal at position s.correct_btn
    std::vector<std::string> ordered(3);
    ordered[s.correct_btn] = pb.goal_label;
    int fake_idx = 0;
    for (int i = 0; i < 3; i++) {
        if (i != s.correct_btn) {
            ordered[i] = (fake_idx == 0) ? pb.fake1_label : pb.fake2_label;
            fake_idx++;
        }
    }

    dpp::component row;
    for (int i = 0; i < 3; i++) {
        row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label(ordered[i])
            .set_style(dpp::cos_secondary)
            .set_id("heist_p" + std::to_string(s.phase) + "_" + std::to_string(s.channel_id) + "_" + std::to_string(i))
        );
    }

    dpp::embed embed = bronx::info(desc);
    return dpp::message().add_embed(embed).add_component(row);
}

// ── Execute payout after all phases ─────────────────────────────────────────
static void execute_heist_payout(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    std::unique_lock<std::mutex> lock(g_heist_mutex);
    auto it = g_heist_sessions.find(channel_id);
    if (it == g_heist_sessions.end()) return;
    HeistSession s = it->second;
    g_heist_sessions.erase(it);
    lock.unlock();

    if (s.members.empty()) return;

    int total_contribution = 0;
    for (auto& [uid, pts] : s.contribution) total_contribution += pts;

    // Prize is vault_target scaled by average team performance (0-100%)
    double team_score = (s.members.size() > 0)
        ? (double)total_contribution / (double)(s.members.size() * 3)
        : 0.0;
    int64_t total_prize = (int64_t)(s.vault_target * team_score);

    std::string result_desc = "**🏦 Heist Complete!**\n\n"
        "**Team performance:** " + std::to_string((int)(team_score * 100)) + "%\n"
        "**Total prize pool:** $" + format_number(total_prize) + "\n\n"
        "**Payouts:**\n";

    if (total_contribution == 0) {
        // Failed — refund entry fees
        for (uint64_t mid : s.members)
            db->update_wallet(mid, s.entry_fee);
        result_desc = "**🏦 Heist Failed!**\n\n"
            "The crew couldn't coordinate in time.\n"
            "Entry fees refunded to all participants.";
    } else {
        for (uint64_t mid : s.members) {
            int pts = s.contribution.count(mid) ? s.contribution.at(mid) : 0;
            int64_t share = (total_contribution > 0)
                ? (int64_t)((double)pts / total_contribution * total_prize)
                : (total_prize / (int64_t)s.members.size());
            if (share > 0) db->update_wallet(mid, share);
            result_desc += "<@" + std::to_string(mid) + "> — "
                + std::to_string(pts) + "/3 pts → **$" + format_number(share) + "**\n";
        }
    }

    // Disable old phase message buttons and send result
    dpp::message result_msg;
    result_msg.add_embed(bronx::success(result_desc));
    result_msg.channel_id = channel_id;
    bot.message_create(result_msg);
}

// ── Advance to the next heist phase ─────────────────────────────────────────
static void advance_heist_phase(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    {
        std::lock_guard<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(channel_id);
        if (it == g_heist_sessions.end()) return;
        it->second.phase++;
        it->second.phase_clicked.clear();
        if (it->second.phase > 3) {
            // All phases done
        } else {
            // Randomize correct button
            std::mt19937 rng(std::random_device{}());
            it->second.correct_btn = (int)(rng() % 3);
        }
    }

    // Check if done
    {
        std::lock_guard<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(channel_id);
        if (it == g_heist_sessions.end()) return;
        if (it->second.phase > 3) {
            // done, execute payout below
        } else {
            // Send phase message
            auto msg = build_phase_message(it->second);
            msg.channel_id = channel_id;
            bot.message_create(msg, [&bot, db, channel_id](const dpp::confirmation_callback_t& cb) {
                if (!cb.is_error()) {
                    // Store phase message id
                    auto msg_obj = std::get<dpp::message>(cb.value);
                    std::lock_guard<std::mutex> lock(g_heist_mutex);
                    auto it2 = g_heist_sessions.find(channel_id);
                    if (it2 != g_heist_sessions.end())
                        it2->second.phase_msg_id = msg_obj.id;
                }
            });

            // 15-second timer then advance
            std::thread([&bot, db, channel_id]() {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                advance_heist_phase(bot, db, channel_id);
            }).detach();
            return;
        }
    }
    execute_heist_payout(bot, db, channel_id);
}

// ── Launch heist from lobby (called when Start is pressed or timer fires) ───
static void start_heist_game(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    {
        std::unique_lock<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(channel_id);
        if (it == g_heist_sessions.end() || it->second.started) return;
        if (it->second.members.size() < 2) {
            // Not enough players — cancel and refund
            HeistSession s = it->second;
            g_heist_sessions.erase(it);
            lock.unlock();
            for (uint64_t mid : s.members)
                db->update_wallet(mid, s.entry_fee);
            dpp::message msg;
            msg.add_embed(bronx::error("heist cancelled — not enough players (need at least 2). entry fees refunded."));
            msg.channel_id = channel_id;
            bot.message_create(msg);
            return;
        }
        std::mt19937 rng(std::random_device{}());
        it->second.started    = true;
        it->second.phase      = 1;
        it->second.correct_btn = (int)(rng() % 3);
        it->second.phase_clicked.clear();
    }

    // Send phase 1 message
    {
        std::lock_guard<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(channel_id);
        if (it == g_heist_sessions.end()) return;
        auto msg = build_phase_message(it->second);
        msg.channel_id = channel_id;
        bot.message_create(msg, [&bot, db, channel_id](const dpp::confirmation_callback_t& cb) {
            if (!cb.is_error()) {
                auto msg_obj = std::get<dpp::message>(cb.value);
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                auto it2 = g_heist_sessions.find(channel_id);
                if (it2 != g_heist_sessions.end())
                    it2->second.phase_msg_id = msg_obj.id;
            }
        });
    }

    // 15s timer for first phase
    std::thread([&bot, db, channel_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        advance_heist_phase(bot, db, channel_id);
    }).detach();
}

// ============================================================================
// COMMAND BUILDER + INTERACTION HANDLERS
// ============================================================================

inline Command* get_heist_command(Database* db) {
    static Command* cmd = new Command(
        "heist",
        "launch a cooperative vault heist with your crew",
        "games",
        {"rob2"},
        false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t uid     = event.msg.author.id;
            uint64_t chan_id = event.msg.channel_id;

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::info(
                    "**🏦 Guild Heists**\n\n"
                    "Team up with other players to crack a vault!\n\n"
                    "**Commands:**\n"
                    "`heist start [entry_fee]` — open a heist lobby (default $5,000)\n"
                    "`heist join` — join the active heist in this channel\n"
                    "`heist status` — check current heist status\n\n"
                    "**How it works:**\n"
                    "1. Host opens lobby (up to 6 players, 60-second window)\n"
                    "2. Three rounds of button challenges (15 seconds each)\n"
                    "3. Payout based on team performance and individual contribution"
                ));
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            // ── heist start [entry_fee] ──────────────────────────────────────
            if (sub == "start") {
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                if (g_heist_sessions.count(chan_id)) {
                    bronx::send_message(bot, event, bronx::error("a heist is already running in this channel!"));
                    return;
                }

                int64_t entry_fee = 5000;
                if (args.size() >= 2) {
                    try {
                        auto me = db->get_user(uid);
                        entry_fee = parse_amount(args[1], me ? me->wallet : 0);
                    } catch (...) {}
                }
                if (entry_fee < 1000) entry_fee = 1000;
                if (entry_fee > 100000000LL) entry_fee = 100000000LL;

                auto me = db->get_user(uid);
                if (!me || me->wallet < entry_fee) {
                    bronx::send_message(bot, event, bronx::error("you need $" + format_number(entry_fee) + " to start a heist"));
                    return;
                }
                db->update_wallet(uid, -entry_fee);

                HeistSession s;
                s.channel_id  = chan_id;
                s.guild_id    = event.msg.guild_id;
                s.host_id     = uid;
                s.entry_fee   = entry_fee;
                s.vault_target = entry_fee * 20; // 20x entry fee = max potential vault
                s.members.push_back(uid);
                s.paid_entry.insert(uid);
                s.contribution[uid] = 0;
                g_heist_sessions[chan_id] = s;

                auto msg = build_lobby_message(g_heist_sessions[chan_id]);
                msg.channel_id = chan_id;
                bot.message_create(msg, [&bot, db, chan_id](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error()) {
                        auto msg_obj = std::get<dpp::message>(cb.value);
                        std::lock_guard<std::mutex> lock(g_heist_mutex);
                        auto it = g_heist_sessions.find(chan_id);
                        if (it != g_heist_sessions.end())
                            it->second.lobby_msg_id = msg_obj.id;
                    }
                    // 60s lobby timer
                    std::thread([&bot, db, chan_id]() {
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                        start_heist_game(bot, db, chan_id);
                    }).detach();
                });
                return;
            }

            // ── heist join ──────────────────────────────────────────────────
            if (sub == "join") {
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                auto it = g_heist_sessions.find(chan_id);
                if (it == g_heist_sessions.end()) {
                    bronx::send_message(bot, event, bronx::error("no active heist lobby in this channel"));
                    return;
                }
                if (it->second.started) {
                    bronx::send_message(bot, event, bronx::error("heist already in progress!"));
                    return;
                }
                for (uint64_t mid : it->second.members) {
                    if (mid == uid) { bronx::send_message(bot, event, bronx::error("you're already in this heist")); return; }
                }
                if ((int)it->second.members.size() >= 6) {
                    bronx::send_message(bot, event, bronx::error("heist is full (max 6 players)"));
                    return;
                }
                auto me = db->get_user(uid);
                if (!me || me->wallet < it->second.entry_fee) {
                    bronx::send_message(bot, event, bronx::error("you need $" + format_number(it->second.entry_fee) + " to join"));
                    return;
                }
                db->update_wallet(uid, -it->second.entry_fee);
                it->second.members.push_back(uid);
                it->second.paid_entry.insert(uid);
                it->second.contribution[uid] = 0;
                bronx::send_message(bot, event, bronx::success("you joined the heist! entry fee: $" + format_number(it->second.entry_fee)));
                return;
            }

            // ── heist status ────────────────────────────────────────────────
            if (sub == "status") {
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                auto it = g_heist_sessions.find(chan_id);
                if (it == g_heist_sessions.end()) {
                    bronx::send_message(bot, event, bronx::info("no active heist in this channel"));
                    return;
                }
                std::string desc = "**🏦 Heist Status**\n\n"
                    "phase: **" + (it->second.started ? PHASE_NAMES[it->second.phase] : "lobby") + "**\n"
                    "players: " + std::to_string(it->second.members.size()) + "\n"
                    "vault: $" + format_number(it->second.vault_target);
                bronx::send_message(bot, event, bronx::info(desc));
                return;
            }

            bronx::send_message(bot, event, bronx::error("unknown subcommand. use `heist` for help"));
        }
    );
    return cmd;
}

// ── Register button interaction handlers ────────────────────────────────────
inline void register_heist_handlers(dpp::cluster& bot, Database* db) {

    // Join button
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("heist_join_", 0) != 0) return;

        uint64_t chan_id = 0;
        try { chan_id = std::stoull(cid.substr(11)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        std::lock_guard<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(chan_id);
        if (it == g_heist_sessions.end() || it->second.started) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("heist lobby no longer active")).set_flags(dpp::m_ephemeral));
            return;
        }
        for (uint64_t mid : it->second.members) {
            if (mid == uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you're already in this heist")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        if ((int)it->second.members.size() >= 6) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("heist is full")).set_flags(dpp::m_ephemeral));
            return;
        }
        int64_t fee = it->second.entry_fee;
        int64_t wallet = db->get_wallet(uid);
        if (wallet < fee) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you need $" + format_number(fee) + " to join")).set_flags(dpp::m_ephemeral));
            return;
        }
        db->update_wallet(uid, -fee);
        it->second.members.push_back(uid);
        it->second.paid_entry.insert(uid);
        it->second.contribution[uid] = 0;

        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::success("you joined the heist! ($" + format_number(fee) + " deducted)")).set_flags(dpp::m_ephemeral));
    });

    // Start button (host only)
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("heist_start_", 0) != 0) return;

        uint64_t chan_id = 0;
        try { chan_id = std::stoull(cid.substr(12)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        {
            std::lock_guard<std::mutex> lock(g_heist_mutex);
            auto it = g_heist_sessions.find(chan_id);
            if (it == g_heist_sessions.end() || it->second.started) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("heist not in lobby phase")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (it->second.host_id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("only the heist host can start early")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        event.reply(dpp::ir_deferred_update_message, dpp::message());
        start_heist_game(bot, db, chan_id);
    });

    // Phase buttons (heist_p1_chanid_btnidx, heist_p2_..., heist_p3_...)
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("heist_p", 0) != 0) return;
        // format: heist_p<phase>_<chanid>_<btn>
        // e.g.    heist_p1_12345_0
        std::string rest = cid.substr(7); // "1_12345_0"
        size_t under1 = rest.find('_');
        if (under1 == std::string::npos) return;
        int phase = 0;
        try { phase = std::stoi(rest.substr(0, under1)); } catch (...) { return; }
        rest = rest.substr(under1 + 1);
        size_t under2 = rest.find('_');
        if (under2 == std::string::npos) return;
        uint64_t chan_id = 0;
        int btn_idx = 0;
        try { chan_id = std::stoull(rest.substr(0, under2)); btn_idx = std::stoi(rest.substr(under2 + 1)); } catch (...) { return; }

        uint64_t uid = event.command.get_issuing_user().id;

        std::lock_guard<std::mutex> lock(g_heist_mutex);
        auto it = g_heist_sessions.find(chan_id);
        if (it == g_heist_sessions.end() || it->second.phase != phase) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this phase is over")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Must be a participant
        bool is_member = false;
        for (uint64_t mid : it->second.members) if (mid == uid) { is_member = true; break; }
        if (!is_member) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you're not in this heist")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Only one click per phase per player
        if (it->second.phase_clicked.count(uid)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you already clicked this phase!")).set_flags(dpp::m_ephemeral));
            return;
        }

        it->second.phase_clicked.insert(uid);
        bool correct = (btn_idx == it->second.correct_btn);
        if (correct) it->second.contribution[uid]++;

        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(
                correct ? bronx::success("✅ correct! contribution +1") : bronx::error("❌ wrong button!")
            ).set_flags(dpp::m_ephemeral));
    });
}

} // namespace games
} // namespace commands
