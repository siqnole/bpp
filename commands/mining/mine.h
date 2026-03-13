#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "mining_helpers.h"
#include "../global_boss.h"
#include "../pets/pets.h"
#include "../skill_tree/skill_tree.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include <dpp/dpp.h>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <mutex>
#include <set>
#include <cmath>

using namespace bronx::db;

namespace commands {
namespace mining {

// ============================================================================
// MINING MINIGAME – 3x3 grid, ores fly by, click to grab
// ============================================================================

static const int MINE_COOLDOWN_SECONDS = 30;

// Support server ID for home court bonus
static const uint64_t MINE_SUPPORT_SERVER_ID = 1259717095382319215ULL;
// Owner ID for owner bonus
static const uint64_t MINE_OWNER_ID = 814226043924643880ULL;

// Active mining session for a user
struct MiningSession {
    uint64_t user_id;
    uint64_t channel_id;
    uint64_t message_id;

    // Gear info
    std::string pickaxe_id;
    std::string minecart_id;
    std::string bag_id;
    int pickaxe_level;
    int minecart_level;
    int bag_level;
    std::string pickaxe_meta;
    std::string minecart_meta;
    std::string bag_meta;

    // Derived stats
    int bag_capacity;        // max ores collectible
    double rip_chance;       // bag rip chance on timeout (0.0 - 1.0)
    int speed_ms;            // how often ores appear (ms)
    int multimine = 0;       // max extra ores per hit (0 = disabled, up to 20)
    double multiore_chance = 0.0; // chance (0.0-1.0) each hit spawns 1-3 random bonus ores
    int multiore_max = 3;        // max random bonus ores per trigger

    // Game state
    std::vector<MineInfo> collected;
    int64_t total_value = 0;
    int ores_spawned = 0;

    // Current ore on the grid (only one at a time randomly placed)
    int current_ore_row = -1;  // -1 = no active ore
    int current_ore_col = -1;
    OreType current_ore;
    int64_t current_ore_value = 0;
    double current_ore_probability = 0.0;

    // Pool
    std::vector<OreType> pool;
    std::vector<int> weights;
    int total_weight = 0;

    // Skill tree bonuses
    double skill_value_bonus = 0.0;
    double skill_rare_bonus = 0.0;
    double skill_double_chance = 0.0;
    double skill_celestial_chance = 0.0;
    double skill_void_chance = 0.0;

    // Timing
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    bool active = false;
    bool timed_out = false;
    int prestige_level = 0;
    uint64_t guild_id = 0;
    bool is_boosting = false;

    // Cashout bonus
    int64_t bonus_value = 0;
    std::string bonus_ore_id;

    // RNG
    std::mt19937 gen;
    std::discrete_distribution<> dist;
};

static std::unordered_map<uint64_t, MiningSession> mining_sessions;
static std::mutex mining_mutex;

// ────────────────────────────────────────────────────────────────────────────
// Helper: spawn a new ore on a random cell
// ────────────────────────────────────────────────────────────────────────────
static void spawn_ore(MiningSession& s) {
    int idx;
    try {
        idx = s.dist(s.gen);
        if (idx < 0 || idx >= (int)s.pool.size()) idx = 0;
    } catch (...) { idx = 0; }
    s.current_ore = s.pool[idx];
    // random grid position (0-2, 0-2)
    std::uniform_int_distribution<int> pos(0, 2);
    s.current_ore_row = pos(s.gen);
    s.current_ore_col = pos(s.gen);
    s.ores_spawned++;

    // pre-roll value
    std::uniform_int_distribution<int64_t> valdis(s.current_ore.min_value, s.current_ore.max_value);
    s.current_ore_value = valdis(s.gen);

    // probability
    if (s.total_weight > 0)
        s.current_ore_probability = (s.weights[idx] / (double)s.total_weight) * 100.0;
    else
        s.current_ore_probability = 0.01;
}

// ────────────────────────────────────────────────────────────────────────────
// Build the mining embed + 3x3 button grid
// ────────────────────────────────────────────────────────────────────────────
static dpp::message build_mining_message(MiningSession& s) {
    dpp::message msg;

    // Embed description
    std::string desc;
    desc += "⛏️ **Mining in progress…**\n";
    if (s.prestige_level > 0) {
        desc += "⭐ Prestige " + std::to_string(s.prestige_level) + " [+" + std::to_string(s.prestige_level * 5) + "% value]\n";
    }
    if (s.guild_id == MINE_SUPPORT_SERVER_ID) {
        desc += "🏠 Home Court [+5%]\n";
    }
    if (s.is_boosting) {
        desc += "💎 Supporter [+10%]\n";
    }
    if (s.multimine > 0) {
        desc += "⛏️x Multimine [up to " + std::to_string(s.multimine) + " extra]\n";
    }
    if (s.multiore_chance > 0.001) {
        char moc[32];
        snprintf(moc, sizeof(moc), "%.0f%%", s.multiore_chance * 100.0);
        desc += "🎲 Multi-Ore [" + std::string(moc) + " chance, up to " + std::to_string(s.multiore_max) + " bonus]\n";
    }
    desc += "Bag: **" + std::to_string(s.collected.size()) + "/" + std::to_string(s.bag_capacity) + "**";
    if (!s.collected.empty()) {
        desc += "  |  Value: **$" + format_number(s.total_value) + "**";
    }
    desc += "\n";

    // Time left as relative timestamp
    auto end_epoch = std::chrono::system_clock::to_time_t(s.end_time);
    desc += "Time left: <t:" + std::to_string(end_epoch) + ":R>\n";
    desc += "\nClick the ore when it appears! Missing it = it flies away.";
    if (s.rip_chance > 0.01) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f%%", s.rip_chance * 100.0);
        desc += "\n" + bronx::EMOJI_WARNING + " Bag rip chance on timeout: **" + std::string(buf) + "**";
    }

