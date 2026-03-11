#pragma once
#include <string>
#include <vector>
#include <random>
#include <ctime>

namespace commands {
namespace mining {

// ============================================================================
// ORE DEFINITIONS & HELPERS
// ============================================================================

// Ore rarity tiers (mirrors fishing)
enum class OreEffect {
    None,
    Flat,         // add fixed bonus based on luck
    Exponential,  // value squared by luck
    Logarithmic,  // multiply by log(luck)
    NLogN,        // n*log(n) style boost
    Wacky,        // random multiplier 1-5x
    Jackpot,      // high risk: 0.2x or 8x value (coin flip)
    Critical,     // 50% chance for 2x, otherwise normal
    Volatile,     // random multiplier between 0.3x and 4.0x
    Surge,        // flat bonus: gear_level^2 * 50
    Diminishing,  // better at low luck: val * (3.0 - luck/50.0), min 0.5x
    Cascading,    // stacking small multipliers: 1.15^(1-6 random)
    Wealthy,      // bonus based on wallet
    Banker,       // bonus based on bank
    Miner,        // multiplier based on ores mined
    Merchant,     // bonus based on ores sold
    Ascended,     // exponential prestige scaling
    Collector,    // bonus per unique ore types owned
    Persistent    // bonus based on total ores mined + sold
};

struct OreType {
    std::string name;
    std::string emoji;
    int weight;            // rarity (higher = more common)
    int64_t min_value;
    int64_t max_value;
    OreEffect effect;
    double effect_chance;
    int min_pickaxe_level; // minimum pickaxe level required (0 = none)
    int max_pickaxe_level; // level at which this ore stops appearing (0 = none)
    std::string description;
};

// Info about a single mined ore for the receipt
enum class OreBonusType { Normal = 0, DoubleCatch = 1, Multimine = 2, MultiOre = 3 };

struct MineInfo {
    OreType ore;
    int64_t value;
    bool hadEffect;
    OreEffect effectType;
    double probability;
    std::string item_id;   // unique DB ID
    bool sold = false;
    OreBonusType bonus_type = OreBonusType::Normal;
};

// ============================================================================
// ORE CATALOG  (weights: Common 150-250, Uncommon 60-120, Rare 20-50,
//               Epic 8-18, Legendary 1-6, Prestige 1-4)
// ============================================================================
static std::vector<OreType> ore_types = {
    // ─── Common tier (pickaxe level 0) ───
    {"stone",           "🪨", 250,  5,      20,    OreEffect::None,        0.0,  0, 10, "plain old rock"},
    {"coal",            "⬛", 220,  10,     35,    OreEffect::None,        0.03, 0, 10, "fuel for the furnace"},
    {"copper ore",      "🟤", 200,  15,     50,    OreEffect::Flat,        0.08, 0, 10, "ductile orange metal"},
    {"tin ore",         "⬜", 190,  12,     40,    OreEffect::None,        0.04, 0, 10, "soft silvery metal"},
    {"clay",            "🟫", 210,  8,      25,    OreEffect::None,        0.02, 0, 10, "good for pottery"},
    {"flint",           "🔘", 180,  18,     55,    OreEffect::Cascading,   0.06, 0, 10, "sharp when chipped"},
    {"sandstone",       "🟡", 195,  10,     30,    OreEffect::None,        0.03, 0, 10, "layered sedimentary rock"},
    {"quartz",          "💎", 160,  25,     70,    OreEffect::Flat,        0.09, 0, 10, "crystalline silica"},
    {"iron ore",        "🔩", 150,  30,     90,    OreEffect::Flat,        0.1,  0, 10, "backbone of civilization"},
    {"mica",            "✨", 175,  14,     42,    OreEffect::Cascading,   0.07, 0, 10, "shimmering mineral flakes"},
    {"salt crystal",    "🧊", 185,  12,     38,    OreEffect::None,        0.04, 0, 10, "crystallized NaCl"},
    {"limestone",       "⬜", 170,  20,     60,    OreEffect::None,        0.05, 0, 10, "ancient sea floors"},
    {"granite",         "🪨", 165,  22,     65,    OreEffect::Flat,        0.06, 0, 10, "tough igneous rock"},
    {"obsidian shard",  "🖤", 140,  35,     100,   OreEffect::Critical,    0.1,  0, 10, "volcanic glass fragment"},
    {"pyrite",          "🌟", 155,  28,     80,    OreEffect::Jackpot,     0.12, 0, 10, "fool's gold"},

    // ─── Uncommon tier (pickaxe level 1) ───
    {"silver ore",      "🥈", 95,   80,     250,   OreEffect::Flat,        0.1,  1, 10, "precious shiny metal"},
    {"gold ore",        "🥇", 75,   120,    380,   OreEffect::Logarithmic, 0.12, 1, 10, "the ultimate currency"},
    {"lead ore",        "⬛", 105,  60,     180,   OreEffect::None,        0.06, 1, 10, "dense heavy metal"},
    {"zinc ore",        "🔘", 100,  65,     200,   OreEffect::Cascading,   0.09, 1, 10, "galvanizing mineral"},
    {"nickel ore",      "⚪", 90,   90,     275,   OreEffect::NLogN,       0.1,  1, 10, "magnetic silvery metal"},
    {"cobalt ore",      "🔵", 80,   100,    320,   OreEffect::Flat,        0.11, 1, 10, "brilliant blue tint"},
    {"lapis lazuli",    "💙", 70,   130,    400,   OreEffect::NLogN,       0.12, 1, 0,  "deep blue gemstone"},
    {"topaz",           "🟡", 65,   150,    450,   OreEffect::Wacky,       0.13, 1, 0,  "golden brilliance"},
    {"garnet",          "🔴", 60,   160,    500,   OreEffect::Volatile,    0.14, 1, 0,  "deep crimson crystal"},
    {"amethyst",        "🟣", 55,   180,    550,   OreEffect::Exponential, 0.12, 1, 0,  "royal purple quartz"},
    {"jade",            "🟢", 68,   140,    420,   OreEffect::Logarithmic, 0.1,  1, 10, "eastern treasure stone"},
    {"marble slab",     "🏛️", 88,   95,     290,   OreEffect::Flat,        0.08, 1, 10, "smooth architectural stone"},

    // ─── Rare tier (pickaxe level 2) ───
    {"platinum ore",    "⚪", 42,   350,    900,   OreEffect::NLogN,       0.14, 2, 0,  "rarer than gold"},
    {"titanium ore",    "🛡️", 38,   400,    1000,  OreEffect::Logarithmic, 0.12, 2, 0,  "aerospace-grade metal"},
    {"ruby",            "❤️", 30,   500,    1400,  OreEffect::Critical,    0.16, 2, 0,  "blood-red gemstone"},
    {"sapphire",        "💙", 28,   550,    1500,  OreEffect::Wacky,       0.15, 2, 0,  "cornflower blue brilliance"},
    {"emerald",         "💚", 25,   600,    1800,  OreEffect::Exponential, 0.14, 2, 0,  "vivid green treasure"},
    {"opal",            "🌈", 32,   450,    1200,  OreEffect::Volatile,    0.17, 2, 0,  "plays with every color"},
    {"aquamarine",      "🩵", 35,   380,    1050,  OreEffect::Flat,        0.11, 2, 0,  "sea-blue crystal"},
    {"tungsten ore",    "⚙️", 40,   320,    850,   OreEffect::Surge,       0.13, 2, 0,  "incredibly dense metal"},
    {"meteorite shard", "☄️", 22,   650,    1900,  OreEffect::Jackpot,     0.2,  2, 0,  "fell from the sky"},
    {"ancient fossil",  "🦴", 36,   380,    1000,  OreEffect::Diminishing, 0.12, 2, 10, "millions of years old"},

    // ─── Epic tier (pickaxe level 3) ───
    {"diamond",         "💎", 15,   1200,   4000,  OreEffect::Critical,    0.2,  3, 0,  "hardest natural material"},
    {"alexandrite",     "💜", 12,   1500,   5000,  OreEffect::Wacky,       0.18, 3, 0,  "color-changing marvel"},
    {"black opal",      "🖤", 10,   1800,   6000,  OreEffect::Volatile,    0.2,  3, 0,  "rarest opal variety"},
    {"palladium ore",   "🪙", 14,   1300,   4500,  OreEffect::NLogN,       0.16, 3, 0,  "catalyst metal"},
    {"iridium ore",     "🌌", 8,    2000,   7000,  OreEffect::Exponential, 0.18, 3, 0,  "densest natural element"},
    {"mithril ore",     "🔮", 6,    2500,   8000,  OreEffect::Surge,       0.15, 3, 0,  "lighter than silk, harder than steel"},
    {"osmium ore",      "⚫", 9,    1900,   6500,  OreEffect::Logarithmic, 0.14, 3, 0,  "densest element known"},
    {"star sapphire",   "⭐", 7,    2200,   7500,  OreEffect::Cascading,   0.19, 3, 0,  "displays a six-rayed star"},
    {"dragon stone",    "🐉", 5,    3000,   10000, OreEffect::Jackpot,     0.22, 3, 0,  "formed in volcanic hearts"},

    // ─── Legendary tier (pickaxe level 4-5) ───
    {"philosopher's stone","🔴", 3, 5000,   15000, OreEffect::Exponential, 0.25, 4, 0,  "transmutes base metals"},
    {"void crystal",    "🕳️", 2,   8000,   25000, OreEffect::Wacky,       0.22, 4, 0,  "contains nothingness"},
    {"unobtanium",      "💫", 2,   10000,  30000, OreEffect::Jackpot,     0.28, 4, 0,  "theoretically impossible"},
    {"celestial ore",   "🌠", 1,   15000,  50000, OreEffect::Cascading,   0.3,  4, 0,  "fallen from the heavens"},
    {"adamantite",      "🛡️", 2,   12000,  35000, OreEffect::NLogN,       0.24, 4, 0,  "indestructible alloy"},
    {"world core shard","🌍", 1,   20000,  60000, OreEffect::Surge,       0.26, 5, 0,  "fragment of earth's heart"},
    {"stardust ore",    "✨", 1,   25000,  80000, OreEffect::Exponential, 0.3,  5, 0,  "cosmic particles condensed"},
    {"eternity gem",    "♾️", 1,   30000,  100000,OreEffect::Wacky,       0.35, 5, 0,  "time frozen in crystal"},

    // ─── Prestige tier ores (P1-P10) ───
    // P1 (pickaxe level 7)
    {"molten core",     "🔥", 4,   15000,  50000,  OreEffect::Exponential, 0.2,  7, 0,  "birthed in a prestige furnace"},
    {"phoenix ember",   "🕊️", 3,   18000,  60000,  OreEffect::Wacky,       0.22, 7, 0,  "ashes of rebirth"},
    {"infernal ruby",   "♦️", 5,   16000,  55000,  OreEffect::NLogN,       0.21, 7, 0,  "forged in the reset fire"},
    // P2 (level 8)
    {"shadow ore",      "🌑", 3,   30000,  100000, OreEffect::Wacky,       0.25, 8, 0,  "absorbs all light"},
    {"void shard",      "⬛", 2,   35000,  120000, OreEffect::Exponential, 0.27, 8, 0,  "fragment of the abyss"},
    {"dark matter",     "🔮", 4,   32000,  110000, OreEffect::NLogN,       0.26, 8, 0,  "mysterious cosmic substance"},
    // P3 (level 9)
    {"nebula crystal",  "🌌", 3,   60000,  200000, OreEffect::Miner,       0.25, 9, 0,  "stardust compressed"},
    {"astral shard",    "✨", 2,   70000,  240000, OreEffect::Collector,   0.26, 9, 0,  "galactic energy condensed"},
    {"cosmic topaz",    "🌈", 4,   65000,  220000, OreEffect::Wealthy,     0.24, 9, 0,  "shimmers with starlight"},
    // P4 (level 10)
    {"stellar diamond", "🌠", 3,   120000, 400000, OreEffect::Exponential, 0.3,  10, 0, "harder than spacetime"},
    {"gravity gem",     "⚫", 2,   140000, 480000, OreEffect::NLogN,       0.32, 10, 0, "bends light around itself"},
    {"solar topaz",     "☀️", 4,   130000, 440000, OreEffect::Wacky,       0.31, 10, 0, "contains a tiny sun"},
    // P5 (level 11)
    {"eternal ore",     "💫", 2,   250000, 1000000, OreEffect::Ascended,   0.4,  11, 0, "transcends time itself"},
    {"immortal gem",    "👼", 1,   300000, 1200000, OreEffect::Persistent, 0.38, 11, 0, "blessed with endless life"},
    {"divine crystal",  "🏨", 3,   275000, 1100000, OreEffect::Banker,     0.36, 11, 0, "forged by the gods"},
    // P6 (level 12)
    {"primordial ore",  "🌊", 3,   500000,  2500000, OreEffect::Exponential, 0.25, 12, 0, "predates the universe"},
    {"genesis stone",   "🐋", 1,   750000,  3000000, OreEffect::NLogN,       0.22, 12, 0, "first mineral ever formed"},
    {"origin crystal",  "🐍", 2,   600000,  2800000, OreEffect::Wacky,       0.27, 12, 0, "seed of all gemstones"},
    // P7 (level 13)
    {"mana ore",        "🔮", 3,   1000000, 5000000, OreEffect::Wacky,       0.28, 13, 0, "pure magical energy"},
    {"arcane crystal",  "⚗️", 1,   1500000, 6000000, OreEffect::Exponential, 0.25, 13, 0, "woven from ancient spells"},
    {"spell stone",     "📜", 2,   1200000, 5500000, OreEffect::NLogN,       0.26, 13, 0, "enchanted mineral"},
    // P8 (level 14)
    {"quantum ore",     "📦", 2,   2000000, 10000000, OreEffect::Volatile,   0.4,  14, 0, "exists in superposition"},
    {"entangled gem",   "🔗", 1,   2500000, 12000000, OreEffect::Cascading,  0.35, 14, 0, "two crystals linked"},
    {"superposition",   "👻", 3,   2200000, 11000000, OreEffect::Jackpot,    0.38, 14, 0, "both here and not here"},
    // P9 (level 15)
    {"time ore",        "⏰", 3,   5000000, 25000000, OreEffect::NLogN,      0.3,  15, 0, "frozen temporal energy"},
    {"paradox gem",     "🔄", 1,   6000000, 30000000, OreEffect::Wacky,      0.32, 15, 0, "defies causality"},
    {"temporal shard",  "⚙️", 2,   5500000, 28000000, OreEffect::Exponential,0.31, 15, 0, "warps spacetime"},
    // P10 (level 16)
    {"infinity ore",    "♾️", 2,   10000000,50000000,  OreEffect::Ascended,   0.5,  16, 0, "beyond all boundaries"},
    {"omega crystal",   "🔱",  1,   12000000,60000000,  OreEffect::Persistent, 0.48, 16, 0, "the final mineral"},
    {"limit breaker gem","🚀",3,   11000000,55000000,  OreEffect::Collector,  0.46, 16, 0, "shatters constraints"},
};

// Generate unique ore item ID (prefixed M to distinguish from fish U)
inline std::string generate_ore_id() {
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string id = "M";
    for (int i = 0; i < 6; i++) id += charset[dis(gen)];
    return id;
}

// Determine ore rarity string from type
inline std::string get_ore_rarity(const std::string& ore_name) {
    for (const auto& o : ore_types) {
        if (o.name == ore_name) {
            if (o.min_pickaxe_level >= 7) return "legendary";
            if (o.min_pickaxe_level >= 5) return "legendary";
            if (o.min_pickaxe_level == 4) return "legendary";
            if (o.min_pickaxe_level == 3) return "epic";
            if (o.min_pickaxe_level == 2) return "rare";
            if (o.max_pickaxe_level == 0) {
                if (o.weight <= 3) return "legendary";
                if (o.weight <= 8) return "epic";
                if (o.weight <= 15) return "rare";
            }
            return "normal";
        }
    }
    return "normal";
}

// Simple JSON helpers (same pattern as fishing)
inline int parse_mine_meta_int(const std::string& meta, const std::string& key, int def = 0) {
    size_t pos = meta.find("\"" + key + "\":");
    if (pos == std::string::npos) return def;
    pos = meta.find_first_of("0123456789-", pos + key.size() + 3);
    if (pos == std::string::npos) return def;
    size_t end = pos;
    while (end < meta.size() && (std::isdigit(meta[end]) || meta[end] == '-')) end++;
    try { return std::stoi(meta.substr(pos, end - pos)); } catch (...) {}
    return def;
}

inline double parse_mine_meta_double(const std::string& meta, const std::string& key, double def = 0.0) {
    size_t pos = meta.find("\"" + key + "\":");
    if (pos == std::string::npos) return def;
    pos = meta.find_first_of("0123456789.-", pos + key.size() + 3);
    if (pos == std::string::npos) return def;
    size_t end = pos;
    while (end < meta.size() && (std::isdigit(meta[end]) || meta[end] == '.' || meta[end] == '-')) end++;
    try { return std::stod(meta.substr(pos, end - pos)); } catch (...) {}
    return def;
}

inline std::vector<std::string> parse_mine_meta_array(const std::string& meta, const std::string& key) {
    std::vector<std::string> out;
    size_t pos = meta.find("\"" + key + "\":");
    if (pos == std::string::npos) return out;
    pos = meta.find('[', pos);
    if (pos == std::string::npos) return out;
    size_t end = meta.find(']', pos + 1);
    if (end == std::string::npos) return out;
    std::string arr = meta.substr(pos + 1, end - pos - 1);
    size_t i = 0;
    while (i < arr.size()) {
        size_t start = arr.find('"', i);
        if (start == std::string::npos) break;
        size_t finish = arr.find('"', start + 1);
        if (finish == std::string::npos) break;
        out.push_back(arr.substr(start + 1, finish - start - 1));
        i = finish + 1;
    }
    return out;
}

// Parse spawn_rates from minecart metadata: {"spawn_rates":{"ore_name":weight,...}}
// Returns a map of ore_name -> extra_weight (added to default pool weights)
inline std::map<std::string, int> parse_spawn_rates(const std::string& meta) {
    std::map<std::string, int> rates;
    size_t pos = meta.find("\"spawn_rates\":");
    if (pos == std::string::npos) return rates;
    pos = meta.find('{', pos + 14);
    if (pos == std::string::npos) return rates;
    size_t end = meta.find('}', pos + 1);
    if (end == std::string::npos) return rates;
    std::string obj = meta.substr(pos + 1, end - pos - 1);
    // Parse "key":value pairs
    size_t i = 0;
    while (i < obj.size()) {
        size_t ks = obj.find('"', i);
        if (ks == std::string::npos) break;
        size_t ke = obj.find('"', ks + 1);
        if (ke == std::string::npos) break;
        std::string key = obj.substr(ks + 1, ke - ks - 1);
        size_t vs = obj.find_first_of("0123456789-", ke + 1);
        if (vs == std::string::npos) break;
        size_t ve = vs;
        while (ve < obj.size() && (std::isdigit(obj[ve]) || obj[ve] == '-')) ve++;
        try { rates[key] = std::stoi(obj.substr(vs, ve - vs)); } catch (...) {}
        i = ve;
    }
    return rates;
}

} // namespace mining
} // namespace commands
