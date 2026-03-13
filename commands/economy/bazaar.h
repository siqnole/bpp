#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/core/connection_pool.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <mariadb/mysql.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <sstream>

using namespace bronx::db;
using namespace bronx::db::server_economy_operations;

namespace commands {
namespace bazaar {

// ============================================================================
// P2P BAZAAR  ─  Open marketplace for item listings
// ============================================================================
// Flow:
//   sell   → item removed from seller inventory (escrowed), listing created
//   buy    → coins deducted from buyer, item given, seller receives coins - 5% tax
//   cancel → item returned to seller
//   expire → items auto-returned after 24h
//
// Subcommands:
//   bazaar              — show help / browse featured
//   bazaar sell <item> <qty> <price>
//   bazaar buy <id>
//   bazaar cancel <id>
//   bazaar my            — view your active listings
//   bazaar search <term> — find listings by item name
//   bazaar browse [page] — browse all active listings
// ============================================================================

static constexpr double BAZAAR_TAX_RATE = 0.05;  // 5% tax on sales
static constexpr int BAZAAR_MAX_LISTINGS = 10;    // max active per user
static constexpr int BAZAAR_EXPIRY_HOURS = 24;
static constexpr int64_t BAZAAR_MAX_PRICE = 999999999;   // 999M cap
static constexpr int BAZAAR_PAGE_SIZE = 10;

static bool bazaar_tables_created = false;

inline void ensure_bazaar_tables(Database* db) {
    if (bazaar_tables_created) return;
    db->execute(
        "CREATE TABLE IF NOT EXISTS bazaar_listings ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  seller_id BIGINT UNSIGNED NOT NULL,"
        "  guild_id BIGINT UNSIGNED DEFAULT NULL,"
        "  item_id VARCHAR(100) NOT NULL,"
        "  item_type VARCHAR(50) NOT NULL DEFAULT 'other',"
        "  item_name VARCHAR(200) NOT NULL DEFAULT '',"
        "  item_metadata TEXT,"
        "  item_level INT NOT NULL DEFAULT 1,"
        "  quantity INT NOT NULL DEFAULT 1,"
        "  price_per_unit BIGINT NOT NULL,"
        "  total_price BIGINT NOT NULL,"
        "  status ENUM('active','sold','cancelled','expired') DEFAULT 'active',"
        "  buyer_id BIGINT UNSIGNED DEFAULT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL DEFAULT '2099-01-01 00:00:00',"
        "  sold_at TIMESTAMP NULL,"
        "  INDEX idx_guild_status (guild_id, status),"
        "  INDEX idx_seller (seller_id, status),"
        "  INDEX idx_item (item_id, status),"
        "  INDEX idx_expires (status, expires_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    bazaar_tables_created = true;
}

// Expire old listings and return items to sellers
inline void expire_old_listings(Database* db) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return;

    std::string q =
        "SELECT id, seller_id, item_id, item_type, quantity, item_metadata, item_level "
        "FROM bazaar_listings WHERE status='active' AND expires_at < NOW() LIMIT 50";
    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return;

    struct ExpiredListing {
        int64_t id; uint64_t seller;
        std::string item_id, item_type, metadata;
        int quantity, level;
    };
    std::vector<ExpiredListing> expired;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        expired.push_back({
            row[0] ? std::stoll(row[0]) : 0,
            row[1] ? std::stoull(row[1]) : 0,
            row[2] ? row[2] : "",
            row[3] ? row[3] : "other",
            row[5] ? row[5] : "{}",
            row[4] ? std::stoi(row[4]) : 1,
            row[6] ? std::stoi(row[6]) : 1,
        });
    }
    mysql_free_result(res);