    auto embed = bronx::create_embed(desc);
    msg.add_embed(embed);

    // Build 3x3 grid (3 action rows, 3 buttons each)
    for (int r = 0; r < 3; r++) {
        dpp::component row;
        for (int c = 0; c < 3; c++) {
            dpp::component btn;
            btn.set_type(dpp::cot_button);
            std::string cid = "mine_cell_" + std::to_string(s.user_id) + "_" + std::to_string(r) + "_" + std::to_string(c);
            btn.set_id(cid);

            if (s.active && s.current_ore_row == r && s.current_ore_col == c) {
                // Ore is here!
                if (s.current_ore.emoji.rfind("<", 0) == 0) {
                    // Custom Discord emoji — can't be used with set_emoji,
                    // so put it in the label instead and leave emoji unset.
                    btn.set_label(s.current_ore.emoji);
                } else if (!s.current_ore.emoji.empty()) {
                    // Standard Unicode emoji — show it as the button emoji
                    btn.set_label("");
                    btn.set_emoji(s.current_ore.emoji, 0, false);
                } else {
                    btn.set_label("ore");
                }
                btn.set_style(dpp::cos_success);
            } else {
                btn.set_label("‎ "); // invisible character so button renders
                // Use a different emoji for empty cells to make the grid visible
                btn.set_emoji("⬛", 0, false);
                btn.set_style(dpp::cos_secondary);
            }
            row.add_component(btn);
        }
        msg.add_component(row);
    }

    // Stop mining button (row 4)
    {
        dpp::component row;
        dpp::component stop_btn;
        stop_btn.set_type(dpp::cot_button)
            .set_label("🛑 Stop Mining & Collect")
            .set_style(dpp::cos_danger)
            .set_id("mine_stop_" + std::to_string(s.user_id));
        row.add_component(stop_btn);
        msg.add_component(row);
    }

