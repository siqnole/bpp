#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/core/connection_pool.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <mariadb/mysql.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

using namespace bronx::db;

namespace commands {
namespace trading_post {

// ============================================================================
// TRADING POST  ─  Player-to-player item trading with escrow
// ============================================================================
// Escrow flow:
//   offer created  → item removed from sender, stored in trade_offers table
//   offer accepted → item moved to recipient, coins transferred (5% tax)
//   offer expired / declined / cancelled → item returned to sender
//
// Commands:
//   trade offer @user <item_id> <qty> [price]
//   trade accept <id>
//   trade decline <id>
//   trade cancel <id>
//   trade list
// ============================================================================

static bool trading_tables_created = false;

inline void ensure_trading_tables(Database* db) {
    if (trading_tables_created) return;
    db->execute(
        "CREATE TABLE IF NOT EXISTS trade_offers ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  from_user_id BIGINT UNSIGNED NOT NULL,"
        "  to_user_id BIGINT UNSIGNED NOT NULL,"
        "  item_id VARCHAR(100) NOT NULL,"
        "  item_type VARCHAR(50) NOT NULL DEFAULT 'other',"
        "  item_metadata TEXT,"
        "  item_level INT NOT NULL DEFAULT 1,"
        "  quantity INT NOT NULL DEFAULT 1,"
        "  asking_price BIGINT NOT NULL DEFAULT 0,"
        "  status ENUM('pending','accepted','declined','cancelled','expired') DEFAULT 'pending',"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL DEFAULT '2099-01-01 00:00:00',"
        "  INDEX idx_to_user (to_user_id, status),"
        "  INDEX idx_from_user (from_user_id, status)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    trading_tables_created = true;
}

// Expire old pending offers and return items to senders
inline void expire_old_offers(Database* db) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return;

    std::string find_q =
        "SELECT id, from_user_id, item_id, item_type, quantity, item_metadata, item_level "
        "FROM trade_offers WHERE status='pending' AND expires_at < NOW() LIMIT 50";
    if (mysql_query(conn->get(), find_q.c_str()) != 0) { db->get_pool()->release(conn); return; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    db->get_pool()->release(conn);
    if (!res) return;

    struct ExpiredOffer {
        int64_t id; uint64_t from_user;
        std::string item_id, item_type, metadata;
        int quantity, level;
    };
    std::vector<ExpiredOffer> expired;
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

    for (auto& o : expired) {
        if (o.from_user && !o.item_id.empty())
            db->add_item(o.from_user, o.item_id, o.item_type, o.quantity, o.metadata, o.level);
        auto conn2 = db->get_pool()->acquire();
        if (conn2) {
            mysql_query(conn2->get(), ("UPDATE trade_offers SET status='expired' WHERE id=" + std::to_string(o.id)).c_str());
            db->get_pool()->release(conn2);
        }
    }
}

// Safe MySQL string escaping
inline std::string trade_esc(const std::string& s) {
    std::string r; r.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\'') r += "\\'";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

// Human-readable item name from item_id
inline std::string trade_item_name(const std::string& item_id) {
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

// Parse @mention or raw snowflake from a string
inline uint64_t parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

} // namespace trading_post

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC COMMAND BUILDER
// ─────────────────────────────────────────────────────────────────────────────
inline ::std::vector<Command*> get_trading_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    trading_post::ensure_trading_tables(db);

    static Command* trade_cmd = new Command(
        "trade",
        "send and receive item trade offers with other players",
        "economy",
        {"tr"},
        false,
        // ── TEXT HANDLER ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            trading_post::expire_old_offers(db);
            uint64_t uid = event.msg.author.id;

            if (args.empty()) {
                auto embed = bronx::info(
                    "**🤝 Trading Post**\n\n"
                    "Trade inventory items with other players!\n\n"
                    "**Commands:**\n"
                    "`trade offer <@user> <item_id> <qty> [price]` — send a trade offer\n"
                    "`trade accept <id>` — accept an incoming offer\n"
                    "`trade decline <id>` — decline an incoming offer\n"
                    "`trade cancel <id>` — cancel your outgoing offer\n"
                    "`trade list` — view your active offers\n\n"
                    "*items are held in escrow until the trade is resolved*\n"
                    "*5% transaction tax on all priced trades*"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            // ── trade offer @user <item_id> <qty> [price] ───────────────────
            if (sub == "offer" || sub == "send") {
                if (args.size() < 4) {
                    bronx::send_message(bot, event, bronx::error("usage: `trade offer <@user> <item_id> <qty> [price]`"));
                    return;
                }
                uint64_t target_id = trading_post::parse_mention(args[1]);
                if (!target_id || target_id == uid) {
                    bronx::send_message(bot, event, bronx::error("invalid target (cannot trade with yourself)"));
                    return;
                }
                std::string item_id = args[2];
                std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

                int qty = 1;
                try { qty = std::stoi(args[3]); } catch (...) {}
                if (qty < 1 || qty > 9999) {
                    bronx::send_message(bot, event, bronx::error("quantity must be between 1 and 9999"));
                    return;
                }

                int64_t price = 0;
                if (args.size() >= 5) {
                    try {
                        auto me = db->get_user(uid);
                        price = parse_amount(args[4], me ? me->wallet : 0);
                    } catch (...) {}
                }
                if (price < 0) { bronx::send_message(bot, event, bronx::error("asking price cannot be negative")); return; }

                // Verify sender has the item
                auto inv = db->get_inventory(uid);
                InventoryItem* item = nullptr;
                for (auto& it2 : inv) {
                    if (it2.item_id == item_id) { item = &it2; break; }
                }
                if (!item || item->quantity < qty) {
                    bronx::send_message(bot, event, bronx::error(
                        "you don't have **" + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id) + "**"
                    ));
                    return;
                }

                db->ensure_user_exists(target_id);

                // Escrow: remove from sender inventory
                if (!db->remove_item(uid, item_id, qty)) {
                    bronx::send_message(bot, event, bronx::error("failed to escrow item — try again"));
                    return;
                }

                // Create trade offer
                auto conn = db->get_pool()->acquire();
                if (!conn) {
                    db->add_item(uid, item_id, item->item_type, qty, item->metadata, item->level);
                    bronx::send_message(bot, event, bronx::error("database error"));
                    return;
                }

                std::string meta_esc = trading_post::trade_esc(item->metadata.empty() ? "{}" : item->metadata);
                std::string id_esc   = trading_post::trade_esc(item_id);
                std::string type_esc = trading_post::trade_esc(item->item_type);

                std::string ins =
                    "INSERT INTO trade_offers (from_user_id, to_user_id, item_id, item_type, item_metadata, item_level, quantity, asking_price, expires_at) VALUES ("
                    + std::to_string(uid) + ","
                    + std::to_string(target_id) + ","
                    "'" + id_esc + "',"
                    "'" + type_esc + "',"
                    "'" + meta_esc + "',"
                    + std::to_string(item->level) + ","
                    + std::to_string(qty) + ","
                    + std::to_string(price) + ","
                    "DATE_ADD(NOW(), INTERVAL 24 HOUR))";

                if (mysql_query(conn->get(), ins.c_str()) != 0) {
                    db->get_pool()->release(conn);
                    db->add_item(uid, item_id, item->item_type, qty, item->metadata, item->level);
                    bronx::send_message(bot, event, bronx::error("failed to create trade offer"));
                    return;
                }
                int64_t new_id = (int64_t)mysql_insert_id(conn->get());
                db->get_pool()->release(conn);

                auto embed = bronx::success(
                    "**🤝 Trade Offer Sent!**\n\n"
                    "**Offer ID:** `#" + std::to_string(new_id) + "`\n"
                    "**Item:** " + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id) + "\n"
                    "**Asking price:** " + (price > 0 ? "$" + format_number(price) : "free") + "\n"
                    "**Expires:** 24 hours\n\n"
                    "*sent to <@" + std::to_string(target_id) + ">*\n"
                    "*they can accept with* `trade accept " + std::to_string(new_id) + "`"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            // ── trade accept <id> ────────────────────────────────────────────
            if (sub == "accept") {
                if (args.size() < 2) { bronx::send_message(bot, event, bronx::error("usage: `trade accept <offer_id>`")); return; }
                int64_t offer_id = 0;
                try { offer_id = std::stoll(args[1]); } catch (...) {}
                if (offer_id <= 0) { bronx::send_message(bot, event, bronx::error("invalid offer ID")); return; }

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

                std::string sel =
                    "SELECT from_user_id, item_id, item_type, item_metadata, item_level, quantity, asking_price, status "
                    "FROM trade_offers WHERE id=" + std::to_string(offer_id) +
                    " AND to_user_id=" + std::to_string(uid);
                if (mysql_query(conn->get(), sel.c_str()) != 0) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("database error")); return; }
                MYSQL_RES* res = mysql_store_result(conn->get());
                db->get_pool()->release(conn);
                if (!res) { bronx::send_message(bot, event, bronx::error("offer not found")); return; }
                MYSQL_ROW row = mysql_fetch_row(res);
                if (!row) { mysql_free_result(res); bronx::send_message(bot, event, bronx::error("offer not found or not sent to you")); return; }

                uint64_t from_user = std::stoull(row[0] ? row[0] : "0");
                std::string item_id   = row[1] ? row[1] : "";
                std::string item_type = row[2] ? row[2] : "other";
                std::string metadata  = row[3] ? row[3] : "{}";
                int level             = row[4] ? std::stoi(row[4]) : 1;
                int qty               = row[5] ? std::stoi(row[5]) : 1;
                int64_t asking_price  = row[6] ? std::stoll(row[6]) : 0;
                std::string status    = row[7] ? row[7] : "";
                mysql_free_result(res);

                if (status != "pending") {
                    bronx::send_message(bot, event, bronx::error("offer is no longer pending (status: " + status + ")"));
                    return;
                }

                // Check buyer has enough coins
                if (asking_price > 0) {
                    int64_t wallet = db->get_wallet(uid);
                    if (wallet < asking_price) {
                        bronx::send_message(bot, event, bronx::error(
                            "you need $" + format_number(asking_price) + " to accept (you have $" + format_number(wallet) + ")"
                        ));
                        return;
                    }
                    int64_t tax = (int64_t)(asking_price * 0.05);
                    int64_t seller_gets = asking_price - tax;
                    db->update_wallet(uid, -asking_price);
                    db->update_wallet(from_user, seller_gets);
                }

                // Give item to buyer
                db->add_item(uid, item_id, item_type, qty, metadata, level);

                // Mark accepted
                auto conn2 = db->get_pool()->acquire();
                if (conn2) {
                    mysql_query(conn2->get(), ("UPDATE trade_offers SET status='accepted' WHERE id=" + std::to_string(offer_id)).c_str());
                    db->get_pool()->release(conn2);
                }

                std::string desc = "**" + bronx::EMOJI_CHECK + " Trade Accepted!**\n\n"
                    "**Received:** " + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id) + "\n";
                if (asking_price > 0) {
                    int64_t tax = (int64_t)(asking_price * 0.05);
                    desc += "**Paid:** $" + format_number(asking_price) + " (5% tax: $" + format_number(tax) + ")\n";
                }
                auto embed = bronx::success(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);

                // Notify seller
                bot.direct_message_create(from_user, dpp::message().add_embed(
                    bronx::success(
                        "**🤝 Trade offer #" + std::to_string(offer_id) + " was accepted!**\n\n"
                        "<@" + std::to_string(uid) + "> accepted "
                        + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id)
                        + (asking_price > 0 ? "\nyou received **$" + format_number((int64_t)(asking_price * 0.95)) + "**" : "")
                    )
                ), nullptr);
                return;
            }

