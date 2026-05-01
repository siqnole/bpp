#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <random>

namespace commands {
namespace global_boss {

using namespace bronx::db;

// --- Archetypes ---
struct BossArchetype {
    std::string name;
    std::string tag;
    double mine_cmd_mult;
    double ores_mult;
    double fish_cmd_mult;
    double fish_mult;
    double fish_profit_mult;
    double gamble_cmd_mult;
    double gamble_profit_mult;
};

static const std::vector<BossArchetype> ARCHETYPES = {
    {"Balanced",                "⚖️ Balanced",     1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {"Mining Colossus",         "⛏️ Mining",       2.5, 3.0, 0.5, 0.5, 0.6, 0.7, 0.5},
    {"Deep Sea Terror",         "🎣 Fishing",      0.5, 0.5, 2.5, 3.0, 2.0, 0.7, 0.5},
    {"Fortune's Bane",          "🎰 Gambling",     0.5, 0.5, 0.5, 0.5, 0.6, 3.0, 3.5},
    {"The Golden Hoarder",      "💰 Profit",       0.7, 0.7, 0.7, 0.7, 3.5, 1.0, 2.5},
    {"Mining Leviathan",        "⛏️🐉 Mine+Fish",  2.0, 2.0, 2.0, 2.0, 1.0, 0.3, 0.3},
    {"Gambler's Nightmare",     "🎰💀 Gamble+Mine", 1.8, 2.0, 0.4, 0.4, 0.5, 2.5, 2.5},
    {"Abyssal Merchant",        "🎣💰 Fish+Profit", 0.4, 0.4, 2.0, 2.5, 3.0, 0.5, 0.8},
};

struct GoalRange { int64_t min_val; int64_t max_val; };

static const GoalRange BASE_MINE_COMMANDS   = {   500,   2000};
static const GoalRange BASE_ORES_MINED      = {  5000,  20000};
static const GoalRange BASE_FISH_COMMANDS   = {  5000,  15000};
static const GoalRange BASE_FISH_CAUGHT     = { 50000, 150000};
static const GoalRange BASE_FISH_PROFIT     = {2000000000000LL, 8000000000000LL};
static const GoalRange BASE_GAMBLE_COMMANDS = {   500,   3000};
static const GoalRange BASE_GAMBLE_PROFIT   = {500000000000LL, 5000000000000LL};

static const std::vector<std::string> BOSS_NAMES = {
    "Leviathan of the Deep", "Titanforge Golem", "Kraken of the Abyss", "Obsidian Wyrm",
    "The Phantom Mariner", "Magma Serpent", "Crystal Hydra", "Storm Colossus",
    "Abyssal Devourer", "Iron Behemoth", "The Gilded Serpent", "Void Harbinger",
    "Coral Titan", "Ember Wraith", "Diamond Goliath"
};

static const std::vector<std::string> BOSS_EMOJIS = {
    "🐉", "🗿", "🦑", "🐲", "👻", "🔥", "💎", "⛈️", "🌊", "⚔️", "🐍", "🕳️", "🪸", "🔮", "💠"
};

// --- System Structs ---
struct BossGoals {
    int64_t mine_commands;
    int64_t ores_mined;
    int64_t fish_commands;
    int64_t fish_caught;
    int64_t fish_profit;
    int64_t gamble_commands;
    int64_t gamble_profit;
    int archetype_idx;
};

struct BossReward {
    int lootbox_min;
    int lootbox_max;
    int64_t cash;
    int bait_count;
    std::string lootbox_tier;
};

static const std::vector<std::string> HIGH_TIER_BAITS = {
    "bait_rare", "bait_epic", "bait_legendary"
};

struct BossProgress {
    int id = 0;
    int boss_number = 0;
    std::string boss_name;
    std::string archetype;
    int64_t mine_commands = 0, ores_mined = 0;
    int64_t fish_commands = 0, fish_caught = 0, fish_profit = 0;
    int64_t gamble_commands = 0, gamble_profit = 0;
    int64_t goal_mine_commands = 1000, goal_ores_mined = 10000;
    int64_t goal_fish_commands = 10000, goal_fish_caught = 100000;
    int64_t goal_fish_profit = 5000000000000LL;
    int64_t goal_gamble_commands = 1500, goal_gamble_profit = 2000000000000LL;
    bool defeated = false;
};

struct ContributorEntry {
    uint64_t user_id;
    int64_t mine_commands, ores_mined;
    int64_t fish_commands, fish_caught, fish_profit;
    int64_t gamble_commands, gamble_profit;
    int64_t total_score;
};

// --- Functions ---
std::string format_number(int64_t num);
BossGoals generate_boss_goals(int boss_number);
BossReward get_reward_for_rank(int rank);
std::string lootbox_display_name(const std::string& tier);
std::string reward_summary(int rank);
void distribute_boss_rewards(Database* db, int boss_id);

void ensure_boss_table(Database* db);
int get_or_create_boss(Database* db);
BossProgress get_boss_progress(Database* db, int boss_id);
bool increment_boss_stat(Database* db, int boss_id, uint64_t user_id, const std::string& column, int64_t amount);

// --- Hooks for other commands ---
void on_fish_command(Database* db, uint64_t user_id, int64_t fish_count);
void on_fish_profit(Database* db, uint64_t user_id, int64_t profit);
void on_gamble_command(Database* db, uint64_t user_id, int64_t profit);
void on_mine_command(Database* db, uint64_t user_id);
void on_ores_mined(Database* db, uint64_t user_id, int64_t count);

std::vector<ContributorEntry> get_top_contributors(Database* db, int boss_id, int limit = 10);
ContributorEntry get_user_contribution(Database* db, int boss_id, uint64_t user_id);
int get_defeated_count(Database* db);

std::string make_progress_bar(int64_t current, int64_t goal, int width = 16);
std::string format_short(int64_t num);
std::string archetype_tag(const std::string& name);

static constexpr int BOSS_TOTAL_PAGES = 3;
dpp::component build_boss_paginator(int boss_id, int page, uint64_t user_id);
dpp::message build_boss_page(Database* db, int boss_id, int page, uint64_t requesting_user);

Command* get_boss_command(Database* db);
std::vector<Command*> get_global_boss_commands(Database* db);
void register_boss_interactions(dpp::cluster& bot, Database* db);

} // namespace global_boss
} // namespace commands