    for (auto& l : expired) {
        if (l.seller && !l.item_id.empty())
            db->add_item(l.seller, l.item_id, l.item_type, l.quantity, l.metadata, l.level);
        auto c2 = db->get_pool()->acquire();
        if (c2) {
            mysql_query(c2->get(), ("UPDATE bazaar_listings SET status='expired' WHERE id=" + std::to_string(l.id)).c_str());
            db->get_pool()->release(c2);
        }
    }
}

// Safe escaping
inline std::string bz_esc(const std::string& s) {
    std::string r; r.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\'') r += "\\'";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

// Human-readable item name from item_id
inline std::string item_display_name(const std::string& item_id) {
    auto pos = item_id.find('_');
    if (pos == std::string::npos) return item_id;
    std::string prefix = item_id.substr(0, pos);
    std::string suffix = item_id.substr(pos + 1);
    for (size_t i = 0; i < suffix.size(); i++)
        if (i == 0 || suffix[i-1] == '_') suffix[i] = (char)toupper((unsigned char)suffix[i]);
    std::replace(suffix.begin(), suffix.end(), '_', ' ');
    if (prefix == "rod")      return suffix + " Rod";
    if (prefix == "bait")     return suffix + " Bait";
    if (prefix == "lootbox")  return suffix + " Lootbox";
    if (prefix == "pickaxe")  return suffix + " Pickaxe";
    if (prefix == "minecart") return suffix + " Minecart";
    return item_id;
}

// Count active listings for a user in a scope
inline int count_active_listings(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    std::string q = "SELECT COUNT(*) FROM bazaar_listings WHERE seller_id=" + std::to_string(user_id) +
        " AND status='active'";
    if (guild_id) q += " AND guild_id=" + std::to_string(*guild_id);
    else q += " AND guild_id IS NULL";

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return 0; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int count = row && row[0] ? std::stoi(row[0]) : 0;
    mysql_free_result(res);
    return count;
}

// ── SELL ─────────────────────────────────────────────────────────────────────
inline void handle_sell(dpp::cluster& bot, const dpp::message_create_t& event,
                        const std::vector<std::string>& args, Database* db,
                        std::optional<uint64_t> guild_id) {
    uint64_t uid = event.msg.author.id;

    if (args.size() < 4) {
        bronx::send_message(bot, event, bronx::error(
            "usage: `bazaar sell <item_id> <quantity> <price_per_unit>`\n"
            "example: `bazaar sell rod_gold 1 5000`"
        ));
        return;
    }

    std::string item_id = args[1];
    std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

    int qty = 1;
    try { qty = std::stoi(args[2]); } catch (...) {}
    if (qty < 1 || qty > 9999) {
        bronx::send_message(bot, event, bronx::error("quantity must be between 1 and 9999"));
        return;
    }

    int64_t price_per = 0;
    try {
        auto me = db->get_user(uid);
        price_per = parse_amount(args[3], me ? me->wallet : 0);
    } catch (...) {}
    if (price_per < 1 || price_per > BAZAAR_MAX_PRICE) {
        bronx::send_message(bot, event, bronx::error("price must be between $1 and $" + format_number(BAZAAR_MAX_PRICE)));
        return;
    }

    // Check listing limit
    int active = count_active_listings(db, uid, guild_id);
    if (active >= BAZAAR_MAX_LISTINGS) {
        bronx::send_message(bot, event, bronx::error(
            "you already have **" + std::to_string(active) + "/" + std::to_string(BAZAAR_MAX_LISTINGS) +
            "** active listings. cancel some first."
        ));
        return;
    }

    // Verify item ownership
    auto inv = db->get_inventory(uid);
    InventoryItem* item = nullptr;
    for (auto& it : inv) {
        if (it.item_id == item_id) { item = &it; break; }
    }
    if (!item || item->quantity < qty) {
        bronx::send_message(bot, event, bronx::error(
            "you don't have **" + std::to_string(qty) + "x " + item_display_name(item_id) + "**"
        ));
        return;
    }

    // Escrow: remove from inventory
    if (!db->remove_item(uid, item_id, qty)) {
        bronx::send_message(bot, event, bronx::error("failed to escrow item — try again"));
        return;
    }

    // Create listing
    int64_t total = price_per * qty;
    std::string display = item_display_name(item_id);
    auto conn = db->get_pool()->acquire();
    if (!conn) {
        db->add_item(uid, item_id, item->item_type, qty, item->metadata, item->level);
        bronx::send_message(bot, event, bronx::error("database error"));
        return;
    }

    std::string guild_val = guild_id ? std::to_string(*guild_id) : "NULL";
    std::string ins =
        "INSERT INTO bazaar_listings (seller_id, guild_id, item_id, item_type, item_name, "
        "item_metadata, item_level, quantity, price_per_unit, total_price, expires_at) VALUES ("
        + std::to_string(uid) + "," + guild_val + ",'"
        + bz_esc(item_id) + "','" + bz_esc(item->item_type) + "','" + bz_esc(display) + "','"
        + bz_esc(item->metadata.empty() ? "{}" : item->metadata) + "',"
        + std::to_string(item->level) + "," + std::to_string(qty) + ","
        + std::to_string(price_per) + "," + std::to_string(total) + ","
        "DATE_ADD(NOW(), INTERVAL " + std::to_string(BAZAAR_EXPIRY_HOURS) + " HOUR))";

    if (mysql_query(conn->get(), ins.c_str()) != 0) {
        db->get_pool()->release(conn);
        db->add_item(uid, item_id, item->item_type, qty, item->metadata, item->level);
        bronx::send_message(bot, event, bronx::error("failed to create listing"));
        return;
    }
    int64_t listing_id = (int64_t)mysql_insert_id(conn->get());
    db->get_pool()->release(conn);

    auto embed = bronx::success(
        "**🏪 Listed on Bazaar!**\n\n"
        "**Listing #" + std::to_string(listing_id) + "**\n"
        "📦 **" + std::to_string(qty) + "x** " + display + "\n"
        "💰 **$" + format_number(price_per) + "** per unit"
        + (qty > 1 ? " (**$" + format_number(total) + "** total)" : "") + "\n"
        "⏳ Expires in " + std::to_string(BAZAAR_EXPIRY_HOURS) + " hours\n\n"
        "*5% tax deducted from sale proceeds*"
    );
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

// ── BUY ──────────────────────────────────────────────────────────────────────
inline void handle_buy(dpp::cluster& bot, const dpp::message_create_t& event,
                       const std::vector<std::string>& args, Database* db,
                       std::optional<uint64_t> guild_id) {
    uint64_t uid = event.msg.author.id;

    if (args.size() < 2) {
        bronx::send_message(bot, event, bronx::error("usage: `bazaar buy <listing_id> [quantity]`"));
        return;
    }

    int64_t listing_id = 0;
    try { listing_id = std::stoll(args[1]); } catch (...) {}
    if (listing_id <= 0) {
        bronx::send_message(bot, event, bronx::error("invalid listing ID"));
        return;
    }

    int buy_qty = -1; // -1 = buy all
    if (args.size() >= 3) {
        try { buy_qty = std::stoi(args[2]); } catch (...) {}
        if (buy_qty < 1) { bronx::send_message(bot, event, bronx::error("invalid quantity")); return; }
    }

    // Fetch listing
    auto conn = db->get_pool()->acquire();
    if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

    std::string q = "SELECT id, seller_id, guild_id, item_id, item_type, item_name, item_metadata, "
        "item_level, quantity, price_per_unit FROM bazaar_listings WHERE id=" + std::to_string(listing_id) +
        " AND status='active' FOR UPDATE";
    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("database error")); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("database error")); return; }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("listing **#" + std::to_string(listing_id) + "** not found or no longer active"));
        return;
    }

    uint64_t seller_id = row[1] ? std::stoull(row[1]) : 0;
    uint64_t list_guild = row[2] ? std::stoull(row[2]) : 0;
    std::string item_id = row[3] ? row[3] : "";
    std::string item_type = row[4] ? row[4] : "other";
    std::string item_name = row[5] ? row[5] : item_display_name(item_id);
    std::string item_meta = row[6] ? row[6] : "{}";
    int item_level = row[7] ? std::stoi(row[7]) : 1;
    int available_qty = row[8] ? std::stoi(row[8]) : 0;
    int64_t price_per = row[9] ? std::stoll(row[9]) : 0;
    mysql_free_result(res);

    // Scope check — if listing has a guild_id, buyer must be in same guild
    if (list_guild && guild_id && list_guild != *guild_id) {
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("this listing is from a different server"));
        return;
    }

    if (seller_id == uid) {
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("you can't buy your own listing! use `bazaar cancel " + std::to_string(listing_id) + "` instead"));
        return;
    }

    int actual_qty = (buy_qty < 0 || buy_qty > available_qty) ? available_qty : buy_qty;
    int64_t total_cost = price_per * actual_qty;

    // Check buyer funds
    int64_t wallet = get_wallet_unified(db, uid, guild_id);
    if (wallet < total_cost) {
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error(
            "you need **$" + format_number(total_cost) + "** but only have **$" + format_number(wallet) + "**"
        ));
        return;
    }

    // Execute transaction
    int remaining = available_qty - actual_qty;
    std::string status_update = remaining > 0
        ? "UPDATE bazaar_listings SET quantity=" + std::to_string(remaining) +
          ", total_price=" + std::to_string(price_per * remaining) + " WHERE id=" + std::to_string(listing_id)
        : "UPDATE bazaar_listings SET status='sold', buyer_id=" + std::to_string(uid) +
          ", sold_at=NOW() WHERE id=" + std::to_string(listing_id);

    if (mysql_query(conn->get(), status_update.c_str()) != 0) {
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("transaction failed"));
        return;
    }
    db->get_pool()->release(conn);

    // Deduct from buyer
    update_wallet_unified(db, uid, guild_id, -total_cost);

    // Give item to buyer
    db->add_item(uid, item_id, item_type, actual_qty, item_meta, item_level);

    // Pay seller (minus tax)
    int64_t tax = (int64_t)(total_cost * BAZAAR_TAX_RATE);
    int64_t seller_gets = total_cost - tax;
    update_wallet_unified(db, seller_id, guild_id, seller_gets);

    auto embed = bronx::success(
        "**🛒 Purchase Complete!**\n\n"
        "**Listing #" + std::to_string(listing_id) + "**\n"
        "📦 Bought **" + std::to_string(actual_qty) + "x** " + item_name + "\n"
        "💰 Paid **$" + format_number(total_cost) + "**\n"
        "💸 Tax: **$" + format_number(tax) + "** (5%)\n"
        "👤 Seller <@" + std::to_string(seller_id) + "> receives **$" + format_number(seller_gets) + "**"
        + (remaining > 0 ? "\n\n*" + std::to_string(remaining) + " remaining in this listing*" : "")
    );
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

