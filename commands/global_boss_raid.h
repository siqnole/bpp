#pragma once
#include "command.h"
#include "embed_style.h"
#include "database/core/database.h"
#include "economy_core.h"
#include "../server_logger.h"
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

// ============================================================================
// BOSS RAIDS — Cooperative multiplayer combat event
// ============================================================================
//
// Players team up (2–8) to fight a boss together over 3 combat rounds.
//
// Flow:
//   raid start [entry_fee]  — host opens a lobby ($10,000 default; 30s window)
//   players click "⚔️ Join" — up to 8 players
//   host clicks "▶ Start"  — force-starts
//
//   Combat (3 rounds, 15s each):
//     ⚔️ Attack  — deal 50–80 boss damage; counted toward payout share
//     🛡️ Defend  — halve incoming damage from boss this round
//     💊 Heal    — restore 30 HP (up to 100 max)
//
//   After each round: boss attacks every player 20-40 dmg (halved if defended)
//   Dead players (HP ≤ 0) can no longer act but receive consolation payout (20%)
//
//   Payout pool = entry_fee × participants × 2
//     — distributed proportionally to damage dealt
//     — dead players receive 20% of their fair share
//
// Tables: boss_raids, raid_participants
// ============================================================================

namespace commands {

struct RaidSession {
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t host_id;
    int64_t  entry_fee;
    int64_t  total_pool;        // (entry_fee × members × 2) + taxed_pool
    int64_t  taxed_pool = 0;    // amount taken from the rich

    std::vector<uint64_t>       members;
    std::map<uint64_t, int>     player_hp;          // 100 starting HP
    std::map<uint64_t, int64_t> damage_dealt;       // total dmg to boss
    std::map<uint64_t, std::string> round_actions;  // "attack"/"defend"/"heal" this round
    std::set<uint64_t>          round_acted;        // who has chosen this round
    std::set<uint64_t>          defended_this_round;// for boss attack reduction

    bool started = false;
    int  round   = 0;           // 0=lobby, 1/2/3=combat, 4=done
    int64_t boss_hp_max  = 0;
    int64_t boss_hp      = 0;

