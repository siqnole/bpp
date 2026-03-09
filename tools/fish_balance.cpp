#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace std;

enum class FishEffect { None, Flat, Exponential, Logarithmic, NLogN, Wacky };

struct FishType {
    string name;
    string emoji;
    int weight;
    int64_t min_value;
    int64_t max_value;
    FishEffect effect;
    double effect_chance;
    int min_gear_level;
    int max_gear_level;
    string description;
};

static vector<FishType> fish_types = {
    {"common fish",     "🐟", 60, 10,    50,   FishEffect::None,        0.0, 0, 10, "a plain, everyday fish"},
    {"shrimp",          "🦐", 50, 5,     20,   FishEffect::None,        0.05,0, 10, "tiny and plentiful"},
    {"minnow",          "🐟", 45, 5,     15,   FishEffect::None,        0.05,0, 10, "small bait fish"},
    {"carp",            "🐟", 40, 40,    100,  FishEffect::Flat,        0.1, 0, 10, "warty pond dweller"},
    {"goldfish",        "🐠", 35, 20,    80,   FishEffect::None,        0.02,0, 10, "your escaped pet"},
    {"trout",           "🐟", 30, 50,    120,  FishEffect::Flat,        0.1, 0, 10, "freshwater favorite"},
    {"salmon",          "🐠", 28, 50,    150,  FishEffect::Flat,        0.1, 0, 10, "popular grilled dish"},
    {"clownfish",       "🐠", 25, 70,    180,  FishEffect::None,        0.05,0, 10, "reef colored entertainer"},
    {"tropical fish",   "🐡", 22, 100,   300,  FishEffect::Logarithmic, 0.1, 0, 10, "bright reef dweller"},
    {"catfish",         "🐟", 20, 60,    200,  FishEffect::Logarithmic, 0.1, 0, 10, "whiskered bottom feeder"},
    {"seahorse",        "🐴", 18, 60,    150,  FishEffect::None,        0.05,0, 10, "delicate equine swimmer"},
    {"eel",             "🐍", 16, 80,    250,  FishEffect::NLogN,       0.1, 0, 10, "slippery electric"},
    {"pufferfish",      "🐡", 14, 150,   400,  FishEffect::Wacky,       0.15,0, 0, "inflates when startled"},
    {"jellyfish",       "🪼", 12, 200,   500,  FishEffect::Logarithmic, 0.1, 0, 0, "gelatinous stinger"},
    {"crab",            "🦀", 10, 30,    90,   FishEffect::Flat,        0.1, 0, 10, "hard shell crustacean"},
    {"squid",           "🦑", 8, 300,    700,  FishEffect::Exponential, 0.1, 0, 0, "ink-squirting cephalopod"},
    {"octopus",         "🐙", 6, 200,    500,  FishEffect::Flat,        0.1, 0, 0, "eight-armed clever predator"},
    {"giant squid",     "🦑", 2, 1000,   4000,  FishEffect::Exponential, 0.1, 3, 0, "colossal cephalopod"},
    {"anglerfish",      "🎣", 2, 800,    3000,  FishEffect::Logarithmic, 0.1, 3, 0, "lurking lure predator"},
    {"lobster",         "🦞", 4, 500,   1200,  FishEffect::Flat,        0.05,0, 0, "expensive shellfish"},
    {"shark",           "🦈", 3, 500,   1000,  FishEffect::NLogN,       0.1, 0, 0, "apex predator of the deep"},
    {"manta ray",       "🐋", 2, 800,   2000,  FishEffect::Flat,        0.1, 0, 0, "giant flat glider"},
    {"whale",           "🐋", 1, 1000,  3000,  FishEffect::Exponential, 0.1, 0, 0, "massive ocean mammal"},
    {"dolphin",         "🐬", 1, 1000,  3000,  FishEffect::Wacky,       0.1, 0, 0, "playful marine mammal"},
    {"kraken",          "🦑", 1, 5000, 15000,  FishEffect::Wacky,       0.2, 0, 0, "legendary sea monster"},
    {"golden fish",     "✨", 1, 5000, 10000,  FishEffect::Wacky,       0.1, 0, 0, "shimmering rare fish"},
    {"legendary fish",  "🐉", 1, 20000,100000, FishEffect::Exponential, 0.1, 0, 0, "ancient mythical catch"}
};

struct Rod { string id; int level; int price; int luck; int capacity; };
struct Bait { string id; int level; int price; vector<string> unlocks; int bonus; int multiplier; };

