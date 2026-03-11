#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <cstring>
#include <cmath>
#include <chrono>
#include <random>
#include <unordered_map>

using namespace bronx::db;

namespace commands {
namespace global_boss {

// Local format_number helper (avoids dependency on economy_core.h)
inline std::string format_number(int64_t num) {
    bool neg = num < 0;
    if (neg) num = -num;
    std::string str = std::to_string(num);
    int insert_position = str.length() - 3;
    while (insert_position > 0) {
        str.insert(insert_position, ",");
        insert_position -= 3;
    }
    return neg ? "-" + str : str;
}

// ============================================================================
// GLOBAL BOSS SYSTEM — Randomized Thresholds + Gambling
// ============================================================================
// Each boss has randomly generated goals with an archetype bias:
//   Mining-focused, Fishing-focused, Gambling-focused, Balanced, Profit-focused
//
// Tracked stats:
//   - mine_commands, ores_mined          (mining)
//   - fish_commands, fish_caught         (fishing)
//   - fish_profit                        (economy)
//   - gamble_commands, gamble_profit      (gambling)
// ============================================================================

// ── Boss archetype definitions ──────────────────────────────────────────────
struct BossArchetype {
    std::string name;
    std::string tag;
    // Multipliers for each goal category (1.0 = normal)
    double mine_cmd_mult;
    double ores_mult;
    double fish_cmd_mult;
    double fish_mult;
    double fish_profit_mult;
    double gamble_cmd_mult;
    double gamble_profit_mult;
};

static const std::vector<BossArchetype> ARCHETYPES = {
    //  name                     tag                mc   ore   fc   fi   fp   gc   gp
    {"Balanced",                "⚖️ Balanced",     1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
    {"Mining Colossus",         "⛏️ Mining",       2.5, 3.0, 0.5, 0.5, 0.6, 0.7, 0.5},
    {"Deep Sea Terror",         "🎣 Fishing",      0.5, 0.5, 2.5, 3.0, 2.0, 0.7, 0.5},
    {"Fortune's Bane",          "🎰 Gambling",     0.5, 0.5, 0.5, 0.5, 0.6, 3.0, 3.5},
    {"The Golden Hoarder",      "💰 Profit",       0.7, 0.7, 0.7, 0.7, 3.5, 1.0, 2.5},
    {"Mining Leviathan",        "⛏️🐉 Mine+Fish",  2.0, 2.0, 2.0, 2.0, 1.0, 0.3, 0.3},
    {"Gambler's Nightmare",     "🎰💀 Gamble+Mine", 1.8, 2.0, 0.4, 0.4, 0.5, 2.5, 2.5},
    {"Abyssal Merchant",        "🎣💰 Fish+Profit", 0.4, 0.4, 2.0, 2.5, 3.0, 0.5, 0.8},
};

// Base goal ranges (min, max) before archetype multiplier
struct GoalRange { int64_t min_val; int64_t max_val; };

static const GoalRange BASE_MINE_COMMANDS   = {   500,   2000};
static const GoalRange BASE_ORES_MINED      = {  5000,  20000};
static const GoalRange BASE_FISH_COMMANDS   = {  5000,  15000};
static const GoalRange BASE_FISH_CAUGHT     = { 50000, 150000};
static const GoalRange BASE_FISH_PROFIT     = {2000000000000LL, 8000000000000LL}; // 2T-8T
static const GoalRange BASE_GAMBLE_COMMANDS = {   500,   3000};
static const GoalRange BASE_GAMBLE_PROFIT   = {500000000000LL, 5000000000000LL};  // 500B-5T

// Boss names/themes
static const std::vector<std::string> BOSS_NAMES = {
    "Leviathan of the Deep",
    "Titanforge Golem",
    "Kraken of the Abyss",
    "Obsidian Wyrm",
    "The Phantom Mariner",
    "Magma Serpent",
    "Crystal Hydra",
    "Storm Colossus",
    "Abyssal Devourer",
    "Iron Behemoth",
    "The Gilded Serpent",
    "Void Harbinger",
    "Coral Titan",
    "Ember Wraith",
    "Diamond Goliath",
};

static const std::vector<std::string> BOSS_EMOJIS = {
    "🐉", "🗿", "🦑", "🐲", "👻", "🔥", "💎", "⛈️", "🌊", "⚔️",
    "🐍", "🕳️", "🪸", "🔮", "💠",
};

// ── Generate random goals for a new boss ────────────────────────────────────
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

inline BossGoals generate_boss_goals(int boss_number) {
    std::mt19937 rng(std::random_device{}());

    int arch_idx = rng() % ARCHETYPES.size();
    const auto& arch = ARCHETYPES[arch_idx];

    auto rand_in_range = [&](const GoalRange& r, double mult) -> int64_t {
        double lo = r.min_val * mult;
        double hi = r.max_val * mult;
        if (lo > hi) std::swap(lo, hi);
        std::uniform_int_distribution<int64_t> dist((int64_t)lo, std::max((int64_t)lo, (int64_t)hi));
        int64_t val = dist(rng);
        // Round to "nice" numbers
        if (val >= 1000000000000LL) {
            val = (val / 100000000000LL) * 100000000000LL;
        } else if (val >= 1000000000LL) {
            val = (val / 1000000000LL) * 1000000000LL;
        } else if (val >= 1000000LL) {
            val = (val / 1000000LL) * 1000000LL;
        } else if (val >= 1000LL) {
            val = (val / 100LL) * 100LL;
        }
        return std::max((int64_t)100, val);
    };

    BossGoals g;
    g.mine_commands   = rand_in_range(BASE_MINE_COMMANDS,   arch.mine_cmd_mult);
    g.ores_mined      = rand_in_range(BASE_ORES_MINED,      arch.ores_mult);
    g.fish_commands   = rand_in_range(BASE_FISH_COMMANDS,    arch.fish_cmd_mult);
    g.fish_caught     = rand_in_range(BASE_FISH_CAUGHT,      arch.fish_mult);
    g.fish_profit     = rand_in_range(BASE_FISH_PROFIT,      arch.fish_profit_mult);
    g.gamble_commands = rand_in_range(BASE_GAMBLE_COMMANDS,  arch.gamble_cmd_mult);
    g.gamble_profit   = rand_in_range(BASE_GAMBLE_PROFIT,    arch.gamble_profit_mult);
    g.archetype_idx   = arch_idx;
    return g;
}

// ── Reward tiers ────────────────────────────────────────────────────────────

// Forward declarations for display helpers used in reward_summary
inline std::string format_short(int64_t num);

struct BossReward {
    int lootbox_min;
    int lootbox_max;
    int64_t cash;
    int bait_count;           // number of high-tier bait pieces
    std::string lootbox_tier; // item_id of lootbox tier to give
};

// High-tier bait options (randomly chosen)
static const std::vector<std::string> HIGH_TIER_BAITS = {
    "bait_rare", "bait_epic", "bait_legendary"
};

inline BossReward get_reward_for_rank(int rank) {
    if (rank == 1)  return {15, 20, 100000000LL, 40, "lootbox_legendary"};
    if (rank == 2)  return {12, 15,  75000000LL, 30, "lootbox_legendary"};
    if (rank == 3)  return {10, 12,  50000000LL, 25, "lootbox_epic"};
    if (rank == 4)  return { 8, 10,  35000000LL, 20, "lootbox_epic"};
    if (rank == 5)  return { 6,  8,  25000000LL, 15, "lootbox_epic"};
    if (rank <= 30) return { 4,  6,  10000000LL, 10, "lootbox_rare"};
    if (rank <= 50) return { 2,  4,   5000000LL,  5, "lootbox_rare"};
    if (rank <= 100)return { 1,  2,   2000000LL,  3, "lootbox_uncommon"};
    return                  { 1,  1,    500000LL,  1, "lootbox_common"};
}

inline std::string lootbox_display_name(const std::string& tier) {
    if (tier == "lootbox_legendary") return "👑 Legendary";
    if (tier == "lootbox_epic")      return "🎀 Epic";
    if (tier == "lootbox_rare")      return "🎁 Rare";
    if (tier == "lootbox_uncommon")  return "📫 Uncommon";
    return "📦 Common";
}

inline std::string reward_summary(int rank) {
    auto r = get_reward_for_rank(rank);
    std::string lb_range = (r.lootbox_min == r.lootbox_max)
        ? std::to_string(r.lootbox_min)
        : std::to_string(r.lootbox_min) + "-" + std::to_string(r.lootbox_max);
    return lb_range + "x " + lootbox_display_name(r.lootbox_tier)
         + " │ $" + format_short(r.cash)
         + " │ " + std::to_string(r.bait_count) + "x 🪱";
}

// Distribute rewards to all contributors of a defeated boss
inline void distribute_boss_rewards(Database* db, int boss_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return;

    // Check if already distributed
    std::string chk = "SELECT rewards_distributed FROM global_boss WHERE id = " + std::to_string(boss_id);
    if (mysql_query(conn->get(), chk.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row && row[0] && std::string(row[0]) == "1") {
                mysql_free_result(r);
                db->get_pool()->release(conn);
                return; // already distributed
            }
            mysql_free_result(r);
        }
    }

    // Get ALL contributors ranked by total_score
    std::string q = "SELECT user_id FROM global_boss_contributors WHERE boss_id = " + std::to_string(boss_id) +
                    " ORDER BY (mine_commands + ores_mined + fish_commands + fish_caught + gamble_commands) DESC";

    if (mysql_query(conn->get(), q.c_str()) != 0) {
        db->get_pool()->release(conn);
        return;
    }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return; }

