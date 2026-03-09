#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace commands {
namespace crafting {

// ============================================================================
// CRAFTING RECIPE DEFINITIONS
// ============================================================================

// An ingredient needed for a recipe
struct Ingredient {
    std::string item_id;        // inventory item_id or special keyword
    std::string item_type;      // "collectible", "rod", "bait", "pickaxe", "coins", etc.
    std::string display_name;   // human-readable name
    std::string emoji;          // display emoji
    int quantity;               // how many needed
};

// What a recipe produces
struct CraftOutput {
    std::string item_id;        // inventory item_id for the result
    std::string item_type;      // "crafted", "rod", "bait", "pickaxe", "boosts", "tools", "potion"
    std::string name;           // display name
    std::string emoji;          // display emoji
    std::string description;    // what it does
    int quantity;               // how many produced
    int level;                  // item level (for equipment)
    std::string metadata;       // JSON metadata for the item
};

// A crafting recipe
struct Recipe {
    std::string id;             // unique recipe ID
    std::string name;           // display name
    std::string emoji;          // visual emoji
    std::string description;    // what this recipe does
    std::string category;       // "fishing", "mining", "utility", "prestige"
    std::vector<Ingredient> ingredients;
    CraftOutput output;
    int prestige_required;      // minimum prestige level (0 = no requirement)
};

// ============================================================================
// RECIPE CATALOG
// ============================================================================

inline const std::vector<Recipe>& get_recipes() {
    static const std::vector<Recipe> recipes = {
        // ====================================================================
        // FISHING RECIPES
        // ====================================================================
        {
            "bait_refinery", "Bait Refinery", "\xF0\x9F\xA7\xAA",
            "Compress 50 common bait into 5 rare bait",
            "fishing",
            {
                {"bait_common", "bait", "Common Bait", "\xF0\x9F\xAA\xB1", 50}
            },
            {"bait_rare", "bait", "Rare Bait", "\xF0\x9F\xAA\xB1", "Refined high-quality bait", 5, 3, R"({"name":"Rare Bait","description":"Crafted from 50 common bait"})"},
            0
        },
        {
            "bait_distillery", "Bait Distillery", "\xE2\x9A\x97\xEF\xB8\x8F",
            "Distill 25 rare bait into 3 epic bait",
            "fishing",
            {
                {"bait_rare", "bait", "Rare Bait", "\xF0\x9F\xAA\xB1", 25}
            },
            {"bait_epic", "bait", "Epic Bait", "\xF0\x9F\xAA\xB1", "Highly concentrated bait", 3, 4, R"({"name":"Epic Bait","description":"Distilled from rare bait"})"},
            0
        },
        {
            "golden_rod", "Gilded Rod", "\xF0\x9F\x8C\x9F",
            "A cosmetic rod with +5% fish value — forged from gold ore and a gold rod",
            "fishing",
            {
                {"gold_ore", "collectible", "Gold Ore", "\xF0\x9F\xAA\x99", 10},
                {"diamond_ore", "collectible", "Diamond Ore", "\xF0\x9F\x92\x8E", 5},
                {"rod_gold", "rod", "Gold Rod", "\xF0\x9F\x8E\xA3", 1}
            },
            {"rod_gilded", "rod", "Gilded Rod", "\xF0\x9F\x8C\x9F", "+5% fish value, forged in gold", 1, 5, R"({"name":"Gilded Rod","luck":0.12,"capacity":35,"fish_value_bonus":0.05,"description":"A rod forged from gold and diamond"})"},
            0
        },
        {
            "philosophers_bait", "Philosopher's Bait", "\xF0\x9F\x94\xAE",
            "Guaranteed epic+ fish for 5 casts — crafted from legendary materials",
            "fishing",
            {
                {"philosophers_stone", "collectible", "Philosopher's Stone", "\xF0\x9F\x94\xAE", 1},
                {"bait_legendary", "bait", "Legendary Bait", "\xE2\x9C\xA8", 1}
            },
            {"bait_philosopher", "bait", "Philosopher's Bait", "\xF0\x9F\x94\xAE", "Guarantees epic+ fish for 5 casts", 1, 5, R"({"name":"Philosopher's Bait","description":"Guarantees epic+ fish","uses_remaining":5,"epic_guarantee":true})"},
            2
        },

        // ====================================================================
        // MINING RECIPES
        // ====================================================================
        {
            "void_pickaxe", "Void Pickaxe", "\xF0\x9F\x95\xB3\xEF\xB8\x8F",
            "Prestige-tier mining tool — tears through reality itself",
            "mining",
            {
                {"void_crystal", "collectible", "Void Crystal", "\xF0\x9F\x95\xB3\xEF\xB8\x8F", 3},
                {"celestial_ore", "collectible", "Celestial Ore", "\xF0\x9F\x8C\x8C", 2},
                {"pickaxe_diamond", "pickaxe", "Diamond Pickaxe", "\xE2\x9B\x8F\xEF\xB8\x8F", 1}
            },
            {"pickaxe_void", "pickaxe", "Void Pickaxe", "\xF0\x9F\x95\xB3\xEF\xB8\x8F", "Prestige-tier pickaxe that mines through dimensions", 1, 7, R"({"name":"Void Pickaxe","description":"Tears through reality","tier":"prestige"})"},
            3
        },
        {
            "meteor_pickaxe", "Meteor Pickaxe", "\xE2\x98\x84\xEF\xB8\x8F",
            "Temporary 2x ore value for 24h — forged from meteor fragments",
            "mining",
            {
                {"meteor_fragment", "collectible", "Meteor Fragment", "\xE2\x98\x84\xEF\xB8\x8F", 10}
            },
            {"pickaxe_meteor", "pickaxe", "Meteor Pickaxe", "\xE2\x98\x84\xEF\xB8\x8F", "2x ore value for 24 hours", 1, 4, R"({"name":"Meteor Pickaxe","description":"2x ore value","duration_hours":24,"ore_value_mult":2.0})"},
            0
        },

        // ====================================================================
        // UTILITY / CROSS-SKILL RECIPES
        // ====================================================================
        {
            "lucky_charm", "Lucky Charm", "\xF0\x9F\x8D\x80",
            "+20% luck for 2 hours — stronger than shop boosts",
            "utility",
            {
                {"gold_ore", "collectible", "Gold Ore", "\xF0\x9F\xAA\x99", 5},
                {"rare_fish", "collectible", "Rare Fish", "\xF0\x9F\x90\xA0", 3},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 50000}
            },
            {"lucky_charm", "boosts", "Lucky Charm", "\xF0\x9F\x8D\x80", "+20% luck for 2 hours", 1, 1, R"({"name":"Lucky Charm","description":"+20% luck for 2h","duration_hours":2,"luck_bonus":0.20,"type":"boost"})"},
            0
        },
        {
            "treasure_compass", "Treasure Compass", "\xF0\x9F\xA7\xAD",
            "Guaranteed $50K-$500K find — follow the compass to buried treasure",
            "utility",
            {
                {"treasure_map", "collectible", "Treasure Map", "\xF0\x9F\x97\xBA\xEF\xB8\x8F", 1},
                {"metal_detector", "tools", "Metal Detector", "\xF0\x9F\x94\x8D", 1},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 500}
            },
            {"treasure_compass", "tools", "Treasure Compass", "\xF0\x9F\xA7\xAD", "Find buried treasure worth $50K-$500K", 1, 1, R"({"name":"Treasure Compass","description":"Follow it to find treasure","min_value":50000,"max_value":500000})"},
            0
        },
        {
            "auto_sell_net", "Auto-Sell Net", "\xF0\x9F\xAA\xA4",
            "Next 50 fish caught are automatically sold at 110% value",
            "utility",
            {
                {"common_fish", "collectible", "Common Fish", "\xF0\x9F\x90\x9F", 20},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 25000}
            },
            {"auto_sell_net", "tools", "Auto-Sell Net", "\xF0\x9F\xAA\xA4", "Auto-sells next 50 fish at 110% value", 1, 1, R"({"name":"Auto-Sell Net","description":"Auto-sell 50 fish at 110%","uses_remaining":50,"sell_bonus":0.10})"},
            0
        },
        {
            "drill_bit", "Drill Bit", "\xF0\x9F\x94\xA9",
            "Next 20 mine commands yield double ore quantity",
            "utility",
            {
                {"iron_ore", "collectible", "Iron Ore", "\xE2\x9A\x99\xEF\xB8\x8F", 15},
                {"coal_ore", "collectible", "Coal", "\xE2\xAC\x9B", 30},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 10000}
            },
            {"drill_bit", "tools", "Drill Bit", "\xF0\x9F\x94\xA9", "Double ore for 20 mine commands", 1, 1, R"({"name":"Drill Bit","description":"Double ore quantity","uses_remaining":20,"ore_quantity_mult":2.0})"},
            0
        },
        {
            "insurance_policy", "Insurance Policy", "\xF0\x9F\x9B\xA1\xEF\xB8\x8F",
            "Protects against rob losses for 24 hours",
            "utility",
            {
                {"gold_ore", "collectible", "Gold Ore", "\xF0\x9F\xAA\x99", 3},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 100000}
            },
            {"insurance_policy", "boosts", "Insurance Policy", "\xF0\x9F\x9B\xA1\xEF\xB8\x8F", "Rob protection for 24h", 1, 1, R"({"name":"Insurance Policy","description":"Protects from robbery","duration_hours":24,"rob_protection":true})"},
            0
        },

        // ====================================================================
        // LOOTBOX FUSION
        // ====================================================================
        {
            "lootbox_fusion_uncommon", "Lootbox Fusion", "\xF0\x9F\x93\xA6",
            "Fuse 5 Common Lootboxes into 1 Uncommon Lootbox",
            "utility",
            {
                {"lootbox_common", "other", "Common Lootbox", "\xF0\x9F\x93\xA6", 5}
            },
            {"lootbox_uncommon", "other", "Uncommon Lootbox", "\xF0\x9F\x93\xA6", "An uncommon lootbox from fusion", 1, 1, R"({"name":"Uncommon Lootbox","tier":"uncommon"})"},
            0
        },
        {
            "lootbox_fusion_rare", "Rare Lootbox Fusion", "\xF0\x9F\x93\xA6",
            "Fuse 5 Uncommon Lootboxes into 1 Rare Lootbox",
            "utility",
            {
                {"lootbox_uncommon", "other", "Uncommon Lootbox", "\xF0\x9F\x93\xA6", 5}
            },
            {"lootbox_rare", "other", "Rare Lootbox", "\xF0\x9F\x93\xA6", "A rare lootbox from fusion", 1, 1, R"({"name":"Rare Lootbox","tier":"rare"})"},
            0
        },

        // ====================================================================
        // PRESTIGE RECIPES
        // ====================================================================
        {
            "enchantment_scroll_luck", "Luck Enchantment Scroll", "\xF0\x9F\x93\x9C",
            "Apply to rod or pickaxe for permanent +3% luck",
            "prestige",
            {
                {"diamond_ore", "collectible", "Diamond Ore", "\xF0\x9F\x92\x8E", 10},
                {"legendary_fish", "collectible", "Legendary Fish", "\xF0\x9F\x90\x89", 2},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 500000}
            },
            {"enchantment_luck", "crafted", "Luck Enchantment Scroll", "\xF0\x9F\x93\x9C", "+3% permanent luck on equipment", 1, 1, R"({"name":"Luck Enchantment Scroll","description":"+3% luck","enchant_type":"luck","enchant_value":0.03})"},
            5
        },
        {
            "enchantment_scroll_value", "Value Enchantment Scroll", "\xF0\x9F\x93\x9C",
            "Apply to rod or pickaxe for permanent +5% sell value",
            "prestige",
            {
                {"ruby_ore", "collectible", "Ruby", "\xF0\x9F\x94\xB4", 5},
                {"sapphire_ore", "collectible", "Sapphire", "\xF0\x9F\x94\xB5", 5},
                {"coins", "coins", "Coins", "\xF0\x9F\xAA\x99", 500000}
            },
            {"enchantment_value", "crafted", "Value Enchantment Scroll", "\xF0\x9F\x93\x9C", "+5% permanent sell value on equipment", 1, 1, R"({"name":"Value Enchantment Scroll","description":"+5% sell value","enchant_type":"value","enchant_value":0.05})"},
            5
        },
        {
            "combo_token_exchange", "Combo Token Exchange", "\xF0\x9F\x8E\x9F\xEF\xB8\x8F",
            "Exchange 10 combo tokens for a random boost",
            "utility",
            {
                {"combo_token", "other", "Combo Token", "\xF0\x9F\x8E\x9F\xEF\xB8\x8F", 10}
            },
            {"random_boost", "boosts", "Random Boost", "\xF0\x9F\x8E\xB2", "A randomly selected boost item", 1, 1, R"({"name":"Random Boost","description":"Random boost from combo tokens"})"},
            0
        }
    };
    return recipes;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Find a recipe by ID
