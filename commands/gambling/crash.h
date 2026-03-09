#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

// ============================================================================
// CRASH — Rising-multiplier gambling game
// ============================================================================
// A multiplier starts at 1.00x and climbs exponentially. Cash out at any time.
// If the multiplier crashes before you cash out, you lose your bet.
//
// Crash point is hidden and pre-determined at game start.
// ~4% of games crash instantly at 1.00x (house edge).
// ============================================================================

struct CrashGame {
    uint64_t      user_id;
    uint64_t      channel_id;
    dpp::snowflake message_id = 0;
    int64_t       bet;
    double        current_mult = 1.00;
    double        crash_point;
    bool          cashed_out   = false;
    bool          crashed      = false;
    bool          active       = true;
    int           tick         = 0;
};

static std::map<uint64_t, CrashGame> active_crash_games;
static std::mutex crash_mutex;

// ── Crash point generation ────────────────────────────────────────────────────
// Uses an exponential distribution; ~4% instant bust for house edge.
// Mean crash_point ≈ 2.5x (player must cash out early for consistent profits).
static double generate_crash_point() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double r = dis(rng);
    if (r < 0.04) return 1.00; // 4% instant bust
    // Exponential: P(crash > x) = e^(-(x-1)/1.5) → mean excess ≈ 1.5x above 1.0
    return std::max(1.01, 1.0 + (-std::log(1.0 - r) * 1.5));
}

// ── Multiplier growth per second ──────────────────────────────────────────────
// Starts slow, accelerates: ~3% growth at 1x, ~8% at 2x, ~20% at 5x
static double next_mult(double m) {
    return 1.0 + (m - 1.0) * 1.08 + 0.025;
}

static std::string fmt_mult(double m) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2fx", m);
    return buf;
}

// ── Build the game embed ──────────────────────────────────────────────────────
static dpp::message build_crash_msg(const CrashGame& g) {
    dpp::embed embed;
    dpp::message msg;
    msg.id         = g.message_id;
    msg.channel_id = g.channel_id;

    if (g.cashed_out) {
        int64_t payout = (int64_t)(g.bet * g.current_mult);
        int64_t profit = payout - g.bet;
        embed = bronx::success(
            "**bet:** $" + format_number(g.bet) + "\n"
            "**cash-out:** " + fmt_mult(g.current_mult) + "\n"
            "**payout:** $" + format_number(payout) + " (+$" + format_number(profit) + ")\n\n"
            "*actual crash was at " + fmt_mult(g.crash_point) + "*"
        );
        embed.set_title("✅ Cashed out at " + fmt_mult(g.current_mult) + "!");

    } else if (g.crashed) {
        embed = bronx::error(
            "**bet:** $" + format_number(g.bet) + "\n"
            "**lost:** $" + format_number(g.bet) + "\n\n"
            "*better luck next time*"
        );
        embed.set_title("💥 CRASHED at " + fmt_mult(g.crash_point) + "!");

    } else {
        // Active — show climbing multiplier
        int bars = std::min(10, (int)((g.current_mult - 1.0) * 5.0));
        std::string bar = "```[";
        for (int i = 0; i < 10; i++) bar += (i < bars) ? "▓" : "░";
        bar += "]```";

        int64_t if_cashout = (int64_t)(g.bet * g.current_mult);
        embed = bronx::info(
            "## 🚀 " + fmt_mult(g.current_mult) + "\n"
            + bar +
            "**bet:** $" + format_number(g.bet) + "\n"
            "**value now:** $" + format_number(if_cashout) + "\n\n"
            "⚠️ *click **Cash Out** before it crashes!*"
        );
        embed.set_title("🟢 CRASH — running");

        dpp::component btn = dpp::component()
            .set_type(dpp::cot_button)
            .set_label("💸 Cash Out (" + fmt_mult(g.current_mult) + ")")
            .set_style(dpp::cos_success)
            .set_id("crash_cashout_" + std::to_string(g.user_id));
        dpp::component row;
        row.add_component(btn);
        msg.add_component(row);
    }

    msg.add_embed(embed);
    return msg;
}

// ── Ticker thread — runs 1 tick per second ────────────────────────────────────
static void run_crash_ticker(dpp::cluster& bot, Database* db, uint64_t uid) {
    // Give message_create a moment to store the message_id
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        bool crashed = false;
        bool still_active = false;

        {
            std::lock_guard<std::mutex> lk(crash_mutex);
            auto it = active_crash_games.find(uid);
            if (it == active_crash_games.end() || !it->second.active) return;
            CrashGame& g = it->second;
            if (g.cashed_out) return;

            g.tick++;
            g.current_mult = next_mult(g.current_mult);

            if (g.current_mult >= g.crash_point || g.tick > 120) {
                g.current_mult = g.crash_point;
                g.crashed = true;
                g.active  = false;
                crashed = true;
            } else {
                still_active = true;
            }
        }

        if (crashed) {
            std::lock_guard<std::mutex> lk(crash_mutex);
            auto it = active_crash_games.find(uid);
            if (it == active_crash_games.end()) return;
            // Bet already deducted at game start — nothing to return
            track_gambling_result(bot, db, it->second.channel_id, uid, false, -it->second.bet);
            bot.message_edit(build_crash_msg(it->second));

            std::thread([uid]() {
                std::this_thread::sleep_for(std::chrono::seconds(30));
                std::lock_guard<std::mutex> lk2(crash_mutex);
                active_crash_games.erase(uid);
            }).detach();
            return;
        }

        // Update display every 2 ticks to stay within edit rate limits
        if (still_active) {
            std::unique_lock<std::mutex> lk(crash_mutex);
            auto it = active_crash_games.find(uid);
            if (it == active_crash_games.end() || !it->second.active) return;
            int t = it->second.tick;
            CrashGame copy = it->second;
            lk.unlock();
            if (t % 2 == 0) bot.message_edit(build_crash_msg(copy));
        }
    }
}

