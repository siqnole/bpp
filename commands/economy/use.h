#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "helpers.h"
#include "../fishing/fishing_helpers.h"
#include "../mining/mining_helpers.h"
#include "lootbox_catalog.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <sstream>
#include <ctime>
#include <map>
#include <functional>
#include <chrono>
#include <mutex>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace use_item {

// thread-local random engine
static thread_local std::mt19937 rng(std::random_device{}());

// ============================================================================
// BOOST DEFINITIONS
// ============================================================================

struct BoostDef {
    std::string item_id;
    std::string name;
    std::string emoji;
    std::string description;
    std::string boost_type;     // "xp", "luck", "money"
    double multiplier;          // e.g. 1.5 = 50% boost
    int duration_seconds;       // how long the boost lasts
};

inline const std::vector<BoostDef>& get_boost_catalog() {
    static std::vector<BoostDef> catalog = {
        // XP Boosts
        {"xp_boost_small",  "XP Boost (15min)",    "⚡", "1.25x XP for 15 minutes",   "xp",    1.25, 900},
        {"xp_boost_medium", "XP Boost (30min)",    "⚡", "1.5x XP for 30 minutes",    "xp",    1.50, 1800},
        {"xp_boost_large",  "XP Boost (1hr)",      "⚡", "1.75x XP for 1 hour",       "xp",    1.75, 3600},
        {"xp_boost_mega",   "Mega XP Boost (2hr)", "🌟", "2x XP for 2 hours",         "xp",    2.00, 7200},
        // Luck Boosts
        {"luck_boost_small",  "Luck Boost (15min)",  "🍀", "1.2x luck for 15 minutes",  "luck",  1.20, 900},
        {"luck_boost_medium", "Luck Boost (30min)",  "🍀", "1.4x luck for 30 minutes",  "luck",  1.40, 1800},
        {"luck_boost_large",  "Luck Boost (1hr)",    "🍀", "1.6x luck for 1 hour",      "luck",  1.60, 3600},
        // Money Boosts
        {"money_boost_small",  "Money Boost (15min)", "💸", "1.25x money for 15 minutes","money", 1.25, 900},
        {"money_boost_medium", "Money Boost (30min)", "💸", "1.5x money for 30 minutes", "money", 1.50, 1800},
        {"money_boost_large",  "Money Boost (1hr)",   "💸", "1.75x money for 1 hour",    "money", 1.75, 3600},
    };
    return catalog;
}

// Find boost definition by item_id
inline const BoostDef* find_boost(const std::string& item_id) {
    for (const auto& b : get_boost_catalog()) {
        if (b.item_id == item_id) return &b;
    }
    return nullptr;
}

// ============================================================================
// TOOL DEFINITIONS (usable tools)
// ============================================================================

struct ToolDef {
    std::string item_id;
    std::string name;
    std::string emoji;
    std::string description;
    std::string tool_type;      // "metal_detector", "treasure_map", etc.
};

inline const std::vector<ToolDef>& get_tool_catalog() {
    static std::vector<ToolDef> catalog = {
        {"metal_detector", "Metal Detector",  "🔍", "find hidden treasures — gives random money", "metal_detector"},
        {"treasure_map",   "Treasure Map",    "🗺️", "follow the map to a stash of money",        "treasure_map"},
        {"lucky_coin",     "Lucky Coin",      "🪙", "flip it for a chance at double value",       "lucky_coin"},
    };
    return catalog;
}

// Find tool definition by item_id
inline const ToolDef* find_tool(const std::string& item_id) {
    for (const auto& t : get_tool_catalog()) {
        if (t.item_id == item_id) return &t;
    }
    return nullptr;
}

// ============================================================================
// ACTIVE BOOSTS SYSTEM
// ============================================================================

// Active boost stored per user (in memory — resets on restart)
struct ActiveBoost {
    std::string boost_type;     // "xp", "luck", "money"
    double multiplier;
    std::chrono::steady_clock::time_point expires_at;
};

// Global active boosts map: user_id -> list of active boosts
inline std::map<uint64_t, std::vector<ActiveBoost>>& get_active_boosts() {
    static std::map<uint64_t, std::vector<ActiveBoost>> boosts;
    return boosts;
}

inline std::mutex& get_boost_mutex() {
    static std::mutex mtx;
    return mtx;
}

// Clean expired boosts for a user
inline void clean_expired_boosts(uint64_t user_id) {
    auto now = std::chrono::steady_clock::now();
    auto& boosts = get_active_boosts();
    auto it = boosts.find(user_id);
    if (it == boosts.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(), [&now](const ActiveBoost& b) {
        return now >= b.expires_at;
    }), vec.end());
    if (vec.empty()) boosts.erase(it);
}