    dpp::snowflake lobby_msg_id = 0;
    dpp::snowflake round_msg_id = 0;
};

static std::mutex g_raid_mutex;
static std::map<uint64_t, RaidSession> g_raid_sessions; // keyed by channel_id


// ── Format boss HP bar ────────────────────────────────────────────────────

// ── Format boss HP bar ────────────────────────────────────────────────────
static std::string boss_hp_bar(int64_t hp, int64_t max_hp) {
    int filled = (max_hp > 0) ? (int)(10 * hp / max_hp) : 0;
    std::string bar = "```[";
    for (int i = 0; i < 10; i++) bar += (i < filled) ? "█" : "░";
    bar += "] " + format_number(hp) + " / " + format_number(max_hp) + "```";
    return bar;
}

// ── Build lobby message ───────────────────────────────────────────────────
static dpp::message build_raid_lobby(const RaidSession& s) {
    std::string desc = "**🐉 Boss Raid — Lobby**\n\n"
        "entry fee: **$" + format_number(s.entry_fee) + "**\n"
        "prize pool: **$" + format_number(s.entry_fee * 2) + " × players**\n\n"
        "**Raiders (" + std::to_string(s.members.size()) + "/8):**\n";
    for (uint64_t mid : s.members)
        desc += "• <@" + std::to_string(mid) + ">\n";
    if (s.taxed_pool > 0) {
        desc += "\n💎 **Necessary Poison Active**\n"
                "The server's elite have 'donated' **$" + format_number(s.taxed_pool) + "** to the prize pool!";
    }

    dpp::component join_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_label("⚔️ Join Raid")
        .set_style(dpp::cos_danger)
        .set_id("raid_join_" + std::to_string(s.channel_id));

    dpp::component start_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_label("▶ Start Raid")
        .set_style(dpp::cos_primary)
        .set_id("raid_start_" + std::to_string(s.channel_id));

    dpp::component row;
    row.add_component(join_btn);
    row.add_component(start_btn);

    return dpp::message().add_embed(bronx::info(desc)).add_component(row);
}

// ── Build combat round message ────────────────────────────────────────────
static const std::vector<std::string> ROUND_NAMES = {"", "Round 1 — Charge!", "Round 2 — Advance!", "Round 3 — Final Stand!"};

static dpp::message build_raid_round(const RaidSession& s) {
    std::string desc = "**🐉 Boss Raid — " + ROUND_NAMES[s.round] + "**\n\n"
        "**Boss HP:**\n" + boss_hp_bar(s.boss_hp, s.boss_hp_max) +
        "\n**Prize Pool:** **$" + format_number(s.total_pool) + "**" + (s.taxed_pool > 0 ? " (💎 Necessary Poison active)" : "") +
        "\n\n**Raiders:**\n";
    for (uint64_t mid : s.members) {
        int hp = s.player_hp.count(mid) ? s.player_hp.at(mid) : 0;
        bool acted = s.round_acted.count(mid) > 0;
        std::string hp_str = (hp <= 0) ? "💀 DEAD" : ("❤️ " + std::to_string(hp) + " HP");
        desc += (acted ? bronx::EMOJI_CHECK : std::string("⏳")) + std::string(" <@") + std::to_string(mid) + "> — " + hp_str + "\n";
    }
    desc += "\n*15 seconds to choose your action!*";

    dpp::component atk = dpp::component().set_type(dpp::cot_button)
        .set_label("⚔️ Attack").set_style(dpp::cos_danger)
        .set_id("raid_action_" + std::to_string(s.channel_id) + "_attack");
    dpp::component def = dpp::component().set_type(dpp::cot_button)
        .set_label("🛡️ Defend").set_style(dpp::cos_success)
        .set_id("raid_action_" + std::to_string(s.channel_id) + "_defend");
    dpp::component heal = dpp::component().set_type(dpp::cot_button)
        .set_label("💊 Heal").set_style(dpp::cos_secondary)
        .set_id("raid_action_" + std::to_string(s.channel_id) + "_heal");

    dpp::component row;
    row.add_component(atk);
    row.add_component(def);
    row.add_component(heal);

    return dpp::message().add_embed(bronx::info(desc)).add_component(row);
}

// ── Execute payouts and close raid ───────────────────────────────────────
static void execute_raid_payout(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    std::unique_lock<std::mutex> lock(g_raid_mutex);
    auto it = g_raid_sessions.find(channel_id);
    if (it == g_raid_sessions.end()) return;
    RaidSession s = it->second;
    g_raid_sessions.erase(it);
    lock.unlock();

    if (s.members.empty()) return;

    int64_t total_damage = 0;
    for (auto& [uid, dmg] : s.damage_dealt) total_damage += dmg;

    bool boss_killed = (s.boss_hp <= 0);
    // If boss not killed, pool scales by how much damage was dealt
    double boss_pct = (s.boss_hp_max > 0)
        ? std::min(1.0, (double)(s.boss_hp_max - s.boss_hp) / s.boss_hp_max)
        : 1.0;
    int64_t actual_pool = (int64_t)(s.total_pool * (boss_killed ? 1.0 : boss_pct));

    std::string result_desc = std::string(boss_killed ? "☠️ **BOSS DEFEATED!**" : "💀 **Raid Over — Boss Survived**")
        + "\n\nBoss HP remaining: " + (boss_killed ? "0" : format_number(s.boss_hp))
        + "\nTotal payout: **$" + format_number(actual_pool) + "**\n\n"
        "**Payouts:**\n";

    if (total_damage == 0) {
        // Nobody hit anything — consolation refund
        for (uint64_t mid : s.members) {
            db->update_wallet(mid, s.entry_fee);
        }
        result_desc += "No damage dealt — entry fees refunded.";
    } else {
        for (uint64_t mid : s.members) {
            int64_t dmg = s.damage_dealt.count(mid) ? s.damage_dealt.at(mid) : 0;
            int hp = s.player_hp.count(mid) ? s.player_hp.at(mid) : 0;
            bool survived = hp > 0;
            double share = (double)dmg / (double)total_damage;
            int64_t payout = (int64_t)(actual_pool * share);
            if (!survived) payout = payout * 20 / 100; // 20% consolation if dead
            if (payout > 0) db->update_wallet(mid, payout);

            std::string status = survived ? "⚔️ survived" : "💀 fell";
            result_desc += "<@" + std::to_string(mid) + "> — dmg: " + format_number(dmg)
                + " | " + status + " → **$" + format_number(payout) + "**\n";
        }
    }

    if (s.taxed_pool > 0) {
        result_desc += "\n*This raid was subsidized by **$" + format_number(s.taxed_pool) + "** from the server's elite via Necessary Poison.*";
    }

    dpp::message msg;
    msg.add_embed(boss_killed ? bronx::success(result_desc) : bronx::error(result_desc));
    msg.channel_id = channel_id;
    bot.message_create(msg);
}

// ── Process end of round (resolve actions, boss attacks) ─────────────────
static void resolve_raid_round(dpp::cluster& bot, Database* db, uint64_t channel_id);

static void start_raid_round(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    // Send the round message
    {
        std::lock_guard<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(channel_id);
        if (it == g_raid_sessions.end()) return;
        auto msg = build_raid_round(it->second);
        msg.channel_id = channel_id;
        bot.message_create(msg, [&bot, db, channel_id](const dpp::confirmation_callback_t& cb) {
            if (!cb.is_error()) {
                auto msg_obj = std::get<dpp::message>(cb.value);
                std::lock_guard<std::mutex> lock(g_raid_mutex);
                auto it2 = g_raid_sessions.find(channel_id);
                if (it2 != g_raid_sessions.end())
                    it2->second.round_msg_id = msg_obj.id;
            }
        });
    }
    // 15-second timer then resolve
    std::thread([&bot, db, channel_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        resolve_raid_round(bot, db, channel_id);
    }).detach();
}

static void resolve_raid_round(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    std::mt19937 rng(std::random_device{}());
    std::string resolution;

    {
        std::lock_guard<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(channel_id);
        if (it == g_raid_sessions.end()) return;
        RaidSession& s = it->second;

        // Process each player action (default to attack if no action)
        for (uint64_t mid : s.members) {
            if (s.player_hp.count(mid) && s.player_hp.at(mid) <= 0) continue; // dead
            std::string action = s.round_actions.count(mid) ? s.round_actions.at(mid) : "attack";

            if (action == "attack") {
                int dmg = 50 + (int)(rng() % 31); // 50-80
                s.boss_hp -= dmg;
                s.damage_dealt[mid] += dmg;
            } else if (action == "heal") {
                int& hp = s.player_hp[mid];
                hp = std::min(100, hp + 30);
            } else if (action == "defend") {
                s.defended_this_round.insert(mid);
            }
        }
        if (s.boss_hp < 0) s.boss_hp = 0;

        // Boss counter-attacks all living players
        for (uint64_t mid : s.members) {
            if (s.player_hp.count(mid) && s.player_hp.at(mid) <= 0) continue;
            int boss_dmg = 20 + (int)(rng() % 21); // 20-40
            if (s.defended_this_round.count(mid)) boss_dmg = boss_dmg / 2;
            s.player_hp[mid] -= boss_dmg;
        }

        // Clear per-round state  
        s.round_actions.clear();
        s.round_acted.clear();
        s.defended_this_round.clear();

        // Advance
        s.round++;
    }

    // Check if done (boss dead or all rounds over)
    {
        std::lock_guard<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(channel_id);
        if (it == g_raid_sessions.end()) return;
        if (it->second.boss_hp <= 0 || it->second.round > 3) {
            // done
        } else {
            // Continue — start next round
            start_raid_round(bot, db, channel_id);
            return;
        }
    }
    execute_raid_payout(bot, db, channel_id);
}

// ── Launch raid from lobby ────────────────────────────────────────────────
static void launch_raid(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    {
        std::unique_lock<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(channel_id);
        if (it == g_raid_sessions.end() || it->second.started) return;

        if ((int)it->second.members.size() < 2) {
            RaidSession s = it->second;
            g_raid_sessions.erase(it);
            lock.unlock();
            for (uint64_t mid : s.members) db->update_wallet(mid, s.entry_fee);
            dpp::message msg;
            msg.add_embed(bronx::error("raid cancelled — not enough players (need at least 2). entry fees refunded."));
            msg.channel_id = channel_id;
            bot.message_create(msg);
            return;
        }

        // Init raid
        int n = (int)it->second.members.size();
        it->second.started      = true;
        it->second.round        = 1;
        it->second.boss_hp_max  = (int64_t)n * 5000;
        it->second.boss_hp      = it->second.boss_hp_max;
        
        // --- Necessary Poison: Tax the Top 3 Richest ---
        auto wealthy_users = db->get_guild_top_wealthy_users(it->second.guild_id, 3);
        int64_t total_taxed = 0;
        for (auto& wu : wealthy_users) {
            if (wu.value < 1000000) continue; // targeting users with $1M+
            
            int64_t tax = (int64_t)((double)wu.value * 0.025); // 2.5% tax
            if (tax < 5000) tax = 5000; // minimum impact
            
            // Deduct from wallet first, then bank
            int64_t wallet = db->get_wallet(wu.user_id);
            if (wallet >= tax) {
                db->update_wallet(wu.user_id, -tax);
            } else {
                int64_t remaining = tax - wallet;
                db->update_wallet(wu.user_id, -wallet);
                db->update_bank(wu.user_id, -remaining);
            }
            total_taxed += tax;

            // Notify the victim (async)
            uint64_t victim_id = wu.user_id;
            uint64_t g_id = it->second.guild_id;
            std::string tax_str = format_number(tax);
            bot.direct_message_create(victim_id, dpp::message("💎 **Necessary Poison: Wealth Redistribution**\n\nThe server elite must provide. You have been taxed **$" + tax_str + "** (2.5%) to fund a local Boss Raid. Thank you for your service."), [victim_id](const dpp::confirmation_callback_t& cb){});
        }
        
        it->second.taxed_pool = total_taxed;
        it->second.total_pool = (it->second.entry_fee * (int64_t)n * 2LL) + total_taxed;

        // Log "Necessary Poison" to economy-logs
        if (total_taxed > 0) {
            dpp::embed log_emb = bronx::info("Economy Transaction: Necessary Poison")
                .set_color(0x3B82F6)
                .set_title("💎 Wealth Redistribution")
                .add_field("Total Taxed", "$" + format_number(total_taxed), true)
                .add_field("Victims", std::to_string(wealthy_users.size()), true)
                .add_field("Raid Target", "Boss Raid in <#" + std::to_string(it->second.channel_id) + ">", false);
            log_emb.set_timestamp(time(0));
            bronx::logger::ServerLogger::get().log_embed(it->second.guild_id, bronx::logger::LOG_TYPE_ECONOMY, log_emb);
        }

        for (uint64_t mid : it->second.members) {
            it->second.player_hp[mid]    = 100;
            it->second.damage_dealt[mid] = 0;
        }
    }
    start_raid_round(bot, db, channel_id);
}

// ============================================================================
// COMMAND BUILDER + INTERACTION HANDLERS
// ============================================================================

inline std::vector<Command*> get_boss_raid_commands(Database* db) {

    static Command* cmd = new Command(
        "raid",
        "start a cooperative boss raid with your crew",
        "games",
        {"bossraid"},
        false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t uid     = event.msg.author.id;
            uint64_t chan_id = event.msg.channel_id;

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::info(
                    "**🐉 Boss Raids**\n\n"
                    "Team up to defeat a powerful boss for massive rewards!\n\n"
                    "**Commands:**\n"
                    "`raid start [entry_fee]` — open a raid lobby (default $10,000)\n"
                    "`raid status` — check active raid status\n\n"
                    "**Actions (during combat):**\n"
                    "⚔️ **Attack** — deal 50-80 damage (earns payout share)\n"
                    "🛡️ **Defend** — halve boss damage this round\n"
                    "💊 **Heal**   — restore 30 HP\n\n"
                    "**Mechanics:**\n"
                    "• Boss HP = 5,000 × number of raiders\n"
                    "• 3 combat rounds (15 seconds each)\n"
                    "• Payout scales with boss damage dealt\n"
                    "• Fallen raiders still get 20% consolation"
                ));
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            if (sub == "start") {
                std::lock_guard<std::mutex> lock(g_raid_mutex);
                if (g_raid_sessions.count(chan_id)) {
                    bronx::send_message(bot, event, bronx::error("a raid is already running in this channel!"));
                    return;
                }

                int64_t entry_fee = 10000;
                if (args.size() >= 2) {
                    try {
                        auto me = db->get_user(uid);
                        entry_fee = parse_amount(args[1], me ? me->wallet : 0);
                    } catch (...) {}
                }
                if (entry_fee < 1000) entry_fee = 1000;
                if (entry_fee > 500000000LL) entry_fee = 500000000LL;

                auto me = db->get_user(uid);
                if (!me || me->wallet < entry_fee) {
                    bronx::send_message(bot, event, bronx::error("you need $" + format_number(entry_fee) + " to start a raid"));
                    return;
                }
                db->update_wallet(uid, -entry_fee);

                RaidSession s;
                s.channel_id = chan_id;
                s.guild_id   = event.msg.guild_id;
                s.host_id    = uid;
                s.entry_fee  = entry_fee;
                s.total_pool = 0; // set on launch
                s.members.push_back(uid);
                g_raid_sessions[chan_id] = s;

                auto msg = build_raid_lobby(g_raid_sessions[chan_id]);
                msg.channel_id = chan_id;
                bot.message_create(msg, [&bot, db, chan_id](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error()) {
                        auto msg_obj = std::get<dpp::message>(cb.value);
                        std::lock_guard<std::mutex> lock(g_raid_mutex);
                        auto it = g_raid_sessions.find(chan_id);
                        if (it != g_raid_sessions.end())
                            it->second.lobby_msg_id = msg_obj.id;
                    }
                    // 30s lobby timer
                    std::thread([&bot, db, chan_id]() {
                        std::this_thread::sleep_for(std::chrono::seconds(30));
                        launch_raid(bot, db, chan_id);
                    }).detach();
                });
                return;
            }

