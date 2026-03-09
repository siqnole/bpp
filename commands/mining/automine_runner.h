#pragma once
#include "../../database/core/database.h"
#include "mining_helpers.h"
#include "../economy_core.h"
#include "../global_boss.h"
#include <chrono>
#include <iostream>
#include <cmath>
#include <set>

using namespace bronx::db;

namespace commands {
namespace mining {

// AUTO-SELL FEE applied to gross value when the autominer sells
static constexpr double AUTOMINER_SELL_FEE = 0.22; // 22% convenience fee (same as autofisher)

// Run one automining cycle for a single user.
// Returns the total gross value of ores mined this cycle.
inline int64_t run_automine_for_user(Database* db, uint64_t user_id) {
    try {
        // Find user's mining gear
        auto inv = db->get_inventory(user_id);
        std::string pickaxe_id, minecart_id, bag_id;
        int pickaxe_lvl = 0, minecart_lvl = 0, bag_lvl = 0;
        std::string pickaxe_meta, minecart_meta, bag_meta;

        for (auto& item : inv) {
            if (item.item_id.rfind("pickaxe_", 0) == 0 && item.level > pickaxe_lvl) {
                pickaxe_id = item.item_id; pickaxe_lvl = item.level; pickaxe_meta = item.metadata;
            }
            if (item.item_id.rfind("minecart_", 0) == 0 && item.level > minecart_lvl) {
                minecart_id = item.item_id; minecart_lvl = item.level; minecart_meta = item.metadata;
            }
            if (item.item_id.rfind("bag_", 0) == 0 && item.level > bag_lvl) {
                bag_id = item.item_id; bag_lvl = item.level; bag_meta = item.metadata;
            }
        }

        if (pickaxe_id.empty() || minecart_id.empty() || bag_id.empty()) {
            std::cerr << "Autominer user=" << user_id << " missing gear\n";
            return 0;
        }

        int bag_capacity = parse_mine_meta_int(bag_meta, "capacity", 5);
        int prestige_level = db->get_prestige(user_id);

        // Build ore pool
        std::vector<OreType> pool;
        for (auto& ore : ore_types) {
            if (ore.max_pickaxe_level > 0 && pickaxe_lvl >= ore.max_pickaxe_level) continue;
            if (ore.min_pickaxe_level > 0 && pickaxe_lvl < ore.min_pickaxe_level) continue;
            pool.push_back(ore);
        }
        if (pool.empty()) return 0;

        int luck = parse_mine_meta_int(pickaxe_meta, "luck", 0);
        auto spawn_rates = parse_spawn_rates(minecart_meta);
        int max_w = 0;
        for (auto& o : pool) max_w = std::max(max_w, o.weight);
        std::vector<int> weights;
        for (auto& o : pool) {
            int w = o.weight > 0 ? o.weight : 1;
            if (luck != 0 && max_w > 0) w += (int)((max_w - w) * (luck / 100.0));
            auto sr = spawn_rates.find(o.name);
            if (sr != spawn_rates.end()) w += sr->second;
            weights.push_back(w);
        }
        int total_weight = 0;
        for (int w : weights) total_weight += w;
        if (total_weight <= 0) return 0;

        std::random_device rd_dev;
        std::mt19937 gen(rd_dev());
        std::discrete_distribution<> dis;
        try {
            dis = std::discrete_distribution<>(weights.begin(), weights.end());
        } catch (...) { return 0; }

        // Autominer mines a fraction of bag capacity per cycle (simulates partial session)
        int num_ores = std::max(1, bag_capacity / 3);
        int64_t total_value = 0;

        for (int i = 0; i < num_ores; i++) {
            int idx;
            try { idx = dis(gen); if (idx < 0 || idx >= (int)pool.size()) idx = 0; }
            catch (...) { idx = 0; }

            const auto& ore = pool[idx];
            std::uniform_int_distribution<int64_t> valdis(ore.min_value, ore.max_value);
            int64_t val = valdis(gen);
            if (luck != 0) val += val * luck / 100;

            int prestige_bonus = prestige_level * 5;
            if (prestige_bonus > 0) val += val * prestige_bonus / 100;

            // Simple effect check
            double roll = (double)rand() / RAND_MAX;
            if (roll < ore.effect_chance) {
                switch (ore.effect) {
                    case OreEffect::Flat: val += luck + pickaxe_lvl; break;
                    case OreEffect::Exponential: val = (int64_t)(val * pow(1.0 + luck / 100.0, 2)); break;
                    case OreEffect::Wacky: val *= (rand() % 5 + 1); break;
                    case OreEffect::Jackpot: val = (rand() % 2 == 0) ? (int64_t)(val * 0.2) : (int64_t)(val * 8); break;
                    case OreEffect::Critical: val = (rand() % 2 == 0) ? val * 2 : val; break;
                    case OreEffect::Cascading: { int rolls = 1 + rand() % 6; val = (int64_t)(val * pow(1.15, rolls)); break; }
                    default: break;
                }
            }

            total_value += val;

            // Auto-sell immediately at fee
            int64_t payout = (int64_t)(val * (1.0 - AUTOMINER_SELL_FEE));
            db->update_wallet(user_id, payout);
            db->increment_stat(user_id, "ores_mined", 1);
            db->increment_stat(user_id, "ores_sold", 1);
        }

        std::cout << "Autominer user=" << user_id
                  << " mined=" << num_ores << " value=$" << total_value << "\n";

        // Track global boss progress from autominer
        if (num_ores > 0) {
            global_boss::on_mine_command(db, user_id);
            global_boss::on_ores_mined(db, user_id, (int64_t)num_ores);
        }

        return total_value;

    } catch (const std::exception& e) {
        std::cerr << "Autominer error user=" << user_id << ": " << e.what() << "\n";
        return 0;
    } catch (...) {
        std::cerr << "Autominer unknown error user=" << user_id << "\n";
        return 0;
    }
}

} // namespace mining
} // namespace commands