// Activate a boost for a user
inline void activate_boost(uint64_t user_id, const std::string& boost_type, double multiplier, int duration_seconds) {
    std::lock_guard<std::mutex> lock(get_boost_mutex());
    clean_expired_boosts(user_id);
    auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(duration_seconds);
    get_active_boosts()[user_id].push_back({boost_type, multiplier, expires});
}

// Get the combined multiplier for a boost type (uses highest active, no stacking)
inline double get_boost_multiplier(uint64_t user_id, const std::string& boost_type) {
    std::lock_guard<std::mutex> lock(get_boost_mutex());
    clean_expired_boosts(user_id);
    auto it = get_active_boosts().find(user_id);
    if (it == get_active_boosts().end()) return 1.0;
    double mult = 1.0;
    for (const auto& b : it->second) {
        if (b.boost_type == boost_type) {
            mult = std::max(mult, b.multiplier);
        }
    }
    return mult;
}

// Get all active boosts for a user (for display)
inline std::vector<std::pair<std::string, std::pair<double, int>>> get_user_boosts(uint64_t user_id) {
    std::lock_guard<std::mutex> lock(get_boost_mutex());
    clean_expired_boosts(user_id);
    std::vector<std::pair<std::string, std::pair<double, int>>> result;
    auto it = get_active_boosts().find(user_id);
    if (it == get_active_boosts().end()) return result;
    auto now = std::chrono::steady_clock::now();
    for (const auto& b : it->second) {
        int remaining = (int)std::chrono::duration_cast<std::chrono::seconds>(b.expires_at - now).count();
        if (remaining > 0) {
            result.push_back({b.boost_type, {b.multiplier, remaining}});
        }
    }
    return result;
}

// ============================================================================
// LOOTBOX OPENING LOGIC
// ============================================================================

struct RewardResult {
    std::string emoji;
    std::string description;
    std::string type;
    int64_t amount;
    std::string item_id;       // for consolidation
    std::string display_name;  // for consolidation
};

// Roll a single reward from a lootbox reward pool
inline RewardResult roll_reward(Database* db, uint64_t user_id, const std::vector<LootReward>& pool) {
    // Calculate total weight
    double total_weight = 0;
    for (const auto& r : pool) total_weight += r.weight;

    // Weighted random selection
    std::uniform_real_distribution<double> dist(0.0, total_weight);
    double roll = dist(rng);

    double cumulative = 0;
    const LootReward* selected = &pool[0];
    for (const auto& r : pool) {
        cumulative += r.weight;
        if (roll <= cumulative) {
            selected = &r;
            break;
        }
    }

    // Determine quantity
    int64_t amount = selected->min_amount;
    if (selected->max_amount > selected->min_amount) {
        std::uniform_int_distribution<int64_t> amt_dist(selected->min_amount, selected->max_amount);
        amount = amt_dist(rng);
    }

    RewardResult result;
    result.emoji = selected->emoji;
    result.type = selected->type;
    result.amount = amount;
    result.item_id = selected->item_id;
    result.display_name = selected->display_name;

    if (selected->type == "money") {
        double mult = get_boost_multiplier(user_id, "money");
        amount = (int64_t)(amount * mult);
        result.amount = amount;
        db->update_wallet(user_id, amount);
        result.description = "**$" + economy::format_number(amount) + "**";
        if (mult > 1.0) result.description += " *(boosted!)*";
    }
    else if (selected->type == "bait") {
        db->add_item(user_id, selected->item_id, "bait", (int)amount, "{}", 1);
        result.description = "**" + selected->display_name + "** x" + std::to_string(amount);
    }
    else if (selected->type == "fish") {
        for (int i = 0; i < (int)amount; ++i) {
            std::string fish_id = "F_" + std::to_string(user_id) + "_" + std::to_string(time(nullptr)) + "_" + std::to_string(rng() % 10000);
            int64_t fish_value = 0;
            for (const auto& ft : fishing::fish_types) {
                if (ft.name == selected->item_id) {
                    std::uniform_int_distribution<int64_t> val_dist(ft.min_value, ft.max_value);
                    fish_value = val_dist(rng);
                    break;
                }
            }
            std::string metadata = "{\"name\":\"" + selected->display_name + "\",\"value\":" + std::to_string(fish_value) + "}";
            db->add_item(user_id, fish_id, "collectible", 1, metadata, 1);
        }
        result.description = "**" + selected->display_name + "**";
        if (amount > 1) result.description += " x" + std::to_string(amount);
    }
    else if (selected->type == "ore") {
        for (int i = 0; i < (int)amount; ++i) {
            std::string ore_id = "M_" + std::to_string(user_id) + "_" + std::to_string(time(nullptr)) + "_" + std::to_string(rng() % 10000);
            int64_t ore_value = 0;
            for (const auto& ot : mining::ore_types) {
                if (ot.name == selected->item_id) {
                    std::uniform_int_distribution<int64_t> val_dist(ot.min_value, ot.max_value);
                    ore_value = val_dist(rng);
                    break;
                }
            }
            std::string metadata = "{\"name\":\"" + selected->display_name + "\",\"value\":" + std::to_string(ore_value) + "}";
            db->add_item(user_id, ore_id, "collectible", 1, metadata, 1);
        }
        result.description = "**" + selected->display_name + "**";
        if (amount > 1) result.description += " x" + std::to_string(amount);
    }
    else if (selected->type == "boost") {
        db->add_item(user_id, selected->item_id, "boosts", (int)amount, "{}", 1);
        result.description = "**" + selected->display_name + "**";
        if (amount > 1) result.description += " x" + std::to_string(amount);
    }
    else if (selected->type == "item") {
        std::string itype = "other";
        db->add_item(user_id, selected->item_id, itype, (int)amount, "{}", 1);
        result.description = "**" + selected->display_name + "**";
        if (amount > 1) result.description += " x" + std::to_string(amount);
    }

    return result;
}