            if (sub == "status") {
                std::lock_guard<std::mutex> lock(g_raid_mutex);
                auto it = g_raid_sessions.find(chan_id);
                if (it == g_raid_sessions.end()) {
                    bronx::send_message(bot, event, bronx::info("no active raid in this channel"));
                    return;
                }
                std::string desc = "**🐉 Raid Status**\n\n"
                    "phase: **" + (it->second.started ? ROUND_NAMES[it->second.round] : "lobby") + "**\n"
                    "players: " + std::to_string(it->second.members.size()) + "\n"
                    "boss HP: " + (it->second.started ? format_number(it->second.boss_hp) + " / " + format_number(it->second.boss_hp_max) : "not started");
                bronx::send_message(bot, event, bronx::info(desc));
                return;
            }

            bronx::send_message(bot, event, bronx::error("unknown subcommand. use `raid` for help"));
        }
    );

    return {cmd};
}

inline void register_boss_raid_interactions(dpp::cluster& bot, Database* db) {
    // Join button
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("raid_join_", 0) != 0) return;
        uint64_t chan_id = 0;
        try { chan_id = std::stoull(cid.substr(10)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        std::lock_guard<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(chan_id);
        if (it == g_raid_sessions.end() || it->second.started) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("raid lobby no longer active")).set_flags(dpp::m_ephemeral));
            return;
        }
        for (uint64_t mid : it->second.members) {
            if (mid == uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you're already in this raid")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        if ((int)it->second.members.size() >= 8) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("raid is full (max 8 raiders)")).set_flags(dpp::m_ephemeral));
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
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::success("⚔️ you joined the raid! ($" + format_number(fee) + " deducted)")).set_flags(dpp::m_ephemeral));
    });

    // Start button (host only)
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("raid_start_", 0) != 0) return;
        uint64_t chan_id = 0;
        try { chan_id = std::stoull(cid.substr(11)); } catch (...) { return; }
        uint64_t uid = event.command.get_issuing_user().id;

        {
            std::lock_guard<std::mutex> lock(g_raid_mutex);
            auto it = g_raid_sessions.find(chan_id);
            if (it == g_raid_sessions.end() || it->second.started) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("raid not in lobby phase")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (it->second.host_id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("only the raid host can start early")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        event.reply(dpp::ir_deferred_update_message, dpp::message());
        launch_raid(bot, db, chan_id);
    });

    // Combat action buttons: raid_action_<chanid>_attack/defend/heal
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("raid_action_", 0) != 0) return;
        // format: raid_action_<chanid>_<attack|defend|heal>
        std::string rest = cid.substr(12); // "<chanid>_<action>"
        size_t under = rest.rfind('_');
        if (under == std::string::npos) return;
        uint64_t chan_id = 0;
        std::string action;
        try { chan_id = std::stoull(rest.substr(0, under)); action = rest.substr(under + 1); } catch (...) { return; }

        uint64_t uid = event.command.get_issuing_user().id;

        std::lock_guard<std::mutex> lock(g_raid_mutex);
        auto it = g_raid_sessions.find(chan_id);
        if (it == g_raid_sessions.end() || !it->second.started || it->second.round < 1 || it->second.round > 3) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("no active combat round")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Must be a participant
        bool is_member = false;
        for (uint64_t mid : it->second.members) if (mid == uid) { is_member = true; break; }
        if (!is_member) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you're not in this raid")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Dead players can't act
        if (it->second.player_hp.count(uid) && it->second.player_hp.at(uid) <= 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("💀 you have fallen in battle...")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Already acted this round?
        if (it->second.round_acted.count(uid)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you already chose your action this round!")).set_flags(dpp::m_ephemeral));
            return;
        }

        it->second.round_actions[uid] = action;
        it->second.round_acted.insert(uid);

        std::string action_label = (action == "attack") ? "⚔️ Attack" : (action == "defend") ? "🛡️ Defend" : "💊 Heal";
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().add_embed(bronx::success("you chose **" + action_label + "**!")).set_flags(dpp::m_ephemeral));
    });
}

} // namespace commands