// ── CANCEL ───────────────────────────────────────────────────────────────────
inline void handle_cancel(dpp::cluster& bot, const dpp::message_create_t& event,
                          const std::vector<std::string>& args, Database* db) {
    uint64_t uid = event.msg.author.id;

    if (args.size() < 2) {
        bronx::send_message(bot, event, bronx::error("usage: `bazaar cancel <listing_id>`"));
        return;
    }

    int64_t listing_id = 0;
    try { listing_id = std::stoll(args[1]); } catch (...) {}
    if (listing_id <= 0) { bronx::send_message(bot, event, bronx::error("invalid listing ID")); return; }

    auto conn = db->get_pool()->acquire();
    if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

    std::string q = "SELECT seller_id, item_id, item_type, quantity, item_metadata, item_level, item_name "
        "FROM bazaar_listings WHERE id=" + std::to_string(listing_id) + " AND status='active'";
    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return; }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("listing **#" + std::to_string(listing_id) + "** not found or no longer active"));
        return;
    }

    uint64_t seller = row[0] ? std::stoull(row[0]) : 0;
    if (seller != uid) {
        mysql_free_result(res);
        db->get_pool()->release(conn);
        bronx::send_message(bot, event, bronx::error("that's not your listing"));
        return;
    }

    std::string item_id = row[1] ? row[1] : "";
    std::string item_type = row[2] ? row[2] : "other";
    int qty = row[3] ? std::stoi(row[3]) : 1;
    std::string meta = row[4] ? row[4] : "{}";
    int level = row[5] ? std::stoi(row[5]) : 1;
    std::string name = row[6] ? row[6] : item_display_name(item_id);
    mysql_free_result(res);

    // Cancel listing
    mysql_query(conn->get(), ("UPDATE bazaar_listings SET status='cancelled' WHERE id=" + std::to_string(listing_id)).c_str());
    db->get_pool()->release(conn);

    // Return item
    db->add_item(uid, item_id, item_type, qty, meta, level);

    auto embed = bronx::success(
        "**🔄 Listing Cancelled**\n\n"
        "**#" + std::to_string(listing_id) + "** — " + std::to_string(qty) + "x " + name + "\n"
        "Items returned to your inventory."
    );
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

