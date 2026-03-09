#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../fishing/fishing_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <random>
#include <mutex>
#include <map>

using namespace bronx::db;

namespace commands {
namespace passive {

// ============================================================================
// FISH POND — Passive income from stocking fish
// ============================================================================
// Build a pond (one-time $50,000). Stock it with fish from inventory.
// Every 6 hours it generates coins based on stocked fish rarity.
// Upgrade pond (5 levels) to increase capacity.
//
// Subcommands:
//   /pond build          — build your pond ($50,000)
//   /pond stock <fish>   — move a fish from inventory to pond
//   /pond collect        — collect generated coins
//   /pond upgrade        — upgrade pond capacity
//   /pond view           — see your pond & stocked fish
//   /pond remove <slot>  — remove a fish from the pond
// ============================================================================

// Pond upgrade costs & capacities
struct PondLevel {
    int level;
    int capacity;
    int64_t upgrade_cost;
    std::string name;
};

static const std::vector<PondLevel> pond_levels = {
    {1, 5,   0,        "Tiny Pond"},
    {2, 10,  100000,   "Small Pond"},
    {3, 15,  500000,   "Medium Pond"},
    {4, 25,  2000000,  "Large Pond"},
    {5, 40,  10000000, "Grand Pond"},
};

// Passive income per 6h cycle based on rarity
static int64_t get_rarity_income(const std::string& rarity) {
    if (rarity == "prestige")  return 5000;
    if (rarity == "legendary") return 2000;
    if (rarity == "epic")      return 800;
    if (rarity == "rare")      return 300;
    if (rarity == "uncommon")  return 100;
    return 30; // common
}

static std::string get_rarity_emoji(const std::string& rarity) {
    if (rarity == "prestige")  return "🌟";
    if (rarity == "legendary") return "⭐";
    if (rarity == "epic")      return "💜";
    if (rarity == "rare")      return "💙";
    if (rarity == "uncommon")  return "💚";
    return "⬜";
}

// Table creation (called on first use)
static bool g_pond_tables_created = false;
static std::mutex g_pond_mutex;

static void ensure_pond_tables(Database* db) {
    if (g_pond_tables_created) return;
    std::lock_guard<std::mutex> lock(g_pond_mutex);
    if (g_pond_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS fish_ponds ("
        "  user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "  pond_level INT NOT NULL DEFAULT 1,"
        "  capacity INT NOT NULL DEFAULT 5,"
        "  last_collect TIMESTAMP NULL DEFAULT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_last_collect (last_collect)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS pond_fish ("
        "  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  fish_name VARCHAR(100) NOT NULL,"
        "  fish_emoji VARCHAR(32) NOT NULL DEFAULT '🐟',"
        "  rarity VARCHAR(20) NOT NULL DEFAULT 'common',"
        "  base_value INT NOT NULL DEFAULT 10,"
        "  stocked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_user (user_id),"
        "  FOREIGN KEY (user_id) REFERENCES fish_ponds(user_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_pond_tables_created = true;
}

// ── DB helpers ──────────────────────────────────────────────────────────────

struct PondInfo {
    int pond_level;
    int capacity;
    std::optional<std::chrono::system_clock::time_point> last_collect;
    bool exists;
};

struct PondFish {
    uint64_t id;
    std::string fish_name;
    std::string fish_emoji;
    std::string rarity;
    int base_value;
};

static PondInfo get_pond(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {0, 0, std::nullopt, false};
    
    std::string sql = "SELECT pond_level, capacity, last_collect FROM fish_ponds WHERE user_id = " + std::to_string(user_id);
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                PondInfo info;
                info.exists = true;
                info.pond_level = std::stoi(row[0]);
                info.capacity = std::stoi(row[1]);
                if (row[2]) {
                    struct tm tm = {};
                    strptime(row[2], "%Y-%m-%d %H:%M:%S", &tm);
                    info.last_collect = std::chrono::system_clock::from_time_t(timegm(&tm));
                }
                mysql_free_result(res);
                db->get_pool()->release(conn);
                return info;
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return {0, 0, std::nullopt, false};
}

static std::vector<PondFish> get_pond_fish(Database* db, uint64_t user_id) {
    std::vector<PondFish> fish;
    auto conn = db->get_pool()->acquire();
    if (!conn) return fish;
    
    std::string sql = "SELECT id, fish_name, fish_emoji, rarity, base_value FROM pond_fish WHERE user_id = " + std::to_string(user_id) + " ORDER BY base_value DESC";
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                PondFish f;
                f.id = std::stoull(row[0]);
                f.fish_name = row[1];
                f.fish_emoji = row[2];
                f.rarity = row[3];
                f.base_value = std::stoi(row[4]);
                fish.push_back(f);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return fish;
}

static bool create_pond(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    std::string sql = "INSERT IGNORE INTO fish_ponds (user_id, pond_level, capacity) VALUES (" + std::to_string(user_id) + ", 1, 5)";
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0;
    db->get_pool()->release(conn);
    return ok;
}

static bool upgrade_pond(Database* db, uint64_t user_id, int new_level, int new_capacity) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    std::string sql = "UPDATE fish_ponds SET pond_level = " + std::to_string(new_level) + ", capacity = " + std::to_string(new_capacity) + " WHERE user_id = " + std::to_string(user_id);
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0;
    db->get_pool()->release(conn);
    return ok;
}

static bool add_pond_fish(Database* db, uint64_t user_id, const std::string& name, const std::string& emoji, const std::string& rarity, int base_value) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    // Escape strings
    char esc_name[201], esc_emoji[65], esc_rarity[41];
    mysql_real_escape_string(conn->get(), esc_name, name.c_str(), name.size());
    mysql_real_escape_string(conn->get(), esc_emoji, emoji.c_str(), emoji.size());
    mysql_real_escape_string(conn->get(), esc_rarity, rarity.c_str(), rarity.size());
    
    std::string sql = "INSERT INTO pond_fish (user_id, fish_name, fish_emoji, rarity, base_value) VALUES (" +
        std::to_string(user_id) + ", '" + esc_name + "', '" + esc_emoji + "', '" + esc_rarity + "', " + std::to_string(base_value) + ")";
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0;
    db->get_pool()->release(conn);
    return ok;
}

static bool remove_pond_fish(Database* db, uint64_t user_id, uint64_t fish_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    std::string sql = "DELETE FROM pond_fish WHERE id = " + std::to_string(fish_id) + " AND user_id = " + std::to_string(user_id);
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0 && mysql_affected_rows(conn->get()) > 0;
    db->get_pool()->release(conn);
    return ok;
}

static int64_t collect_pond_income(Database* db, uint64_t user_id) {
    auto fish = get_pond_fish(db, user_id);
    if (fish.empty()) return 0;
    
    int64_t total = 0;
    for (const auto& f : fish) {
        total += get_rarity_income(f.rarity);
    }
    
    // Update last_collect
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    std::string sql = "UPDATE fish_ponds SET last_collect = NOW() WHERE user_id = " + std::to_string(user_id);
    mysql_query(conn->get(), sql.c_str());
    db->get_pool()->release(conn);
    
    return total;
}

static int count_pond_fish(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    std::string sql = "SELECT COUNT(*) FROM pond_fish WHERE user_id = " + std::to_string(user_id);
    int count = 0;
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
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

// ── Build the pond view embed ───────────────────────────────────────────────

static dpp::embed build_pond_embed(Database* db, uint64_t user_id, const dpp::user& user) {
    auto pond = get_pond(db, user_id);
    auto fish = get_pond_fish(db, user_id);
    
    const auto& level_info = pond_levels[pond.pond_level - 1];
    
    int64_t income_per_cycle = 0;
    for (const auto& f : fish) {
        income_per_cycle += get_rarity_income(f.rarity);
    }
    
    dpp::embed embed;
    embed.set_color(0x4FC3F7);
    embed.set_title("🏞️ " + user.username + "'s " + level_info.name);
    
    std::string desc = "**Level " + std::to_string(pond.pond_level) + "/5** • ";
    desc += std::to_string(fish.size()) + "/" + std::to_string(pond.capacity) + " fish stocked\n";
    desc += "💰 **$" + economy::format_number(income_per_cycle) + "** per 6h cycle\n\n";
    
    if (fish.empty()) {
        desc += "*your pond is empty! use `/pond stock` to add fish*";
    } else {
        for (size_t i = 0; i < fish.size(); i++) {
            desc += get_rarity_emoji(fish[i].rarity) + " " + fish[i].fish_emoji + " **" + fish[i].fish_name + "** — $" + economy::format_number(get_rarity_income(fish[i].rarity)) + "/cycle\n";
        }
    }
    
    // Next collect time
    if (pond.last_collect) {
        auto next = *pond.last_collect + std::chrono::hours(6);
        auto now = std::chrono::system_clock::now();
        if (now >= next) {
            desc += "\n✅ **Ready to collect!** Use `/pond collect`";
        } else {
            auto ts = std::chrono::system_clock::to_time_t(next);
            desc += "\n⏰ Next collect: <t:" + std::to_string(ts) + ":R>";
        }
    } else if (!fish.empty()) {
        desc += "\n✅ **Ready to collect!** Use `/pond collect`";
    }
    
    embed.set_description(desc);
    
    // Upgrade info
    if (pond.pond_level < 5) {
        const auto& next_level = pond_levels[pond.pond_level];
        embed.add_field("⬆️ Next Upgrade", "Level " + std::to_string(next_level.level) + " (" + next_level.name + ") — " + std::to_string(next_level.capacity) + " slots — $" + economy::format_number(next_level.upgrade_cost), true);
    }
    
    bronx::add_invoker_footer(embed, user);
    return embed;
}

// ── Command handler ─────────────────────────────────────────────────────────

inline Command* get_pond_command(Database* db) {
    static Command* cmd = new Command(
        "pond",
        "manage your fish pond for passive income",
        "passive",
        {"fishpond", "fp"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_pond_tables(db);
            uint64_t uid = event.msg.author.id;
            db->ensure_user_exists(uid);
            
            std::string sub = args.empty() ? "view" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "build" || sub == "create") {
                auto pond = get_pond(db, uid);
                if (pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you already have a pond! use `b.pond view` to see it"));
                    return;
                }
                int64_t cost = 50000;
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    bronx::send_message(bot, event, bronx::error("you need **$" + economy::format_number(cost) + "** to build a pond (you have $" + economy::format_number(wallet) + ")"));
                    return;
                }
                db->update_wallet(uid, -cost);
                create_pond(db, uid);
                auto embed = bronx::success("you built a **Tiny Pond**! 🏞️\nstock it with fish using `b.pond stock <fish name>`");
                embed.add_field("💰 Cost", "$" + economy::format_number(cost), true);
                embed.add_field("📦 Capacity", "5 fish", true);
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "stock" || sub == "add") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pond! build one with `b.pond build` ($50,000)"));
                    return;
                }
                int current = count_pond_fish(db, uid);
                if (current >= pond.capacity) {
                    bronx::send_message(bot, event, bronx::error("your pond is full! (" + std::to_string(current) + "/" + std::to_string(pond.capacity) + ") upgrade with `b.pond upgrade`"));
                    return;
                }
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify which fish to stock: `b.pond stock <fish name>`"));
                    return;
                }
                // Join remaining args as fish name
                std::string fish_name;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) fish_name += " ";
                    fish_name += args[i];
                }
                std::transform(fish_name.begin(), fish_name.end(), fish_name.begin(), ::tolower);
                
                // Find matching fish in inventory (collectible type)
                auto inventory = db->get_inventory(uid);
                bool found = false;
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    // Check if item name matches (item metadata contains fish name)
                    std::string meta = item.metadata;
                    std::string item_name_lower = item.item_id;
                    std::transform(item_name_lower.begin(), item_name_lower.end(), item_name_lower.begin(), ::tolower);
                    
                    // Parse fish name from metadata
                    size_t name_pos = meta.find("\"name\":\"");
                    std::string stored_name;
                    if (name_pos != std::string::npos) {
                        size_t start = name_pos + 8;
                        size_t end = meta.find("\"", start);
                        stored_name = meta.substr(start, end - start);
                    }
                    std::string stored_lower = stored_name;
                    std::transform(stored_lower.begin(), stored_lower.end(), stored_lower.begin(), ::tolower);
                    
                    if (stored_lower.find(fish_name) != std::string::npos || fish_name.find(stored_lower) != std::string::npos) {
                        // Found a matching fish - determine rarity
                        std::string rarity = "common";
                        std::string emoji = "🐟";
                        for (const auto& ft : fishing::fish_types) {
                            std::string ft_lower = ft.name;
                            std::transform(ft_lower.begin(), ft_lower.end(), ft_lower.begin(), ::tolower);
                            if (ft_lower == stored_lower) {
                                emoji = ft.emoji;
                                rarity = fishing::get_fish_rarity(ft.name);
                                break;
                            }
                        }
                        
                        // Remove from inventory and add to pond  
                        if (db->remove_item(uid, item.item_id, 1)) {
                            int base_val = get_rarity_income(rarity);
                            add_pond_fish(db, uid, stored_name, emoji, rarity, base_val);
                            auto embed = bronx::success(emoji + " **" + stored_name + "** was added to your pond!\n💰 Generates **$" + economy::format_number(base_val) + "** per 6h cycle");
                            bronx::send_message(bot, event, embed);
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    bronx::send_message(bot, event, bronx::error("couldn't find a fish matching \"" + fish_name + "\" in your inventory"));
                }
                
            } else if (sub == "collect" || sub == "claim") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pond! build one with `b.pond build`"));
                    return;
                }
                auto fish = get_pond_fish(db, uid);
                if (fish.empty()) {
                    bronx::send_message(bot, event, bronx::error("your pond is empty! stock it with `b.pond stock <fish>`"));
                    return;
                }
                // Check 6h cooldown
                if (pond.last_collect) {
                    auto next = *pond.last_collect + std::chrono::hours(6);
                    auto now = std::chrono::system_clock::now();
                    if (now < next) {
                        auto ts = std::chrono::system_clock::to_time_t(next);
                        bronx::send_message(bot, event, bronx::error("your pond isn't ready yet! next collect <t:" + std::to_string(ts) + ":R>"));
                        return;
                    }
                }
                int64_t income = collect_pond_income(db, uid);
                db->update_wallet(uid, income);
                auto embed = bronx::success("collected **$" + economy::format_number(income) + "** from your pond! 🏞️\n*" + std::to_string(fish.size()) + " fish produced income*");
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "upgrade") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pond! build one with `b.pond build`"));
                    return;
                }
                if (pond.pond_level >= 5) {
                    bronx::send_message(bot, event, bronx::error("your pond is already max level!"));
                    return;
                }
                const auto& next = pond_levels[pond.pond_level]; // 0-indexed, current level as index = next level
                int64_t wallet = db->get_wallet(uid);
                if (wallet < next.upgrade_cost) {
                    bronx::send_message(bot, event, bronx::error("you need **$" + economy::format_number(next.upgrade_cost) + "** to upgrade (you have $" + economy::format_number(wallet) + ")"));
                    return;
                }
                db->update_wallet(uid, -next.upgrade_cost);
                upgrade_pond(db, uid, next.level, next.capacity);
                auto embed = bronx::success("pond upgraded to **" + next.name + "**! 🏞️\n📦 Capacity: **" + std::to_string(next.capacity) + " fish**");
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "remove") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pond!"));
                    return;
                }
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify which slot to remove: `b.pond remove <number>`"));
                    return;
                }
                int slot;
                try { slot = std::stoi(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid slot number"));
                    return;
                }
                auto fish = get_pond_fish(db, uid);
                if (slot < 1 || slot > (int)fish.size()) {
                    bronx::send_message(bot, event, bronx::error("invalid slot (1-" + std::to_string(fish.size()) + ")"));
                    return;
                }
                auto& f = fish[slot - 1];
                remove_pond_fish(db, uid, f.id);
                // Note: fish is consumed, not returned to inventory
                auto embed = bronx::success(f.fish_emoji + " **" + f.fish_name + "** was released from your pond");
                bronx::send_message(bot, event, embed);
                
            } else { // view
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pond! build one with `b.pond build` ($50,000)"));
                    return;
                }
                auto embed = build_pond_embed(db, uid, event.msg.author);
                bronx::send_message(bot, event, embed);
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_pond_tables(db);
            uint64_t uid = event.command.get_issuing_user().id;
            db->ensure_user_exists(uid);
            
            std::string sub = "view";
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) {
                sub = ci_options[0].name;
            }
            
            if (sub == "build") {
                auto pond = get_pond(db, uid);
                if (pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you already have a pond!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int64_t cost = 50000;
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    event.reply(dpp::message().add_embed(bronx::error("you need **$" + economy::format_number(cost) + "** to build a pond")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -cost);
                create_pond(db, uid);
                auto embed = bronx::success("you built a **Tiny Pond**! 🏞️\nstock it with fish using `/pond stock`");
                embed.add_field("💰 Cost", "$" + economy::format_number(cost), true);
                embed.add_field("📦 Capacity", "5 fish", true);
                event.reply(dpp::message().add_embed(embed));
                
            } else if (sub == "stock") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have a pond! build one with `/pond build`")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int current = count_pond_fish(db, uid);
                if (current >= pond.capacity) {
                    event.reply(dpp::message().add_embed(bronx::error("your pond is full! upgrade with `/pond upgrade`")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string fish_name = std::get<std::string>(ci_options[0].options[0].value);
                std::transform(fish_name.begin(), fish_name.end(), fish_name.begin(), ::tolower);
                
                auto inventory = db->get_inventory(uid);
                bool found = false;
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    std::string meta = item.metadata;
                    size_t name_pos = meta.find("\"name\":\"");
                    std::string stored_name;
                    if (name_pos != std::string::npos) {
                        size_t start = name_pos + 8;
                        size_t end = meta.find("\"", start);
                        stored_name = meta.substr(start, end - start);
                    }
                    std::string stored_lower = stored_name;
                    std::transform(stored_lower.begin(), stored_lower.end(), stored_lower.begin(), ::tolower);
                    
                    if (stored_lower.find(fish_name) != std::string::npos || fish_name.find(stored_lower) != std::string::npos) {
                        std::string rarity = "common";
                        std::string emoji = "🐟";
                        for (const auto& ft : fishing::fish_types) {
                            std::string ft_lower = ft.name;
                            std::transform(ft_lower.begin(), ft_lower.end(), ft_lower.begin(), ::tolower);
                            if (ft_lower == stored_lower) {
                                emoji = ft.emoji;
                                rarity = fishing::get_fish_rarity(ft.name);
                                break;
                            }
                        }
                        if (db->remove_item(uid, item.item_id, 1)) {
                            int base_val = get_rarity_income(rarity);
                            add_pond_fish(db, uid, stored_name, emoji, rarity, base_val);
                            auto embed = bronx::success(emoji + " **" + stored_name + "** was added to your pond!\n💰 Generates **$" + economy::format_number(base_val) + "** per 6h cycle");
                            event.reply(dpp::message().add_embed(embed));
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    event.reply(dpp::message().add_embed(bronx::error("couldn't find a fish matching that name in your inventory")).set_flags(dpp::m_ephemeral));
                }
                
            } else if (sub == "collect") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have a pond!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                auto fish = get_pond_fish(db, uid);
                if (fish.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("your pond is empty!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (pond.last_collect) {
                    auto next = *pond.last_collect + std::chrono::hours(6);
                    auto now = std::chrono::system_clock::now();
                    if (now < next) {
                        auto ts = std::chrono::system_clock::to_time_t(next);
                        event.reply(dpp::message().add_embed(bronx::error("not ready yet! next collect <t:" + std::to_string(ts) + ":R>")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                int64_t income = collect_pond_income(db, uid);
                db->update_wallet(uid, income);
                auto embed = bronx::success("collected **$" + economy::format_number(income) + "** from your pond! 🏞️");
                event.reply(dpp::message().add_embed(embed));
                
            } else if (sub == "upgrade") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have a pond!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (pond.pond_level >= 5) {
                    event.reply(dpp::message().add_embed(bronx::error("already max level!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                const auto& next = pond_levels[pond.pond_level];
                int64_t wallet = db->get_wallet(uid);
                if (wallet < next.upgrade_cost) {
                    event.reply(dpp::message().add_embed(bronx::error("need **$" + economy::format_number(next.upgrade_cost) + "** (have $" + economy::format_number(wallet) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -next.upgrade_cost);
                upgrade_pond(db, uid, next.level, next.capacity);
                auto embed = bronx::success("pond upgraded to **" + next.name + "**! 🏞️\n📦 Capacity: **" + std::to_string(next.capacity) + " fish**");
                event.reply(dpp::message().add_embed(embed));
                
            } else if (sub == "remove") {
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have a pond!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int slot = static_cast<int>(std::get<int64_t>(ci_options[0].options[0].value));
                auto fish = get_pond_fish(db, uid);
                if (slot < 1 || slot > (int)fish.size()) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid slot")).set_flags(dpp::m_ephemeral));
                    return;
                }
                auto& f = fish[slot - 1];
                remove_pond_fish(db, uid, f.id);
                event.reply(dpp::message().add_embed(bronx::success(f.fish_emoji + " **" + f.fish_name + "** released from pond")));
                
            } else { // view
                auto pond = get_pond(db, uid);
                if (!pond.exists) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have a pond! build one with `/pond build` ($50,000)")).set_flags(dpp::m_ephemeral));
                    return;
                }
                event.reply(dpp::message().add_embed(build_pond_embed(db, uid, event.command.get_issuing_user())));
            }
        },
        // slash options
        {
            dpp::command_option(dpp::co_sub_command, "build", "build a fish pond ($50,000)"),
            dpp::command_option(dpp::co_sub_command, "stock", "stock a fish from your inventory")
                .add_option(dpp::command_option(dpp::co_string, "fish", "name of the fish to stock", true)),
            dpp::command_option(dpp::co_sub_command, "collect", "collect passive income from your pond"),
            dpp::command_option(dpp::co_sub_command, "upgrade", "upgrade your pond capacity"),
            dpp::command_option(dpp::co_sub_command, "view", "view your fish pond"),
            dpp::command_option(dpp::co_sub_command, "remove", "remove a fish from your pond")
                .add_option(dpp::command_option(dpp::co_integer, "slot", "slot number to remove", true)),
        }
    );
    return cmd;
}

} // namespace passive
} // namespace commands