// Open a lootbox and return the embed message
inline dpp::embed open_lootbox(Database* db, uint64_t user_id, const LootboxDef& lootbox,
                                const dpp::user& invoker) {
    std::uniform_int_distribution<int> roll_dist(lootbox.min_rolls, lootbox.max_rolls);
    int num_rolls = roll_dist(rng);

    std::vector<RewardResult> rewards;
    for (int i = 0; i < num_rolls; ++i) {
        rewards.push_back(roll_reward(db, user_id, lootbox.reward_pool));
    }

    std::string tier_text;
    switch (lootbox.tier) {
        case LootboxTier::Common:    tier_text = "COMMON"; break;
        case LootboxTier::Uncommon:  tier_text = "UNCOMMON"; break;
        case LootboxTier::Rare:      tier_text = "RARE"; break;
        case LootboxTier::Epic:      tier_text = "EPIC"; break;
        case LootboxTier::Legendary: tier_text = "LEGENDARY"; break;
        case LootboxTier::Prestige:  tier_text = "PRESTIGE"; break;
    }

    uint32_t color;
    switch (lootbox.tier) {
        case LootboxTier::Common:    color = 0x95A5A6; break;
        case LootboxTier::Uncommon:  color = 0x2ECC71; break;
        case LootboxTier::Rare:      color = 0x3498DB; break;
        case LootboxTier::Epic:      color = 0x9B59B6; break;
        case LootboxTier::Legendary: color = 0xF39C12; break;
        case LootboxTier::Prestige:  color = 0xE91E63; break;
    }

    std::string desc = lootbox.emoji + " **" + tier_text + " LOOTBOX OPENED!** " + lootbox.emoji + "\n\n";
    desc += "you received:\n\n";

    // Consolidate duplicate rewards (e.g. "Legendary Bait x2" + "Legendary Bait x1" → "Legendary Bait x3")
    struct MergedReward { std::string emoji; std::string type; std::string item_id; std::string display_name; int64_t amount; bool boosted; };
    std::vector<MergedReward> merged;
    for (auto& r : rewards) {
        // Money rewards: group all money together by summing amounts
        // Other rewards: group by type + item_id
        std::string key = (r.type == "money") ? "money" : (r.type + ":" + r.item_id);
        bool found = false;
        for (auto& m : merged) {
            std::string mkey = (m.type == "money") ? "money" : (m.type + ":" + m.item_id);
            if (key == mkey) {
                m.amount += r.amount;
                if (r.description.find("boosted") != std::string::npos) m.boosted = true;
                found = true;
                break;
            }
        }
        if (!found) {
            merged.push_back({r.emoji, r.type, r.item_id, r.display_name, r.amount,
                              r.description.find("boosted") != std::string::npos});
        }
    }

    // Build display lines from merged rewards
    for (auto& m : merged) {
        if (m.type == "money") {
            desc += m.emoji + " **$" + economy::format_number(m.amount) + "**";
            if (m.boosted) desc += " *(boosted!)*";
            desc += "\n";
        } else {
            desc += m.emoji + " **" + m.display_name + "**";
            if (m.amount > 1) desc += " x" + std::to_string(m.amount);
            desc += "\n";
        }
    }

    auto embed = dpp::embed()
        .set_description(desc)
        .set_color(color)
        .set_timestamp(time(0));

    bronx::add_invoker_footer(embed, invoker);
    bronx::maybe_add_support_link(embed);

    int64_t balance = db->get_wallet(user_id);
    log_entry(db, user_id, "USE", "opened " + lootbox.name + " (" + std::to_string(num_rolls) + " rewards)", 0, balance);

    return embed;
}

// ============================================================================
// TOOL USE LOGIC
// ============================================================================