    std::vector<uint64_t> ranked_users;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ranked_users.push_back(std::stoull(row[0] ? row[0] : "0"));
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);

    std::mt19937 rng(std::random_device{}());

    for (size_t i = 0; i < ranked_users.size(); i++) {
        int rank = (int)(i + 1);
        uint64_t uid = ranked_users[i];
        BossReward reward = get_reward_for_rank(rank);

        // Random lootbox count in range
        int lb_count = reward.lootbox_min;
        if (reward.lootbox_max > reward.lootbox_min) {
            std::uniform_int_distribution<int> lb_dist(reward.lootbox_min, reward.lootbox_max);
            lb_count = lb_dist(rng);
        }

        // Give lootboxes
        db->add_item(uid, reward.lootbox_tier, "other", lb_count, "{}", 1);

        // Give cash
        db->update_wallet(uid, reward.cash);

        // Give random high-tier bait
        for (int b = 0; b < reward.bait_count; b++) {
            const std::string& bait_id = HIGH_TIER_BAITS[rng() % HIGH_TIER_BAITS.size()];
            // Get proper metadata from shop so bait works correctly
            auto shop_item = db->get_shop_item(bait_id);
            if (shop_item) {
                db->add_item(uid, bait_id, "bait", 1, shop_item->metadata, shop_item->level);
            } else {
                db->add_item(uid, bait_id, "bait", 1, "{}", 3);
            }
        }
    }