// ── MY LISTINGS ──────────────────────────────────────────────────────────────
inline void handle_my(dpp::cluster& bot, const dpp::message_create_t& event,
                      Database* db, std::optional<uint64_t> guild_id) {
    uint64_t uid = event.msg.author.id;
    auto conn = db->get_pool()->acquire();
    if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

    std::string q = "SELECT id, item_name, quantity, price_per_unit, total_price, "
        "TIMESTAMPDIFF(MINUTE, NOW(), expires_at) as mins_left "
        "FROM bazaar_listings WHERE seller_id=" + std::to_string(uid) + " AND status='active'";
    if (guild_id) q += " AND guild_id=" + std::to_string(*guild_id);
    q += " ORDER BY created_at DESC LIMIT 15";

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return;

    std::string desc;
    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        count++;
        std::string id = row[0] ? row[0] : "?";
        std::string name = row[1] ? row[1] : "?";
        int qty = row[2] ? std::stoi(row[2]) : 0;
        int64_t ppu = row[3] ? std::stoll(row[3]) : 0;
        int64_t total = row[4] ? std::stoll(row[4]) : 0;
        int mins = row[5] ? std::stoi(row[5]) : 0;

        std::string time_left = mins > 60 ? std::to_string(mins / 60) + "h" : std::to_string(std::max(0, mins)) + "m";
        desc += "`#" + id + "` **" + std::to_string(qty) + "x** " + name +
            " — $" + format_number(ppu) + "/ea ($" + format_number(total) + " total) — ⏳ " + time_left + "\n";
    }
    mysql_free_result(res);

    if (count == 0) {
        bronx::send_message(bot, event, bronx::info("**📋 My Listings**\n\nYou have no active listings.\nUse `bazaar sell <item> <qty> <price>` to list something!"));
        return;
    }

    auto embed = bronx::info("**📋 My Listings** (" + std::to_string(count) + "/" + std::to_string(BAZAAR_MAX_LISTINGS) + ")\n\n" + desc +
        "\n*cancel with* `bazaar cancel <id>`");
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