inline dpp::embed use_tool(Database* db, uint64_t user_id, const ToolDef& tool,
                            const dpp::user& invoker) {
    std::string desc;
    int64_t reward = 0;

    if (tool.tool_type == "metal_detector") {
        std::uniform_int_distribution<int64_t> dist(200, 5000);
        reward = dist(rng);
        double mult = get_boost_multiplier(user_id, "money");
        reward = (int64_t)(reward * mult);

        std::uniform_int_distribution<int> jackpot_roll(1, 100);
        int j = jackpot_roll(rng);
        if (j <= 5) {
            reward *= 10;
            desc = tool.emoji + " **JACKPOT!** your metal detector found a buried treasure!\n\n";
        } else if (j <= 25) {
            reward *= 3;
            desc = tool.emoji + " your metal detector picked up a strong signal... you found something valuable!\n\n";
        } else {
            desc = tool.emoji + " you swept the area with your metal detector...\n\n";
        }
        db->update_wallet(user_id, reward);
        desc += "💰 you found **$" + economy::format_number(reward) + "**!";
    }
    else if (tool.tool_type == "treasure_map") {
        std::uniform_int_distribution<int64_t> dist(1000, 15000);
        reward = dist(rng);
        double mult = get_boost_multiplier(user_id, "money");
        reward = (int64_t)(reward * mult);

        static const std::vector<std::string> flavors = {
            "you followed the map through the forest and found a hidden chest!",
            "after solving the riddle, you discovered the treasure buried beneath a tree!",
            "the X on the map led you to a cave filled with gold!",
            "you navigated the clues and found the stash in an old well!",
            "the treasure was hidden behind a waterfall — classic!",
        };
        std::uniform_int_distribution<size_t> f_dist(0, flavors.size() - 1);
        desc = tool.emoji + " " + flavors[f_dist(rng)] + "\n\n";
        db->update_wallet(user_id, reward);
        desc += "💰 you received **$" + economy::format_number(reward) + "**!";
    }
    else if (tool.tool_type == "lucky_coin") {
        std::uniform_int_distribution<int> flip(0, 1);
        if (flip(rng)) {
            std::uniform_int_distribution<int64_t> dist(2000, 10000);
            reward = dist(rng);
            double mult = get_boost_multiplier(user_id, "money");
            reward = (int64_t)(reward * mult);
            db->update_wallet(user_id, reward);
            desc = tool.emoji + " you flipped the lucky coin... **heads!** 🎉\n\n";
            desc += "💰 fortune smiles on you: **$" + economy::format_number(reward) + "**!";
        } else {
            reward = 100;
            db->update_wallet(user_id, reward);
            desc = tool.emoji + " you flipped the lucky coin... **tails!**\n\n";
            desc += "💸 better luck next time! you got **$" + economy::format_number(reward) + "** consolation.";
        }
    }
    else {
        desc = "you used the **" + tool.name + "** but nothing happened...";
    }

    auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
    bronx::add_invoker_footer(embed, invoker);
    bronx::maybe_add_support_link(embed);

    int64_t balance = db->get_wallet(user_id);
    log_entry(db, user_id, "USE", "used " + tool.name + " (+$" + economy::format_number(reward) + ")", reward, balance);

    return embed;
}

// ============================================================================
// USABLE ITEM RESOLUTION
// ============================================================================

struct UsableItem {
    enum Type { Lootbox, Boost, Tool, Unknown } type;
    std::string item_id;
    std::string display_name;
};

inline UsableItem resolve_usable_item(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    // Normalised forms: spaces→underscores (matches IDs) and underscores→spaces (matches names)
    std::string lower_underscored = lower;
    std::replace(lower_underscored.begin(), lower_underscored.end(), ' ', '_');
    std::string lower_spaced = lower;
    std::replace(lower_spaced.begin(), lower_spaced.end(), '_', ' ');

    auto fuzzy_match = [&](const std::string& id, const std::string& name) -> bool {
        // Both-direction substring check, including normalized forms
        if (id.find(lower) != std::string::npos || lower.find(id) != std::string::npos) return true;
        if (name.find(lower) != std::string::npos || lower.find(name) != std::string::npos) return true;
        // space/underscore normalized comparisons
        if (id == lower_underscored || name == lower_spaced) return true;
        if (id.find(lower_underscored) != std::string::npos || lower_underscored.find(id) != std::string::npos) return true;
        return false;
    };

    // Check lootboxes — exact match (including normalized)
    for (const auto& lb : get_lootbox_catalog()) {
        std::string id = lb.item_id, name = lb.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (id == lower || name == lower || id == lower_underscored || name == lower_spaced)
            return {UsableItem::Lootbox, lb.item_id, lb.name};
    }
    // Fuzzy match lootboxes
    for (const auto& lb : get_lootbox_catalog()) {
        std::string id = lb.item_id, name = lb.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        // tier shorthand: "legendary" matches "lootbox_legendary"
        if (id.size() > 8) {
            std::string tier = id.substr(8);
            if (tier == lower) return {UsableItem::Lootbox, lb.item_id, lb.name};
        }
        if (fuzzy_match(id, name)) return {UsableItem::Lootbox, lb.item_id, lb.name};
    }

    // Check boosts — exact match
    for (const auto& b : get_boost_catalog()) {
        std::string id = b.item_id, name = b.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (id == lower || name == lower || id == lower_underscored || name == lower_spaced)
            return {UsableItem::Boost, b.item_id, b.name};
    }
    // Fuzzy match boosts
    for (const auto& b : get_boost_catalog()) {
        std::string id = b.item_id, name = b.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (fuzzy_match(id, name)) return {UsableItem::Boost, b.item_id, b.name};
    }

    // Check tools — exact match
    for (const auto& t : get_tool_catalog()) {
        std::string id = t.item_id, name = t.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (id == lower || name == lower || id == lower_underscored || name == lower_spaced)
            return {UsableItem::Tool, t.item_id, t.name};
    }
    // Fuzzy match tools
    for (const auto& t : get_tool_catalog()) {
        std::string id = t.item_id, name = t.name;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (fuzzy_match(id, name)) return {UsableItem::Tool, t.item_id, t.name};
    }

    return {UsableItem::Unknown, "", ""};
}