// ── Command ───────────────────────────────────────────────────────────────────
inline Command* get_crash_command(Database* db) {
    static Command* cmd = new Command(
        "crash",
        "bet on a rising multiplier — cash out before it crashes!",
        "gambling",
        {"cr"},
        false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::info(
                    "**💥 Crash**\n\n"
                    "A multiplier starts at **1.00x** and climbs. Cash out before it crashes!\n\n"
                    "**Usage:** `crash <bet>`\n"
                    "**Example:** `crash 5000`\n\n"
                    "• The longer you wait, the bigger the payout\n"
                    "• But the crash could happen any second...\n"
                    "• ~50% of games crash before 2x\n"
                    "• Historic high multipliers reach 50x+"
                ));
                return;
            }

            if (!db->try_claim_cooldown(uid, "crash", 5)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait 5 seconds between crash games"));
                return;
            }

            {
                std::lock_guard<std::mutex> lk(crash_mutex);
                if (active_crash_games.count(uid)) {
                    bronx::send_message(bot, event, bronx::error("you already have an active crash game!"));
                    return;
                }
            }

            auto user = db->get_user(uid);
            if (!user) return;

            int64_t bet;
            try { bet = parse_amount(args[0], user->wallet); }
            catch (...) { bronx::send_message(bot, event, bronx::error("invalid bet amount")); return; }

            if (bet < 100)      { bronx::send_message(bot, event, bronx::error("minimum crash bet is $100")); return; }
            if (bet > MAX_BET)  { bronx::send_message(bot, event, bronx::error("maximum bet is $2,000,000,000")); return; }
            if (bet > user->wallet) { bronx::send_message(bot, event, bronx::error("you don't have that much")); return; }

            db->update_wallet(uid, -bet);

            CrashGame g;
            g.user_id     = uid;
            g.channel_id  = event.msg.channel_id;
            g.bet         = bet;
            g.crash_point = generate_crash_point();

            {
                std::lock_guard<std::mutex> lk(crash_mutex);
                active_crash_games[uid] = g;
            }

            auto init_msg = build_crash_msg(g);
            init_msg.channel_id = event.msg.channel_id;

            bot.message_create(init_msg, [&bot, db, uid](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::lock_guard<std::mutex> lk(crash_mutex);
                    auto it = active_crash_games.find(uid);
                    if (it != active_crash_games.end()) {
                        db->update_wallet(uid, it->second.bet); // refund on error
                        active_crash_games.erase(it);
                    }
                    return;
                }
                {
                    auto msg_obj = std::get<dpp::message>(cb.value);
                    std::lock_guard<std::mutex> lk(crash_mutex);
                    auto it = active_crash_games.find(uid);
                    if (it != active_crash_games.end())
                        it->second.message_id = msg_obj.id;
                }
                std::thread([&bot, db, uid]() {
                    run_crash_ticker(bot, db, uid);
                }).detach();
            });
        }
    );
    return cmd;
}

// ── Cash Out button handler ───────────────────────────────────────────────────
inline void register_crash_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("crash_cashout_", 0) != 0) return;

        uint64_t uid;
        try { uid = std::stoull(event.custom_id.substr(14)); }
        catch (...) { return; }

        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this isn't your crash game!")).set_flags(dpp::m_ephemeral));
            return;
        }

        int64_t payout = 0;
        bool ok = false;
        CrashGame game_copy;

        {
            std::lock_guard<std::mutex> lk(crash_mutex);
            auto it = active_crash_games.find(uid);
            if (it == active_crash_games.end() || !it->second.active
                || it->second.cashed_out || it->second.crashed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("game is no longer active (or already crashed)"))
                        .set_flags(dpp::m_ephemeral));
                return;
            }
            auto& g       = it->second;
            g.cashed_out  = true;
            g.active      = false;
            payout        = (int64_t)(g.bet * g.current_mult);
            game_copy     = g;
            ok            = true;
        }

        if (!ok) return;

        db->update_wallet(uid, payout);
        int64_t profit = payout - game_copy.bet;
        track_gambling_result(bot, db, game_copy.channel_id, uid, true, profit);

        event.reply(dpp::ir_update_message, build_crash_msg(game_copy));

        // Clean up after 30 seconds
        std::thread([uid]() {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            std::lock_guard<std::mutex> lk(crash_mutex);
            active_crash_games.erase(uid);
        }).detach();
    });
}

} // namespace gambling
} // namespace commands