    // Mark rewards as distributed
    auto conn2 = db->get_pool()->acquire();
    if (conn2) {
        std::string mark = "UPDATE global_boss SET rewards_distributed = TRUE WHERE id = " + std::to_string(boss_id);
        mysql_query(conn2->get(), mark.c_str());
        db->get_pool()->release(conn2);
    }

    std::cout << "[global_boss] distributed rewards to " << ranked_users.size()
              << " contributors for boss #" << boss_id << std::endl;
}

// ── Database helpers ────────────────────────────────────────────────────────

inline void ensure_boss_table(Database* db) {
    static bool created = false;
    if (created) return;

    db->execute(
        "CREATE TABLE IF NOT EXISTS global_boss ("
        "  id INT PRIMARY KEY AUTO_INCREMENT,"
        "  boss_number INT NOT NULL DEFAULT 1,"
        "  boss_name VARCHAR(128) NOT NULL,"
        "  archetype VARCHAR(64) NOT NULL DEFAULT 'Balanced',"
        "  mine_commands BIGINT NOT NULL DEFAULT 0,"
        "  ores_mined BIGINT NOT NULL DEFAULT 0,"
        "  fish_commands BIGINT NOT NULL DEFAULT 0,"
        "  fish_caught BIGINT NOT NULL DEFAULT 0,"
        "  fish_profit BIGINT NOT NULL DEFAULT 0,"
        "  gamble_commands BIGINT NOT NULL DEFAULT 0,"
        "  gamble_profit BIGINT NOT NULL DEFAULT 0,"
        "  goal_mine_commands BIGINT NOT NULL DEFAULT 1000,"
        "  goal_ores_mined BIGINT NOT NULL DEFAULT 10000,"
        "  goal_fish_commands BIGINT NOT NULL DEFAULT 10000,"
        "  goal_fish_caught BIGINT NOT NULL DEFAULT 100000,"
        "  goal_fish_profit BIGINT NOT NULL DEFAULT 5000000000000,"
        "  goal_gamble_commands BIGINT NOT NULL DEFAULT 1500,"
        "  goal_gamble_profit BIGINT NOT NULL DEFAULT 2000000000000,"
        "  defeated BOOLEAN NOT NULL DEFAULT FALSE,"
        "  defeated_at TIMESTAMP NULL DEFAULT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );

    db->execute(
        "CREATE TABLE IF NOT EXISTS global_boss_contributors ("
        "  boss_id INT NOT NULL,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  mine_commands BIGINT NOT NULL DEFAULT 0,"
        "  ores_mined BIGINT NOT NULL DEFAULT 0,"
        "  fish_commands BIGINT NOT NULL DEFAULT 0,"
        "  fish_caught BIGINT NOT NULL DEFAULT 0,"
        "  fish_profit BIGINT NOT NULL DEFAULT 0,"
        "  gamble_commands BIGINT NOT NULL DEFAULT 0,"
        "  gamble_profit BIGINT NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (boss_id, user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );

    // Migration: add new columns to existing tables (MySQL-compatible)
    db->execute("CALL _add_col_if_missing('global_boss','gamble_commands','BIGINT NOT NULL DEFAULT 0')");
    db->execute("CALL _add_col_if_missing('global_boss','gamble_profit','BIGINT NOT NULL DEFAULT 0')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_mine_commands','BIGINT NOT NULL DEFAULT 1000')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_ores_mined','BIGINT NOT NULL DEFAULT 10000')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_fish_commands','BIGINT NOT NULL DEFAULT 10000')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_fish_caught','BIGINT NOT NULL DEFAULT 100000')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_fish_profit','BIGINT NOT NULL DEFAULT 5000000000000')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_gamble_commands','BIGINT NOT NULL DEFAULT 1500')");
    db->execute("CALL _add_col_if_missing('global_boss','goal_gamble_profit','BIGINT NOT NULL DEFAULT 2000000000000')");
    db->execute("CALL _add_col_if_missing('global_boss','archetype','VARCHAR(64) NOT NULL DEFAULT \'Balanced\'')");
    db->execute("CALL _add_col_if_missing('global_boss_contributors','gamble_commands','BIGINT NOT NULL DEFAULT 0')");
    db->execute("CALL _add_col_if_missing('global_boss_contributors','gamble_profit','BIGINT NOT NULL DEFAULT 0')");
    db->execute("CALL _add_col_if_missing('global_boss','rewards_distributed','BOOLEAN NOT NULL DEFAULT FALSE')");
    // DROP FOREIGN KEY — MySQL-compatible
    db->execute("CALL _drop_fk_if_exists('global_boss_contributors','global_boss_contributors_ibfk_1')");

    created = true;
}

// Get or create the current active boss.  Returns boss_id.
inline int get_or_create_boss(Database* db) {
    ensure_boss_table(db);
    auto conn = db->get_pool()->acquire();
    if (!conn) return -1;

    const char* sel = "SELECT id FROM global_boss WHERE defeated = FALSE ORDER BY id DESC LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return -1;
    }

    int boss_id = -1;
    MYSQL_BIND res[1];
    memset(res, 0, sizeof(res));
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = &boss_id;

    if (mysql_stmt_execute(stmt) == 0) {
        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);
        if (mysql_stmt_fetch(stmt) != 0) boss_id = -1;
    }
    mysql_stmt_close(stmt);

    if (boss_id > 0) {
        db->get_pool()->release(conn);
        return boss_id;
    }

    // Determine next boss number
    const char* count_q = "SELECT COALESCE(MAX(boss_number), 0) FROM global_boss";
    MYSQL_STMT* cs = mysql_stmt_init(conn->get());
    int prev_num = 0;
    if (cs && mysql_stmt_prepare(cs, count_q, strlen(count_q)) == 0) {
        MYSQL_BIND cr[1];
        memset(cr, 0, sizeof(cr));
        cr[0].buffer_type = MYSQL_TYPE_LONG;
        cr[0].buffer = &prev_num;
        if (mysql_stmt_execute(cs) == 0) {
            mysql_stmt_bind_result(cs, cr);
            mysql_stmt_store_result(cs);
            mysql_stmt_fetch(cs);
        }
        mysql_stmt_close(cs);
    } else {
        if (cs) mysql_stmt_close(cs);
    }

    int new_num = prev_num + 1;
    std::string boss_name = BOSS_NAMES[(new_num - 1) % BOSS_NAMES.size()];
    BossGoals goals = generate_boss_goals(new_num);
    const auto& arch = ARCHETYPES[goals.archetype_idx];

    std::string ins = "INSERT INTO global_boss (boss_number, boss_name, archetype, "
                      "goal_mine_commands, goal_ores_mined, goal_fish_commands, goal_fish_caught, "
                      "goal_fish_profit, goal_gamble_commands, goal_gamble_profit) VALUES ("
                      + std::to_string(new_num) + ", '" + boss_name + "', '" + arch.name + "', "
                      + std::to_string(goals.mine_commands) + ", "
                      + std::to_string(goals.ores_mined) + ", "
                      + std::to_string(goals.fish_commands) + ", "
                      + std::to_string(goals.fish_caught) + ", "
                      + std::to_string(goals.fish_profit) + ", "
                      + std::to_string(goals.gamble_commands) + ", "
                      + std::to_string(goals.gamble_profit) + ")";

    if (mysql_query(conn->get(), ins.c_str()) != 0) {
        std::cerr << "[global_boss] failed to create boss: " << mysql_error(conn->get()) << std::endl;
        db->get_pool()->release(conn);
        return -1;
    }
    boss_id = (int)mysql_insert_id(conn->get());
    db->get_pool()->release(conn);

    std::cout << "[global_boss] spawned boss #" << new_num << " \"" << boss_name
              << "\" archetype=" << arch.name << " (id=" << boss_id << ")\n";
    return boss_id;
}