// ============================================================================
// HELPER: format time remaining for boosts
// ============================================================================

inline std::string format_duration(int seconds) {
    if (seconds <= 0) return "expired";
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    std::string result;
    if (h > 0) result += std::to_string(h) + "h ";
    if (m > 0) result += std::to_string(m) + "m ";
    if (s > 0 && h == 0) result += std::to_string(s) + "s";
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

// ============================================================================
// HELPER: get tier color
// ============================================================================

inline uint32_t tier_color(LootboxTier tier) {
    switch (tier) {
        case LootboxTier::Common:    return 0x95A5A6;
        case LootboxTier::Uncommon:  return 0x2ECC71;
        case LootboxTier::Rare:      return 0x3498DB;
        case LootboxTier::Epic:      return 0x9B59B6;
        case LootboxTier::Legendary: return 0xF39C12;
        case LootboxTier::Prestige:  return 0xE91E63;
    }
    return 0xB4A7D6;
}

// ============================================================================
// COMMANDS
// ============================================================================

// `use <item> [amount]` — use an item from inventory
inline Command* create_use_command(Database* db) {
    return new Command("use", "use an item from your inventory (lootboxes, boosts, tools)", "economy", {"open", "activate"}, true,
        // ── text handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (args.empty()) {
                std::string help = "**usage:** `use <item> [amount]`\n\n";
                help += "**usable items:**\n";
                help += "📦 **Lootboxes** — open for random rewards\n";
                help += "⚡ **Boosts** — activate temporary multipliers\n";
                help += "🔧 **Tools** — use for instant rewards\n\n";
                help += "**examples:**\n";
                help += "`use common lootbox`\n";
                help += "`use xp boost`\n";
                help += "`use metal detector`\n";
                help += "`use lootbox_rare 3`\n";
                auto embed = bronx::info(help);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);

            // Parse args: last arg may be a quantity or the keyword "all"
            std::string item_name;
            int use_count = 1;
            bool use_all = false;

            if (args.size() >= 2) {
                std::string last = args.back();
                if (last == "all") {
                    use_all = true;
                    for (size_t i = 0; i < args.size() - 1; ++i) {
                        if (i > 0) item_name += " ";
                        item_name += args[i];
                    }
                } else {
                    try {
                        int parsed = std::stoi(last);
                        if (parsed > 0) {
                            use_count = parsed;
                            for (size_t i = 0; i < args.size() - 1; ++i) {
                                if (i > 0) item_name += " ";
                                item_name += args[i];
                            }
                        } else {
                            for (const auto& a : args) { if (!item_name.empty()) item_name += " "; item_name += a; }
                        }
                    } catch (...) {
                        for (const auto& a : args) { if (!item_name.empty()) item_name += " "; item_name += a; }
                    }
                }
            } else {
                item_name = args[0];
            }

            if (!use_all && use_count > 10) {
                bronx::send_message(bot, event, bronx::error("you can only use up to **10** items at a time — or use `all` to open everything"));
                return;
            }
            if (use_count <= 0) use_count = 1;

            auto usable = resolve_usable_item(item_name);
            if (usable.type == UsableItem::Unknown) {
                bronx::send_message(bot, event, bronx::error("unknown item — check your inventory for usable items"));
                return;
            }

            int owned = db->get_item_quantity(user_id, usable.item_id);
            if (owned <= 0) {
                bronx::send_message(bot, event, bronx::error("you don't have any **" + usable.display_name + "** in your inventory"));
                return;
            }
            if (use_all) {
                use_count = owned;
            }
            if (owned < use_count) {
                bronx::send_message(bot, event, bronx::error("you only have **" + std::to_string(owned) + "x " + usable.display_name + "** (tried to use " + std::to_string(use_count) + ")"));
                return;
            }

            // === LOOTBOX ===
            if (usable.type == UsableItem::Lootbox) {
                const LootboxDef* lb = find_lootbox(usable.item_id);
                if (!lb) { bronx::send_message(bot, event, bronx::error("lootbox definition not found")); return; }

                if (lb->tier == LootboxTier::Prestige && db->get_prestige(user_id) < 1) {
                    bronx::send_message(bot, event, bronx::error("you need **prestige 1+** to open prestige lootboxes"));
                    return;
                }

                for (int i = 0; i < use_count; ++i) db->remove_item(user_id, usable.item_id, 1);

                if (use_count == 1) {
                    auto embed = open_lootbox(db, user_id, *lb, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    // Bulk open — combine rewards
                    std::string desc = lb->emoji + " **OPENED " + std::to_string(use_count) + "x " + lb->name + "!** " + lb->emoji + "\n\n";
                    int64_t total_money = 0;
                    std::map<std::string, int64_t> item_counts;

                    for (int i = 0; i < use_count; ++i) {
                        std::uniform_int_distribution<int> rd(lb->min_rolls, lb->max_rolls);
                        int n = rd(rng);
                        for (int j = 0; j < n; ++j) {
                            auto rw = roll_reward(db, user_id, lb->reward_pool);
                            if (rw.type == "money") total_money += rw.amount;
                            else item_counts[rw.emoji + " " + rw.description]++;
                        }
                    }

                    if (total_money > 0) desc += "💰 **$" + economy::format_number(total_money) + "** total cash\n";
                    for (const auto& [n, c] : item_counts) desc += n + (c > 1 ? " x" + std::to_string(c) : "") + "\n";

                    auto embed = dpp::embed().set_description(desc).set_color(tier_color(lb->tier)).set_timestamp(time(0));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    log_entry(db, user_id, "USE", "bulk opened " + std::to_string(use_count) + "x " + lb->name, 0, db->get_wallet(user_id));
                }
                return;
            }

            // === BOOST ===
            if (usable.type == UsableItem::Boost) {
                const BoostDef* boost = find_boost(usable.item_id);
                if (!boost) { bronx::send_message(bot, event, bronx::error("boost definition not found")); return; }

                if (use_count > 1) {
                    bronx::send_message(bot, event, bronx::error("you can only activate one boost at a time — use them one by one"));
                    return;
                }

                db->remove_item(user_id, usable.item_id, 1);
                activate_boost(user_id, boost->boost_type, boost->multiplier, boost->duration_seconds);

                std::string dur = format_duration(boost->duration_seconds);
                std::string desc = boost->emoji + " **" + boost->name + " activated!**\n\n";
                desc += "**Type:** " + boost->boost_type + " boost\n";
                desc += "**Multiplier:** " + std::to_string(boost->multiplier).substr(0, 4) + "x\n";
                desc += "**Duration:** " + dur + "\n\n";
                desc += "*your " + boost->boost_type + " gains are now boosted!*";

                auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                log_entry(db, user_id, "USE", "activated " + boost->name, 0, db->get_wallet(user_id));
                return;
            }

            // === TOOL ===
            if (usable.type == UsableItem::Tool) {
                const ToolDef* tool = find_tool(usable.item_id);
                if (!tool) { bronx::send_message(bot, event, bronx::error("tool definition not found")); return; }

                for (int i = 0; i < use_count; ++i) db->remove_item(user_id, usable.item_id, 1);

                if (use_count == 1) {
                    auto embed = use_tool(db, user_id, *tool, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    int64_t total = 0;
                    for (int i = 0; i < use_count; ++i) {
                        int64_t rw = 0;
                        if (tool->tool_type == "metal_detector") { std::uniform_int_distribution<int64_t> d(200,5000); rw = d(rng); }
                        else if (tool->tool_type == "treasure_map") { std::uniform_int_distribution<int64_t> d(1000,15000); rw = d(rng); }
                        else if (tool->tool_type == "lucky_coin") { std::uniform_int_distribution<int> f(0,1); rw = f(rng) ? std::uniform_int_distribution<int64_t>(2000,10000)(rng) : 100; }
                        rw = (int64_t)(rw * get_boost_multiplier(user_id, "money"));
                        total += rw;
                    }
                    db->update_wallet(user_id, total);
                    std::string desc = tool->emoji + " **used " + std::to_string(use_count) + "x " + tool->name + "!**\n\n💰 total: **$" + economy::format_number(total) + "**";
                    auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    log_entry(db, user_id, "USE", "bulk used " + std::to_string(use_count) + "x " + tool->name, total, db->get_wallet(user_id));
                }
                return;
            }
        },
        // ── slash handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string item_name = std::get<std::string>(event.get_parameter("item"));
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);

            int64_t use_count = 1;
            try {
                auto amt = event.get_parameter("amount");
                if (std::holds_alternative<int64_t>(amt)) use_count = std::get<int64_t>(amt);
            } catch (...) {}

            if (use_count > 10) { event.reply(dpp::message().add_embed(bronx::error("max 10 at a time"))); return; }
            if (use_count <= 0) use_count = 1;

            auto usable = resolve_usable_item(item_name);
            if (usable.type == UsableItem::Unknown) { event.reply(dpp::message().add_embed(bronx::error("unknown item"))); return; }

            int owned = db->get_item_quantity(user_id, usable.item_id);
            if (owned <= 0) { event.reply(dpp::message().add_embed(bronx::error("you don't have any **" + usable.display_name + "**"))); return; }
            if (owned < (int)use_count) { event.reply(dpp::message().add_embed(bronx::error("you only have **" + std::to_string(owned) + "x " + usable.display_name + "**"))); return; }

            // Lootbox
            if (usable.type == UsableItem::Lootbox) {
                const LootboxDef* lb = find_lootbox(usable.item_id);
                if (!lb) { event.reply(dpp::message().add_embed(bronx::error("lootbox not found"))); return; }
                if (lb->tier == LootboxTier::Prestige && db->get_prestige(user_id) < 1) {
                    event.reply(dpp::message().add_embed(bronx::error("you need **prestige 1+** for prestige lootboxes")));
                    return;
                }
                for (int i = 0; i < (int)use_count; ++i) db->remove_item(user_id, usable.item_id, 1);

                if (use_count == 1) {
                    event.reply(dpp::message().add_embed(open_lootbox(db, user_id, *lb, event.command.get_issuing_user())));
                } else {
                    std::string desc = lb->emoji + " **OPENED " + std::to_string(use_count) + "x " + lb->name + "!** " + lb->emoji + "\n\n";
                    int64_t total_money = 0;
                    std::map<std::string, int64_t> ic;
                    for (int i = 0; i < (int)use_count; ++i) {
                        std::uniform_int_distribution<int> rd(lb->min_rolls, lb->max_rolls);
                        int n = rd(rng);
                        for (int j = 0; j < n; ++j) {
                            auto rw = roll_reward(db, user_id, lb->reward_pool);
                            if (rw.type == "money") total_money += rw.amount;
                            else ic[rw.emoji + " " + rw.description]++;
                        }
                    }
                    if (total_money > 0) desc += "💰 **$" + economy::format_number(total_money) + "** total cash\n";
                    for (const auto& [n, c] : ic) desc += n + (c > 1 ? " x" + std::to_string(c) : "") + "\n";
                    auto embed = dpp::embed().set_description(desc).set_color(tier_color(lb->tier)).set_timestamp(time(0));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    log_entry(db, user_id, "USE", "bulk opened " + std::to_string(use_count) + "x " + lb->name, 0, db->get_wallet(user_id));
                }
                return;
            }

            // Boost
            if (usable.type == UsableItem::Boost) {
                const BoostDef* b = find_boost(usable.item_id);
                if (!b) { event.reply(dpp::message().add_embed(bronx::error("boost not found"))); return; }
                if (use_count > 1) { event.reply(dpp::message().add_embed(bronx::error("activate one boost at a time"))); return; }
                db->remove_item(user_id, usable.item_id, 1);
                activate_boost(user_id, b->boost_type, b->multiplier, b->duration_seconds);
                std::string desc = b->emoji + " **" + b->name + " activated!**\n\n";
                desc += "**Type:** " + b->boost_type + "\n**Multiplier:** " + std::to_string(b->multiplier).substr(0,4) + "x\n";
                desc += "**Duration:** " + format_duration(b->duration_seconds);
                event.reply(dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_SUCCESS)));
                log_entry(db, user_id, "USE", "activated " + b->name, 0, db->get_wallet(user_id));
                return;
            }

            // Tool
            if (usable.type == UsableItem::Tool) {
                const ToolDef* t = find_tool(usable.item_id);
                if (!t) { event.reply(dpp::message().add_embed(bronx::error("tool not found"))); return; }
                for (int i = 0; i < (int)use_count; ++i) db->remove_item(user_id, usable.item_id, 1);
                if (use_count == 1) {
                    event.reply(dpp::message().add_embed(use_tool(db, user_id, *t, event.command.get_issuing_user())));
                } else {
                    int64_t total = 0;
                    for (int i = 0; i < (int)use_count; ++i) {
                        int64_t rw = 0;
                        if (t->tool_type == "metal_detector") { std::uniform_int_distribution<int64_t> d(200,5000); rw = d(rng); }
                        else if (t->tool_type == "treasure_map") { std::uniform_int_distribution<int64_t> d(1000,15000); rw = d(rng); }
                        else if (t->tool_type == "lucky_coin") { std::uniform_int_distribution<int> f(0,1); rw = f(rng) ? std::uniform_int_distribution<int64_t>(2000,10000)(rng) : 100; }
                        rw = (int64_t)(rw * get_boost_multiplier(user_id, "money"));
                        total += rw;
                    }
                    db->update_wallet(user_id, total);
                    std::string desc = t->emoji + " **used " + std::to_string(use_count) + "x " + t->name + "!**\n\n💰 total: **$" + economy::format_number(total) + "**";
                    event.reply(dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_SUCCESS)));
                    log_entry(db, user_id, "USE", "bulk used " + std::to_string(use_count) + "x " + t->name, total, db->get_wallet(user_id));
                }
                return;
            }
        },
        // ── slash options ──
        {
            dpp::command_option(dpp::co_string, "item", "the item to use", true),
            dpp::command_option(dpp::co_integer, "amount", "number of items to use (max 10)", false)
        }
    );
}