// ── BROWSE ───────────────────────────────────────────────────────────────────
inline void handle_browse(dpp::cluster& bot, const dpp::message_create_t& event,
                          const std::vector<std::string>& args, Database* db,
                          std::optional<uint64_t> guild_id) {
    int page = 1;
    if (args.size() >= 2) {
        try { page = std::stoi(args[1]); } catch (...) {}
        if (page < 1) page = 1;
    }
    int offset = (page - 1) * BAZAAR_PAGE_SIZE;

    auto conn = db->get_pool()->acquire();
    if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

    // Count total
    std::string count_q = "SELECT COUNT(*) FROM bazaar_listings WHERE status='active'";
    if (guild_id) count_q += " AND guild_id=" + std::to_string(*guild_id);
    else count_q += " AND guild_id IS NULL";

    int total = 0;
    if (mysql_query(conn->get(), count_q.c_str()) == 0) {
        MYSQL_RES* cr = mysql_store_result(conn->get());
        if (cr) {
            MYSQL_ROW r = mysql_fetch_row(cr);
            if (r && r[0]) total = std::stoi(r[0]);
            mysql_free_result(cr);
        }
    }

    std::string q = "SELECT id, seller_id, item_name, quantity, price_per_unit, total_price "
        "FROM bazaar_listings WHERE status='active'";
    if (guild_id) q += " AND guild_id=" + std::to_string(*guild_id);
    else q += " AND guild_id IS NULL";
    q += " ORDER BY created_at DESC LIMIT " + std::to_string(BAZAAR_PAGE_SIZE) +
         " OFFSET " + std::to_string(offset);

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return;

    std::string desc;
    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        count++;
        std::string id_str = row[0] ? row[0] : "?";
        std::string seller = row[1] ? row[1] : "?";
        std::string name = row[2] ? row[2] : "?";
        int qty = row[3] ? std::stoi(row[3]) : 0;
        int64_t ppu = row[4] ? std::stoll(row[4]) : 0;
        int64_t total_p = row[5] ? std::stoll(row[5]) : 0;

        desc += "`#" + id_str + "` **" + std::to_string(qty) + "x** " + name +
            " — $" + format_number(ppu) + "/ea" +
            (qty > 1 ? " ($" + format_number(total_p) + ")" : "") +
            " — by <@" + seller + ">\n";
    }
    mysql_free_result(res);

    if (count == 0) {
        bronx::send_message(bot, event, bronx::info("**🏪 Bazaar**\n\nNo active listings. Be the first to sell something!\n`bazaar sell <item> <qty> <price>`"));
        return;
    }

    int total_pages = (total + BAZAAR_PAGE_SIZE - 1) / BAZAAR_PAGE_SIZE;
    auto embed = bronx::info(
        "**🏪 Bazaar** — page " + std::to_string(page) + "/" + std::to_string(total_pages) +
        " (" + std::to_string(total) + " listings)\n\n" + desc +
        "\n*buy with* `bazaar buy <id>` — *next page:* `bazaar browse " + std::to_string(page + 1) + "`"
    );
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

