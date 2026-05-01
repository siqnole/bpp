#pragma once
#include "../../database/core/database.h"
#include "fishing_helpers.h"
#include "../economy_core.h"
#include "../global_boss.h"
#include <chrono>
#include <chrono>
#include "../../utils/logger.h"
#include <cmath>
#include <set>
#include <map>
#include <mutex>

using namespace bronx::db;

namespace commands {
namespace fishing {

// AUTO-SELL FEE applied to gross value when the autofisher sells on the user's behalf.
static constexpr double AUTOSELL_FEE = 0.22; // 22% convenience fee

// ── DM notification throttling ──────────────────────────────────────────
// Track last DM time per (user_id, reason) to avoid spamming
static std::map<std::pair<uint64_t, std::string>, std::chrono::steady_clock::time_point> af_dm_throttle;
static std::mutex af_dm_mutex;

// Send a DM to user about an autofish failure, throttled to once per hour per reason
inline void af_notify_user(dpp::cluster* bot, uint64_t user_id, const std::string& reason, const std::string& message) {
    if (!bot) return;
    auto key = std::make_pair(user_id, reason);
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(af_dm_mutex);
        auto it = af_dm_throttle.find(key);
        if (it != af_dm_throttle.end() &&
            std::chrono::duration_cast<std::chrono::hours>(now - it->second).count() < 1) {
            return; // already notified within the last hour
        }
        af_dm_throttle[key] = now;
    }
    try {
        dpp::embed embed = dpp::embed()
            .set_color(0xE5989B) // soft red
            .set_title("🎣 autofisher paused")
            .set_description(message)
            .set_timestamp(time(0));
        bot->direct_message_create(user_id, dpp::message().add_embed(embed));
    } catch (...) {}
}

// Optional: set from main.cpp so the runner can send DMs
static dpp::cluster* af_bot_ptr = nullptr;
inline void set_autofish_bot(dpp::cluster* bot) { af_bot_ptr = bot; }

// Attempt to top up the autofisher's bait supply by drawing from the user's bank.
// Returns the number of bait units added (0 if disabled or insufficient funds).
inline int autofisher_try_buy_bait(Database* db, uint64_t user_id, const AutofisherConfig& cfg, int needed) {
    if (cfg.max_bank_draw <= 0 || cfg.af_bait_id.empty()) return 0;
    auto shop_item = db->get_shop_item(cfg.af_bait_id);
    if (!shop_item || shop_item->price <= 0) return 0;

    int64_t user_bank = db->get_bank(user_id);
    if (user_bank <= 0) return 0;

    // How many can we afford, capped at max_bank_draw per purchase and what we need
    int64_t budget  = std::min(user_bank, cfg.max_bank_draw);
    int     can_buy = (int)(budget / shop_item->price);
    int     to_buy  = std::min(can_buy, needed);
    if (to_buy <= 0) return 0;

    int64_t cost = shop_item->price * to_buy;
    if (!db->update_bank(user_id, -cost)) return 0;

    db->autofisher_deposit_bait(user_id, to_buy);
    bronx::logger::debug("autofish", "bought " + std::to_string(to_buy) + "x " + cfg.af_bait_id + " for $" + std::to_string(cost) + " from user " + std::to_string(user_id) + "'s bank");
    return to_buy;
}

// Run one autofishing cycle for a single user.
// Returns the total gross value of fish caught this cycle (before any auto-sell fee).
inline int64_t run_autofish_for_user(Database* db, uint64_t user_id) {
    try {
        // Load autofisher configuration (own gear, bait pool, settings)
        auto maybe_cfg = db->get_autofisher_config(user_id);
        if (!maybe_cfg) {
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " no config row");
            return 0;
        }
        AutofisherConfig cfg = *maybe_cfg;

        if (cfg.af_rod_id.empty()) {
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " no rod equipped");
            af_notify_user(af_bot_ptr, user_id, "no_rod",
                "your autofisher has **no rod equipped**.\nuse `autofisher equip rod <id>` to fix this.");
            return 0;
        }
        if (cfg.af_bait_id.empty()) {
            bronx::logger::warn("autofish", "user=" + std::to_string(user_id) + " no bait type set");
            af_notify_user(af_bot_ptr, user_id, "no_bait",
                "your autofisher has **no bait type set**.\nuse `autofisher equip bait <id>` to fix this.");
            return 0;
        }