    return msg;
}

// ────────────────────────────────────────────────────────────────────────────
// Build the results embed (after mining ends)
// ────────────────────────────────────────────────────────────────────────────
static dpp::message build_mining_results(MiningSession& s, Database* db, bool ripped) {
    std::string desc;
    if (ripped) {
        desc += "💥 **Your bag ripped!** All ores lost!\n";
        desc += "Collected " + std::to_string(s.collected.size()) + " ores worth $" + format_number(s.total_value) + " but they all fell out.\n";
        desc += "\n*Upgrade your bag to reduce rip chance!*";
    } else {
        desc += "⛏️ **Mining results:**\n\n";
        for (size_t i = 0; i < s.collected.size() && i < 15; i++) {
            auto& o = s.collected[i];
            desc += o.ore.emoji + " **" + o.ore.name + "** `[" + o.item_id + "]`";
            char bufp[32];
            snprintf(bufp, sizeof(bufp), " *(%.2f%%)*", o.probability);
            desc += bufp;
            desc += " – __$" + format_number(o.value) + "__";
            if (o.hadEffect) {
                desc += " ✨";
            }
            if (o.bonus_type == OreBonusType::Multimine) {
                desc += " ⛏️x";
            } else if (o.bonus_type == OreBonusType::MultiOre) {
                desc += " 🎲";
            } else if (o.bonus_type == OreBonusType::DoubleCatch) {
                desc += " 2️⃣";
            }
            desc += "\n";
        }
        if (s.collected.size() > 15) {
            desc += "*...and " + std::to_string(s.collected.size() - 15) + " more*\n";
        }
        if (s.bonus_value > 0) {
            desc += "\n\n🎁 **Cashout Bonus** `[" + s.bonus_ore_id + "]` – __$" + format_number(s.bonus_value) + "__";
        }
        desc += "\n**Total ores:** " + std::to_string(s.collected.size() + (s.bonus_value > 0 ? 1 : 0));
        // Count bonus ores by type
        int mm_count = 0, mo_count = 0, dc_count = 0;
        for (auto& o : s.collected) {
            if (o.bonus_type == OreBonusType::Multimine) mm_count++;
            else if (o.bonus_type == OreBonusType::MultiOre) mo_count++;
            else if (o.bonus_type == OreBonusType::DoubleCatch) dc_count++;
        }
        if (mm_count > 0 || mo_count > 0 || dc_count > 0) {
            desc += " (";
            bool first = true;
            if (mm_count > 0) { desc += "⛏️x" + std::to_string(mm_count); first = false; }
            if (mo_count > 0) { if (!first) desc += ", "; desc += "🎲" + std::to_string(mo_count); first = false; }
            if (dc_count > 0) { if (!first) desc += ", "; desc += "2️⃣" + std::to_string(dc_count); }
            desc += " bonus)";
        }
        desc += "\n**Total value:** $" + format_number(s.total_value);
        if (s.prestige_level > 0) {
            desc += "\n⭐ Prestige " + std::to_string(s.prestige_level) + " [+" + std::to_string(s.prestige_level * 5) + "%]";
        }
        if (s.guild_id == MINE_SUPPORT_SERVER_ID) {
            desc += "\n🏠 Home Court [+5%]";
        }
        if (s.is_boosting) {
            desc += "\n💎 Supporter [+10%]";
        }
        desc += "\n\nUse `sellore <id>` to sell or `minv` to view your ores.";
    }

    auto embed = bronx::create_embed(desc, ripped ? bronx::COLOR_ERROR : bronx::COLOR_SUCCESS);
    dpp::message msg;
    msg.add_embed(embed);
    return msg;
}

// ────────────────────────────────────────────────────────────────────────────
// Apply ore effect  (mirrors fishing effects)
// ────────────────────────────────────────────────────────────────────────────
static int64_t apply_ore_effect(Database* db, MiningSession& s, const OreType& ore, int64_t base, bool& triggered) {
    triggered = false;
    int64_t val = base;
    int luck = parse_mine_meta_int(s.pickaxe_meta, "luck", 0);
    double roll = (double)rand() / RAND_MAX;
    if (roll >= ore.effect_chance) return val;
    triggered = true;
    switch (ore.effect) {
        case OreEffect::Flat:        val += luck + s.pickaxe_level; break;
        case OreEffect::Exponential: val = (int64_t)(val * pow(1.0 + luck / 100.0, 2)); break;
        case OreEffect::Logarithmic: val = (int64_t)(val * log2(luck + 2)); break;
        case OreEffect::NLogN: { double n = luck + s.pickaxe_level; val = (int64_t)(val * (n * log2(n + 2))); break; }
        case OreEffect::Wacky:       val *= (rand() % 5 + 1); break;
        case OreEffect::Jackpot:     val = (rand() % 2 == 0) ? (int64_t)(val * 0.2) : (int64_t)(val * 8); break;
        case OreEffect::Critical:    val = (rand() % 2 == 0) ? val * 2 : val; break;
        case OreEffect::Volatile:    val = (int64_t)(val * (0.3 + (rand() % 38) / 10.0)); break;
        case OreEffect::Surge:       val += s.pickaxe_level * s.pickaxe_level * 50; break;
        case OreEffect::Diminishing: { double m = 3.0 - luck / 50.0; val = (int64_t)(val * std::max(0.5, m)); break; }
        case OreEffect::Cascading:   { int rolls = 1 + rand() % 6; val = (int64_t)(val * pow(1.15, rolls)); break; }
        case OreEffect::Wealthy:     { int64_t w = db->get_wallet(s.user_id); val = (int64_t)(val * (1.0 + sqrt((double)w) / 1000.0)); break; }
        case OreEffect::Banker:      { int64_t b = db->get_bank(s.user_id); val = (int64_t)(val * (1.0 + log10((double)b + 1) / 5.0)); break; }
        case OreEffect::Miner:       { int64_t mc = db->get_stat(s.user_id, "ores_mined"); val = (int64_t)(val * (1.0 + (double)mc / 50000.0)); break; }
        case OreEffect::Merchant:    { int64_t ms = db->get_stat(s.user_id, "ores_sold"); val += ms / 100; break; }
        case OreEffect::Ascended:    { double mult = std::min(10.0, pow(1.5, s.prestige_level)); val = (int64_t)(val * mult); break; }
        case OreEffect::Collector:   { auto inv = db->get_inventory(s.user_id); std::set<std::string> unique; for (auto& i : inv) if (i.item_type == "collectible" && i.item_id[0] == 'M') unique.insert(i.item_id); val += unique.size() * 100; break; }
        case OreEffect::Persistent:  { int64_t mc = db->get_stat(s.user_id, "ores_mined"); int64_t ms = db->get_stat(s.user_id, "ores_sold"); val = (int64_t)(val * log2((double)(mc + ms) + 2.0)); break; }
        default: break;
    }
    return val;
}

// ────────────────────────────────────────────────────────────────────────────
// Finalize session: store ores in DB or rip
// ────────────────────────────────────────────────────────────────────────────
static void finalize_mining(Database* db, MiningSession& s, bool from_timeout) {
    s.active = false;
    s.timed_out = from_timeout;

    // Check bag rip on timeout
    if (from_timeout && !s.collected.empty()) {
        double roll = (double)rand() / RAND_MAX;
        if (roll < s.rip_chance) {
            // Bag rips – lose everything. Don't store to DB.
            s.collected.clear();
            s.total_value = 0;
            return;
        }
    }

    // Store all collected ores as collectible items  
    for (auto& ore_info : s.collected) {
        std::string metadata = "{\"name\":\"" + ore_info.ore.name + "\",\"value\":" + std::to_string(ore_info.value) + ",\"locked\":false,\"type\":\"ore\"}";
        db->add_item(s.user_id, ore_info.item_id, "collectible", 1, metadata);
        db->increment_stat(s.user_id, "ores_mined", 1);
        // Track per-species ore mastery
        db->increment_stat(s.user_id, "ore_mastery_" + ore_info.ore.name, 1);
    }

    // Track daily challenge stats for mining
    ::commands::daily_challenges::track_daily_stat(db, s.user_id, "ores_mined", (int64_t)s.collected.size());
    ::commands::daily_challenges::track_daily_stat(db, s.user_id, "ore_value_today", s.total_value);

    // Track global boss ores mined (count all collected ores including bonus below)
    int64_t total_ores_for_boss = (int64_t)s.collected.size();

    // Cashout bonus: award a bonus ore worth 1-40% of the total bag value
    if (s.total_value > 0 && !s.collected.empty()) {
        std::uniform_int_distribution<int> bonus_pct(1, 25);
        int pct = bonus_pct(s.gen);
        s.bonus_value = (s.total_value * pct) / 100;
        if (s.bonus_value > 0) {
            s.bonus_ore_id = generate_ore_id();
            std::string bonus_meta = "{\"name\":\"Cashout Bonus\",\"value\":" + std::to_string(s.bonus_value) + ",\"locked\":false,\"type\":\"ore\"}";
            db->add_item(s.user_id, s.bonus_ore_id, "collectible", 1, bonus_meta);
            db->increment_stat(s.user_id, "ores_mined", 1);
            s.total_value += s.bonus_value;
            total_ores_for_boss++;
        }
    }

    // Submit ores mined to global boss
    if (total_ores_for_boss > 0) {
        global_boss::on_ores_mined(db, s.user_id, total_ores_for_boss);
        ::commands::pets::pet_hooks::on_mine(db, s.user_id, total_ores_for_boss);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Get /mine command
// ────────────────────────────────────────────────────────────────────────────
inline Command* get_mine_command(Database* db) {
    auto do_mine = [db](uint64_t uid, uint64_t guild_id, bool is_boosting) -> dpp::message {
        try {
            // Check for active session
            {
                std::lock_guard<std::mutex> lock(mining_mutex);
                auto it = mining_sessions.find(uid);
                if (it != mining_sessions.end() && it->second.active) {
                    return dpp::message().add_embed(bronx::error("you're already mining! finish your current session first"));
                }
            }

            // Check equipped mining gear
            // Mining gear is stored as inventory items with specific naming:
            // pickaxe_*, minecart_*, bag_*
            auto inv = db->get_inventory(uid);
            std::string pickaxe_id, minecart_id, bag_id;
            int pickaxe_lvl = 0, minecart_lvl = 0, bag_lvl = 0;
            std::string pickaxe_meta, minecart_meta, bag_meta;

            // Look for active mining gear via a simple convention:
            // We store equipped mining gear in the "active_mining_gear" key in user stats
            // or just find the highest-level pickaxe/minecart/bag the user owns
            for (auto& item : inv) {
                if (item.item_id.rfind("pickaxe_", 0) == 0 && item.level > pickaxe_lvl) {
                    pickaxe_id = item.item_id;
                    pickaxe_lvl = item.level;
                    pickaxe_meta = item.metadata;
                }
                if (item.item_id.rfind("minecart_", 0) == 0 && item.level > minecart_lvl) {
                    minecart_id = item.item_id;
                    minecart_lvl = item.level;
                    minecart_meta = item.metadata;
                }
                if (item.item_id.rfind("bag_", 0) == 0 && item.level > bag_lvl) {
                    bag_id = item.item_id;
                    bag_lvl = item.level;
                    bag_meta = item.metadata;
                }
            }

            if (pickaxe_id.empty()) {
                return dpp::message().add_embed(bronx::error("you need a pickaxe to mine! buy one from the shop (`shop pickaxe`)"));
            }
            if (minecart_id.empty()) {
                return dpp::message().add_embed(bronx::error("you need a minecart to mine! buy one from the shop (`shop minecart`)"));
            }
            if (bag_id.empty()) {
                return dpp::message().add_embed(bronx::error("you need a mining bag! buy one from the shop (`shop bag`)"));
            }

            // Build session
            MiningSession session;
            session.user_id = uid;
            session.pickaxe_id = pickaxe_id;
            session.minecart_id = minecart_id;
            session.bag_id = bag_id;
            session.pickaxe_level = pickaxe_lvl;
            session.minecart_level = minecart_lvl;
            session.bag_level = bag_lvl;
            session.pickaxe_meta = pickaxe_meta;
            session.minecart_meta = minecart_meta;
            session.bag_meta = bag_meta;

            session.bag_capacity = parse_mine_meta_int(bag_meta, "capacity", 5);
            session.rip_chance = parse_mine_meta_double(bag_meta, "rip_chance", 0.20);
            session.speed_ms = parse_mine_meta_int(minecart_meta, "speed", 8000);
            session.multimine = std::min(20, parse_mine_meta_int(pickaxe_meta, "multimine", 0));
            session.multiore_chance = parse_mine_meta_double(pickaxe_meta, "multiore_chance", 0.0);
            session.multiore_max = std::max(1, std::min(5, parse_mine_meta_int(pickaxe_meta, "multiore_max", 3)));
            session.prestige_level = db->get_prestige(uid);
            session.guild_id = guild_id;
            session.is_boosting = is_boosting;
            
            // Fetch skill tree bonuses
            session.skill_value_bonus = commands::skill_tree::get_skill_bonus(db, uid, "ore_value_bonus");
            session.skill_rare_bonus = commands::skill_tree::get_skill_bonus(db, uid, "rare_ore_bonus");
            session.skill_double_chance = commands::skill_tree::get_skill_bonus(db, uid, "double_ore_chance");
            session.skill_celestial_chance = commands::skill_tree::get_skill_bonus(db, uid, "celestial_ore_bonus");
            session.skill_void_chance = commands::skill_tree::get_skill_bonus(db, uid, "void_crystal_bonus");
            
            // Apply capacity boost from "Deep Veins"
            double yield_bonus = commands::skill_tree::get_skill_bonus(db, uid, "ore_yield_bonus");
            if (yield_bonus > 0) {
                session.bag_capacity += static_cast<int>(session.bag_capacity * (yield_bonus / 100.0));
            }

            // Build ore pool based on pickaxe level
            auto spawn_rates = parse_spawn_rates(minecart_meta);
            for (auto& ore : ore_types) {
                if (ore.max_pickaxe_level > 0 && pickaxe_lvl >= ore.max_pickaxe_level) continue;
                if (ore.min_pickaxe_level > 0 && pickaxe_lvl < ore.min_pickaxe_level) continue;
                session.pool.push_back(ore);
            }
            if (session.pool.empty()) {
                return dpp::message().add_embed(bronx::error("no ores available for your gear level"));
            }

            // Build weights
            int luck = parse_mine_meta_int(pickaxe_meta, "luck", 0);
            int max_w = 0;
            for (auto& o : session.pool) max_w = std::max(max_w, o.weight);
            for (auto& o : session.pool) {
                int w = o.weight > 0 ? o.weight : 1;
                if (luck != 0 && max_w > 0) w += (int)((max_w - w) * (luck / 100.0));
                
                // Apply skill tree rare/celestial/void boosts based on ore name/pool weight
                if (session.skill_rare_bonus > 0 && w < 100 && w > 10) { // arbitrary bound for rare
                    w += (int)(w * (session.skill_rare_bonus / 100.0));
                }
                if (session.skill_celestial_chance > 0 && o.name.find("Celestial") != std::string::npos) {
                    w += (int)(w * (session.skill_celestial_chance / 100.0));
                }
                if (session.skill_void_chance > 0 && o.name.find("Void") != std::string::npos) {
                    w += (int)(w * (session.skill_void_chance / 100.0));
                }

                // Apply minecart spawn rate boosts
                auto sr = spawn_rates.find(o.name);
                if (sr != spawn_rates.end()) w += sr->second;
                session.weights.push_back(w);
            }
            session.total_weight = 0;
            for (int w : session.weights) session.total_weight += w;
            if (session.total_weight <= 0) {
                return dpp::message().add_embed(bronx::error("invalid mining weight distribution"));
            }

            // Init RNG
            std::random_device rd;
            session.gen = std::mt19937(rd());
            try {
                session.dist = std::discrete_distribution<>(session.weights.begin(), session.weights.end());
            } catch (...) {
                return dpp::message().add_embed(bronx::error("failed to initialize mining RNG"));
            }

            // Timing: 30 seconds of mining  
            session.start_time = std::chrono::system_clock::now();
            int duration_seconds = parse_mine_meta_int(pickaxe_meta, "duration", 30);
            session.end_time = session.start_time + std::chrono::seconds(duration_seconds);
            session.active = true;

            // Spawn first ore
            spawn_ore(session);

            dpp::message msg = build_mining_message(session);

            // Store session  
            {
                std::lock_guard<std::mutex> lock(mining_mutex);
                mining_sessions[uid] = std::move(session);
            }

            // Apply cooldown
            db->set_cooldown(uid, "mine", MINE_COOLDOWN_SECONDS);

            // Track global boss progress (mine command)
            global_boss::on_mine_command(db, uid);

            return msg;
        } catch (const std::exception& e) {
            std::cerr << "mine exception for user " << uid << ": " << e.what() << "\n";
            return dpp::message().add_embed(bronx::error("an internal error occurred while starting mining"));
        }
    };

    static Command* mine = new Command("mine", "start a mining session to collect ores", "mining", {"dig", "mining"}, true,
        [db, do_mine](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            uint64_t guild_id = event.msg.guild_id;

            if (db->is_on_cooldown(uid, "mine")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "mine")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event, bronx::error("your pickaxe needs sharpening! try again <t:" + std::to_string(timestamp) + ":R>"));
                }
                return;
            }

            if (guild_id != 0) {
                bot.guild_get_member(guild_id, uid, [db, do_mine, &bot, event, uid, guild_id](const dpp::confirmation_callback_t& callback) {
                    bool is_boosting = false;
                    if (!callback.is_error()) {
                        auto member = std::get<dpp::guild_member>(callback.value);
                        is_boosting = (member.premium_since > 0);
                    }
                    dpp::message msg = do_mine(uid, guild_id, is_boosting);
                    if (!msg.embeds.empty()) {
                        bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
                    }
                    bronx::send_message(bot, event, msg);
                });
                return;
            }

            dpp::message msg = do_mine(uid, guild_id, false);
            if (!msg.embeds.empty()) {
                bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            }
            bronx::send_message(bot, event, msg);
        },
        [db, do_mine](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            uint64_t guild_id = event.command.guild_id;

            if (db->is_on_cooldown(uid, "mine")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "mine")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(bronx::error("your pickaxe needs sharpening! try again <t:" + std::to_string(timestamp) + ":R>")));
                }
                return;
            }

            // Defer the interaction immediately to prevent token expiry (>3s)
            event.thinking(false, [db, do_mine, &bot, event, uid, guild_id](const dpp::confirmation_callback_t& thinking_cb) {
                if (thinking_cb.is_error()) return;

                if (guild_id != 0) {
                    bot.guild_get_member(guild_id, uid, [db, do_mine, event, uid, guild_id](const dpp::confirmation_callback_t& callback) {
                        bool is_boosting = false;
                        if (!callback.is_error()) {
                            auto member = std::get<dpp::guild_member>(callback.value);
                            is_boosting = (member.premium_since > 0);
                        }
                        dpp::message msg = do_mine(uid, guild_id, is_boosting);
                        event.edit_response(msg);
                    });
                    return;
                }

                dpp::message msg = do_mine(uid, guild_id, false);
                event.edit_response(msg);
            });
        });

    return mine;
}