int main() {
    vector<Rod> rods = {
        {"rod_wood",1,500,0,1},
        {"rod_iron",2,2000,5,2},
        {"rod_steel",3,60000,10,3},
        {"rod_gold",4,200000,20,5},
        {"rod_diamond",5,1000000,50,10}
    };
    vector<Bait> baits = {
        {"bait_common",1,50,{"common fish","salmon"},50,100},
        {"bait_uncommon",2,200,{"common fish","salmon","tropical fish"},75,150},
        {"bait_rare",3,2000,{"salmon","tropical fish","octopus","shark","giant squid","anglerfish"},100,200},
        {"bait_epic",4,50000,{"whale","golden fish"},10,400},
        {"bait_legendary",5,500000,{"legendary fish"},20,40}
    };

    random_device rd;
    mt19937 gen(rd());

    const int trials = 100000;

    cout << "simulating " << trials << " trials per combo...\n";

    for (auto &rod : rods) {
        for (auto &bait : baits) {
            // ensure compatibility
            if (abs(rod.level - bait.level) > 2) continue;
            int capacity = rod.capacity;
            int used_bait = capacity; // assume always enough

            // build pool
            vector<FishType> pool;
            for (auto &f : fish_types) {
                if (!bait.unlocks.empty()) {
                    if (find(bait.unlocks.begin(), bait.unlocks.end(), f.name) == bait.unlocks.end())
                        continue;
                }
                int gear_lvl = min(rod.level, bait.level);
                if (f.max_gear_level > 0 && gear_lvl >= f.max_gear_level) continue;
                if (f.min_gear_level > 0 && gear_lvl < f.min_gear_level) continue;
                pool.push_back(f);
            }
            if (pool.empty()) pool = fish_types;
            if (bait.level > 2) {
                pool.erase(remove_if(pool.begin(), pool.end(), [](const FishType &f){ return f.name=="common fish"; }), pool.end());
            }
            if (pool.empty()) pool = fish_types;
            int luck = rod.luck;
            int max_w = 0;
            for (auto &f : pool) max_w = max(max_w, f.weight);
            vector<int> weights;
            for (auto &f : pool) {
                int w = f.weight;
                if (luck != 0 && max_w > 0) w += int((max_w - w) * (luck / 100.0));
                weights.push_back(w);
            }
            discrete_distribution<> dis(weights.begin(), weights.end());

            double total_profit = 0;
            std::unordered_map<std::string,int> catch_counts;
            for (int t = 0; t < trials; t++) {
                int64_t total_val = 0;
                // compute extra fish from bait bonus and potential rod/bait synergy
                int synergy = (rod.level == bait.level ? rod.level : 1);
                int extra_fish = (used_bait * bait.bonus * synergy) / 100;
                for (int i = 0; i < used_bait; ++i) {
                    auto &fish = pool[dis(gen)];
                    catch_counts[fish.name]++;
                    uniform_int_distribution<int64_t> valdis(fish.min_value, fish.max_value);
                    int64_t base = valdis(gen);
                    if (luck != 0) base = base + (base * luck / 100);
                    if (bait.level > 1) base += bait.level * 5;
                    if (bait.multiplier != 0) base += (base * bait.multiplier / 100);
                    int64_t val = base;
                    double roll = double(rand())/RAND_MAX;
                    if (roll < fish.effect_chance) {
                        switch (fish.effect) {
                            case FishEffect::Flat: val += luck + bait.level; break;
                            case FishEffect::Exponential: val = int64_t(val * pow(1.0 + luck/100.0,2)); break;
                            case FishEffect::Logarithmic: val = int64_t(val * log2(luck+2)); break;
                            case FishEffect::NLogN: {
                                double n = luck + bait.level;
                                val = int64_t(val * (n * log2(n+2)));
                                break;
                            }
                            case FishEffect::Wacky: val *= (rand()%5 +1); break;
                            case FishEffect::Jackpot: val = (rand()%2==0) ? int64_t(val*0.2) : int64_t(val*8); break;
                            case FishEffect::Critical: val = (rand()%2==0) ? val*2 : val; break;
                            case FishEffect::Volatile: val = int64_t(val * (0.3 + (rand()%38)/10.0)); break;
                            case FishEffect::Surge: val += gear_lvl*gear_lvl*50; break;
                            case FishEffect::Diminishing: { double m = 3.0 - luck/50.0; val = int64_t(val * max(0.5, m)); break; }
                            case FishEffect::Cascading: { int rolls = 1 + rand()%6; val = int64_t(val * pow(1.15, rolls)); break; }
                            case FishEffect::Wealthy: val = int64_t(val * 1.5); break;
                            case FishEffect::Banker: val = int64_t(val * 1.4); break;
                            case FishEffect::Fisher: val = int64_t(val * 1.2); break;
                            case FishEffect::Merchant: val += 1000; break;
                            case FishEffect::Gambler: val = (rand()%2==0) ? val*2 : int64_t(val*0.5); break;
                            case FishEffect::Ascended: val = int64_t(val * pow(1.5, gear_lvl/3)); break;
                            case FishEffect::Underdog: val = int64_t(val * 1.5); break;
                            case FishEffect::HotStreak: val = int64_t(val * 1.5); break;
                            case FishEffect::Collector: val += 5000; break;
                            case FishEffect::Persistent: val = int64_t(val * log2(20002)); break;
                            default: break;
                        }
                    }
                    total_val += val;
                }
                // roll the bonus fish too
                for (int i = 0; i < extra_fish; ++i) {
                    auto &fish = pool[dis(gen)];
                    catch_counts[fish.name]++;
                    uniform_int_distribution<int64_t> valdis(fish.min_value, fish.max_value);
                    int64_t base = valdis(gen);
                    if (luck != 0) base = base + (base * luck / 100);
                    if (bait.level > 1) base += bait.level * 5;
                    if (bait.multiplier != 0) base += (base * bait.multiplier / 100);
                    int64_t val = base;
                    double roll = double(rand())/RAND_MAX;
                    if (roll < fish.effect_chance) {
                        switch (fish.effect) {
                            case FishEffect::Flat: val += luck + bait.level; break;
                            case FishEffect::Exponential: val = int64_t(val * pow(1.0 + luck/100.0,2)); break;
                            case FishEffect::Logarithmic: val = int64_t(val * log2(luck+2)); break;
                            case FishEffect::NLogN: {
                                double n = luck + bait.level;
                                val = int64_t(val * (n * log2(n+2)));
                                break;
                            }
                            case FishEffect::Wacky: val *= (rand()%5 +1); break;
                            case FishEffect::Jackpot: val = (rand()%2==0) ? int64_t(val*0.2) : int64_t(val*8); break;
                            case FishEffect::Critical: val = (rand()%2==0) ? val*2 : val; break;
                            case FishEffect::Volatile: val = int64_t(val * (0.3 + (rand()%38)/10.0)); break;
                            case FishEffect::Surge: val += gear_lvl*gear_lvl*50; break;
                            case FishEffect::Diminishing: { double m = 3.0 - luck/50.0; val = int64_t(val * max(0.5, m)); break; }
                            case FishEffect::Cascading: { int rolls = 1 + rand()%6; val = int64_t(val * pow(1.15, rolls)); break; }
                            case FishEffect::Wealthy: val = int64_t(val * 1.5); break;
                            case FishEffect::Banker: val = int64_t(val * 1.4); break;
                            case FishEffect::Fisher: val = int64_t(val * 1.2); break;
                            case FishEffect::Merchant: val += 1000; break;
                            case FishEffect::Gambler: val = (rand()%2==0) ? val*2 : int64_t(val*0.5); break;
                            case FishEffect::Ascended: val = int64_t(val * pow(1.5, gear_lvl/3)); break;
                            case FishEffect::Underdog: val = int64_t(val * 1.5); break;
                            case FishEffect::HotStreak: val = int64_t(val * 1.5); break;
                            case FishEffect::Collector: val += 5000; break;
                            case FishEffect::Persistent: val = int64_t(val * log2(20002)); break;
                            default: break;
                        }
                    }
                    total_val += val;
                }
                int64_t bait_cost = int64_t(bait.price) * used_bait;
                int64_t profit = total_val - bait_cost;
                if (profit < 0 && bait.level <= 3) {
                    int64_t deficit = -profit;
                    int64_t min_val = fish_types.empty() ? 0 : fish_types.front().min_value;
                    if (min_val > 0) {
                        int64_t extra = ((deficit + min_val - 1) / min_val) * min_val;
                        if (extra > 5000) extra = 5000;
                        profit += extra;
                    }
                }
                total_profit += profit;
            }
            double avg_profit = total_profit / trials;
            cout << "rod=" << rod.id << " (lvl" << rod.level << ") ";
            cout << "bait=" << bait.id << " (lvl" << bait.level << ") ";
            cout << "avg profit=" << avg_profit << "\n";
            // show distribution of catches
            if (!catch_counts.empty()) {
                cout << "  distribution:";
                for (auto &p : catch_counts) {
                    double pct = (double)p.second / (trials * used_bait) * 100.0;
                    cout << " " << p.first << "(" << p.second << "," << pct << "%)";
                }
                cout << "\n";
            }
        }
    }
    return 0;
}