inline const Recipe* find_recipe(const std::string& recipe_id) {
    for (const auto& recipe : get_recipes()) {
        if (recipe.id == recipe_id) return &recipe;
    }
    return nullptr;
}

// Find recipes by category
inline std::vector<const Recipe*> get_recipes_by_category(const std::string& category) {
    std::vector<const Recipe*> result;
    for (const auto& recipe : get_recipes()) {
        if (recipe.category == category) result.push_back(&recipe);
    }
    return result;
}

// Search recipes by name (case-insensitive partial match)
inline std::vector<const Recipe*> search_recipes(const std::string& query) {
    std::vector<const Recipe*> result;
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
    
    for (const auto& recipe : get_recipes()) {
        std::string lower_name = recipe.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        std::string lower_id = recipe.id;
        std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), ::tolower);
        
        if (lower_name.find(lower_query) != std::string::npos || 
            lower_id.find(lower_query) != std::string::npos) {
            result.push_back(&recipe);
        }
    }
    return result;
}

// Check if a collectible item_id matches a recipe ingredient by name
// Fish are stored with unique IDs (Uxxxxxx) and ore with (Mxxxxxx),
// so we need to match by the "name" field in their metadata JSON
inline bool matches_ingredient_by_name(const std::string& metadata, const std::string& ingredient_name) {
    // Quick JSON parse for "name":"<value>"
    auto pos = metadata.find("\"name\"");
    if (pos == std::string::npos) return false;
    
    pos = metadata.find("\"", pos + 6);
    if (pos == std::string::npos) return false;
    pos++; // skip opening quote
    
    auto end = metadata.find("\"", pos);
    if (end == std::string::npos) return false;
    
    std::string name = metadata.substr(pos, end - pos);
    
    // Case-insensitive compare
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    std::string lower_ingredient = ingredient_name;
    std::transform(lower_ingredient.begin(), lower_ingredient.end(), lower_ingredient.begin(), ::tolower);
    
    return lower_name == lower_ingredient;
}

// Get a display-friendly category name
inline std::string get_category_display(const std::string& category) {
    if (category == "fishing") return "\xF0\x9F\x8E\xA3 Fishing";
    if (category == "mining") return "\xE2\x9B\x8F\xEF\xB8\x8F Mining";
    if (category == "utility") return "\xF0\x9F\x94\xA7 Utility";
    if (category == "prestige") return "\xE2\xAD\x90 Prestige";
    return category;
}

// Get all unique categories
inline std::vector<std::string> get_recipe_categories() {
    std::vector<std::string> cats;
    std::map<std::string, bool> seen;
    for (const auto& recipe : get_recipes()) {
        if (!seen[recipe.category]) {
            cats.push_back(recipe.category);
            seen[recipe.category] = true;
        }
    }
    return cats;
}

} // namespace crafting
} // namespace commands
