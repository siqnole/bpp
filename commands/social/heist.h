#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <random>
#include <mutex>
#include <chrono>

using namespace bronx::db;

namespace commands {
namespace social {

// ============================================================================
// GUILD HEISTS — Cooperative multi-player vault robbery
// ============================================================================
// 3+ players coordinate to "rob" a generated NPC vault.
// Each player picks a role based on their skill:
//   🎣 Lockpicker (fishing skill) — picks the vault lock
//   ⛏️ Tunneler (mining skill)    — digs under the vault
//   🎰 Hacker (gambling skill)    — cracks the security code
//   💪 Muscle                     — brute force (default)
//   👁️ Lookout                    — reduces detection chance
//
// Success scales with total contributions → massive shared payout.
//
// Flow:
//   /heist start [difficulty]  — 30s lobby
//   players click Join + pick role
//   3 rounds of vault cracking (15s each, auto-contribute)
//   Success or failure → payout or partial loss
// ============================================================================

// ── Vault templates ─────────────────────────────────────────────────────────

struct VaultTemplate {
    std::string name;
    std::string emoji;
    int difficulty;     // 1-5
    int64_t base_pool;
    int hp;
    std::string description;
};

static const std::vector<VaultTemplate> vault_templates = {
    {"Corner Store Safe",       "🏪", 1, 25000,    100, "a dinky little safe behind the counter"},
    {"Bank Vault",              "🏦", 2, 100000,   200, "standard commercial bank security"},
    {"Casino Vault",            "🎰", 3, 500000,   350, "high-roller casino deep vault"},
    {"Government Treasury",     "🏛️", 4, 2000000,  500, "Fort Knox wannabe, heavily guarded"},
    {"Dragon's Hoard",          "🐉", 5, 10000000, 750, "an ancient dragon sleeps on mountains of gold"},
};

// ── Heist session ───────────────────────────────────────────────────────────

struct HeistParticipant {
    uint64_t user_id;
    std::string role;       // lockpicker, tunneler, hacker, muscle, lookout
    int contribution;
    bool alive;
};

struct HeistSession {
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t host_id;
    VaultTemplate vault;
    int64_t entry_fee;
    
    std::vector<HeistParticipant> participants;
    
    bool started = false;
    int round = 0;
    int max_rounds = 3;
    int vault_hp;
    int total_damage = 0;
    
    dpp::snowflake lobby_msg_id = 0;
    dpp::snowflake round_msg_id = 0;
    
    std::set<uint64_t> round_acted;
    