// `boosts` — view your active boosts
inline Command* create_boosts_command(Database* db) {
    return new Command("boosts", "view your active boosts", "economy", {"boost", "activeboosts", "myboosts"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            auto active = get_user_boosts(user_id);

            if (active.empty()) {
                auto embed = bronx::info("you have no active boosts\n\nuse `use <boost>` to activate a boost from your inventory");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string desc = "⚡ **Active Boosts**\n\n";
            for (const auto& [type, info] : active) {
                auto [mult, remaining] = info;
                std::string emoji = (type == "xp") ? "⚡" : (type == "luck") ? "🍀" : "💸";
                desc += emoji + " **" + type + " boost** — " + std::to_string(mult).substr(0, 4) + "x • " + format_duration(remaining) + " remaining\n";
            }

            auto embed = bronx::create_embed(desc, bronx::COLOR_INFO);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            auto active = get_user_boosts(user_id);

            if (active.empty()) {
                event.reply(dpp::message().add_embed(bronx::info("you have no active boosts\n\nuse `/use <boost>` to activate one")));
                return;
            }

            std::string desc = "⚡ **Active Boosts**\n\n";
            for (const auto& [type, info] : active) {
                auto [mult, remaining] = info;
                std::string emoji = (type == "xp") ? "⚡" : (type == "luck") ? "🍀" : "💸";
                desc += emoji + " **" + type + " boost** — " + std::to_string(mult).substr(0, 4) + "x • " + format_duration(remaining) + " remaining\n";
            }

            event.reply(dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_INFO)));
        },
        {}
    );
}