// ── Boss progress struct ────────────────────────────────────────────────────
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

inline BossProgress get_boss_progress(Database* db, int boss_id) {
    BossProgress bp;
    auto conn = db->get_pool()->acquire();
    if (!conn) return bp;

    const char* q = "SELECT id, boss_number, boss_name, archetype, "
                    "mine_commands, ores_mined, fish_commands, fish_caught, fish_profit, "
                    "gamble_commands, gamble_profit, "
                    "goal_mine_commands, goal_ores_mined, goal_fish_commands, goal_fish_caught, "
                    "goal_fish_profit, goal_gamble_commands, goal_gamble_profit, defeated "
                    "FROM global_boss WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return bp;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &boss_id;
    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return bp;
    }

    int r_id = 0, r_num = 0;
    char r_name[129] = {}, r_arch[65] = {};
    unsigned long r_name_len = 0, r_arch_len = 0;
    int64_t r_mc = 0, r_om = 0, r_fc = 0, r_fi = 0, r_fp = 0, r_gc = 0, r_gp = 0;
    int64_t r_gmc = 0, r_gom = 0, r_gfc = 0, r_gfi = 0, r_gfp = 0, r_ggc = 0, r_ggp = 0;
    signed char r_defeated = 0;

    MYSQL_BIND res[19];
    memset(res, 0, sizeof(res));
    int i = 0;
    res[i].buffer_type = MYSQL_TYPE_LONG;     res[i].buffer = &r_id;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONG;     res[i].buffer = &r_num;  i++;
    res[i].buffer_type = MYSQL_TYPE_STRING;   res[i].buffer = r_name;  res[i].buffer_length = 128; res[i].length = &r_name_len; i++;
    res[i].buffer_type = MYSQL_TYPE_STRING;   res[i].buffer = r_arch;  res[i].buffer_length = 64;  res[i].length = &r_arch_len; i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_mc;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_om;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_fc;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_fi;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_fp;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gc;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gp;   i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gmc;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gom;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gfc;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gfi;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_gfp;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_ggc;  i++;
    res[i].buffer_type = MYSQL_TYPE_LONGLONG; res[i].buffer = &r_ggp;  i++;
    res[i].buffer_type = MYSQL_TYPE_TINY;     res[i].buffer = &r_defeated; i++;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    if (mysql_stmt_fetch(stmt) == 0) {
        bp.id = r_id;
        bp.boss_number = r_num;
        bp.boss_name = std::string(r_name, r_name_len);
        bp.archetype = std::string(r_arch, r_arch_len);
        bp.mine_commands = r_mc;       bp.ores_mined = r_om;
        bp.fish_commands = r_fc;       bp.fish_caught = r_fi;
        bp.fish_profit = r_fp;
        bp.gamble_commands = r_gc;     bp.gamble_profit = r_gp;
        bp.goal_mine_commands = r_gmc; bp.goal_ores_mined = r_gom;
        bp.goal_fish_commands = r_gfc; bp.goal_fish_caught = r_gfi;
        bp.goal_fish_profit = r_gfp;
        bp.goal_gamble_commands = r_ggc; bp.goal_gamble_profit = r_ggp;
        bp.defeated = (r_defeated != 0);
    }
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return bp;
}