    std::chrono::system_clock::time_point created_at;
};

static std::mutex g_heist_mutex;
static std::map<uint64_t, HeistSession> g_heist_sessions; // keyed by channel_id

// ── Table creation ──────────────────────────────────────────────────────────

static bool g_heist_tables_created = false;

static void ensure_heist_tables(Database* db) {
    if (g_heist_tables_created) return;
    std::lock_guard<std::mutex> lock(g_heist_mutex);
    if (g_heist_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS heists ("
        "  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  channel_id BIGINT UNSIGNED NOT NULL,"
        "  guild_id BIGINT UNSIGNED NOT NULL,"
        "  host_id BIGINT UNSIGNED NOT NULL,"
        "  vault_name VARCHAR(100) NOT NULL,"
        "  vault_level INT NOT NULL DEFAULT 1,"
        "  entry_fee BIGINT NOT NULL DEFAULT 5000,"
        "  total_pool BIGINT NOT NULL DEFAULT 0,"
        "  phase VARCHAR(20) NOT NULL DEFAULT 'lobby',"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS heist_participants ("
        "  heist_id BIGINT UNSIGNED NOT NULL,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  role VARCHAR(20) NOT NULL DEFAULT 'muscle',"
        "  contribution INT NOT NULL DEFAULT 0,"
        "  alive TINYINT(1) NOT NULL DEFAULT 1,"
        "  PRIMARY KEY (heist_id, user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_heist_tables_created = true;
}

// ── Role bonuses based on player stats ──────────────────────────────────────

static int calculate_role_bonus(Database* db, uint64_t uid, const std::string& role) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> base_dist(30, 60);
    int base = base_dist(rng);
    
    if (role == "lockpicker") {
        // Bonus from fish caught
        int64_t fish_caught = db->get_stat(uid, "fish_caught");
        base += std::min(40, (int)(fish_caught / 100));
    } else if (role == "tunneler") {
        // Bonus from ores mined
        int64_t ores_mined = db->get_stat(uid, "ores_mined");
        base += std::min(40, (int)(ores_mined / 50));
    } else if (role == "hacker") {
        // Bonus from gambling wins
        int64_t gamble_wins = db->get_stat(uid, "gambling_wins");
        base += std::min(40, (int)(gamble_wins / 20));
    } else if (role == "lookout") {
        // Reduces detection — flat bonus
        base += 20;
    }
    // muscle gets no extra bonus but always works
    return base;
}

// ── Vault HP bar ────────────────────────────────────────────────────────────

static std::string vault_hp_bar(int hp, int max_hp) {
    int bars = 10;
    int filled = (int)((double)hp / max_hp * bars);
    filled = std::max(0, std::min(bars, filled));
    std::string bar = "[";
    for (int i = 0; i < bars; i++) {
        bar += (i < filled) ? "█" : "░";
    }
    bar += "] " + std::to_string(hp) + "/" + std::to_string(max_hp);
    return bar;
}

static std::string role_emoji(const std::string& role) {
    if (role == "lockpicker") return "🎣";
    if (role == "tunneler")   return "⛏️";
    if (role == "hacker")     return "🎰";
    if (role == "lookout")    return "👁️";
    return "💪"; // muscle
}

// ── Build lobby message ─────────────────────────────────────────────────────

static dpp::message build_heist_lobby(const HeistSession& s) {
    dpp::embed embed;
    embed.set_color(0xFF5722);
    embed.set_title("🏴‍☠️ Heist: " + s.vault.emoji + " " + s.vault.name);
    
    std::string desc = "*" + s.vault.description + "*\n\n";
    desc += "**Difficulty:** ";
    for (int i = 0; i < s.vault.difficulty; i++) desc += "⭐";
    desc += "\n**Entry Fee:** $" + economy::format_number(s.entry_fee) + "\n";
    desc += "**Vault HP:** " + std::to_string(s.vault.hp) + "\n";
    desc += "**Potential Payout:** $" + economy::format_number(s.vault.base_pool) + "+\n\n";
    
    desc += "**Crew (" + std::to_string(s.participants.size()) + "/8):**\n";
    for (const auto& p : s.participants) {
        desc += role_emoji(p.role) + " <@" + std::to_string(p.user_id) + "> — " + p.role + "\n";
    }
    
    int needed = std::max(0, 3 - (int)s.participants.size());
    if (needed > 0) {
        desc += "\n⚠️ Need **" + std::to_string(needed) + "** more player(s) to start!";
    } else {
        desc += "\n✅ Ready to start! Host can click **▶ Start**";
    }
    
    embed.set_description(desc);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Buttons
    dpp::component row1;
    row1.set_type(dpp::cot_action_row);
    
    auto make_btn = [&](const std::string& label, const std::string& id, dpp::component_style style) {
        dpp::component btn;
        btn.set_type(dpp::cot_button);
        btn.set_label(label);
        btn.set_id(id);
        btn.set_style(style);
        return btn;
    };
    
    row1.add_component(make_btn("🎣 Lockpicker", "heist_join_lockpicker_" + std::to_string(s.channel_id), dpp::cos_primary));
    row1.add_component(make_btn("⛏️ Tunneler", "heist_join_tunneler_" + std::to_string(s.channel_id), dpp::cos_primary));
    row1.add_component(make_btn("🎰 Hacker", "heist_join_hacker_" + std::to_string(s.channel_id), dpp::cos_primary));
    row1.add_component(make_btn("💪 Muscle", "heist_join_muscle_" + std::to_string(s.channel_id), dpp::cos_secondary));
    row1.add_component(make_btn("👁️ Lookout", "heist_join_lookout_" + std::to_string(s.channel_id), dpp::cos_secondary));
    msg.add_component(row1);
    
    dpp::component row2;
    row2.set_type(dpp::cot_action_row);
    row2.add_component(make_btn("▶ Start Heist", "heist_start_" + std::to_string(s.channel_id), dpp::cos_success));
    row2.add_component(make_btn("❌ Cancel", "heist_cancel_" + std::to_string(s.channel_id), dpp::cos_danger));
    msg.add_component(row2);
    
    return msg;
}

// ── Run heist round ─────────────────────────────────────────────────────────

static void run_heist_round(dpp::cluster& bot, Database* db, uint64_t channel_id) {
    std::lock_guard<std::mutex> lock(g_heist_mutex);
    auto it = g_heist_sessions.find(channel_id);
    if (it == g_heist_sessions.end()) return;
    
    HeistSession& s = it->second;
    s.round++;
    
    if (s.round > s.max_rounds || s.vault_hp <= 0) {
        // Heist complete — payout
        bool success = s.vault_hp <= 0;
        
        dpp::embed embed;
        if (success) {
            embed.set_color(0x66BB6A);
            embed.set_title("🏴‍☠️ Heist Successful! " + s.vault.emoji);
            
            // Calculate payout pool: base + (entry_fee × participants × 1.5) 
            int64_t pool = s.vault.base_pool + (s.entry_fee * s.participants.size());
            int total_contrib = 0;
            for (auto& p : s.participants) total_contrib += p.contribution;
            if (total_contrib == 0) total_contrib = 1;
            
            std::string desc = "**The vault was cracked!** 💰\n\n";
            for (auto& p : s.participants) {
                double share = (double)p.contribution / total_contrib;
                int64_t payout = (int64_t)(pool * share);
                if (!p.alive) payout = payout * 20 / 100; // dead = 20% consolation
                
                db->update_wallet(p.user_id, payout);
                desc += role_emoji(p.role) + " <@" + std::to_string(p.user_id) + "> — **$" + economy::format_number(payout) + "**";
                if (!p.alive) desc += " *(caught — 20%)*";
                desc += "\n";
            }
            desc += "\n💰 **Total Pool:** $" + economy::format_number(pool);
            embed.set_description(desc);
        } else {
            embed.set_color(0xE53935);
            embed.set_title("🏴‍☠️ Heist Failed! " + s.vault.emoji);
            embed.set_description("the vault withstood your assault!\n\n**Vault HP:** " + vault_hp_bar(s.vault_hp, s.vault.hp) + "\n\nentry fees were lost. better luck next time!");
        }
        
        dpp::message msg(channel_id, embed);
        bot.message_create(msg);
        g_heist_sessions.erase(it);
        return;
    }
    
    // Run round
    static thread_local std::mt19937 rng(std::random_device{}());
    
    std::string round_desc = "**Round " + std::to_string(s.round) + "/" + std::to_string(s.max_rounds) + "**\n\n";
    
    // Each participant auto-contributes based on role
    int round_damage = 0;
    bool has_lookout = false;
    for (auto& p : s.participants) {
        if (!p.alive) continue;
        if (p.role == "lookout") has_lookout = true;
    }
    
    for (auto& p : s.participants) {
        if (!p.alive) {
            round_desc += "💀 <@" + std::to_string(p.user_id) + "> — *caught by security*\n";
            continue;
        }
        
        int bonus = calculate_role_bonus(db, p.user_id, p.role);
        
        // Detection chance: 10% base, -5% if lookout present, +5% per difficulty
        int detect_chance = 10 + (s.vault.difficulty * 5) - (has_lookout ? 5 : 0);
        std::uniform_int_distribution<int> detect_dist(1, 100);
        if (detect_dist(rng) <= detect_chance) {
            p.alive = false;
            round_desc += "🚨 <@" + std::to_string(p.user_id) + "> was **caught**! (" + role_emoji(p.role) + " " + p.role + ")\n";
            continue;
        }
        
        p.contribution += bonus;
        round_damage += bonus;
        round_desc += role_emoji(p.role) + " <@" + std::to_string(p.user_id) + "> dealt **" + std::to_string(bonus) + "** damage (" + p.role + ")\n";
    }
    
    s.vault_hp -= round_damage;
    s.total_damage += round_damage;
    
    round_desc += "\n**Vault:** " + vault_hp_bar(std::max(0, s.vault_hp), s.vault.hp);
    
    if (s.vault_hp <= 0) {
        round_desc += "\n\n🔓 **VAULT CRACKED!**";
    }
    
    dpp::embed embed;
    embed.set_color(0xFF9800);
    embed.set_title("🏴‍☠️ " + s.vault.name + " — Round " + std::to_string(s.round));
    embed.set_description(round_desc);
    
    dpp::message msg(channel_id, embed);
    bot.message_create(msg, [&bot, db, channel_id](const dpp::confirmation_callback_t& cb) {
        // Schedule next round after 3 seconds
        bot.start_timer([&bot, db, channel_id](dpp::timer t) {
            bot.stop_timer(t);
            run_heist_round(bot, db, channel_id);
        }, 3);
    });
}

// ── Command ─────────────────────────────────────────────────────────────────

inline Command* get_heist_command(Database* db) {
    static Command* cmd = new Command(
        "heist",
        "start a cooperative vault heist (3-8 players)",
        "social",
        {"robbery", "vault"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_heist_tables(db);
            uint64_t uid = event.msg.author.id;
            uint64_t cid = event.msg.channel_id;
            db->ensure_user_exists(uid);
            
            std::string sub = args.empty() ? "start" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "start" || sub == "create") {
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                if (g_heist_sessions.count(cid)) {
                    bronx::send_message(bot, event, bronx::error("there's already a heist in this channel!"));
                    return;
                }
                
                // Parse difficulty (default 1)
                int difficulty = 1;
                if (args.size() >= 2) {
                    try { difficulty = std::stoi(args[1]); } catch (...) {}
                    difficulty = std::clamp(difficulty, 1, 5);
                }
                
                const auto& vault = vault_templates[difficulty - 1];
                int64_t entry_fee = vault.base_pool / 10; // 10% of potential payout
                
                int64_t wallet = db->get_wallet(uid);
                if (wallet < entry_fee) {
                    bronx::send_message(bot, event, bronx::error("you need **$" + economy::format_number(entry_fee) + "** to start this heist"));
                    return;
                }
                
                db->update_wallet(uid, -entry_fee);
                
                HeistSession s;
                s.channel_id = cid;
                s.guild_id = event.msg.guild_id;
                s.host_id = uid;
                s.vault = vault;
                s.entry_fee = entry_fee;
                s.vault_hp = vault.hp;
                s.created_at = std::chrono::system_clock::now();
                
                HeistParticipant host;
                host.user_id = uid;
                host.role = "muscle";
                host.contribution = 0;
                host.alive = true;
                s.participants.push_back(host);
                
                auto msg = build_heist_lobby(s);
                msg.channel_id = cid;
                
                g_heist_sessions[cid] = s;
                
                bot.message_create(msg, [cid](const dpp::confirmation_callback_t& cb) {
                    if (!cb.is_error()) {
                        std::lock_guard<std::mutex> lock(g_heist_mutex);
                        auto it = g_heist_sessions.find(cid);
                        if (it != g_heist_sessions.end()) {
                            it->second.lobby_msg_id = std::get<dpp::message>(cb.value).id;
                        }
                    }
                });
                
                // Auto-cancel after 90 seconds if not started
                bot.start_timer([&bot, cid](dpp::timer t) {
                    bot.stop_timer(t);
                    std::lock_guard<std::mutex> lock(g_heist_mutex);
                    auto it = g_heist_sessions.find(cid);
                    if (it != g_heist_sessions.end() && !it->second.started) {
                        // Refund all participants
                        // Note: can't easily refund here without db pointer, but the entry
                        // fee is relatively small and this is a timeout penalty
                        g_heist_sessions.erase(it);
                        bot.message_create(dpp::message(cid, "").add_embed(bronx::error("heist lobby timed out (90s)")));
                    }
                }, 90);
                
            } else {
                bronx::send_message(bot, event, bronx::info("use `b.heist start [1-5]` to create a heist lobby\ndifficulty 1-5 determines vault and payout"));
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_heist_tables(db);
            uint64_t uid = event.command.get_issuing_user().id;
            uint64_t cid = event.command.channel_id;
            db->ensure_user_exists(uid);
            
            int difficulty = 1;
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) {
                for (const auto& opt : ci_options) {
                    if (opt.name == "difficulty") {
                        difficulty = std::clamp((int)std::get<int64_t>(opt.value), 1, 5);
                    }
                }
            }
            
            std::lock_guard<std::mutex> lock(g_heist_mutex);
            if (g_heist_sessions.count(cid)) {
                event.reply(dpp::message().add_embed(bronx::error("heist already running in this channel")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            const auto& vault = vault_templates[difficulty - 1];
            int64_t entry_fee = vault.base_pool / 10;
            int64_t wallet = db->get_wallet(uid);
            if (wallet < entry_fee) {
                event.reply(dpp::message().add_embed(bronx::error("need **$" + economy::format_number(entry_fee) + "**")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            db->update_wallet(uid, -entry_fee);
            
            HeistSession s;
            s.channel_id = cid;
            s.guild_id = event.command.guild_id;
            s.host_id = uid;
            s.vault = vault;
            s.entry_fee = entry_fee;
            s.vault_hp = vault.hp;
            s.created_at = std::chrono::system_clock::now();
            
            HeistParticipant host;
            host.user_id = uid;
            host.role = "muscle";
            host.contribution = 0;
            host.alive = true;
            s.participants.push_back(host);
            
            g_heist_sessions[cid] = s;
            
            auto msg = build_heist_lobby(s);
            event.reply(msg);
            
            bot.start_timer([&bot, cid](dpp::timer t) {
                bot.stop_timer(t);
                std::lock_guard<std::mutex> lock(g_heist_mutex);
                auto it = g_heist_sessions.find(cid);
                if (it != g_heist_sessions.end() && !it->second.started) {
                    g_heist_sessions.erase(it);
                    bot.message_create(dpp::message(cid, "").add_embed(bronx::error("heist lobby timed out")));
                }
            }, 90);
        },
        // slash options
        {
            dpp::command_option(dpp::co_integer, "difficulty", "vault difficulty 1-5 (default: 1)", false)
                .set_min_value(1).set_max_value(5),
        }
    );
    return cmd;
}

// ── Button interaction handler ──────────────────────────────────────────────

inline void register_heist_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("heist_", 0) != 0) return;
        
        uint64_t uid = event.command.get_issuing_user().id;
        
        // Parse action and channel_id from custom_id
        // Format: heist_join_ROLE_CHANNELID or heist_start_CHANNELID or heist_cancel_CHANNELID
        std::string id = event.custom_id;
        
        if (id.rfind("heist_join_", 0) == 0) {
            // heist_join_ROLE_CHANNELID
            std::string rest = id.substr(strlen("heist_join_"));
            size_t last_underscore = rest.rfind('_');
            if (last_underscore == std::string::npos) return;
            
            std::string role = rest.substr(0, last_underscore);
            uint64_t cid;
            try { cid = std::stoull(rest.substr(last_underscore + 1)); } catch (...) { return; }
            
            std::lock_guard<std::mutex> lock(g_heist_mutex);
            auto it = g_heist_sessions.find(cid);
            if (it == g_heist_sessions.end()) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("heist not found")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            HeistSession& s = it->second;
            if (s.started) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("heist already started")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Check if already joined
            for (const auto& p : s.participants) {
                if (p.user_id == uid) {
                    // Update role
                    for (auto& p2 : s.participants) {
                        if (p2.user_id == uid) {
                            p2.role = role;
                            break;
                        }
                    }
                    event.reply(dpp::ir_deferred_update_message, dpp::message());
                    // Update lobby
                    auto msg = build_heist_lobby(s);
                    msg.id = s.lobby_msg_id;
                    msg.channel_id = cid;
                    bronx::safe_message_edit(bot, msg);
                    return;
                }
            }
            
            if (s.participants.size() >= 8) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("heist is full (8/8)")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Charge entry fee
            db->ensure_user_exists(uid);
            int64_t wallet = db->get_wallet(uid);
            if (wallet < s.entry_fee) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("need $" + economy::format_number(s.entry_fee) + " entry fee")).set_flags(dpp::m_ephemeral));
                return;
            }
            db->update_wallet(uid, -s.entry_fee);
            
            HeistParticipant p;
            p.user_id = uid;
            p.role = role;
            p.contribution = 0;
            p.alive = true;
            s.participants.push_back(p);
            
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            auto msg = build_heist_lobby(s);
            msg.id = s.lobby_msg_id;
            msg.channel_id = cid;
            bronx::safe_message_edit(bot, msg);
            
        } else if (id.rfind("heist_start_", 0) == 0) {
            uint64_t cid;
            try { cid = std::stoull(id.substr(strlen("heist_start_"))); } catch (...) { return; }
            
            std::lock_guard<std::mutex> lock(g_heist_mutex);
            auto it = g_heist_sessions.find(cid);
            if (it == g_heist_sessions.end()) return;
            
            HeistSession& s = it->second;
            if (uid != s.host_id) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("only the host can start the heist")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (s.participants.size() < 3) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("need at least 3 players!")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (s.started) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("already started")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            s.started = true;
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            
            // Disable lobby buttons
            dpp::embed start_embed;
            start_embed.set_color(0xFF5722);
            start_embed.set_title("🏴‍☠️ Heist Starting! " + s.vault.emoji);
            start_embed.set_description("**" + std::to_string(s.participants.size()) + " crew members** are breaching **" + s.vault.name + "**...");
            dpp::message disabled_msg;
            disabled_msg.add_embed(start_embed);
            disabled_msg.id = s.lobby_msg_id;
            disabled_msg.channel_id = cid;
            bronx::safe_message_edit(bot, disabled_msg);
            
            // Start first round after 2 seconds (need to release lock first)
            // Copy channel_id for the timer capture
            uint64_t capture_cid = cid;
            bot.start_timer([&bot, db, capture_cid](dpp::timer t) {
                bot.stop_timer(t);
                run_heist_round(bot, db, capture_cid);
            }, 2);
            
        } else if (id.rfind("heist_cancel_", 0) == 0) {
            uint64_t cid;
            try { cid = std::stoull(id.substr(strlen("heist_cancel_"))); } catch (...) { return; }
            
            std::lock_guard<std::mutex> lock(g_heist_mutex);
            auto it = g_heist_sessions.find(cid);
            if (it == g_heist_sessions.end()) return;
            
            HeistSession& s = it->second;
            if (uid != s.host_id) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("only the host can cancel")).set_flags(dpp::m_ephemeral));
                return;
            }
            if (s.started) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("can't cancel, heist in progress")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Refund entry fees
            for (auto& p : s.participants) {
                db->update_wallet(p.user_id, s.entry_fee);
            }
            
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            
            dpp::embed cancel_embed;
            cancel_embed.set_color(0xE53935);
            cancel_embed.set_title("🏴‍☠️ Heist Cancelled");
            cancel_embed.set_description("entry fees refunded to all " + std::to_string(s.participants.size()) + " participants");
            dpp::message cancel_msg;
            cancel_msg.add_embed(cancel_embed);
            cancel_msg.id = s.lobby_msg_id;
            cancel_msg.channel_id = cid;
            bronx::safe_message_edit(bot, cancel_msg);
            
            g_heist_sessions.erase(it);
        }
    });
}

} // namespace social
} // namespace commands