// `lootboxes` — view all lootbox types and what the user owns
inline Command* create_lootboxes_command(Database* db) {
    return new Command("lootboxes", "view available lootbox types", "economy", {"lootbox", "boxes", "crates"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);

            std::string desc = "📦 **Lootboxes**\n\n";
            for (const auto& lb : get_lootbox_catalog()) {
                int owned = db->get_item_quantity(user_id, lb.item_id);
                desc += lb.emoji + " **" + lb.name + "**";
                if (owned > 0) desc += " — *owned: " + std::to_string(owned) + "*";
                desc += "\n*" + lb.description + "*\n";
                desc += "rewards: " + std::to_string(lb.min_rolls) + "-" + std::to_string(lb.max_rolls) + " items";
                if (lb.price > 0) desc += " • price: $" + economy::format_number(lb.price);
                else desc += " • *drop only*";
                desc += "\n\n";
            }
            desc += "use `use <lootbox>` to open • `buy <lootbox>` to purchase";

            auto embed = bronx::create_embed(desc, bronx::COLOR_INFO);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);

            std::string desc = "📦 **Lootboxes**\n\n";
            for (const auto& lb : get_lootbox_catalog()) {
                int owned = db->get_item_quantity(user_id, lb.item_id);
                desc += lb.emoji + " **" + lb.name + "**";
                if (owned > 0) desc += " — *owned: " + std::to_string(owned) + "*";
                desc += "\n*" + lb.description + "*\n";
                desc += "rewards: " + std::to_string(lb.min_rolls) + "-" + std::to_string(lb.max_rolls) + " items";
                if (lb.price > 0) desc += " • price: $" + economy::format_number(lb.price);
                else desc += " • *drop only*";
                desc += "\n\n";
            }
            desc += "use `/use <lootbox>` to open • `/buy <lootbox>` to purchase";
            event.reply(dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_INFO)));
        },
        {}
    );
}

// ============================================================================
// COMMAND REGISTRATION
// ============================================================================

inline std::vector<Command*> get_use_commands(Database* db) {
    static std::vector<Command*> cmds;
    static bool initialized = false;
    if (!initialized) {
        cmds.push_back(create_use_command(db));
        cmds.push_back(create_boosts_command(db));
        cmds.push_back(create_lootboxes_command(db));
        initialized = true;
    }
    return cmds;
}

} // namespace use_item
} // namespace commands