            // ── trade decline <id> ───────────────────────────────────────────
            if (sub == "decline") {
                if (args.size() < 2) { bronx::send_message(bot, event, bronx::error("usage: `trade decline <offer_id>`")); return; }
                int64_t offer_id = 0;
                try { offer_id = std::stoll(args[1]); } catch (...) {}

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string sel = "SELECT from_user_id, item_id, item_type, item_metadata, item_level, quantity, status "
                                  "FROM trade_offers WHERE id=" + std::to_string(offer_id) + " AND to_user_id=" + std::to_string(uid);
                if (mysql_query(conn->get(), sel.c_str()) != 0) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("database error")); return; }
                MYSQL_RES* res = mysql_store_result(conn->get());
                db->get_pool()->release(conn);
                if (!res) { bronx::send_message(bot, event, bronx::error("offer not found")); return; }
                MYSQL_ROW row = mysql_fetch_row(res);
                if (!row) { mysql_free_result(res); bronx::send_message(bot, event, bronx::error("offer not found")); return; }

                uint64_t from_user = std::stoull(row[0] ? row[0] : "0");
                std::string item_id   = row[1] ? row[1] : "";
                std::string item_type = row[2] ? row[2] : "other";
                std::string metadata  = row[3] ? row[3] : "{}";
                int level = row[4] ? std::stoi(row[4]) : 1;
                int qty   = row[5] ? std::stoi(row[5]) : 1;
                std::string status = row[6] ? row[6] : "";
                mysql_free_result(res);