// ── SEARCH ───────────────────────────────────────────────────────────────────
inline void handle_search(dpp::cluster& bot, const dpp::message_create_t& event,
                          const std::vector<std::string>& args, Database* db,
                          std::optional<uint64_t> guild_id) {
    if (args.size() < 2) {
        bronx::send_message(bot, event, bronx::error("usage: `bazaar search <item name or id>`"));
        return;
    }

    // Combine remaining args as search term
    std::string term;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) term += " ";
        term += args[i];
    }
    std::transform(term.begin(), term.end(), term.begin(), ::tolower);

    auto conn = db->get_pool()->acquire();
    if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

    std::string esc_term = bz_esc(term);
    std::string q = "SELECT id, seller_id, item_name, item_id, quantity, price_per_unit, total_price "
        "FROM bazaar_listings WHERE status='active' AND (LOWER(item_name) LIKE '%" + esc_term + "%' "
        "OR LOWER(item_id) LIKE '%" + esc_term + "%')";
    if (guild_id) q += " AND guild_id=" + std::to_string(*guild_id);
    else q += " AND guild_id IS NULL";
    q += " ORDER BY price_per_unit ASC LIMIT 15";

    if (mysql_query(conn->get(), q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return;

    std::string desc;
    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        count++;
        std::string id_str = row[0] ? row[0] : "?";
        std::string seller = row[1] ? row[1] : "?";
        std::string name = row[2] ? row[2] : (row[3] ? row[3] : "?");
        int qty = row[4] ? std::stoi(row[4]) : 0;
        int64_t ppu = row[5] ? std::stoll(row[5]) : 0;

        desc += "`#" + id_str + "` **" + std::to_string(qty) + "x** " + name +
            " — **$" + format_number(ppu) + "**/ea — <@" + seller + ">\n";
    }
    mysql_free_result(res);

    if (count == 0) {
        bronx::send_message(bot, event, bronx::info("**🔍 Search:** `" + term + "`\n\nNo listings found matching your search."));
        return;
    }

    auto embed = bronx::info("**🔍 Search:** `" + term + "` — " + std::to_string(count) + " result(s)\n\n" + desc +
        "\n*sorted by lowest price — buy with* `bazaar buy <id>`");
    bronx::add_invoker_footer(embed, event.msg.author);
    bronx::send_message(bot, event, embed);
}

} // namespace bazaar

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC COMMAND BUILDER
// ─────────────────────────────────────────────────────────────────────────────
::std::vector<Command*> get_bazaar_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    bazaar::ensure_bazaar_tables(db);

    static Command* bz_cmd = new Command(
        "bazaar",
        "open P2P marketplace — buy and sell items with other players",
        "economy",
        {"bz", "market"},
        false,
        // ── TEXT HANDLER ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            bazaar::expire_old_listings(db);

            std::optional<uint64_t> guild_id;
            if (event.msg.guild_id) guild_id = event.msg.guild_id;
            bool server_mode = guild_id && is_server_economy(db, *guild_id);

            // In global mode, listings are guild-scoped to NULL
            std::optional<uint64_t> scope = server_mode ? guild_id : std::nullopt;

            if (args.empty()) {
                auto embed = bronx::info(
                    "**🏪 Bazaar — P2P Marketplace**\n\n"
                    "Buy and sell items with other players!\n\n"
                    "**Commands:**\n"
                    "`bazaar browse [page]` — browse all listings\n"
                    "`bazaar search <term>` — find items by name\n"
                    "`bazaar sell <item_id> <qty> <price>` — list an item for sale\n"
                    "`bazaar buy <id> [qty]` — buy from a listing\n"
                    "`bazaar cancel <id>` — cancel your listing\n"
                    "`bazaar my` — view your active listings\n\n"
                    "*5% tax on all sales • listings expire after " + std::to_string(bazaar::BAZAAR_EXPIRY_HOURS) + "h*\n"
                    "*max " + std::to_string(bazaar::BAZAAR_MAX_LISTINGS) + " active listings per player*"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            if (sub == "sell" || sub == "list") {
                bazaar::handle_sell(bot, event, args, db, scope);
            } else if (sub == "buy" || sub == "purchase") {
                bazaar::handle_buy(bot, event, args, db, scope);
            } else if (sub == "cancel" || sub == "remove" || sub == "delist") {
                bazaar::handle_cancel(bot, event, args, db);
            } else if (sub == "my" || sub == "mine" || sub == "listings") {
                bazaar::handle_my(bot, event, db, scope);
            } else if (sub == "browse" || sub == "all" || sub == "view") {
                bazaar::handle_browse(bot, event, args, db, scope);
            } else if (sub == "search" || sub == "find" || sub == "lookup") {
                bazaar::handle_search(bot, event, args, db, scope);
            } else {
                // Default: treat as browse
                bazaar::handle_browse(bot, event, args, db, scope);
            }
        },
        nullptr, // no slash handler
        {}       // no slash options
    );

    cmds.push_back(bz_cmd);
    return cmds;
}

} // namespace commands