// ── Increment + defeat check ────────────────────────────────────────────────
inline bool increment_boss_stat(Database* db, int boss_id, uint64_t user_id,
                                const std::string& column, int64_t amount) {
    if (boss_id <= 0 || amount <= 0) return false;
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    std::string upd = "UPDATE global_boss SET " + column + " = " + column + " + " +
                      std::to_string(amount) + " WHERE id = " + std::to_string(boss_id) +
                      " AND defeated = FALSE";
    mysql_query(conn->get(), upd.c_str());

    std::string contrib = "INSERT INTO global_boss_contributors (boss_id, user_id, " + column + ") "
                          "VALUES (" + std::to_string(boss_id) + ", " + std::to_string(user_id) +
                          ", " + std::to_string(amount) + ") "
                          "ON DUPLICATE KEY UPDATE " + column + " = " + column + " + " + std::to_string(amount);
    mysql_query(conn->get(), contrib.c_str());

    // Defeat check: all progress columns >= their goal columns
    std::string check =
        "SELECT 1 FROM global_boss WHERE id = " + std::to_string(boss_id) + " AND defeated = FALSE "
        "AND mine_commands >= goal_mine_commands "
        "AND ores_mined >= goal_ores_mined "
        "AND fish_commands >= goal_fish_commands "
        "AND fish_caught >= goal_fish_caught "
        "AND fish_profit >= goal_fish_profit "
        "AND gamble_commands >= goal_gamble_commands "
        "AND gamble_profit >= goal_gamble_profit";

    bool just_defeated = false;
    if (mysql_query(conn->get(), check.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            just_defeated = (mysql_fetch_row(r) != nullptr);
            mysql_free_result(r);
        }
    }

    if (just_defeated) {
        std::string mark = "UPDATE global_boss SET defeated = TRUE, defeated_at = NOW() "
                           "WHERE id = " + std::to_string(boss_id);
        mysql_query(conn->get(), mark.c_str());
    }

    db->get_pool()->release(conn);

    // Distribute rewards on defeat
    if (just_defeated) {
        distribute_boss_rewards(db, boss_id);
    }

    return just_defeated;
}

// ── Contributor queries ─────────────────────────────────────────────────────
struct ContributorEntry {
    uint64_t user_id;
    int64_t mine_commands, ores_mined;
    int64_t fish_commands, fish_caught, fish_profit;
    int64_t gamble_commands, gamble_profit;
    int64_t total_score;
};

inline std::vector<ContributorEntry> get_top_contributors(Database* db, int boss_id, int limit = 10) {
    std::vector<ContributorEntry> results;
    auto conn = db->get_pool()->acquire();
    if (!conn) return results;

    std::string q = "SELECT user_id, mine_commands, ores_mined, fish_commands, fish_caught, "
                    "fish_profit, gamble_commands, gamble_profit, "
                    "(mine_commands + ores_mined + fish_commands + fish_caught + gamble_commands) as total_score "
                    "FROM global_boss_contributors WHERE boss_id = " + std::to_string(boss_id) +
                    " ORDER BY total_score DESC LIMIT " + std::to_string(limit);

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return results; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return results; }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ContributorEntry e;
        e.user_id         = std::stoull(row[0] ? row[0] : "0");
        e.mine_commands   = std::stoll(row[1] ? row[1] : "0");
        e.ores_mined      = std::stoll(row[2] ? row[2] : "0");
        e.fish_commands   = std::stoll(row[3] ? row[3] : "0");
        e.fish_caught     = std::stoll(row[4] ? row[4] : "0");
        e.fish_profit     = std::stoll(row[5] ? row[5] : "0");
        e.gamble_commands = std::stoll(row[6] ? row[6] : "0");
        e.gamble_profit   = std::stoll(row[7] ? row[7] : "0");
        e.total_score     = std::stoll(row[8] ? row[8] : "0");
        results.push_back(e);
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return results;
}

inline ContributorEntry get_user_contribution(Database* db, int boss_id, uint64_t user_id) {
    ContributorEntry e{};
    e.user_id = user_id;
    auto conn = db->get_pool()->acquire();
    if (!conn) return e;

    std::string q = "SELECT mine_commands, ores_mined, fish_commands, fish_caught, fish_profit, "
                    "gamble_commands, gamble_profit "
                    "FROM global_boss_contributors WHERE boss_id = " + std::to_string(boss_id) +
                    " AND user_id = " + std::to_string(user_id);

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return e; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return e; }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        e.mine_commands   = std::stoll(row[0] ? row[0] : "0");
        e.ores_mined      = std::stoll(row[1] ? row[1] : "0");
        e.fish_commands   = std::stoll(row[2] ? row[2] : "0");
        e.fish_caught     = std::stoll(row[3] ? row[3] : "0");
        e.fish_profit     = std::stoll(row[4] ? row[4] : "0");
        e.gamble_commands = std::stoll(row[5] ? row[5] : "0");
        e.gamble_profit   = std::stoll(row[6] ? row[6] : "0");
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return e;
}

inline int get_defeated_count(Database* db) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    int count = 0;
    if (mysql_query(conn->get(), "SELECT COUNT(*) FROM global_boss WHERE defeated = TRUE") == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) count = std::stoi(row[0]);
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return count;
}

