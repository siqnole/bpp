#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <mutex>

namespace commands {
namespace pets {

using namespace bronx::db;

// --- Pet species definitions ---
struct PetSpecies {
    std::string id;
    std::string name;
    std::string emoji;
    std::string rarity;         // common, uncommon, rare, epic, legendary, prestige
    std::string bonus_type;     // what stat it buffs
    double base_bonus;          // % bonus at level 1
    double bonus_per_level;     // additional % per level
    int64_t adopt_cost;         // coins to adopt
    std::string description;
    int max_level;
    int xp_per_activity;        // XP gained per related activity
};

static const std::vector<PetSpecies> PET_SPECIES = {
    // Common pets
    {"cat",       "cat",        "\xF0\x9F\x90\xB1", "common",    "luck_bonus",         3.0, 0.5, 50000,       "a curious cat that brings you luck",                 20, 5},
    {"dog",       "dog",        "\xF0\x9F\x90\xB6", "common",    "xp_bonus",           3.0, 0.5, 50000,       "a loyal dog that helps you learn faster",            20, 5},
    {"hamster",   "hamster",    "\xF0\x9F\x90\xB9", "common",    "work_bonus",         5.0, 1.0, 50000,       "a hardworking hamster that boosts work earnings",    20, 5},
    
    // Uncommon pets
    {"parrot",    "parrot",     "\xF0\x9F\xA6\x9C", "uncommon",  "fish_value_bonus",   5.0, 0.8, 200000,      "a colorful parrot that appraises fish for more",     25, 8},
    {"rabbit",    "rabbit",     "\xF0\x9F\x90\x87", "uncommon",  "daily_bonus",        5.0, 1.0, 200000,      "a lucky rabbit that boosts daily rewards",           25, 8},
    {"fox",       "fox",        "\xF0\x9F\xA6\x8A", "uncommon",  "rob_protection",     5.0, 0.8, 200000,      "a cunning fox that protects your coins from thieves", 25, 8},
    
    // Rare pets
    {"owl",       "owl",        "\xF0\x9F\xA6\x89", "rare",      "ore_value_bonus",    8.0, 1.0, 1000000,     "a wise owl that finds valuable ore deposits",        30, 10},
    {"dolphin",   "dolphin",    "\xF0\x9F\xAC",     "rare",      "rare_fish_bonus",    5.0, 0.8, 1000000,     "a playful dolphin that attracts rare fish",          30, 10},
    {"wolf",      "wolf",       "\xF0\x9F\x90\xBA", "rare",      "gambling_luck_bonus",3.0, 0.5, 1000000,     "a fierce wolf that brings gambling luck",            30, 10},
    
    // Epic pets
    {"phoenix",   "phoenix",    "\xF0\x9F\x94\xA5", "epic",      "all_value_bonus",    5.0, 0.8, 10000000,    "a mythical phoenix that boosts all item values",     40, 15},
    {"unicorn",   "unicorn",    "\xF0\x9F\xA6\x84", "epic",      "legendary_fish_bonus",3.0, 0.5, 10000000,   "a magical unicorn that attracts legendary fish",     40, 15},
    
    // Legendary pets
    {"dragon",    "dragon",     "\xF0\x9F\x90\x89", "legendary", "all_earnings_bonus", 8.0, 1.0, 100000000,   "an ancient dragon that boosts all earnings",         50, 20},
    
    // Prestige-only pets
    {"void_cat",  "void cat",   "\xF0\x9F\x90\x88\xE2\x80\x8D\xE2\xAC\x9B", "prestige", "prestige_bonus", 10.0, 1.5, 500000000, "a mysterious void cat from another dimension", 50, 25},
};

const PetSpecies* find_species(const std::string& id);
std::string rarity_color(const std::string& rarity);
std::string format_bonus_name(const std::string& bonus);

// --- Pet instance (user's owned pet) ---
struct UserPet {
    int64_t pet_id;             // DB row ID
    std::string species_id;
    std::string nickname;
    int level;
    int xp;
    int hunger;                 // 0-100 (100 = full, 0 = starving)
    bool equipped;
    std::string adopted_at;
    std::string last_fed;
};

int calculate_current_hunger(int stored_hunger, const std::string& last_fed);
int xp_for_level(int level);
double get_pet_effective_bonus(const UserPet& pet, const PetSpecies& species);
std::string hunger_bar(int hunger);

void ensure_pet_tables(Database* db);
std::vector<UserPet> get_user_pets(Database* db, uint64_t user_id);
UserPet* find_user_pet(std::vector<UserPet>& pets_list, const std::string& name);
bool adopt_pet(Database* db, uint64_t user_id, const std::string& species_id, const std::string& nickname);
bool feed_pet(Database* db, int64_t pet_id);
bool equip_pet(Database* db, uint64_t user_id, int64_t pet_id);
bool release_pet(Database* db, uint64_t user_id, int64_t pet_id);
bool rename_pet(Database* db, int64_t pet_id, const std::string& new_name);

bool activity_matches_pet(const std::string& bonus_type, const std::string& activity);
bool award_pet_xp(Database* db, uint64_t user_id, const std::string& activity, int count = 1);

namespace pet_hooks {
    void on_fish(Database* db, uint64_t uid, int fish_count);
    void on_mine(Database* db, uint64_t uid, int ore_count);
    void on_gamble(Database* db, uint64_t uid);
    void on_work(Database* db, uint64_t uid);
    void on_daily(Database* db, uint64_t uid);
}

static const int PET_SHOP_PER_PAGE = 4;
dpp::message build_shop_page(int page, int prestige, uint64_t user_id);
void handle_pet_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db);
Command* create_pet_command(Database* db);

} // namespace pets
} // namespace commands