// ────────────────────────────────────────────────────────────────────────────
// Register mining interaction handlers (button clicks on grid)
// ────────────────────────────────────────────────────────────────────────────
inline void register_mining_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        try {
            // Handle mine cell clicks
            if (event.custom_id.rfind("mine_cell_", 0) == 0) {
                // Format: mine_cell_<uid>_<row>_<col>
                std::string rest = event.custom_id.substr(strlen("mine_cell_"));
                // Parse uid_row_col
                size_t sep1 = rest.find('_');
                if (sep1 == std::string::npos) return;
                size_t sep2 = rest.find('_', sep1 + 1);
                if (sep2 == std::string::npos) return;
                uint64_t uid = std::stoull(rest.substr(0, sep1));
                int row = std::stoi(rest.substr(sep1 + 1, sep2 - sep1 - 1));
                int col = std::stoi(rest.substr(sep2 + 1));

                if (event.command.get_issuing_user().id != uid) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("this mine isn't yours")).set_flags(dpp::m_ephemeral));
                    return;
                }

                // Acknowledge the interaction immediately to prevent token expiry (>3s)
                event.reply(dpp::ir_deferred_update_message, dpp::message());

                std::lock_guard<std::mutex> lock(mining_mutex);
                auto it = mining_sessions.find(uid);
                if (it == mining_sessions.end() || !it->second.active) {
                    // Session already ended, deferred ACK keeps the current message as-is
                    return;
                }

                MiningSession& s = it->second;

                // Check if session has expired
                auto now = std::chrono::system_clock::now();
                if (now >= s.end_time) {
                    // Time's up - finalize with timeout
                    finalize_mining(db, s, true);
                    bool ripped = s.collected.empty() && s.timed_out;
                    dpp::message result = build_mining_results(s, db, ripped);
                    result.id = event.command.msg.id;
                    result.channel_id = event.command.channel_id;
                    bot.message_edit(result);
                    mining_sessions.erase(it);
                    return;
                }

                // Check if clicked on the correct cell
                if (row == s.current_ore_row && col == s.current_ore_col) {
                    // HIT! Collect the ore
                    int64_t val = s.current_ore_value;
                    int luck = parse_mine_meta_int(s.pickaxe_meta, "luck", 0);
                    if (luck != 0) val += val * luck / 100;

                    // Prestige bonus
                    int prestige_bonus = s.prestige_level * 5;
                    if (prestige_bonus > 0) val += val * prestige_bonus / 100;

                    // Active mining bonus (25% for active gameplay vs passive fishing)
                    val += val / 4;

                    // Pickaxe level multiplier (each level adds +6% value)
                    if (s.pickaxe_level > 1) {
                        val += (val * s.pickaxe_level * 6) / 100;
                    }

                    // Home court bonus (5%)
                    if (s.guild_id == MINE_SUPPORT_SERVER_ID) {
                        val += (val * 5) / 100;
                    }

                    // Supporter boost (10%)
                    if (s.is_boosting) {
                        val += (val * 10) / 100;
                    }

                    // Owner bonus (10%)
                    if (uid == MINE_OWNER_ID && s.guild_id != 0) {
                        val += (val * 10) / 100;
                    }

                    // Skill tree value bonus
                    if (s.skill_value_bonus > 0) {
                        val += val * (s.skill_value_bonus / 100.0);
                    }

                    // Apply effect
                    bool triggered = false;
                    val = apply_ore_effect(db, s, s.current_ore, val, triggered);

                    std::string oid = generate_ore_id();
                    MineInfo info{s.current_ore, val, triggered, s.current_ore.effect, s.current_ore_probability, oid, false, OreBonusType::Normal};
                    s.collected.push_back(info);
                    s.total_value += val;
                    
                    // Skill tree double catch chance
                    if (s.skill_double_chance > 0 && (int)s.collected.size() < s.bag_capacity) {
                        std::uniform_real_distribution<double> d_roll(0.0, 100.0);
                        if (d_roll(s.gen) < s.skill_double_chance) {
                            std::string d_oid = generate_ore_id();
                            MineInfo d_info = info; // copy
                            d_info.item_id = d_oid;
                            d_info.bonus_type = OreBonusType::DoubleCatch;
                            s.collected.push_back(d_info);
                            s.total_value += val;
                        }
                    }

                    // Multimine: chance to yield extra ores from the same hit
                    if (s.multimine > 0 && (int)s.collected.size() < s.bag_capacity) {
                        // Roll how many extras (1 to multimine), weighted toward lower counts
                        std::uniform_int_distribution<int> mm_roll(0, 99);
                        int roll = mm_roll(s.gen);
                        int extras = 0;
                        // Probability curve: 40% for 0, then decreasing chance for each extra
                        if (roll >= 40) {
                            // Scale extras: the remaining 60% spread across 1..multimine
                            double frac = (roll - 40) / 60.0; // 0.0 to ~1.0
                            extras = 1 + (int)(frac * frac * (s.multimine - 1)); // quadratic bias toward low
                            extras = std::min(extras, s.multimine);
                        }
                        int space = s.bag_capacity - (int)s.collected.size();
                        extras = std::min(extras, space);
                        for (int mm = 0; mm < extras; mm++) {
                            // Each extra ore gets its own value roll and effect
                            std::uniform_int_distribution<int64_t> mm_valdis(s.current_ore.min_value, s.current_ore.max_value);
                            int64_t mm_val = mm_valdis(s.gen);
                            if (luck != 0) mm_val += mm_val * luck / 100;
                            if (prestige_bonus > 0) mm_val += mm_val * prestige_bonus / 100;
                            mm_val += mm_val / 4; // active mining bonus
                            if (s.pickaxe_level > 1) mm_val += (mm_val * s.pickaxe_level * 8) / 100;
                            if (s.guild_id == MINE_SUPPORT_SERVER_ID) mm_val += (mm_val * 5) / 100;
                            if (s.is_boosting) mm_val += (mm_val * 10) / 100;
                            if (uid == MINE_OWNER_ID && s.guild_id != 0) mm_val += (mm_val * 10) / 100;
                            if (s.skill_value_bonus > 0) mm_val += mm_val * (s.skill_value_bonus / 100.0);
                            bool mm_triggered = false;
                            mm_val = apply_ore_effect(db, s, s.current_ore, mm_val, mm_triggered);
                            std::string mm_oid = generate_ore_id();
                            MineInfo mm_info{s.current_ore, mm_val, mm_triggered, s.current_ore.effect, s.current_ore_probability, mm_oid, false, OreBonusType::Multimine};
                            s.collected.push_back(mm_info);
                            s.total_value += mm_val;
                        }
                    }

                    // Multi-Ore: chance to spawn random bonus ores of different types
                    if (s.multiore_chance > 0.001 && (int)s.collected.size() < s.bag_capacity) {
                        std::uniform_real_distribution<double> mo_roll(0.0, 1.0);
                        if (mo_roll(s.gen) < s.multiore_chance) {
                            // Determine how many bonus ores (1 to multiore_max)
                            std::uniform_int_distribution<int> mo_count(1, s.multiore_max);
                            int bonus_count = mo_count(s.gen);
                            int space = s.bag_capacity - (int)s.collected.size();
                            bonus_count = std::min(bonus_count, space);
                            for (int bo = 0; bo < bonus_count; bo++) {
                                // Roll a random ore from the full pool
                                int bo_idx;
                                try {
                                    bo_idx = s.dist(s.gen);
                                    if (bo_idx < 0 || bo_idx >= (int)s.pool.size()) bo_idx = 0;
                                } catch (...) { bo_idx = 0; }
                                const OreType& bonus_ore = s.pool[bo_idx];
                                double bo_prob = (s.total_weight > 0)
                                    ? (s.weights[bo_idx] / (double)s.total_weight) * 100.0 : 0.01;
                                // Roll value
                                std::uniform_int_distribution<int64_t> bo_valdis(bonus_ore.min_value, bonus_ore.max_value);
                                int64_t bo_val = bo_valdis(s.gen);
                                if (luck != 0) bo_val += bo_val * luck / 100;
                                if (prestige_bonus > 0) bo_val += bo_val * prestige_bonus / 100;
                                bo_val += bo_val / 4; // active mining bonus
                                if (s.pickaxe_level > 1) bo_val += (bo_val * s.pickaxe_level * 6) / 100;
                                if (s.guild_id == MINE_SUPPORT_SERVER_ID) bo_val += (bo_val * 5) / 100;
                                if (s.is_boosting) bo_val += (bo_val * 10) / 100;
                                if (uid == MINE_OWNER_ID && s.guild_id != 0) bo_val += (bo_val * 10) / 100;
                                if (s.skill_value_bonus > 0) bo_val += bo_val * (s.skill_value_bonus / 100.0);
                                bool bo_triggered = false;
                                bo_val = apply_ore_effect(db, s, bonus_ore, bo_val, bo_triggered);
                                std::string bo_oid = generate_ore_id();
                                MineInfo bo_info{bonus_ore, bo_val, bo_triggered, bonus_ore.effect, bo_prob, bo_oid, false, OreBonusType::MultiOre};
                                s.collected.push_back(bo_info);
                                s.total_value += bo_val;
                            }
                        }
                    }

                    // Check if bag is full
                    if ((int)s.collected.size() >= s.bag_capacity) {
                        // Bag full – end session gracefully (no rip chance)
                        finalize_mining(db, s, false);
                        dpp::message result = build_mining_results(s, db, false);
                        result.id = event.command.msg.id;
                        result.channel_id = event.command.channel_id;
                        bot.message_edit(result);
                        mining_sessions.erase(it);
                        return;
                    }

                    // Spawn next ore
                    spawn_ore(s);
                } else {
                    // MISS – the ore flies away, spawn a new one
                    spawn_ore(s);
                }

                // Update the message with new grid
                dpp::message updated = build_mining_message(s);
                updated.id = event.command.msg.id;
                updated.channel_id = event.command.channel_id;
                bot.message_edit(updated);
                return;
            }

            // Handle stop mining button
            if (event.custom_id.rfind("mine_stop_", 0) == 0) {
                std::string rest = event.custom_id.substr(strlen("mine_stop_"));
                uint64_t uid = std::stoull(rest);

                if (event.command.get_issuing_user().id != uid) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("this mine isn't yours")).set_flags(dpp::m_ephemeral));
                    return;
                }

                // Acknowledge the interaction immediately to prevent token expiry (>3s)
                event.reply(dpp::ir_deferred_update_message, dpp::message());

                std::lock_guard<std::mutex> lock(mining_mutex);
                auto it = mining_sessions.find(uid);
                if (it == mining_sessions.end() || !it->second.active) {
                    // Session already ended, deferred ACK keeps the current message as-is
                    return;
                }

                MiningSession& s = it->second;
                // Graceful stop – no rip chance (user stopped manually)
                finalize_mining(db, s, false);
                dpp::message result = build_mining_results(s, db, false);
                result.id = event.command.msg.id;
                result.channel_id = event.command.channel_id;
                bot.message_edit(result);
                mining_sessions.erase(it);
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "mining interaction error: " << e.what() << "\n";
            try {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an error occurred")).set_flags(dpp::m_ephemeral));
            } catch (...) {}
        }
    });
}

} // namespace mining
} // namespace commands