// ── Display helpers ─────────────────────────────────────────────────────────

inline std::string make_progress_bar(int64_t current, int64_t goal, int width = 16) {
    double pct = goal > 0 ? std::min(1.0, (double)current / goal) : 0.0;
    int filled = (int)(pct * width);
    int empty = width - filled;
    std::string bar;
    for (int i = 0; i < filled; i++) bar += "█";
    for (int i = 0; i < empty; i++)  bar += "░";
    return bar + " " + std::to_string((int)(pct * 100)) + "%";
}

inline std::string format_short(int64_t num) {
    if (num >= 1000000000000LL) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1fT", (double)num / 1e12); return buf;
    } else if (num >= 1000000000LL) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1fB", (double)num / 1e9);  return buf;
    } else if (num >= 1000000LL) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1fM", (double)num / 1e6);  return buf;
    } else if (num >= 1000LL) {
        char buf[32]; snprintf(buf, sizeof(buf), "%.1fK", (double)num / 1e3);  return buf;
    }
    return std::to_string(num);
}

inline std::string archetype_tag(const std::string& name) {
    for (auto& a : ARCHETYPES) if (a.name == name) return a.tag;
    return "⚖️ Balanced";
}

// ── Build boss embed (paginated) ────────────────────────────────────────────
// Page 0: Overview — HP, archetype, all progress bars, your contribution
// Page 1: Leaderboard — Top 10 contributors with stats & reward previews
// Page 2: Rewards — Full reward tier breakdown
static constexpr int BOSS_TOTAL_PAGES = 3;

inline dpp::component build_boss_paginator(int boss_id, int page, uint64_t user_id) {
    dpp::component row;
    row.set_type(dpp::cot_action_row);

    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button);
    prev_btn.set_style(dpp::cos_primary);
    prev_btn.set_label("◀");
    prev_btn.set_id("boss_page_" + std::to_string(boss_id) + "_" + std::to_string(page - 1) + "_" + std::to_string(user_id));
    prev_btn.set_disabled(page <= 0);
    row.add_component(prev_btn);

    std::vector<std::string> page_labels = {"Overview", "Leaderboard", "Rewards"};
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button);
    page_btn.set_style(dpp::cos_secondary);
    page_btn.set_label(page_labels[page] + " (" + std::to_string(page + 1) + "/" + std::to_string(BOSS_TOTAL_PAGES) + ")");
    page_btn.set_id("boss_pageinfo_" + std::to_string(boss_id));
    page_btn.set_disabled(true);
    row.add_component(page_btn);

    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button);
    next_btn.set_style(dpp::cos_primary);
    next_btn.set_label("▶");
    next_btn.set_id("boss_page_" + std::to_string(boss_id) + "_" + std::to_string(page + 1) + "_" + std::to_string(user_id));
    next_btn.set_disabled(page >= BOSS_TOTAL_PAGES - 1);
    row.add_component(next_btn);

    return row;
}

