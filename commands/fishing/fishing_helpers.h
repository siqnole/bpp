#pragma once
#include <string>
#include <vector>
#include <random>
#include <ctime>

namespace commands {
namespace fishing {

// Season enum for seasonal fish
enum class Season {
    AllYear,    // Available year-round
    Spring,     // March, April, May
    Summer,     // June, July, August
    Fall,       // September, October, November
    Winter      // December, January, February
};

// Fish types with rarity and value
enum class FishEffect {
    None,
    Flat,       // add fixed bonus based on luck
    Exponential,// value squared by luck
    Logarithmic,// multiply by log(luck)
    NLogN,      // n*log(n) style boost
    Wacky,      // random multiplier 1-5x
    Jackpot,    // high risk: 0.2x or 8x value (coin flip)
    Critical,   // 50% chance for 2x, otherwise normal
    Volatile,   // random multiplier between 0.3x and 4.0x
    Surge,      // flat bonus: gear_level^2 * 50
    Diminishing,// better at low luck: val * (3.0 - luck/50.0), min 0.5x
    Cascading,  // stacking small multipliers: 1.15^(1-6 random)
    // Stat-based effects (scale with user progress)
    Wealthy,    // bonus based on wallet: val * (1 + sqrt(wallet)/1000)
    Banker,     // bonus based on bank: val * (1 + log10(bank+1)/5)
    Fisher,     // multiplier based on fish caught: val * (1 + fish_caught/50000)
    Merchant,   // bonus based on fish sold: val + fish_sold/100
    Gambler,    // risk based on gambling: win_rate > 50% = 2x, else 0.5x
    Ascended,   // exponential prestige scaling: val * (1.5 ^ prestige)
    Underdog,   // inverse wealth: better when poor, val * (2 - wallet/10000000), min 0.5x
    HotStreak,  // based on recent gambling wins: val * (1 + wins/(wins+losses+1))
    Collector,  // bonus per unique fish types owned: val + unique_fish * 100
    Persistent  // bonus based on total fish caught + sold: val * log2(total+2)
};

struct FishType {
    ::std::string name;
    ::std::string emoji;
    int weight; // for rarity (higher = more common)
    int64_t min_value;
    int64_t max_value;
    FishEffect effect;
    double effect_chance; // additional trigger probability
    int min_gear_level;   // minimum rod/bait level required (0 = none)
    int max_gear_level;   // gear level at/above which this fish is impossible (0 = none)
    ::std::string description; // brief flavour text or habitat note
    Season season;        // season when this fish appears (AllYear by default)
};

// information about a particular fish catch used by the receipt paginator
struct CatchInfo {
    FishType fish;
    int64_t value;
    bool hadEffect;
    FishEffect effectType;
    bool isBonus;
    int64_t effect_delta;
    double effect_mult;
    double probability;
    // unique ID corresponding to the database collectible item
    std::string item_id;
    // whether this fish has been sold via quicksell
    bool sold = false;
};

static ::std::vector<FishType> fish_types = {
    // name,emoji,weight,min_val,max_val,effect,chance,min_gear,max_gear,description,season
    // Weights scaled for better probability differentiation (higher = more common)
    // Common tier: 150-250, Uncommon: 60-120, Rare: 20-50, Epic: 8-18, Legendary: 1-6
    {"common fish",     "🐟", 250, 10,    50,   FishEffect::None,        0.0, 0, 10, "a plain, everyday fish", Season::AllYear},
    {"shrimp",          "🦐", 220, 5,     20,   FishEffect::Cascading,   0.08,0, 10, "tiny and plentiful", Season::AllYear},
    {"minnow",          "<:minnow:1476363504926720175>", 200, 5,     15,   FishEffect::None,        0.05,0, 10, "small bait fish", Season::AllYear},
    {"carp",            "<:carp:1476363585864339537>", 180, 40,    100,  FishEffect::Flat,        0.1, 0, 10, "warty pond dweller", Season::AllYear},
    {"goldfish",        "🐠", 160, 20,    80,   FishEffect::None,        0.02,0, 10, "your escaped pet", Season::AllYear},
    {"trout",           "<:trout:1476363836230602943>", 140, 50,    120,  FishEffect::Flat,        0.1, 0, 10, "freshwater favorite", Season::AllYear},
    {"salmon",          "<:salmon:1476363745524580383>", 130, 50,    150,  FishEffect::Flat,        0.1, 0, 10, "popular grilled dish", Season::AllYear},
    {"guppy",           "<:guppy:1476365519979282564>", 190, 8,     25,   FishEffect::None,        0.03,0, 10, "tiny colorful breeder", Season::AllYear},
    {"dace",            "🐟", 175, 15,    45,   FishEffect::None,        0.04,0, 10, "swift stream swimmer", Season::AllYear},
    
    // Creative non-fish creatures - common tier
    {"tadpole",         "🐸", 185, 12,    35,   FishEffect::None,        0.03,0, 10, "future frog", Season::AllYear},
    {"newt",            "🦎", 165, 18,    50,   FishEffect::None,        0.04,0, 10, "small amphibian", Season::AllYear},
    {"water bug",       "🪲", 180, 10,    30,   FishEffect::Cascading,   0.06,0, 10, "skitters on surface", Season::AllYear},
    {"pond snail",      "🐌", 195, 8,     22,   FishEffect::None,        0.02,0, 10, "slow spiral shell", Season::AllYear},
    {"clam",            "🦪", 170, 25,    65,   FishEffect::Flat,        0.08,0, 10, "pearl producer", Season::AllYear},
    {"mussel",          "🦀", 160, 20,    55,   FishEffect::None,        0.05,0, 10, "filter feeding bivalve", Season::AllYear},
    
    // Additional common bait fish (levels 1-2)
    {"bass",             "🐟", 145, 45,    110,  FishEffect::Critical,    0.12, 0, 10, "aggressive freshwater hunter", Season::AllYear},
    {"perch",            "🐠", 155, 30,    90,   FishEffect::None,        0.06, 0, 10, "spiny-finned lake dweller", Season::AllYear},
    {"bluegill",         "🐟", 170, 25,    75,   FishEffect::None,        0.04, 0, 10, "colorful panfish", Season::AllYear},
    {"catfish",          "🐟", 90, 60,    200,  FishEffect::Logarithmic, 0.1, 0, 10, "whiskered bottom feeder", Season::AllYear},
    {"roach",            "🐟", 150, 20,    60,   FishEffect::None,        0.05, 0, 10, "common pond fish", Season::AllYear},
    {"blowfish",            "<:blowfish:1476365762271907900>", 135, 35,    85,   FishEffect::Flat,        0.07, 0, 10, "deep-bodied freshwater fish", Season::AllYear},
    
    // More creative common creatures
    {"frog",             "🐸", 140, 22,    68,   FishEffect::None,        0.05, 0, 10, "ribbit ribbit", Season::AllYear},
    {"turtle",           "🐢", 110, 35,    90,   FishEffect::Flat,        0.08, 0, 10, "slow and steady", Season::AllYear},
    {"crawfish",         "🦞", 155, 18,    52,   FishEffect::Cascading,   0.09, 0, 10, "mini lobster", Season::AllYear},
    {"starfish",         "⭐", 125, 28,    72,   FishEffect::None,        0.06, 0, 10, "five-pointed sea creature", Season::AllYear},
    {"sea urchin",       "🦠", 105, 32,    78,   FishEffect::Volatile,    0.11, 0, 10, "spiky sphere", Season::AllYear},
    {"barnacle",         "🪪", 148, 12,    38,   FishEffect::None,        0.03, 0, 10, "crusty clingers", Season::AllYear},
    {"leech",            "🪱", 120, 15,    42,   FishEffect::Flat,        0.07, 0, 10, "bloodsucking worm", Season::AllYear},
    {"waterfowl feather","🪶", 175, 10,    35,   FishEffect::None,        0.04, 0, 10, "duck lost this", Season::AllYear},
    
    // Uncommon tier fish (level 2)
    {"tuna",             "🐟", 75, 100,   350,  FishEffect::Flat,        0.12, 1, 10, "fast ocean swimmer", Season::AllYear},
    {"mackerel",         "🐠", 95, 80,    250,  FishEffect::Logarithmic, 0.1, 1, 10, "streamlined schooling fish", Season::AllYear},
    {"flounder",         "🐟", 85, 90,    280,  FishEffect::Flat,        0.08, 1, 10, "flat bottom-dwelling fish", Season::AllYear},
    {"cod",              "<:cod:1476363292413923481>", 68, 120,   400,  FishEffect::NLogN,       0.1, 1, 10, "versatile white fish", Season::AllYear},
    {"sea bass",         "🐟", 72, 110,   360,  FishEffect::Logarithmic, 0.09, 1, 10, "prized ocean member", Season::AllYear},
    {"halibut",          "<:halibut:1476363303860310169>", 62, 140,   420,  FishEffect::NLogN,       0.11, 1, 10, "flat white fish treasure", Season::AllYear},
    {"anchovy",          "🐟", 105, 60,    180,  FishEffect::Cascading,   0.1, 1, 10, "tiny silver schooler", Season::AllYear},
    {"sardine",          "🐟", 100, 55,    165,  FishEffect::Cascading,   0.1, 1, 10, "packed in tight groups", Season::AllYear},
    {"herring",          "🐠", 90, 75,    230,  FishEffect::Cascading,   0.12, 1, 10, "north atlantic staple", Season::AllYear},
    {"red snapper",      "🐟", 55, 150,   450,  FishEffect::NLogN,       0.1,  1, 10, "prized reef dweller", Season::AllYear},
    {"wahoo",            "🐠", 48, 160,   500,  FishEffect::Exponential, 0.11, 1, 0, "speed demon of the sea", Season::AllYear},
    {"mahi mahi",        "🐟", 42, 180,   550,  FishEffect::Wacky,       0.12, 1, 0, "colorful dolphinfish", Season::AllYear},

    // More creative uncommon creatures
    {"sea slug",         "🐚", 88, 95,    275,  FishEffect::Flat,        0.09, 1, 10, "slimy and colorful", Season::AllYear},
    {"sea cucumber",     "🥒", 78, 110,   320,  FishEffect::Logarithmic, 0.08, 1, 10, "lumpy sea sausage", Season::AllYear},
    {"conch",            "🐚", 65, 130,   380,  FishEffect::NLogN,       0.1,  1, 10, "spiral shell beauty", Season::AllYear},
    {"sea anemone",      "🌺", 70, 115,   340,  FishEffect::Volatile,    0.12, 1, 10, "flower of the sea", Season::AllYear},
    {"coral piece",      "🪸", 82, 100,   295,  FishEffect::Flat,        0.07, 1, 10, "reef building block", Season::AllYear},
    {"sea sponge",       "🧽", 92, 85,    245,  FishEffect::Cascading,   0.09, 1, 10, "absorbent sea dweller", Season::AllYear},
    {"nautilus",         "🐚", 55, 155,   450,  FishEffect::NLogN,       0.11, 1, 0, "living fossil spiral", Season::AllYear},

    {"clownfish",       "<:clownfish:1476755744098488352>", 110, 70,    180,  FishEffect::None,        0.05,0, 10, "reef colored entertainer", Season::AllYear},
    {"tropical fish",   "🐡", 95, 100,   300,  FishEffect::Logarithmic, 0.1, 0, 10, "bright reef dweller", Season::AllYear},
    {"seahorse",        "🐴", 78, 60,    150,  FishEffect::Diminishing, 0.15,0, 10, "delicate equine swimmer", Season::AllYear},
    {"eel",             "🐍", 65, 80,    250,  FishEffect::NLogN,       0.1, 0, 10, "slippery electric", Season::AllYear},
    {"pufferfish",      "🐡", 50, 150,   400,  FishEffect::Jackpot,     0.2,0, 0, "inflates when startled", Season::AllYear},
    {"jellyfish",       "🪼", 38, 200,   500,  FishEffect::Volatile,    0.15, 0, 0, "gelatinous stinger", Season::AllYear},
    {"crab",            "🦀", 45, 30,    90,   FishEffect::Flat,        0.1, 0, 10, "hard shell crustacean", Season::AllYear},
    {"squid",           "🦑", 28, 300,    700,  FishEffect::Exponential, 0.1, 0, 0, "ink-squirting cephalopod", Season::AllYear},
    {"octopus",         "<a:octopus:1476364735594102945>", 22, 200,    500,  FishEffect::Flat,        0.1, 0, 0, "eight-armed clever predator", Season::AllYear},
    {"lionfish",        "🦁", 35, 120,   350,  FishEffect::Volatile,    0.15,0, 0, "venomous reef striped", Season::AllYear},
    {"moray eel",       "🐍", 30, 180,   450,  FishEffect::Exponential, 0.13,0, 0, "lair-dwelling aggressive", Season::AllYear},
    
    // More creative ocean creatures
    {"sea dragon",      "🐉", 32, 165,   420,  FishEffect::Wacky,       0.14, 0, 0, "leafy sea wonder", Season::AllYear},
    {"narwhal",         "🦄", 25, 240,   580,  FishEffect::Critical,    0.16, 0, 0, "unicorn of the sea", Season::AllYear},
    {"manatee",         "<:manatee:1476755312240234720>", 28, 210,   540,  FishEffect::Flat,        0.12, 0, 0, "gentle sea cow", Season::AllYear},
    {"sea lion",        "🦭", 35, 175,   460,  FishEffect::Logarithmic, 0.13, 0, 0, "playful circus performer", Season::AllYear},
    {"walrus",          "<:walrus:1476755481669144728>", 20, 255,   610,  FishEffect::NLogN,       0.15, 0, 0, "tusked arctic giant", Season::AllYear},
    {"penguin",         "🐧", 42, 140,   380,  FishEffect::Cascading,   0.11, 0, 10, "flightless tuxedo bird", Season::AllYear},
    {"cormorant",       "🦅", 48, 120,   340,  FishEffect::Flat,        0.09, 0, 10, "diving fishing bird", Season::AllYear},
    
    // Rare tier fish (level 3)
    {"barracuda",        "🐟", 24, 400,    900,  FishEffect::Critical,    0.18, 2, 0, "razor-toothed predator", Season::AllYear},
    {"swordfish",        "🗡️", 16, 600,   1400,  FishEffect::NLogN,       0.12, 2, 0, "blade-nosed speedster", Season::AllYear},
    {"marlin",           "🐠", 12, 800,   1800,  FishEffect::Logarithmic, 0.1,  2, 0, "sport fishing legend", Season::AllYear},
    {"stingray",         "🐟", 20, 350,    850,  FishEffect::Volatile,    0.2, 2, 0, "venomous flat swimmer", Season::AllYear},
    {"grouper",          "🐠", 18, 450,   1000,  FishEffect::Flat,        0.14, 2, 0, "large reef ambusher", Season::AllYear},
    {"snapper",          "🐟", 26, 380,    850,  FishEffect::Logarithmic, 0.11, 2, 0, "colorful deep-water hunter", Season::AllYear},

    {"giant squid",     "🦑", 8, 1000,   4000,  FishEffect::Exponential, 0.1, 3, 0, "colossal cephalopod", Season::AllYear},
    {"anglerfish",      "🎣", 9, 800,    3000,  FishEffect::Logarithmic, 0.1, 3, 0, "lurking lure predator", Season::AllYear},
    {"lobster",         "🦞", 15, 500,   1200,  FishEffect::Diminishing, 0.12,0, 0, "expensive shellfish", Season::AllYear},
    {"shark",           "🦈", 12, 500,   1000,  FishEffect::Critical,    0.15, 0, 0, "apex predator of the deep", Season::AllYear},
    {"manta ray",       "🐋", 7, 800,   2000,  FishEffect::Surge,       0.12, 0, 0, "giant flat glider", Season::AllYear},
    {"hammerhead shark", "🐟", 8, 550,   1100,  FishEffect::NLogN,       0.12, 3, 0, "unusual headed predator", Season::AllYear},
    {"scorpion",    "<:scorpion:1476374544548888788>", 10, 700,   1600,  FishEffect::Exponential, 0.14, 3, 0, "venomous dweller", Season::AllYear},
    
    // Creative rare creatures
    {"beluga whale",    "🐋", 8, 850,   2200,  FishEffect::Surge,       0.13, 3, 0, "white arctic whale", Season::AllYear},
    {"electric eel",    "⚡", 10, 720,   1750,  FishEffect::Volatile,    0.16, 3, 0, "shocking predator", Season::AllYear},
    {"giant clam",      "🦪", 12, 550,   1300,  FishEffect::Flat,        0.11, 0, 0, "massive pearl maker", Season::AllYear},
    {"sushi roll",      "🍣", 22, 420,   950,   FishEffect::Critical,    0.17, 2, 0, "ready to eat", Season::AllYear},
    {"treasure chest",  "💰", 6, 1200,  4500,  FishEffect::Jackpot,     0.25, 3, 0, "sunken pirate loot", Season::AllYear},
    {"message bottle",  "🍾", 18, 480,   1050,  FishEffect::Wacky,       0.12, 2, 0, "contains secrets", Season::AllYear},
    {"old boot",        "🥾", 28, 200,   650,   FishEffect::None,        0.05, 2, 10, "someone's lost shoe", Season::AllYear},
    {"anchor",          "⚓", 14, 580,   1350,  FishEffect::Flat,        0.09, 3, 0, "heavy nautical equipment", Season::AllYear},
    
    // Epic tier fish (level 4)
    {"great white shark", "🦈", 6, 1500,  5000,  FishEffect::Critical,    0.22,  3, 0, "ultimate ocean predator", Season::AllYear},
    {"giant octopus",    "🐙", 4, 2000,  6000,  FishEffect::Wacky,       0.15, 3, 0, "massive intelligent cephalopod", Season::AllYear},
    {"colossal squid",   "🦑", 3, 2500,  7500,  FishEffect::NLogN,       0.18, 3, 0, "deep sea titan", Season::AllYear},
    {"blue whale",       "🐋", 2, 3000,  9000,  FishEffect::Surge,       0.15, 3, 0, "largest creature on earth", Season::AllYear},
    {"tiger shark",      "🦈", 4, 1800,  5500,  FishEffect::Volatile,    0.18, 3, 0, "striped ocean terror", Season::AllYear},
    {"sperm whale",      "🐋", 3, 2800,  8500,  FishEffect::Surge,       0.16, 3, 0, "deep diving behemoth", Season::AllYear},

    {"whale",           "<a:whale:1476364400725332018>", 5, 1000,  3000,  FishEffect::Surge,       0.12, 0, 0, "massive ocean mammal", Season::AllYear},
    {"dolphin",         "<:dolphin:1476364733887287429>", 5, 1000,  3000,  FishEffect::Cascading,   0.15, 0, 0, "playful marine mammal", Season::AllYear},
    {"kraken",          "<:kraken:1476364260316676228>", 1, 5000, 15000,  FishEffect::Jackpot,     0.25, 0, 0, "legendary sea monster", Season::AllYear},
    {"orca",            "<:orca:1476364040572899602>", 4, 1500,  4500,  FishEffect::Critical,    0.16, 0, 0, "killer whale apex hunter", Season::AllYear},
    {"giant manta",     "<:manta:1476364150296023120>", 3, 2200,  6500,  FishEffect::Surge,       0.14, 0, 0, "massive graceful glider", Season::AllYear},
    
    // Creative epic creatures
    {"megalodon",       "🦈", 3, 2400,  7200,  FishEffect::Exponential, 0.19, 3, 0, "prehistoric giant shark", Season::AllYear},
    {"giant seahorse",  "🐴", 5, 1700,  5200,  FishEffect::Wacky,       0.14, 3, 0, "majestic equine leviathan", Season::AllYear},
    {"shipwreck",       "🚢", 4, 2100,  6300,  FishEffect::Jackpot,     0.23, 3, 0, "sunken vessel full of treasure", Season::AllYear},
    {"mermaid",         "🧜", 2, 3500, 10500,  FishEffect::Volatile,    0.2,  4, 0, "enchanting sea maiden", Season::AllYear},
    {"triton",          "🔱", 2, 3200,  9600,  FishEffect::Critical,    0.18, 4, 0, "mighty sea god's warrior", Season::AllYear},
    {"sea serpent egg", "🥚", 5, 1900,  5700,  FishEffect::Wacky,       0.16, 3, 0, "about to hatch", Season::AllYear},
    
    // Legendary tier fish (level 5)
    {"leviathan",        "🐍", 2, 8000, 25000,  FishEffect::Exponential, 0.25, 4, 0, "ancient sea serpent", Season::AllYear},
    {"sea serpent",      "🐉", 1, 10000,30000,  FishEffect::Wacky,       0.2,  4, 0, "mythical ocean dragon", Season::AllYear},
    {"seal",             "<a:seal:1476365078235320370>", 3, 4000, 12000,  FishEffect::Diminishing, 0.18,  4, 0, "hes such a good boy :3", Season::AllYear},
    {"ancient turtle",   "🐢", 2, 6000, 20000,  FishEffect::Diminishing, 0.2, 4, 0, "wise elder of the depths", Season::AllYear},
    
    {"abyssal leviathan","🐙", 2, 10000,25000, FishEffect::Exponential, 0.25, 4, 0, "deep‑sea behemoth", Season::AllYear},
    {"celestial kraken","🪐", 1, 50000,200000, FishEffect::Wacky,      0.3, 5, 0, "cosmic tentacled colossus", Season::AllYear},
    {"golden fish",     "✨", 2, 5000, 10000,  FishEffect::Jackpot,     0.3, 0, 0, "shimmering rare fish", Season::AllYear},
    {"legendary fish",  "🐉", 1, 20000,100000, FishEffect::Jackpot,     0.2, 0, 0, "ancient mythical catch", Season::AllYear},
    {"titanic beast",   "🌊", 2, 9000, 28000,  FishEffect::Surge,       0.25, 4, 0, "sunken island carrier", Season::AllYear},
    {"cosmic leviathan","⭐", 1, 15000, 50000, FishEffect::Cascading,   0.3, 4, 0, "woven from starlight", Season::AllYear},
    
    // ============ SEASONAL FISH ============
    // SPRING FISH (March, April, May) - Blooming, renewal, fresh themes
    {"cherry blossom koi", "🌸", 95, 120,   360,  FishEffect::Flat,        0.12, 1, 10, "adorned with petals", Season::Spring},
    {"spring salmon",      "🐟", 85, 140,   420,  FishEffect::Logarithmic, 0.14, 1, 10, "migrating upstream", Season::Spring},
    {"butterfly fish",     "🦋", 70, 180,   540,  FishEffect::Cascading,   0.15, 2, 0, "flutters through coral", Season::Spring},
    {"lily pad frog",      "🐸", 110, 95,    285,  FishEffect::None,        0.08, 0, 10, "croaks in the rain", Season::Spring},
    {"rainbow trout",      "🌈", 65, 220,   660,  FishEffect::Wacky,       0.16, 2, 0, "painted by spring rain", Season::Spring},
    {"tadpole swarm",      "🐸", 130, 75,    225,  FishEffect::Cascading,   0.11, 0, 10, "wriggling mass", Season::Spring},
    {"spring turtle",      "🐢", 55, 280,   840,  FishEffect::Flat,        0.13, 2, 0, "emerging from hibernation", Season::Spring},
    {"garden eel",         "🌱", 45, 350,  1050,  FishEffect::NLogN,       0.15, 2, 0, "sprouts from sand", Season::Spring},
    {"sakura serpent",     "🌸", 15, 850,  2550,  FishEffect::Exponential, 0.18, 3, 0, "legendary spring guardian", Season::Spring},
    {"metamorphosis frog", "🎭", 8, 1400,  4200,  FishEffect::Volatile,    0.2,  3, 0, "mid-transformation", Season::Spring},
    {"bloom leviathan",    "🌺", 3, 3500, 10500,  FishEffect::Wacky,       0.22, 4, 0, "flowered sea titan", Season::Spring},
    {"verdant whale",      "🌿", 2, 5000, 15000,  FishEffect::Surge,       0.24, 4, 0, "covered in seaweed gardens", Season::Spring},
    
    // SUMMER FISH (June, July, August) - Tropical, hot, vibrant themes
    {"sunfish",            "☀️", 100, 110,   330,  FishEffect::Flat,        0.11, 1, 10, "basking in rays", Season::Summer},
    {"tropical parrotfish","🦜", 80, 150,   450,  FishEffect::Logarithmic, 0.13, 1, 10, "colorful coral muncher", Season::Summer},
    {"summer flounder",    "🏖️", 75, 170,   510,  FishEffect::Flat,        0.12, 2, 0, "buried in warm sand", Season::Summer},
    {"fire coral crab",    "🔥", 60, 240,   720,  FishEffect::Volatile,    0.17, 2, 0, "burning bright", Season::Summer},
    {"beach ball puffer",  "⚽", 90, 130,   390,  FishEffect::Jackpot,     0.19, 1, 10, "playful vacation find", Season::Summer},
    {"lava eel",           "🌋", 40, 380,  1140,  FishEffect::Exponential, 0.16, 2, 0, "volcanic underwater river", Season::Summer},
    {"firefly squid",      "✨", 50, 320,   960,  FishEffect::Cascading,   0.15, 2, 0, "bioluminescent wonder", Season::Summer},
    {"heat ray",           "☀️", 20, 750,  2250,  FishEffect::Critical,    0.18, 3, 0, "radiates warmth", Season::Summer},
    {"magma shark",        "🦈", 12, 1200,  3600,  FishEffect::Exponential, 0.2,  3, 0, "swims in lava vents", Season::Summer},
    {"solar whale",        "🌞", 5, 2800,  8400,  FishEffect::Surge,       0.21, 3, 0, "solar powered giant", Season::Summer},
    {"phoenix ray",        "🔥", 3, 4500, 13500,  FishEffect::Volatile,    0.24, 4, 0, "reborn from ocean flames", Season::Summer},
    {"supernova fish",     "💥", 1, 8000, 24000,  FishEffect::Jackpot,     0.28, 4, 0, "explosive summer star", Season::Summer},
    
    // FALL FISH (September, October, November) - Harvest, changing, cozy themes
    {"autumn bass",        "🍂", 105, 100,   300,  FishEffect::None,        0.1,  1, 10, "rust-colored scales", Season::Fall},
    {"harvest carp",       "🌾", 88, 135,   405,  FishEffect::Flat,        0.12, 1, 10, "plump from feeding", Season::Fall},
    {"maple leaf fish",    "🍁", 72, 175,   525,  FishEffect::Cascading,   0.14, 2, 0, "floating gently", Season::Fall},
    {"pumpkin puffer",     "🎃", 65, 215,   645,  FishEffect::Jackpot,     0.18, 2, 0, "spooky halloween catch", Season::Fall},
    {"turkey fish",        "🦃", 55, 265,   795,  FishEffect::Wacky,       0.15, 2, 0, "thanksgiving special", Season::Fall},
    {"acorn crab",         "🌰", 80, 155,   465,  FishEffect::Flat,        0.11, 1, 10, "storing for winter", Season::Fall},
    {"misty eel",          "🌫️", 35, 420,  1260,  FishEffect::Logarithmic, 0.16, 2, 0, "shrouded in fog", Season::Fall},
    {"twilight octopus",   "🌅", 18, 820,  2460,  FishEffect::NLogN,       0.17, 3, 0, "dusk-colored tentacles", Season::Fall},
    {"scarecrow ray",      "👻", 10, 1350,  4050,  FishEffect::Volatile,    0.19, 3, 0, "haunted harvest guardian", Season::Fall},
    {"autumn leviathan",   "🍂", 4, 3200,  9600,  FishEffect::Exponential, 0.22, 4, 0, "draped in falling leaves", Season::Fall},
    {"harvest moon whale", "🌕", 2, 5500, 16500,  FishEffect::Surge,       0.25, 4, 0, "glows with moon's blessing", Season::Fall},
    {"november kraken",    "🦑", 1, 9000, 27000,  FishEffect::Wacky,       0.27, 4, 0, "herald of winter's approach", Season::Fall},
    
    // WINTER FISH (December, January, February) - Cold, ice, snow themes
    {"ice fish",           "❄️", 110, 95,    285,  FishEffect::None,        0.09, 1, 10, "frozen solid", Season::Winter},
    {"snowflake eel",      "❄️", 82, 145,   435,  FishEffect::Flat,        0.13, 1, 10, "unique crystal pattern", Season::Winter},
    {"arctic char",        "🎿", 75, 165,   495,  FishEffect::Logarithmic, 0.11, 2, 0, "cold water specialist", Season::Winter},
    {"frost puffer",       "⛄", 68, 205,   615,  FishEffect::Jackpot,     0.16, 2, 0, "frozen bubble fish", Season::Winter},
    {"penguin fish",       "🐧", 90, 125,   375,  FishEffect::Cascading,   0.12, 1, 10, "tuxedo swimmer", Season::Winter},
    {"polar crab",         "🦀", 60, 250,   750,  FishEffect::Critical,    0.15, 2, 0, "emperor of ice", Season::Winter},
    {"blizzard shark",     "🦈", 25, 680,  2040,  FishEffect::Volatile,    0.18, 3, 0, "swims through snowstorms", Season::Winter},
    {"ice dragon",         "🐉", 15, 950,  2850,  FishEffect::Exponential, 0.17, 3, 0, "frozen ancient wyrm", Season::Winter},
    {"glacier whale",      "🗻", 8, 1600,  4800,  FishEffect::Surge,       0.19, 3, 0, "living iceberg", Season::Winter},
    {"aurora borealis ray","🌌", 5, 2500,  7500,  FishEffect::Wacky,       0.21, 3, 0, "painted by northern lights", Season::Winter},
    {"frostbite leviathan","🧊", 3, 4000, 12000,  FishEffect::Critical,    0.23, 4, 0, "eternal winter guardian", Season::Winter},
    {"absolute zero fish", "🌡️", 1, 7000, 21000,  FishEffect::Jackpot,     0.26, 4, 0, "coldest creature alive", Season::Winter},
    {"christmas kraken",   "🎄", 2, 5000, 15000,  FishEffect::Wacky,       0.25, 4, 0, "festive tentacled gift-giver", Season::Winter},
    
    // Math-themed fish (Infinity Rod + Pi Bait)
    {"rational fish",   "🔢", 8, 3141,  6283,   FishEffect::Logarithmic, 0.15, 6, 0, "perfectly organized numbers", Season::AllYear},
    {"irrational fish", "∞",  3, 5000, 15708,  FishEffect::Exponential, 0.2,  6, 0, "endless decimal places", Season::AllYear},
    {"rooted fish",     "√",  7, 2718,  8154,   FishEffect::NLogN,       0.12, 6, 0, "square root of existence", Season::AllYear},
    {"exponential fish","e",  2, 7389,  22026,  FishEffect::Exponential, 0.25, 6, 0, "grows without bounds", Season::AllYear},
    {"imaginary fish",  "i",  1, 10000, 31416,  FishEffect::Volatile,    0.18,  6, 0, "exists only in complex space", Season::AllYear},
    {"prime fish",      "\\#",  12, 1618,  5089,   FishEffect::Flat,        0.08, 6, 0, "indivisible by nature", Season::AllYear},
    {"fibonacci fish",  "φ",  6, 1123,  4181,   FishEffect::NLogN,       0.1,  6, 0, "golden ratio swimmer", Season::AllYear},
    {"logarithm trout", "📈", 5, 4000, 12000,  FishEffect::Logarithmic, 0.14, 6, 0, "scales exponentially", Season::AllYear},
    {"complex number",  "𝐂",  2, 6000, 18000,  FishEffect::Wacky,       0.16, 6, 0, "multi-dimensional fish", Season::AllYear},
    
    // Dev/Programming-themed fish (Dev Rod + Segmentation Fault)
    {"null pointer",    "⚠️", 18, 500,   2048,   FishEffect::Jackpot,     0.35,  6, 0, "points to nothing", Season::AllYear},
    {"stack overflow",  "📚", 10, 1024,  4096,   FishEffect::Exponential, 0.15, 6, 0, "recursion gone wrong", Season::AllYear},
    {"memory leak",     "🕳️", 14, 512,   1536,   FishEffect::Flat,        0.2,  6, 0, "slowly draining resources", Season::AllYear},
    {"race condition",  "⚡", 6, 2048,  8192,   FishEffect::Volatile,    0.22, 6, 0, "timing is everything", Season::AllYear},
    {"buffer overflow", "💥", 5, 3072,  9216,   FishEffect::Exponential, 0.12, 6, 0, "exceeded all bounds", Season::AllYear},
    {"deadlock",        "🔒", 2, 4096, 12288,   FishEffect::NLogN,       0.1,  6, 0, "forever waiting", Season::AllYear},
    {"segfault",        "💀", 1, 8192, 24576,   FishEffect::Jackpot,     0.3, 6, 0, "core dumped chaos", Season::AllYear},
    {"infinite loop",   "🔁", 10, 1536,  6144,   FishEffect::Cascading,   0.25, 6, 0, "never-ending repetition", Season::AllYear},
    {"garbage collect", "🗑️", 12, 2048,  7200,   FishEffect::Logarithmic, 0.17, 6, 0, "automatically disposed", Season::AllYear},
    
    // Shrek-themed fish (Shrek Rod + Swamp Water)
    {"donkey",          "🫏", 25, 800,   2400,   FishEffect::Cascading,   0.25,  6, 0, "annoying but lovable sidekick", Season::AllYear},
    {"fiona",           "👸", 10, 2000,  6000,   FishEffect::Flat,        0.12, 6, 0, "beautiful princess of the swamp", Season::AllYear},
    {"puss in boots",   "😼", 12, 1500,  4500,   FishEffect::Critical,    0.18, 6, 0, "swashbuckling feline", Season::AllYear},
    {"lord farquaad",   "🤴", 5, 3000,  9000,   FishEffect::Logarithmic, 0.08, 6, 0, "compensating with height", Season::AllYear},
    {"dragon",          "🐉", 4, 4000, 12000,   FishEffect::Exponential, 0.1,  6, 0, "fierce guardian of the tower", Season::AllYear},
    {"gingerbread man", "🍪", 18, 600,   1800,   FishEffect::Flat,        0.18, 6, 0, "run run as fast as you can", Season::AllYear},
    {"shrek",           "🧌", 1, 5000, 15000,   FishEffect::Wacky,       0.3,  6, 0, "better out than in, I always say", Season::AllYear},
    {"pinocchio",       "🪵", 9, 1200,  3600,   FishEffect::NLogN,       0.13, 6, 0, "growing wooden friend", Season::AllYear},
    {"big bad wolf",    "🐺", 6, 2500,  7500,   FishEffect::Exponential, 0.14, 6, 0, "hungry fairy tale villain", Season::AllYear},
    
    // Prestige-themed fish (Prestige rods + baits, levels 7-26)
    // P1 - Ascended Rod + Ascended Bait (level 7)
    {"phoenix fish",    "🔥", 4, 15000, 50000,  FishEffect::Exponential, 0.2,  7, 0, "rises from the ashes of reset", Season::AllYear},
    {"firebird",        "🕊️", 3, 18000, 60000,  FishEffect::Wacky,       0.22, 7, 0, "eternal flames manifest", Season::AllYear},
    {"ember carp",      "🌋", 5, 16000, 55000,  FishEffect::NLogN,       0.21, 7, 0, "glows with inner fire", Season::AllYear},
    {"inferno koi",     "🔥", 6, 12000, 45000,  FishEffect::Wacky,       0.18, 7, 0, "born in volcanic springs", Season::AllYear},
    {"ash marlin",      "⬛", 5, 14000, 48000,  FishEffect::Surge,       0.19, 7, 0, "coated in ancient ash", Season::AllYear},
    {"molten eel",      "🌋", 3, 17000, 58000,  FishEffect::Critical,    0.22, 7, 0, "swims through liquid magma", Season::AllYear},
    {"cinder whale",    "🐋", 2, 20000, 65000,  FishEffect::Volatile,    0.24, 7, 0, "smoldering ocean giant", Season::AllYear},
    
    // P2 - Transcendent Rod + Transcendent Bait (level 8)
    {"void fish",       "<a:void:1476372297286946886>", 3, 30000, 100000, FishEffect::Wacky,       0.25, 8, 0, "swims through the emptiness", Season::AllYear},
    {"abyssal",    "⬛", 2, 35000, 120000, FishEffect::Exponential, 0.27, 8, 0, "given shape", Season::AllYear},
    {"shadow bass",     "🌑", 4, 32000, 110000, FishEffect::NLogN,       0.26, 8, 0, "consumes all light", Season::AllYear},
    {"wraith pike",     "👻", 5, 28000, 95000,  FishEffect::Volatile,    0.23, 8, 0, "phases through solid matter", Season::AllYear},
    {"umbral tuna",     "🌑", 4, 32000, 105000, FishEffect::Cascading,   0.24, 8, 0, "darker than the abyss", Season::AllYear},
    {"dark tide bass",  "🖤", 3, 36000, 115000, FishEffect::Critical,    0.26, 8, 0, "rides the black wave", Season::AllYear},
    {"nightmare angler","😱", 2, 40000, 130000, FishEffect::Jackpot,     0.28, 8, 0, "haunts the deep trenches", Season::AllYear},
    
    // P3 - Ethereal Rod + Ethereal Bait (level 9)
    {"nebula fish",     "🌌", 3, 60000, 200000, FishEffect::Fisher,       0.25,  9, 0, "born of stardust and dreams", Season::AllYear},
    {"stardust sprite", "✨", 2, 70000, 240000, FishEffect::Collector,    0.26, 9, 0, "galactic dust coalesced", Season::AllYear},
    {"aurora trout",    "🌈", 4, 65000, 220000, FishEffect::Wealthy,      0.24, 9, 0, "shimmers with northern lights", Season::AllYear},
    {"spectral cod",    "👻", 5, 55000, 190000, FishEffect::Logarithmic, 0.23, 9, 0, "visible only in moonlight", Season::AllYear},
    {"cosmic ray",      "☄️", 4, 62000, 215000, FishEffect::Surge,       0.25, 9, 0, "fell from outer space", Season::AllYear},
    {"plasma fish",     "⚡", 3, 68000, 230000, FishEffect::Volatile,    0.27, 9, 0, "fourth state of matter", Season::AllYear},
    {"galaxy grouper",  "🌌", 2, 75000, 250000, FishEffect::NLogN,       0.29, 9, 0, "contains entire star systems", Season::AllYear},
    
    // P4 - Celestial Rod + Celestial Bait (level 10)
    {"cosmic fish",     "🌠", 3, 120000, 400000, FishEffect::Exponential, 0.3, 10, 0, "weaves through constellations", Season::AllYear},
    {"stellar phantom", "👻", 2, 140000, 480000, FishEffect::NLogN,       0.32, 10, 0, "ghost of a distant sun", Season::AllYear},
    {"moonbeam ray",    "🌙", 4, 130000, 440000, FishEffect::Wacky,       0.31, 10, 0, "glides on silver light", Season::AllYear},
    {"astral pike",     "🌟", 5, 115000, 390000, FishEffect::Critical,    0.28, 10, 0, "born between the stars", Season::AllYear},
    {"orbit bass",      "🪐", 4, 125000, 420000, FishEffect::Surge,       0.3,  10, 0, "locked in planetary orbit", Season::AllYear},
    {"constellation eel","⭐", 3, 135000, 460000, FishEffect::Cascading,   0.32, 10, 0, "pattern of distant suns", Season::AllYear},
    {"zodiac fish",     "♈", 2, 145000, 490000, FishEffect::Volatile,    0.34, 10, 0, "aligned to celestial signs", Season::AllYear},
    
    // P5 - Divine Rod + Divine Bait (level 11)
    {"eternal fish",    "💫", 2, 250000, 1000000, FishEffect::Ascended,    0.4, 11, 0, "transcends time itself", Season::AllYear},
    {"immortal koi",    "🏨", 1, 300000, 1200000, FishEffect::Persistent,  0.38, 11, 0, "blessed with endless life", Season::AllYear},
    {"ascendant angel", "👼", 3, 275000, 1100000, FishEffect::Banker,      0.36, 11, 0, "divine messenger of depths", Season::AllYear},
    {"holy mackerel",   "😇", 4, 240000, 950000,  FishEffect::Critical,    0.34, 11, 0, "sanctified ocean swimmer", Season::AllYear},
    {"seraphim ray",    "👼", 3, 260000, 1050000, FishEffect::Exponential, 0.36, 11, 0, "six-winged heavenly glider", Season::AllYear},
    {"blessed marlin",  "✝️", 2, 280000, 1150000, FishEffect::Wacky,       0.38, 11, 0, "touched by divine grace", Season::AllYear},
    {"divine dolphin",  "🐬", 1, 320000, 1250000, FishEffect::Surge,       0.4,  11, 0, "messenger of the gods", Season::AllYear},
    
    // P6 - Primordial Rod + Primordial Bait (level 12)
    {"primordial eel",   "🌊", 3, 500000, 2500000,   FishEffect::Exponential, 0.25, 12, 0, "existed before the universe", Season::AllYear},
    {"genesis whale",    "🐋", 1, 750000, 3000000,   FishEffect::NLogN,       0.22, 12, 0, "first creature to swim", Season::AllYear},
    {"origin serpent",   "🐍", 2, 600000, 2800000,   FishEffect::Wacky,       0.27, 12, 0, "mother of all sea life", Season::AllYear},
    {"ancient kraken",   "🦑", 4, 480000, 2400000,   FishEffect::Cascading,   0.24, 12, 0, "predates all civilizations", Season::AllYear},
    {"fossil fish",      "🦴", 3, 550000, 2600000,   FishEffect::Surge,       0.26, 12, 0, "perfectly preserved in amber", Season::AllYear},
    {"primeval shark",   "🦈", 2, 620000, 2900000,   FishEffect::Critical,    0.28, 12, 0, "ancestor of all predators", Season::AllYear},
    {"dawn whale",       "🌅", 1, 700000, 3100000,   FishEffect::Volatile,    0.3,  12, 0, "witnessed the first sunrise", Season::AllYear},
    
    // P7 - Arcane Rod + Arcane Bait (level 13)
    {"mana fish",        "🔮", 3, 1000000, 5000000,   FishEffect::Wacky,       0.28, 13, 0, "pure magical energy condensed", Season::AllYear},
    {"arcane leviathan", "⚗️", 1, 1500000, 6000000,   FishEffect::Exponential, 0.25, 13, 0, "guardian of ancient spells", Season::AllYear},
    {"spell weaver",     "📜", 2, 1200000, 5500000,   FishEffect::NLogN,       0.26, 13, 0, "woven from pure magic", Season::AllYear},
    {"enchanted pike",   "✨", 4, 950000, 4800000,    FishEffect::Volatile,    0.26, 13, 0, "glows with enchantment", Season::AllYear},
    {"rune bass",        "🔮", 3, 1100000, 5200000,   FishEffect::Critical,    0.28, 13, 0, "inscribed with ancient runes", Season::AllYear},
    {"spell shark",      "📖", 2, 1300000, 5800000,   FishEffect::Surge,       0.3,  13, 0, "bites with arcane force", Season::AllYear},
    {"grimoire ray",     "📕", 1, 1400000, 6200000,   FishEffect::Jackpot,     0.32, 13, 0, "pages flutter as fins", Season::AllYear},
    
    // P8 - Quantum Rod + Quantum Bait (level 14)
    {"schrodinger fish", "📦", 2, 2000000, 10000000,  FishEffect::Gambler,     0.4, 14, 0, "both caught and not caught", Season::AllYear},
    {"entangled pair",   "🔗", 1, 2500000, 12000000,  FishEffect::HotStreak,   0.35, 14, 0, "two fish in perfect sync", Season::AllYear},
    {"quantum ghost",    "👻", 3, 2200000, 11000000,  FishEffect::Underdog,    0.38, 14, 0, "exists in superposition", Season::AllYear},
    {"wave function",    "〰️", 4, 1900000, 9500000,   FishEffect::Volatile,    0.36, 14, 0, "collapses when observed", Season::AllYear},
    {"quark pike",       "⚛️", 3, 2100000, 10500000,  FishEffect::Wacky,       0.38, 14, 0, "smallest possible fish", Season::AllYear},
    {"neutrino bass",    "💫", 2, 2300000, 11500000,  FishEffect::NLogN,       0.4,  14, 0, "passes through everything", Season::AllYear},
    {"photon ray",       "💡", 1, 2600000, 12500000,  FishEffect::Exponential, 0.42, 14, 0, "pure light given form", Season::AllYear},
    
    // P9 - Temporal Rod + Temporal Bait (level 15)
    {"time fish",        "⏰", 3, 5000000, 25000000,  FishEffect::NLogN,       0.3,  15, 0, "swims through past and future", Season::AllYear},
    {"paradox salmon",   "🔄", 1, 6000000, 30000000,  FishEffect::Wacky,       0.32, 15, 0, "its own grandfather", Season::AllYear},
    {"temporal anomaly", "⚙️", 2, 5500000, 28000000,  FishEffect::Exponential, 0.31, 15, 0, "distorts spacetime", Season::AllYear},
    {"epoch bass",       "⏳", 4, 4800000, 24000000,  FishEffect::Critical,    0.28, 15, 0, "swam through every era", Season::AllYear},
    {"millennium pike",  "🏛️", 3, 5200000, 26000000,  FishEffect::Cascading,   0.3,  15, 0, "a thousand years old", Season::AllYear},
    {"aeon ray",         "🔄", 2, 5800000, 29000000,  FishEffect::Surge,       0.32, 15, 0, "drifts through geological ages", Season::AllYear},
    {"forever fish",     "♾️", 1, 6200000, 31000000,  FishEffect::Volatile,    0.34, 15, 0, "has no beginning or end", Season::AllYear},
    
    // P10 - Dimensional Rod + Dimensional Bait (level 16)
    {"multiverse carp",  "🌀", 2, 10000000, 50000000,  FishEffect::Exponential, 0.3,  16, 0, "exists in all realities", Season::AllYear},
    {"rift swimmer",     "🕳️", 1, 12000000, 60000000,  FishEffect::Wacky,       0.35, 16, 0, "tears through dimensions", Season::AllYear},
    {"dimensional echo", "📡", 3, 11000000, 55000000,  FishEffect::NLogN,       0.33, 16, 0, "reflection from other worlds", Season::AllYear},
    {"portal fish",      "🌀", 4, 9500000, 48000000,   FishEffect::Volatile,    0.3,  16, 0, "opens gateways when caught", Season::AllYear},
    {"warp pike",        "🚀", 3, 10500000, 52000000,  FishEffect::Critical,    0.33, 16, 0, "bends space while swimming", Season::AllYear},
    {"realm bass",       "👁️", 2, 11500000, 56000000,  FishEffect::Surge,       0.35, 16, 0, "guardian of realm borders", Season::AllYear},
    {"continuum ray",    "⏰", 1, 12500000, 62000000,  FishEffect::Cascading,   0.37, 16, 0, "stitches dimensions together", Season::AllYear},
    
    // P11 - Stellar Rod + Stellar Bait (level 17)
    {"neutron fish",     "⭐", 2, 25000000, 100000000, FishEffect::Merchant,    0.35, 17, 0, "dense as a collapsed star", Season::AllYear},
    {"supernova ray",    "💥", 1, 30000000, 120000000, FishEffect::Wealthy,     0.38, 17, 0, "explodes with cosmic power", Season::AllYear},
    {"pulsar pike",      "🔆", 3, 28000000, 110000000, FishEffect::Fisher,      0.36, 17, 0, "rotating cosmic beacon", Season::AllYear},
    {"solar pike",       "☀️", 4, 24000000, 95000000,  FishEffect::Exponential, 0.33, 17, 0, "forged in solar plasma", Season::AllYear},
    {"magnetar bass",    "🧲", 3, 27000000, 105000000, FishEffect::Volatile,    0.35, 17, 0, "warps magnetic fields", Season::AllYear},
    {"cosmic dust ray",  "🌫️", 2, 29000000, 112000000, FishEffect::NLogN,       0.37, 17, 0, "scattered across light years", Season::AllYear},
    {"dwarf star fish",  "⭐", 1, 32000000, 125000000, FishEffect::Surge,       0.39, 17, 0, "tiny but impossibly dense", Season::AllYear},
    
    // P12 - Galactic Rod + Galactic Bait (level 18)
    {"black hole fish",  "🕳️", 2, 50000000, 250000000, FishEffect::Wacky,       0.38, 18, 0, "consumes light itself", Season::AllYear},
    {"quasar bass",      "✴️", 1, 60000000, 300000000, FishEffect::Exponential, 0.35, 18, 0, "outshines entire galaxies", Season::AllYear},
    {"gravity well",     "⚫", 3, 55000000, 275000000, FishEffect::NLogN,       0.36, 18, 0, "bends space around itself", Season::AllYear},
    {"nebula pike",      "🌌", 4, 48000000, 240000000, FishEffect::Critical,    0.35, 18, 0, "born in stellar nurseries", Season::AllYear},
    {"dark energy bass", "🔮", 3, 52000000, 260000000, FishEffect::Volatile,    0.37, 18, 0, "accelerates expansion", Season::AllYear},
    {"event horizon ray","⚫", 2, 58000000, 290000000, FishEffect::Surge,       0.39, 18, 0, "nothing escapes its pull", Season::AllYear},
    {"stargate fish",    "🌟", 1, 62000000, 310000000, FishEffect::Cascading,   0.41, 18, 0, "gateway between galaxies", Season::AllYear},
    
    // P13 - Universal Rod + Universal Bait (level 19)
    {"entropy fish",     "🌑", 2, 100000000, 500000000, FishEffect::NLogN,       0.38, 19, 0, "embodies heat death", Season::AllYear},
    {"big bang bass",    "💫", 1, 120000000, 600000000, FishEffect::Exponential, 0.4,  19, 0, "creation in fish form", Season::AllYear},
    {"chaos breeder",    "🌪️", 3, 110000000, 550000000, FishEffect::Wacky,       0.39, 19, 0, "spawner of universes", Season::AllYear},
    {"void walker",      "🕳️", 4, 95000000, 480000000,  FishEffect::Volatile,    0.36, 19, 0, "strides between the nothing", Season::AllYear},
    {"chaos pike",       "🌪️", 3, 105000000, 520000000, FishEffect::Critical,    0.38, 19, 0, "thrives in pure disorder", Season::AllYear},
    {"oblivion bass",    "⬛", 2, 115000000, 560000000, FishEffect::Surge,       0.4,  19, 0, "erases what it touches", Season::AllYear},
    {"terminus ray",     "🔚", 1, 125000000, 620000000, FishEffect::Jackpot,     0.42, 19, 0, "marks the end of everything", Season::AllYear},
    
    // P14 - Infinite Rod + Infinite Bait (level 20)
    {"infinity fish",     "♾️", 2, 250000000, 1000000000,  FishEffect::Ascended,    0.5, 20, 0, "beyond all limits", Season::AllYear},
    {"omega whale",       "Ω",  1, 300000000, 1200000000,  FishEffect::Persistent,  0.48, 20, 0, "the final creature", Season::AllYear},
    {"limit breaker",     "🚀", 3, 275000000, 1100000000,  FishEffect::Collector,   0.46, 20, 0, "transcends infinity itself", Season::AllYear},
    {"limitless pike",    "♾️", 4, 240000000, 950000000,   FishEffect::NLogN,       0.42, 20, 0, "knows no boundaries", Season::AllYear},
    {"eternal bass",      "💫", 3, 260000000, 1050000000,  FishEffect::Wacky,       0.44, 20, 0, "exists outside of time", Season::AllYear},
    {"perpetual ray",     "🔄", 2, 280000000, 1100000000,  FishEffect::Surge,       0.46, 20, 0, "endlessly self-renewing", Season::AllYear},
    {"beyond fish",       "🚀", 1, 310000000, 1200000000,  FishEffect::Exponential, 0.48, 20, 0, "past the edge of reality", Season::AllYear},
    
    // P15 - Mythical Rod + Mythical Bait (level 21)
    {"world serpent",     "🐍", 2, 500000000, 2500000000,  FishEffect::NLogN,       0.42, 21, 0, "encircles entire worlds", Season::AllYear},
    {"elder god fish",    "👁️", 1, 600000000, 3000000000,  FishEffect::Wacky,       0.45, 21, 0, "incomprehensible to mortals", Season::AllYear},
    {"ancient one",       "👾", 3, 550000000, 2800000000,  FishEffect::Exponential, 0.43, 21, 0, "awaiting in cosmic void", Season::AllYear},
    {"chimera fish",      "🦁", 4, 480000000, 2400000000,  FishEffect::Volatile,    0.4,  21, 0, "three species fused as one", Season::AllYear},
    {"phoenix whale",     "🔥", 3, 520000000, 2600000000,  FishEffect::Critical,    0.42, 21, 0, "reborn from ocean flames", Season::AllYear},
    {"hydra pike",        "🐉", 2, 560000000, 2900000000,  FishEffect::Cascading,   0.44, 21, 0, "grows two heads when cut", Season::AllYear},
    {"legend bass",       "📖", 1, 620000000, 3100000000,  FishEffect::Surge,       0.46, 21, 0, "spoken of only in myth", Season::AllYear},
    
    // P16 - Omniscient Rod + Omniscient Bait (level 22)
    {"all-seeing fish",   "🔍", 2, 1000000000, 5000000000,   FishEffect::Collector,   0.5, 22, 0, "knows all that was and will be", Season::AllYear},
    {"fate weaver",       "🕸️", 1, 1200000000, 6000000000,   FishEffect::Gambler,     0.52, 22, 0, "controls destiny threads", Season::AllYear},
    {"oracle oracle",     "🔮", 3, 1100000000, 5500000000,   FishEffect::HotStreak,   0.48, 22, 0, "speaks forbidden truths", Season::AllYear},
    {"prophet pike",      "💿", 4, 1150000000, 5750000000,   FishEffect::Banker,      0.49, 22, 0, "foresees all timelines", Season::AllYear},
    {"clairvoyant eel",   "🔮", 4, 950000000, 4800000000,    FishEffect::Critical,    0.46, 22, 0, "sees through time itself", Season::AllYear},
    {"visionary tuna",    "👁️", 3, 1050000000, 5200000000,   FishEffect::Exponential, 0.48, 22, 0, "perceives hidden futures", Season::AllYear},
    {"prescient bass",    "🧿", 2, 1100000000, 5500000000,   FishEffect::Surge,       0.5,  22, 0, "already knew you would catch it", Season::AllYear},
    {"seer ray",          "🌙", 1, 1200000000, 6200000000,   FishEffect::Wacky,       0.52, 22, 0, "prophecy etched on its scales", Season::AllYear},
    
    // P17 - Omnipotent Rod + Omnipotent Bait (level 23)
    {"reality fish",      "🌈", 2, 2500000000, 10000000000,  FishEffect::NLogN,       0.48, 23, 0, "rewrites existence at will", Season::AllYear},
    {"creation koi",      "🎨", 1, 3000000000, 12000000000,  FishEffect::Exponential, 0.5,  23, 0, "brings new worlds to life", Season::AllYear},
    {"world builder",     "🏗️", 3, 2750000000, 11000000000,  FishEffect::Wacky,       0.49, 23, 0, "constructs reality itself", Season::AllYear},
    {"genesis leviathan", "🌅", 4, 2900000000, 11500000000,  FishEffect::Logarithmic, 0.495, 23, 0, "witness to first dawn", Season::AllYear},
    {"tyrant pike",       "👑", 4, 2400000000, 9500000000,   FishEffect::Critical,    0.46, 23, 0, "dominates all lesser fish", Season::AllYear},
    {"emperor bass",      "♚", 3, 2600000000, 10500000000,   FishEffect::Surge,       0.48, 23, 0, "ruler of infinite oceans", Season::AllYear},
    {"dominion ray",      "🏰", 2, 2800000000, 11500000000,  FishEffect::Volatile,    0.5,  23, 0, "commands all waters", Season::AllYear},
    {"crown fish",        "👸", 1, 3100000000, 12500000000,  FishEffect::Cascading,   0.52, 23, 0, "wears a coral crown", Season::AllYear},
    
    // P18 - Transcendent Rod + Transcendent Bait II (level 24)
    {"void emperor",      "👑", 2, 5000000000, 25000000000,  FishEffect::Wacky,       0.5,  24, 0, "rules the emptiness between", Season::AllYear},
    {"concept fish",      "💭", 1, 6000000000, 30000000000,  FishEffect::Exponential, 0.52, 24, 0, "pure abstract thought", Season::AllYear},
    {"void sovereign",    "♔", 3, 5500000000, 27500000000,  FishEffect::NLogN,       0.51, 24, 0, "tyrant of nothingness", Season::AllYear},
    {"abstract entity",   "🎭", 4, 5800000000, 29000000000,  FishEffect::Logarithmic, 0.515, 24, 0, "idea given form", Season::AllYear},
    {"zenith pike",       "⬆️", 4, 4800000000, 24000000000,  FishEffect::Critical,    0.48, 24, 0, "highest point of existence", Season::AllYear},
    {"summit bass",       "🏔️", 3, 5200000000, 26000000000,  FishEffect::Volatile,    0.5,  24, 0, "swims at the peak of all", Season::AllYear},
    {"pinnacle ray",      "🗻", 2, 5600000000, 28000000000,  FishEffect::Surge,       0.52, 24, 0, "apex of aquatic evolution", Season::AllYear},
    {"paradigm fish",     "🔝", 1, 6000000000, 30000000000,  FishEffect::Cascading,   0.54, 24, 0, "shifts the nature of reality", Season::AllYear},
    
    // P19 - Absolute Rod + Absolute Bait (level 25)
    {"axiom fish",        "📜", 2, 10000000000, 50000000000,  FishEffect::Persistent,  0.55, 25, 0, "fundamental truth embodied", Season::AllYear},
    {"singularity",       "⚫", 1, 12000000000, 60000000000,  FishEffect::Ascended,    0.6, 25, 0, "all existence in one point", Season::AllYear},
    {"absolute truth",    "✡️", 3, 11000000000, 55000000000,  FishEffect::Wealthy,     0.58, 25, 0, "foundation of all things", Season::AllYear},
    {"perfect form",      "💎", 4, 11500000000, 57500000000,  FishEffect::Merchant,    0.56, 25, 0, "flawless in every way", Season::AllYear},
    {"supreme pike",      "💎", 4, 9500000000, 48000000000,   FishEffect::Exponential, 0.52, 25, 0, "above all other fish", Season::AllYear},
    {"ultimate eel",      "⚜️", 3, 10500000000, 52000000000,  FishEffect::NLogN,       0.54, 25, 0, "the final form of eels", Season::AllYear},
    {"paramount ray",     "🌟", 2, 11500000000, 56000000000,  FishEffect::Surge,       0.56, 25, 0, "outranks every creature", Season::AllYear},
    {"apex leviathan",    "🐉", 1, 12500000000, 62000000000,  FishEffect::Volatile,    0.58, 25, 0, "peak of natural evolution", Season::AllYear},
    
    // P20 - Ultimate Rod + Ultimate Bait (level 26)
    {"the one fish",      "☀️", 2, 25000000000, 75000000000,   FishEffect::Ascended,    0.65, 26, 0, "there can only be one", Season::AllYear},
    {"end of all",        "🔚", 1, 35000000000, 100000000000,  FishEffect::Persistent,  0.7,  26, 0, "the final catch in existence..?", Season::AllYear},
    {"alpha omega",       "🔀", 3, 30000000000, 90000000000,   FishEffect::Collector,   0.68, 26, 0, "beginning and end combined", Season::AllYear},
    {"eternal paradox",   "♾️", 4, 32000000000, 95000000000,   FishEffect::HotStreak,   0.66, 26, 0, "exists beyond logic itself", Season::AllYear},
    {"final pike",        "🏁", 4, 24000000000, 72000000000,   FishEffect::NLogN,       0.6,  26, 0, "the last fish ever made", Season::AllYear},
    {"omega bass",        "Ω",  3, 28000000000, 85000000000,   FishEffect::Exponential, 0.62, 26, 0, "end letter of all creation", Season::AllYear},
    {"last ray",          "🌅", 2, 30000000000, 92000000000,   FishEffect::Surge,       0.64, 26, 0, "final light of the cosmos", Season::AllYear},
    {"genesis breaker",   "🐋", 1, 35000000000, 100000000000,  FishEffect::Wacky,       0.68, 26, 0, "shatters creation itself", Season::AllYear}
};

// Generate unique fish ID
inline ::std::string generate_fish_id() {
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    ::std::random_device rd;
    ::std::mt19937 gen(rd());
    ::std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    ::std::string id = "U";
    for (int i = 0; i < 6; i++) {
        id += charset[dis(gen)];
    }
    return id;
}

// Determine fish rarity based on the fish name (uses min_gear_level and weight from fish_types)
inline ::std::string get_fish_rarity(const ::std::string& fish_name) {
    // Look up the fish in fish_types and derive rarity from min_gear_level and weight
    for (const auto& f : fish_types) {
        if (f.name == fish_name) {
            // Prestige fish (level 7+) are legendary
            if (f.min_gear_level >= 7) return "legendary";
            // Themed fish (level 6) are legendary
            if (f.min_gear_level == 6) return "legendary";
            // Level 5 = legendary
            if (f.min_gear_level == 5) return "legendary";
            // Level 4 = legendary
            if (f.min_gear_level == 4) return "legendary";
            // Level 3 = epic
            if (f.min_gear_level == 3) return "epic";
            // Level 2 = rare
            if (f.min_gear_level == 2) return "rare";
            
            // For min_gear_level 0-1, also check weight (lower weight = rarer)
            // and max_gear_level (0 means no cap, usually rarer fish)
            if (f.max_gear_level == 0) {
                // No gear cap - these are special fish, use weight to determine rarity
                if (f.weight <= 3) return "legendary";  // whale, dolphin, kraken, etc
                if (f.weight <= 6) return "epic";       // squid, octopus, etc
                if (f.weight <= 12) return "rare";      // jellyfish, lionfish, etc
            }
            
            // Level 0-1 with max_gear_level cap = normal (common)
            return "normal";
        }
    }
    
    // Fallback: check special fish that might not be in the main list
    static const std::vector<std::string> legendary_fish = {
        "leviathan", "sea serpent", "ancient turtle", "abyssal leviathan", 
        "celestial kraken", "golden fish", "legendary fish", "titanic beast",
        "cosmic leviathan", "seal", "whale", "dolphin", "kraken", "orca", "giant manta"
    };
    for (const auto& lf : legendary_fish) if (fish_name == lf) return "legendary";
    
    // Default to normal (common) for unknown fish
    return "normal";
}

// simple JSON helpers (metadata stored as JSON string)
inline int parse_meta_int(const std::string& meta, const std::string& key, int def = 0) {
    size_t pos = meta.find("\"" + key + "\":");
    if (pos == std::string::npos) return def;
    pos = meta.find_first_of("0123456789-", pos + key.size() + 3);
    if (pos == std::string::npos) return def;
    size_t end = pos;
    while (end < meta.size() && (std::isdigit(meta[end]) || meta[end] == '-')) end++;
    try { return std::stoi(meta.substr(pos, end - pos)); } catch(...) {}
    return def;
}

inline std::vector<std::string> parse_meta_array(const std::string& meta, const std::string& key) {
    std::vector<std::string> out;
    size_t pos = meta.find("\"" + key + "\":");
    if (pos == std::string::npos) return out;
    pos = meta.find('[', pos);
    if (pos == std::string::npos) return out;
    size_t end = meta.find(']', pos+1);
    if (end == std::string::npos) return out;
    std::string arr = meta.substr(pos+1, end-pos-1);
    size_t i = 0;
    while (i < arr.size()) {
        size_t start = arr.find('"', i);
        if (start == std::string::npos) break;
        size_t finish = arr.find('"', start+1);
        if (finish == std::string::npos) break;
        out.push_back(arr.substr(start+1, finish-start-1));
        i = finish+1;
    }
    return out;
}

// Get current season based on month
inline Season get_current_season() {
    std::time_t now = std::time(nullptr);
    std::tm* local_time = std::localtime(&now);
    int month = local_time->tm_mon + 1; // tm_mon is 0-11, convert to 1-12
    
    if (month >= 3 && month <= 5) return Season::Spring;
    if (month >= 6 && month <= 8) return Season::Summer;
    if (month >= 9 && month <= 11) return Season::Fall;
    return Season::Winter; // December, January, February
}

// Check if fish is available in the current season
inline bool is_fish_available(const FishType& fish) {
    if (fish.season == Season::AllYear) return true;
    return fish.season == get_current_season();
}

} // namespace fishing
} // namespace commands