        // Rod must still be in the user's own inventory
        if (!db->has_item(user_id, cfg.af_rod_id, 1)) {
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " rod not in inventory");
            af_notify_user(af_bot_ptr, user_id, "rod_missing",
                "your autofisher rod `" + cfg.af_rod_id + "` is **no longer in your inventory** (sold or consumed).\n"
                "equip a new rod with `autofisher equip rod <id>`.");
            return 0;
        }

        // Resolve rod level/meta from user inventory
        int rod_lvl = 1;
        std::string rod_meta;
        for (auto& it : db->get_inventory(user_id)) {
            if (it.item_id == cfg.af_rod_id) { rod_lvl = it.level; rod_meta = it.metadata; break; }
        }

        int bait_lvl      = cfg.af_bait_level;
        std::string bait_meta = cfg.af_bait_meta;

        // Allow common/uncommon/rare bait (levels 1-3) with any rod
        if (bait_lvl > 3 && abs(rod_lvl - bait_lvl) > 2) {
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " incompatible gear levels");
            af_notify_user(af_bot_ptr, user_id, "gear_mismatch",
                "your autofisher gear is **incompatible** — rod level " + std::to_string(rod_lvl) +
                " vs bait level " + std::to_string(bait_lvl) + " (gap > 2).\n"
                "equip compatible gear with `autofisher equip rod|bait <id>`.");
            return 0;
        }

        int capacity = parse_meta_int(rod_meta, "capacity", 1);

        // Try buying more bait from bank if running low
        if (cfg.af_bait_qty < capacity) {
            int bought = autofisher_try_buy_bait(db, user_id, cfg, capacity - cfg.af_bait_qty);
            cfg.af_bait_qty += bought;
        }

        if (cfg.af_bait_qty <= 0) {
            bronx::logger::warn("autofish", "user=" + std::to_string(user_id) + " out of bait");
            af_notify_user(af_bot_ptr, user_id, "out_of_bait",
                "your autofisher is **out of bait** and couldn't buy more.\n"
                "deposit bait with `autofisher deposit` or enable bank draw with `autofisher balance <amount>`.");
            return 0;
        }

        int used_bait = std::min(cfg.af_bait_qty, capacity);
        db->autofisher_consume_bait(user_id, used_bait);

        // Build fish pool
        auto unlocks = parse_meta_array(bait_meta, "unlocks");
        std::vector<FishType> pool;
        if (!unlocks.empty())
            for (auto& f : fish_types)
                if (std::find(unlocks.begin(), unlocks.end(), f.name) != unlocks.end())
                    pool.push_back(f);
        // track whether we have explicit unlocks from bait
        bool has_explicit_unlocks = !unlocks.empty() && !pool.empty();
        if (pool.empty()) pool = fish_types;

        if (bait_lvl > 2)
            pool.erase(std::remove_if(pool.begin(), pool.end(),
                [](const FishType& f){ return f.name == "common fish"; }), pool.end());

        int gear_lvl = std::min(rod_lvl, bait_lvl);
        // when bait has explicit unlocks, use bait_lvl for min_gear check so unlocked fish aren't filtered out
        int unlock_gear_lvl = has_explicit_unlocks ? bait_lvl : gear_lvl;
        int prestige_level = db->get_prestige(user_id);
        pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const FishType& f){
            if (f.max_gear_level > 0 && gear_lvl >= f.max_gear_level) return true;
            if (f.min_gear_level > 0 && unlock_gear_lvl < f.min_gear_level) return true;
            return false;
        }), pool.end());
        if (pool.empty()) {
            // fallback: return bait and error out instead of using all fish types
            db->autofisher_deposit_bait(user_id, used_bait);
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " no fish available for gear combo");
            return 0;
        }

        int luck = parse_meta_int(rod_meta, "luck", 0);
        int max_w = 0;
        for (auto& f : pool) max_w = std::max(max_w, f.weight);
        std::vector<int> weights;
        for (auto& f : pool) {
            int w = f.weight > 0 ? f.weight : 1;
            if (luck != 0 && max_w > 0) w += (int)((max_w - w) * (luck / 100.0));
            weights.push_back(w);
        }
        int total_weight = 0;
        for (int w : weights) total_weight += w;
        if (total_weight <= 0) {
            db->autofisher_deposit_bait(user_id, used_bait);
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " invalid weight distribution");
            return 0;
        }

        std::random_device rd_dev;
        std::mt19937 gen(rd_dev());
        std::discrete_distribution<> dis;
        try {
            dis = std::discrete_distribution<>(weights.begin(), weights.end());
        } catch (const std::invalid_argument& e) {
            db->autofisher_deposit_bait(user_id, used_bait);
            bronx::logger::error("autofish", "user=" + std::to_string(user_id) + " dist error: " + std::string(e.what()));
            return 0;
        }

        bool avoid_common = (cfg.af_bait_id == "bait_rare" || cfg.af_bait_id == "bait_epic" || cfg.af_bait_id == "bait_legendary");

        int bait_bonus = parse_meta_int(bait_meta, "bonus", 0);
        int synergy    = (rod_lvl == bait_lvl ? rod_lvl : 1);
        int extra_fish = (used_bait * bait_bonus * synergy) / 100;
        int total_fish = used_bait + extra_fish;
        int64_t total_value = 0;

        // Pre-fetch stat values that the effect switch reads from DB.
        // Cache them ONCE before the loop so effects don't trigger extra round-trips.
        int64_t prefetch_wallet    = db->get_wallet(user_id);
        int64_t prefetch_bank      = db->get_bank(user_id);
        int64_t prefetch_fish_caught = db->get_stat(user_id, "fish_caught");
        int64_t prefetch_fish_sold   = db->get_stat(user_id, "fish_sold");
        int64_t prefetch_gamble_wins   = db->get_stat(user_id, "gambling_wins");
        int64_t prefetch_gamble_losses = db->get_stat(user_id, "gambling_losses");
        std::set<std::string> prefetch_collectible_ids;
        {
            auto inv = db->get_inventory(user_id);
            for (auto& it : inv)
                if (it.item_type == "collectible") prefetch_collectible_ids.insert(it.item_id);
        }

        // Batch accumulators — filled in the loop, flushed once after
        std::vector<Database::AutofishFishRow> af_batch;
        std::vector<Database::FishCatchRow> fc_batch;
        af_batch.reserve(total_fish);
        fc_batch.reserve(total_fish);

        for (int i = 0; i < total_fish; ++i) {
            int idx;
            const FishType* fishptr;
            do {
                try { idx = dis(gen); if (idx < 0 || idx >= (int)pool.size()) idx = 0; }
                catch (...) { idx = 0; }
                fishptr = &pool[idx];
            } while (avoid_common && fishptr->name == "common fish");

            const auto& fish = *fishptr;
            std::uniform_int_distribution<int64_t> valdis(fish.min_value, fish.max_value);
            int64_t base = valdis(gen);
            if (luck != 0)    base += base * luck / 100;
            if (bait_lvl > 1) base += bait_lvl * 5;
            int bait_mult = parse_meta_int(bait_meta, "multiplier", 0);
            if (bait_mult != 0) base += base * bait_mult / 100;
            int64_t val = base;

            double roll = (double)rand() / RAND_MAX;
            if (roll < fish.effect_chance) {
                switch (fish.effect) {
                    case FishEffect::Flat:        val += luck + bait_lvl; break;
                    case FishEffect::Exponential: val = (int64_t)(val * std::pow(1.0 + luck / 100.0, 2)); break;
                    case FishEffect::Logarithmic: val = (int64_t)(val * std::log2(luck + 2)); break;
                    case FishEffect::NLogN: { double n = luck + bait_lvl; val = (int64_t)(val * (n * std::log2(n + 2))); break; }
                    case FishEffect::Wacky:       val *= (rand() % 5 + 1); break;
                    case FishEffect::Jackpot:     val = (rand()%2==0) ? (int64_t)(val*0.2) : (int64_t)(val*8); break;
                    case FishEffect::Critical:    val = (rand()%2==0) ? val*2 : val; break;
                    case FishEffect::Volatile:    val = (int64_t)(val * (0.3 + (rand()%38)/10.0)); break;
                    case FishEffect::Surge:       val += gear_lvl*gear_lvl*50; break;
                    case FishEffect::Diminishing: { double m = 3.0 - luck/50.0; val = (int64_t)(val * std::max(0.5, m)); break; }
                    case FishEffect::Cascading:   { int rolls = 1 + rand()%6; val = (int64_t)(val * std::pow(1.15, rolls)); break; }
                    case FishEffect::Wealthy:     val = (int64_t)(val * (1.0 + sqrt((double)prefetch_wallet)/1000.0)); break;
                    case FishEffect::Banker:      val = (int64_t)(val * (1.0 + log10((double)prefetch_bank+1)/5.0)); break;
                    case FishEffect::Fisher:      val = (int64_t)(val * (1.0 + (double)prefetch_fish_caught/50000.0)); break;
                    case FishEffect::Merchant:    val += prefetch_fish_sold/100; break;
                    case FishEffect::Gambler:     val = (prefetch_gamble_wins > prefetch_gamble_losses) ? val*2 : (int64_t)(val*0.5); break;
                    case FishEffect::Ascended: { double mult = std::min(10.0, std::pow(1.5, prestige_level)); val = (int64_t)(val * mult); break; }
                    case FishEffect::Underdog: { double m = 2.0 - (double)prefetch_wallet/10000000.0; val = (int64_t)(val * std::max(0.5, m)); break; }
                    case FishEffect::HotStreak:   val = (int64_t)(val * (1.0 + (double)prefetch_gamble_wins/((double)prefetch_gamble_wins+(double)prefetch_gamble_losses+1.0))); break;
                    case FishEffect::Collector:   val += prefetch_collectible_ids.size()*100; break;
                    case FishEffect::Persistent:  val = (int64_t)(val * std::log2((double)(prefetch_fish_caught+prefetch_fish_sold)+2.0)); break;
                    default: break;
                }
            }
            total_value += val;

            std::string fish_meta = "{\"name\":\"" + fish.name + "\",\"value\":" + std::to_string(val) + "}";
            af_batch.push_back({fish.name, val, fish_meta});
            std::string rarity = get_fish_rarity(fish.name);
            fc_batch.push_back({rarity, fish.name, 1.0, val, cfg.af_rod_id, cfg.af_bait_id});
        }

        // Flush both batches in just 2 round-trips instead of 2*total_fish
        db->autofisher_add_fish_batch(user_id, af_batch);
        db->add_fish_catches_batch(user_id, fc_batch);

        // ML log
        int64_t bait_price = 0;
        if (auto bi = db->get_shop_item(cfg.af_bait_id)) bait_price = bi->price;
        db->record_fishing_log(rod_lvl, bait_lvl, total_value - bait_price * used_bait);

        bronx::logger::debug("autofish", "user=" + std::to_string(user_id) + " caught=" + std::to_string(total_fish) + " value=$" + std::to_string(total_value));

        // Track global boss progress from autofish
        if (total_fish > 0) {
            global_boss::on_fish_command(db, user_id, (int64_t)total_fish);
        }

        // ── Auto-sell check ──────────────────────────────────────────────────
        if (cfg.auto_sell) {
            bool trigger = false;
            int64_t stored_val = 0;

            if (cfg.as_trigger == "bag") {
                trigger = (db->autofisher_fish_count(user_id) >= cfg.bag_limit);
            } else if (cfg.as_trigger == "count") {
                trigger = (db->autofisher_fish_count(user_id) >= (int)cfg.as_threshold);
            } else if (cfg.as_trigger == "balance") {
                for (auto& f : db->autofisher_get_fish(user_id)) stored_val += f.value;
                trigger = (stored_val >= cfg.as_threshold);
            }

            if (trigger) {
                int64_t gross  = db->autofisher_clear_fish(user_id);
                int64_t payout = (int64_t)(gross * (1.0 - AUTOSELL_FEE));
                db->update_wallet(user_id, payout);
                // Track global boss fish profit from autofisher auto-sell
                global_boss::on_fish_profit(db, user_id, payout);
                bronx::logger::debug("autofish", "auto-sell user=" + std::to_string(user_id) + " gross=$" + std::to_string(gross) + " payout=$" + std::to_string(payout));
            }
        }

        return total_value;

    } catch (const std::exception& e) {
        bronx::logger::error("autofish", "error user=" + std::to_string(user_id) + ": " + std::string(e.what()));
        return 0;
    } catch (...) {
        bronx::logger::error("autofish", "unknown error user=" + std::to_string(user_id));
        return 0;
    }
}

} // namespace fishing
} // namespace commands
