#pragma once
// Lootbox catalog definitions — shared between use.h and shop.h
// This header has NO economy/fishing/mining dependencies so it's safe to
// include from any header without triggering circular includes.

#include <string>
#include <vector>

namespace commands {
namespace use_item {

// ============================================================================
// LOOTBOX DEFINITIONS
// ============================================================================

enum class LootboxTier {
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary,
    Prestige
};

// A single possible reward from a lootbox
struct LootReward {
    std::string type;           // "money", "fish", "ore", "bait", "boost", "item"
    std::string item_id;        // specific item ID (for fish/ore/bait/item rewards)
    std::string display_name;   // friendly display name
    std::string emoji;          // emoji for display
    int64_t min_amount;         // min money or quantity
    int64_t max_amount;         // max money or quantity
    double weight;              // drop weight (higher = more common)
};

// A lootbox definition
struct LootboxDef {
    std::string item_id;        // inventory item ID (e.g. "lootbox_common")
    std::string name;           // display name
    std::string emoji;          // emoji
    std::string description;    // shop description
    LootboxTier tier;
    int64_t price;              // shop price (0 = not purchasable, drop only)
    int min_rolls;              // minimum number of rewards
    int max_rolls;              // maximum number of rewards
    std::vector<LootReward> reward_pool;
};

// ── reward pool builders ─────────────────────────────────────────────────────

inline std::vector<LootReward> build_common_rewards() {
    return {
        {"money", "", "Cash",        "💵", 100,    500,    30.0},
        {"money", "", "Cash",        "💵", 500,    1500,   20.0},
        {"money", "", "Cash Pile",   "💰", 1500,   3000,   10.0},
        {"bait", "bait_common",  "Common Bait",  "🪱", 1, 5,  25.0},
        {"bait", "bait_common",  "Common Bait",  "🪱", 5, 15, 12.0},
        {"fish", "common fish",   "Common Fish",    "🐟", 1, 1, 15.0},
        {"fish", "shrimp",        "Shrimp",         "🦐", 1, 1, 12.0},
        {"fish", "carp",          "Carp",           "<:carp:1476363585864339537>", 1, 1, 10.0},
        {"fish", "trout",         "Trout",          "<:trout:1476363836230602943>", 1, 1, 8.0},
        {"ore", "coal",          "Coal",           "⬛", 1, 1, 12.0},
        {"ore", "copper ore",    "Copper Ore",     "🟤", 1, 1, 10.0},
        {"ore", "iron ore",      "Iron Ore",       "🔩", 1, 1, 8.0},
        {"boost", "xp_boost_small", "XP Boost (15min)", "⚡", 1, 1, 5.0},
    };
}

inline std::vector<LootReward> build_uncommon_rewards() {
    return {
        {"money", "", "Cash",          "💵", 1000,   3000,   25.0},
        {"money", "", "Cash Stack",    "💰", 3000,   8000,   15.0},
        {"money", "", "Money Bag",     "💰", 8000,   15000,  8.0},
        {"bait", "bait_common",   "Common Bait",   "🪱", 10, 30, 18.0},
        {"bait", "bait_uncommon", "Uncommon Bait",  "🪱", 3,  10, 12.0},
        {"fish", "tuna",          "Tuna",           "🐟", 1, 1, 10.0},
        {"fish", "cod",           "Cod",            "<:cod:1476363292413923481>", 1, 1, 8.0},
        {"fish", "red snapper",   "Red Snapper",    "🐟", 1, 1, 6.0},
        {"fish", "pufferfish",    "Pufferfish",     "🐡", 1, 1, 5.0},
        {"ore", "silver ore",    "Silver Ore",     "🥈", 1, 1, 10.0},
        {"ore", "gold ore",      "Gold Ore",       "🥇", 1, 1, 8.0},
        {"ore", "lapis lazuli",  "Lapis Lazuli",   "💙", 1, 1, 5.0},
        {"boost", "xp_boost_small",  "XP Boost (15min)",   "⚡", 1, 1, 8.0},
        {"boost", "xp_boost_medium", "XP Boost (30min)",   "⚡", 1, 1, 4.0},
        {"boost", "luck_boost_small","Luck Boost (15min)", "🍀", 1, 1, 6.0},
        {"item", "lootbox_common", "Common Lootbox", "📦", 1, 1, 3.0},
    };
}

inline std::vector<LootReward> build_rare_rewards() {
    return {
        {"money", "", "Cash Stack",    "💰", 5000,   15000,  20.0},
        {"money", "", "Money Bag",     "💰", 15000,  40000,  12.0},
        {"money", "", "Treasury",      "🏦", 40000,  80000,  5.0},
        {"bait", "bait_uncommon", "Uncommon Bait",  "🪱", 10, 25, 15.0},
        {"bait", "bait_rare",    "Rare Bait",       "🪱", 3,  10, 10.0},
        {"fish", "squid",         "Squid",          "🦑", 1, 1, 8.0},
        {"fish", "octopus",       "Octopus",        "<a:octopus:1476364735594102945>", 1, 1, 6.0},
        {"fish", "jellyfish",     "Jellyfish",      "🪼", 1, 1, 7.0},
        {"ore", "ruby",          "Ruby",           "❤️", 1, 1, 7.0},
        {"ore", "sapphire",      "Sapphire",       "💙", 1, 1, 6.0},
        {"ore", "emerald",       "Emerald",        "💚", 1, 1, 5.0},
        {"boost", "xp_boost_medium",  "XP Boost (30min)",     "⚡", 1, 1, 8.0},
        {"boost", "xp_boost_large",   "XP Boost (1hr)",       "⚡", 1, 1, 4.0},
        {"boost", "luck_boost_medium","Luck Boost (30min)",   "🍀", 1, 1, 6.0},
        {"boost", "money_boost_small","Money Boost (15min)",  "💸", 1, 1, 5.0},
        {"item", "lootbox_uncommon", "Uncommon Lootbox", "📦", 1, 1, 3.0},
    };
}

inline std::vector<LootReward> build_epic_rewards() {
    return {
        {"money", "", "Money Bag",     "💰", 25000,   75000,  18.0},
        {"money", "", "Treasury",      "🏦", 75000,   200000, 10.0},
        {"money", "", "Vault",         "🏦", 200000,  500000, 4.0},
        {"bait", "bait_rare",    "Rare Bait",    "🪱", 3, 8,  12.0},
        {"bait", "bait_epic",    "Epic Bait",    "🪱", 1, 2,  8.0},
        {"ore", "diamond",       "Diamond",         "💎", 1, 1, 8.0},
        {"ore", "black opal",    "Black Opal",      "🖤", 1, 1, 5.0},
        {"ore", "mithril ore",   "Mithril Ore",     "🔮", 1, 1, 4.0},
        {"ore", "dragon stone",  "Dragon Stone",    "🐉", 1, 1, 3.0},
        {"boost", "xp_boost_large",    "XP Boost (1hr)",      "⚡", 1, 1, 8.0},
        {"boost", "luck_boost_large",  "Luck Boost (1hr)",    "🍀", 1, 1, 6.0},
        {"boost", "money_boost_medium","Money Boost (30min)",  "💸", 1, 1, 5.0},
        {"item", "lootbox_rare", "Rare Lootbox", "📦", 1, 1, 3.0},
    };
}

inline std::vector<LootReward> build_legendary_rewards() {
    return {
        {"money", "", "Treasury",       "🏦", 100000,  400000,  15.0},
        {"money", "", "Vault",          "🏦", 400000,  900000,  8.0},
        {"money", "", "Dragon Hoard",   "🐲", 900000,  2500000, 3.0},
        {"bait", "bait_epic",      "Epic Bait",      "🪱", 2, 5,  10.0},
        {"bait", "bait_legendary", "Legendary Bait",  "🪱", 1, 2,  6.0},
        {"ore", "void crystal",     "Void Crystal",      "🕳️", 1, 1, 6.0},
        {"ore", "unobtanium",       "Unobtanium",        "💫", 1, 1, 4.0},
        {"ore", "celestial ore",    "Celestial Ore",     "🌠", 1, 1, 3.0},
        {"ore", "world core shard", "World Core Shard",  "🌍", 1, 1, 2.0},
        {"boost", "xp_boost_large",     "XP Boost (1hr)",        "⚡", 1, 2, 8.0},
        {"boost", "luck_boost_large",   "Luck Boost (1hr)",      "🍀", 1, 2, 6.0},
        {"boost", "money_boost_large",  "Money Boost (1hr)",     "💸", 1, 1, 5.0},
        {"boost", "xp_boost_mega",      "Mega XP Boost (2hr)",   "🌟", 1, 1, 3.0},
        {"item", "lootbox_epic", "Epic Lootbox", "📦", 1, 1, 2.0},
    };
}

inline std::vector<LootReward> build_prestige_rewards() {
    return {
        {"money", "", "Vault",          "🏦", 500000,   2000000,  12.0},
        {"money", "", "Dragon Hoard",   "🐲", 2000000,  8000000,  6.0},
        {"money", "", "Infinite Vault",  "✨", 8000000,  20000000, 2.0},
        {"bait", "bait_legendary",  "Legendary Bait",  "🪱", 3, 7,  8.0},
        {"ore", "eternal ore",    "Eternal Ore",     "💫", 1, 1, 5.0},
        {"ore", "immortal gem",   "Immortal Gem",    "👼", 1, 1, 3.0},
        {"ore", "divine crystal", "Divine Crystal",  "🏨", 1, 1, 4.0},
        {"boost", "xp_boost_mega",       "Mega XP Boost (2hr)",    "🌟", 1, 2, 7.0},
        {"boost", "money_boost_large",   "Money Boost (1hr)",      "💸", 1, 2, 5.0},
        {"boost", "luck_boost_large",    "Luck Boost (1hr)",       "🍀", 1, 2, 4.0},
        {"item", "lootbox_legendary", "Legendary Lootbox", "📦", 1, 1, 2.0},
    };
}

// ── lootbox catalog ──────────────────────────────────────────────────────────

inline const std::vector<LootboxDef>& get_lootbox_catalog() {
    static std::vector<LootboxDef> catalog = {
        {
            "lootbox_common", "Common Lootbox", "📦",
            "a basic lootbox with common rewards",
            LootboxTier::Common, 500,
            1, 2, build_common_rewards()
        },
        {
            "lootbox_uncommon", "Uncommon Lootbox", "📫",
            "contains better rewards with a chance for boosts",
            LootboxTier::Uncommon, 2500,
            1, 3, build_uncommon_rewards()
        },
        {
            "lootbox_rare", "Rare Lootbox", "🎁",
            "packed with rare fish, ores, and generous money",
            LootboxTier::Rare, 15000,
            2, 3, build_rare_rewards()
        },
        {
            "lootbox_epic", "Epic Lootbox", "🎀",
            "epic-tier rewards including diamonds and big boosts",
            LootboxTier::Epic, 75000,
            2, 4, build_epic_rewards()
        },
        {
            "lootbox_legendary", "Legendary Lootbox", "👑",
            "the rarest rewards await — void crystals, mega boosts, fortunes",
            LootboxTier::Legendary, 500000,
            3, 5, build_legendary_rewards()
        },
        {
            "lootbox_prestige", "Prestige Lootbox", "🌟",
            "exclusive prestige rewards — requires prestige 1+",
            LootboxTier::Prestige, 0,  // Not purchasable, drop only
            3, 6, build_prestige_rewards()
        },
    };
    return catalog;
}

// Find lootbox definition by item_id
inline const LootboxDef* find_lootbox(const std::string& item_id) {
    for (const auto& lb : get_lootbox_catalog()) {
        if (lb.item_id == item_id) return &lb;
    }
    return nullptr;
}

} // namespace use_item
} // namespace commands