                if (status != "pending") { bronx::send_message(bot, event, bronx::error("offer is not pending")); return; }

                db->add_item(from_user, item_id, item_type, qty, metadata, level);
                auto conn2 = db->get_pool()->acquire();
                if (conn2) { mysql_query(conn2->get(), ("UPDATE trade_offers SET status='declined' WHERE id=" + std::to_string(offer_id)).c_str()); db->get_pool()->release(conn2); }

                bronx::send_message(bot, event, bronx::success("offer #" + std::to_string(offer_id) + " declined. items returned to sender."));
                bot.direct_message_create(from_user, dpp::message().add_embed(
                    bronx::error("**your trade offer #" + std::to_string(offer_id) + " was declined.**\n"
                        + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id) + " returned to your inventory.")
                ), nullptr);
                return;
            }

            // ── trade cancel <id> ────────────────────────────────────────────
            if (sub == "cancel") {
                if (args.size() < 2) { bronx::send_message(bot, event, bronx::error("usage: `trade cancel <offer_id>`")); return; }
                int64_t offer_id = 0;
                try { offer_id = std::stoll(args[1]); } catch (...) {}

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string sel = "SELECT item_id, item_type, item_metadata, item_level, quantity, status "
                                  "FROM trade_offers WHERE id=" + std::to_string(offer_id) + " AND from_user_id=" + std::to_string(uid);
                if (mysql_query(conn->get(), sel.c_str()) != 0) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("database error")); return; }
                MYSQL_RES* res = mysql_store_result(conn->get());
                db->get_pool()->release(conn);
                if (!res) { bronx::send_message(bot, event, bronx::error("offer not found")); return; }
                MYSQL_ROW row = mysql_fetch_row(res);
                if (!row) { mysql_free_result(res); bronx::send_message(bot, event, bronx::error("offer not found (or not yours)")); return; }

                std::string item_id   = row[0] ? row[0] : "";
                std::string item_type = row[1] ? row[1] : "other";
                std::string metadata  = row[2] ? row[2] : "{}";
                int level  = row[3] ? std::stoi(row[3]) : 1;
                int qty    = row[4] ? std::stoi(row[4]) : 1;
                std::string status = row[5] ? row[5] : "";
                mysql_free_result(res);

                if (status != "pending") { bronx::send_message(bot, event, bronx::error("offer is not pending")); return; }

                db->add_item(uid, item_id, item_type, qty, metadata, level);
                auto conn2 = db->get_pool()->acquire();
                if (conn2) { mysql_query(conn2->get(), ("UPDATE trade_offers SET status='cancelled' WHERE id=" + std::to_string(offer_id)).c_str()); db->get_pool()->release(conn2); }

                bronx::send_message(bot, event, bronx::success(
                    "offer #" + std::to_string(offer_id) + " cancelled. "
                    + std::to_string(qty) + "x " + trading_post::trade_item_name(item_id) + " returned to your inventory."
                ));
                return;
            }

            // ── trade list ───────────────────────────────────────────────────
            if (sub == "list" || sub == "inbox" || sub == "offers") {
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

                std::string in_q =
                    "SELECT id, from_user_id, item_id, quantity, asking_price FROM trade_offers "
                    "WHERE to_user_id=" + std::to_string(uid) + " AND status='pending' AND expires_at > NOW() ORDER BY id DESC LIMIT 10";
                mysql_query(conn->get(), in_q.c_str());
                MYSQL_RES* in_res = mysql_store_result(conn->get());

                std::string out_q =
                    "SELECT id, to_user_id, item_id, quantity, asking_price FROM trade_offers "
                    "WHERE from_user_id=" + std::to_string(uid) + " AND status='pending' AND expires_at > NOW() ORDER BY id DESC LIMIT 10";
                mysql_query(conn->get(), out_q.c_str());
                MYSQL_RES* out_res = mysql_store_result(conn->get());
                db->get_pool()->release(conn);

                std::string desc = "**🤝 Your Trade Offers**\n\n";

                desc += "**📥 Incoming:**\n";
                if (in_res) {
                    MYSQL_ROW row; int cnt = 0;
                    while ((row = mysql_fetch_row(in_res))) {
                        cnt++;
                        int64_t id = row[0] ? std::stoll(row[0]) : 0;
                        uint64_t from = row[1] ? std::stoull(row[1]) : 0;
                        std::string iid = row[2] ? row[2] : "";
                        int qty = row[3] ? std::stoi(row[3]) : 1;
                        int64_t price = row[4] ? std::stoll(row[4]) : 0;
                        desc += "`#" + std::to_string(id) + "` from <@" + std::to_string(from) + "> — "
                             + std::to_string(qty) + "x " + trading_post::trade_item_name(iid)
                             + (price > 0 ? " for **$" + format_number(price) + "**" : " *(free)*") + "\n";
                    }
                    if (!cnt) desc += "*no incoming offers*\n";
                    mysql_free_result(in_res);
                }

                desc += "\n**📤 Outgoing:**\n";
                if (out_res) {
                    MYSQL_ROW row; int cnt = 0;
                    while ((row = mysql_fetch_row(out_res))) {
                        cnt++;
                        int64_t id = row[0] ? std::stoll(row[0]) : 0;
                        uint64_t to = row[1] ? std::stoull(row[1]) : 0;
                        std::string iid = row[2] ? row[2] : "";
                        int qty = row[3] ? std::stoi(row[3]) : 1;
                        int64_t price = row[4] ? std::stoll(row[4]) : 0;
                        desc += "`#" + std::to_string(id) + "` to <@" + std::to_string(to) + "> — "
                             + std::to_string(qty) + "x " + trading_post::trade_item_name(iid)
                             + (price > 0 ? " for **$" + format_number(price) + "**" : " *(free)*") + "\n";
                    }
                    if (!cnt) desc += "*no outgoing offers*\n";
                    mysql_free_result(out_res);
                }

                desc += "\n*use `trade accept <id>` or `trade decline <id>` to act on incoming offers*";
                auto embed = bronx::info(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            bronx::send_message(bot, event, bronx::error("unknown subcommand. use `trade` for help"));
        }
    );

    cmds.push_back(trade_cmd);
    return cmds;
}

} // namespace commands