inline dpp::message build_boss_page(Database* db, int boss_id, int page, uint64_t requesting_user) {
    BossProgress bp = get_boss_progress(db, boss_id);
    if (bp.id == 0) return dpp::message().add_embed(bronx::error("could not load boss data"));

    std::string emoji = BOSS_EMOJIS[(bp.boss_number - 1) % BOSS_EMOJIS.size()];

    auto pct = [](int64_t cur, int64_t goal) { return goal > 0 ? std::min(1.0, (double)cur / goal) : 1.0; };
    double p_mc = pct(bp.mine_commands, bp.goal_mine_commands);
    double p_om = pct(bp.ores_mined, bp.goal_ores_mined);
    double p_fc = pct(bp.fish_commands, bp.goal_fish_commands);
    double p_fi = pct(bp.fish_caught, bp.goal_fish_caught);
    double p_fp = pct(bp.fish_profit, bp.goal_fish_profit);
    double p_gc = pct(bp.gamble_commands, bp.goal_gamble_commands);
    double p_gp = pct(bp.gamble_profit, bp.goal_gamble_profit);
    double overall = (p_mc + p_om + p_fc + p_fi + p_fp + p_gc + p_gp) / 7.0;
    int overall_pct = (int)(overall * 100);
    int boss_hp = std::max(0, 100 - overall_pct);

    int defeated_count = get_defeated_count(db);
    dpp::embed embed;

    if (page == 0) {
        // ── PAGE 0: OVERVIEW ────────────────────────────────────────────
        if (bp.defeated) {
            embed.set_color(0x2ECC71);
            embed.set_title(emoji + " BOSS DEFEATED: " + bp.boss_name + " " + emoji);
            embed.set_description("**Boss #" + std::to_string(bp.boss_number) + " has been slain!** 🎉\n"
                                  "Rewards have been distributed to all contributors!\n"
                                  "A new boss will spawn when `/boss` is used next.");
        } else {
            embed.set_color(0xE74C3C);
            embed.set_title(emoji + " Global Boss #" + std::to_string(bp.boss_number) + ": " + bp.boss_name);
            std::string atag = archetype_tag(bp.archetype);

            std::string desc;
            desc += "**Boss HP: " + std::to_string(boss_hp) + "%** " + make_progress_bar(overall_pct, 100, 12) + "\n";
            desc += "Type: **" + atag + "**\n\n";

            // Condensed progress — one line per stat
            desc += "⛏️ **Mine Cmds:** " + format_number(bp.mine_commands) + "/" + format_number(bp.goal_mine_commands) + "\n";
            desc += make_progress_bar(bp.mine_commands, bp.goal_mine_commands, 10) + "\n";
            desc += "🪨 **Ores:** " + format_number(bp.ores_mined) + "/" + format_number(bp.goal_ores_mined) + "\n";
            desc += make_progress_bar(bp.ores_mined, bp.goal_ores_mined, 10) + "\n";
            desc += "🎣 **Fish Cmds:** " + format_number(bp.fish_commands) + "/" + format_number(bp.goal_fish_commands) + "\n";
            desc += make_progress_bar(bp.fish_commands, bp.goal_fish_commands, 10) + "\n";
            desc += "🐟 **Fish:** " + format_number(bp.fish_caught) + "/" + format_number(bp.goal_fish_caught) + "\n";
            desc += make_progress_bar(bp.fish_caught, bp.goal_fish_caught, 10) + "\n";
            desc += "🎰 **Gambles:** " + format_number(bp.gamble_commands) + "/" + format_number(bp.goal_gamble_commands) + "\n";
            desc += make_progress_bar(bp.gamble_commands, bp.goal_gamble_commands, 10) + "\n";
            desc += "💵 **Gamble $:** $" + format_short(bp.gamble_profit) + "/$" + format_short(bp.goal_gamble_profit) + "\n";
            desc += make_progress_bar(bp.gamble_profit, bp.goal_gamble_profit, 10) + "\n";
            desc += "💰 **Fish $:** $" + format_short(bp.fish_profit) + "/$" + format_short(bp.goal_fish_profit) + "\n";
            desc += make_progress_bar(bp.fish_profit, bp.goal_fish_profit, 10);

            embed.set_description(desc);
        }

        // Your contribution (compact)
        if (requesting_user != 0 && !bp.defeated) {
            auto c = get_user_contribution(db, boss_id, requesting_user);
            bool has = c.mine_commands || c.ores_mined || c.fish_commands ||
                       c.fish_caught || c.fish_profit || c.gamble_commands || c.gamble_profit;
            if (has) {
                std::string my;
                my += "⛏️ " + format_number(c.mine_commands) + " │ 🪨 " + format_number(c.ores_mined);
                my += " │ 🎣 " + format_number(c.fish_commands) + " │ 🐟 " + format_number(c.fish_caught) + "\n";
                my += "🎰 " + format_number(c.gamble_commands) + " │ 💵 $" + format_short(c.gamble_profit);
                my += " │ 💰 $" + format_short(c.fish_profit);
                embed.add_field("Your Contribution", my, false);
            }
        }
    } else if (page == 1) {
        // ── PAGE 1: LEADERBOARD ─────────────────────────────────────────
        embed.set_color(0xF1C40F);
        embed.set_title("🏆 " + bp.boss_name + " — Leaderboard");

        auto top = get_top_contributors(db, boss_id, 10);
        if (top.empty()) {
            embed.set_description("No contributions yet! Fish, mine, and gamble to damage the boss.");
        } else {
            std::string desc;
            std::vector<std::string> medals = {"🥇", "🥈", "🥉", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣", "🔟"};
            for (size_t i = 0; i < top.size(); i++) {
                auto& e = top[i];
                int rank = (int)(i + 1);
                desc += medals[i] + " <@" + std::to_string(e.user_id) + "> — " + format_number(e.total_score) + " actions\n";
                desc += "   ⛏️ " + format_number(e.mine_commands) + " │ 🪨 " + format_number(e.ores_mined);
                desc += " │ 🎣 " + format_number(e.fish_commands) + " │ 🐟 " + format_number(e.fish_caught) + "\n";
                desc += "   🎁 " + reward_summary(rank) + "\n\n";
            }
            embed.set_description(desc);
        }
    } else if (page == 2) {
        // ── PAGE 2: REWARDS ─────────────────────────────────────────────
        embed.set_color(0x9B59B6);
        embed.set_title("🎁 " + bp.boss_name + " — Rewards");

        std::string desc;
        desc += "Rewards are distributed when the boss is defeated.\n";
        desc += "Your rank is based on total actions (mine + fish + gamble commands, ores, fish caught).\n\n";
        desc += "🥇 **#1:** " + reward_summary(1) + "\n";
        desc += "🥈 **#2:** " + reward_summary(2) + "\n";
        desc += "🥉 **#3:** " + reward_summary(3) + "\n";
        desc += "4️⃣ **#4:** " + reward_summary(4) + "\n";
        desc += "5️⃣ **#5:** " + reward_summary(5) + "\n";
        desc += "📊 **#6-30:** " + reward_summary(6) + "\n";
        desc += "📊 **#31-50:** " + reward_summary(31) + "\n";
        desc += "📊 **#51-100:** " + reward_summary(51) + "\n";
        desc += "📊 **#100+:** " + reward_summary(101) + "\n\n";
        desc += "🪱 = random high-tier bait (rare/epic/legendary)\n";
        desc += "Lootboxes can be opened with `use <lootbox_id>`";
        embed.set_description(desc);
    }

    embed.set_footer(dpp::embed_footer().set_text(
        "Bosses defeated: " + std::to_string(defeated_count) + " │ /boss"
    ));

    dpp::message msg;
    msg.add_embed(embed);
    msg.add_component(build_boss_paginator(boss_id, page, requesting_user));
    return msg;
}

// ── Cached boss ID ──────────────────────────────────────────────────────────

static std::mutex boss_cache_mutex;
static int cached_boss_id = -1;
static std::chrono::steady_clock::time_point cached_boss_time;

inline int get_cached_boss_id(Database* db) {
    std::lock_guard<std::mutex> lock(boss_cache_mutex);
    auto now = std::chrono::steady_clock::now();
    if (cached_boss_id <= 0 || now - cached_boss_time > std::chrono::seconds(60)) {
        cached_boss_id = get_or_create_boss(db);
        cached_boss_time = now;
    }
    return cached_boss_id;
}

inline void invalidate_boss_cache() {
    std::lock_guard<std::mutex> lock(boss_cache_mutex);
    cached_boss_id = -1;
}

// ── Public hooks (called from fish/mine/gamble commands) ────────────────────

inline bool on_fish_command(Database* db, uint64_t user_id, int64_t fish_count) {
    int bid = get_cached_boss_id(db);
    if (bid <= 0) return false;
    bool d1 = increment_boss_stat(db, bid, user_id, "fish_commands", 1);
    bool d2 = increment_boss_stat(db, bid, user_id, "fish_caught", fish_count);
    if (d1 || d2) { invalidate_boss_cache(); return true; }
    return false;
}

inline bool on_fish_profit(Database* db, uint64_t user_id, int64_t profit) {
    if (profit <= 0) return false;
    int bid = get_cached_boss_id(db);
    if (bid <= 0) return false;
    bool d = increment_boss_stat(db, bid, user_id, "fish_profit", profit);
    if (d) { invalidate_boss_cache(); return true; }
    return false;
}

inline bool on_mine_command(Database* db, uint64_t user_id) {
    int bid = get_cached_boss_id(db);
    if (bid <= 0) return false;
    bool d = increment_boss_stat(db, bid, user_id, "mine_commands", 1);
    if (d) { invalidate_boss_cache(); return true; }
    return false;
}

inline bool on_ores_mined(Database* db, uint64_t user_id, int64_t ore_count) {
    int bid = get_cached_boss_id(db);
    if (bid <= 0) return false;
    bool d = increment_boss_stat(db, bid, user_id, "ores_mined", ore_count);
    if (d) { invalidate_boss_cache(); return true; }
    return false;
}

// Call after every gambling game: tracks gamble_commands + gamble_profit (if won)
inline bool on_gamble_command(Database* db, uint64_t user_id, int64_t profit) {
    int bid = get_cached_boss_id(db);
    if (bid <= 0) return false;
    bool d1 = increment_boss_stat(db, bid, user_id, "gamble_commands", 1);
    bool d2 = false;
    if (profit > 0) {
        d2 = increment_boss_stat(db, bid, user_id, "gamble_profit", profit);
    }
    if (d1 || d2) { invalidate_boss_cache(); return true; }
    return false;
}

// ── Command + interaction registration ──────────────────────────────────────

inline Command* get_boss_command(Database* db) {
    static Command* boss = new Command("boss", "view the current global boss and contribute progress", "economy", {"globalboss", "worldboss"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            int boss_id = get_or_create_boss(db);
            if (boss_id <= 0) { bronx::send_message(bot, event, bronx::error("failed to load boss data")); return; }
            invalidate_boss_cache();
            dpp::message msg = build_boss_page(db, boss_id, 0, event.msg.author.id);
            if (!msg.embeds.empty()) bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            bronx::send_message(bot, event, msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            int boss_id = get_or_create_boss(db);
            if (boss_id <= 0) { bronx::safe_slash_reply(bot, event, bronx::error("failed to load boss data")); return; }
            invalidate_boss_cache();
            dpp::message msg = build_boss_page(db, boss_id, 0, event.command.get_issuing_user().id);
            if (!msg.embeds.empty()) bronx::add_invoker_footer(msg.embeds[0], event.command.get_issuing_user());
            bronx::safe_slash_reply(bot, event, msg);
        },
        {}
    );
    return boss;
}

inline void register_boss_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        // Handle boss paginator buttons: boss_page_{boss_id}_{page}_{user_id}
        if (event.custom_id.rfind("boss_page_", 0) != 0) return;
        try {
            // Parse: boss_page_{boss_id}_{page}_{user_id}
            std::string rest = event.custom_id.substr(10); // after "boss_page_"
            auto p1 = rest.find('_');
            if (p1 == std::string::npos) return;
            int boss_id = std::stoi(rest.substr(0, p1));
            std::string rest2 = rest.substr(p1 + 1);
            auto p2 = rest2.find('_');
            if (p2 == std::string::npos) return;
            int page = std::stoi(rest2.substr(0, p2));
            uint64_t owner_id = std::stoull(rest2.substr(p2 + 1));

            // Only the command invoker can use these buttons
            if (event.command.get_issuing_user().id != owner_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("only the command invoker can use these buttons")).set_flags(dpp::m_ephemeral));
                return;
            }

            page = std::max(0, std::min(page, BOSS_TOTAL_PAGES - 1));
            dpp::message msg = build_boss_page(db, boss_id, page, owner_id);
            event.reply(dpp::ir_update_message, msg);
        } catch (...) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("failed to load boss page")).set_flags(dpp::m_ephemeral));
        }
    });
}

} // namespace global_boss

// Public interface
inline std::vector<Command*> get_global_boss_commands(Database* db) {
    return { global_boss::get_boss_command(db) };
}

inline void register_global_boss_interactions(dpp::cluster& bot, Database* db) {
    global_boss::register_boss_interactions(bot, db);
}

} // namespace commands
