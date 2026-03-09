#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../fishing/fishing_helpers.h"
#include "../mining/mining_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <mutex>
#include <map>
#include <cmath>

using namespace bronx::db;

namespace commands {
namespace passive {

// ============================================================================
// COMMODITY MARKET — Fluctuating fish & ore prices
// ============================================================================
// Prices fluctuate ±10-30% daily. Players buy/sell at market prices.
// `/market`        — show today's prices with sparkline trends
// `/market sell`   — sell fish/ore at current market rate
// `/market buy`    — buy commodity at current market rate
// `/market history <item>` — show price history
// ============================================================================

// Table creation
static bool g_market_tables_created = false;
static std::mutex g_market_mutex;

static void ensure_market_tables(Database* db) {
    if (g_market_tables_created) return;
    std::lock_guard<std::mutex> lock(g_market_mutex);
    if (g_market_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS commodity_prices ("
        "  commodity_name VARCHAR(100) NOT NULL,"
        "  commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',"
        "  base_price INT NOT NULL DEFAULT 100,"
        "  current_price INT NOT NULL DEFAULT 100,"
        "  price_modifier DECIMAL(5,2) NOT NULL DEFAULT 1.00,"
        "  trend DECIMAL(5,2) NOT NULL DEFAULT 0.00,"
        "  last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (commodity_name, commodity_type),"
        "  INDEX idx_type (commodity_type)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS commodity_price_history ("
        "  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  commodity_name VARCHAR(100) NOT NULL,"
        "  commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',"
        "  price INT NOT NULL,"
        "  recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_commodity (commodity_name, commodity_type),"
        "  INDEX idx_recorded (recorded_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_market_tables_created = true;
}

// ── Market data structures ──────────────────────────────────────────────────

struct CommodityPrice {
    std::string name;
    std::string type;   // "fish" or "ore"
    int base_price;
    int current_price;
    double modifier;
    double trend;
};

struct PriceHistory {
    int price;
    std::chrono::system_clock::time_point time;
};

// ── DB helpers ──────────────────────────────────────────────────────────────

static std::vector<CommodityPrice> get_all_prices(Database* db, const std::string& type = "") {
    std::vector<CommodityPrice> prices;
    auto conn = db->get_pool()->acquire();
    if (!conn) return prices;
    
    std::string sql = "SELECT commodity_name, commodity_type, base_price, current_price, price_modifier, trend FROM commodity_prices";
    if (!type.empty()) sql += " WHERE commodity_type = '" + type + "'";
    sql += " ORDER BY commodity_type, commodity_name";
    
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                CommodityPrice p;
                p.name = row[0];
                p.type = row[1];
                p.base_price = std::stoi(row[2]);
                p.current_price = std::stoi(row[3]);
                p.modifier = std::stod(row[4]);
                p.trend = std::stod(row[5]);
                prices.push_back(p);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return prices;
}

static std::optional<CommodityPrice> get_price(Database* db, const std::string& name, const std::string& type) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return std::nullopt;
    
    char esc_name[201];
    mysql_real_escape_string(conn->get(), esc_name, name.c_str(), name.size());
    
    std::string sql = "SELECT commodity_name, commodity_type, base_price, current_price, price_modifier, trend FROM commodity_prices WHERE commodity_name = '" + std::string(esc_name) + "' AND commodity_type = '" + type + "'";
    
    std::optional<CommodityPrice> result;
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                CommodityPrice p;
                p.name = row[0];
                p.type = row[1];
                p.base_price = std::stoi(row[2]);
                p.current_price = std::stoi(row[3]);
                p.modifier = std::stod(row[4]);
                p.trend = std::stod(row[5]);
                result = p;
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return result;
}

static std::vector<PriceHistory> get_price_history(Database* db, const std::string& name, const std::string& type, int days = 7) {
    std::vector<PriceHistory> history;
    auto conn = db->get_pool()->acquire();
    if (!conn) return history;
    
    char esc_name[201];
    mysql_real_escape_string(conn->get(), esc_name, name.c_str(), name.size());
    
    std::string sql = "SELECT price, recorded_at FROM commodity_price_history WHERE commodity_name = '" + std::string(esc_name) + 
        "' AND commodity_type = '" + type + "' AND recorded_at > DATE_SUB(NOW(), INTERVAL " + std::to_string(days) + " DAY) ORDER BY recorded_at";
    
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                PriceHistory ph;
                ph.price = std::stoi(row[0]);
                struct tm tm = {};
                strptime(row[1], "%Y-%m-%d %H:%M:%S", &tm);
                ph.time = std::chrono::system_clock::from_time_t(timegm(&tm));
                history.push_back(ph);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return history;
}

// ── Price fluctuation engine (call daily via timer) ─────────────────────────

static void initialize_market_prices(Database* db) {
    ensure_market_tables(db);
    auto conn = db->get_pool()->acquire();
    if (!conn) return;
    
    // Check if prices exist
    std::string check = "SELECT COUNT(*) FROM commodity_prices";
    int count = 0;
    if (mysql_query(conn->get(), check.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) count = std::stoi(row[0]);
            mysql_free_result(res);
        }
    }
    
    if (count == 0) {
        // Seed with popular fish
        struct { std::string name; int base; } fish_commodities[] = {
            {"common fish", 30}, {"bass", 80}, {"trout", 90}, {"salmon", 150},
            {"catfish", 120}, {"tuna", 300}, {"swordfish", 500}, {"marlin", 800},
            {"anglerfish", 1200}, {"oarfish", 2000}, {"coelacanth", 5000},
        };
        for (const auto& f : fish_commodities) {
            char esc[201];
            mysql_real_escape_string(conn->get(), esc, f.name.c_str(), f.name.size());
            std::string sql = "INSERT IGNORE INTO commodity_prices (commodity_name, commodity_type, base_price, current_price) VALUES ('" +
                std::string(esc) + "', 'fish', " + std::to_string(f.base) + ", " + std::to_string(f.base) + ")";
            mysql_query(conn->get(), sql.c_str());
        }
        
        // Seed with popular ores
        struct { std::string name; int base; } ore_commodities[] = {
            {"coal", 22}, {"copper ore", 32}, {"iron ore", 60}, {"silver ore", 165},
            {"gold ore", 250}, {"platinum ore", 625}, {"diamond", 2600},
            {"ruby", 950}, {"emerald", 1200}, {"sapphire", 1025},
        };
        for (const auto& o : ore_commodities) {
            char esc[201];
            mysql_real_escape_string(conn->get(), esc, o.name.c_str(), o.name.size());
            std::string sql = "INSERT IGNORE INTO commodity_prices (commodity_name, commodity_type, base_price, current_price) VALUES ('" +
                std::string(esc) + "', 'ore', " + std::to_string(o.base) + ", " + std::to_string(o.base) + ")";
            mysql_query(conn->get(), sql.c_str());
        }
    }
    db->get_pool()->release(conn);
}

// Called by daily timer — fluctuates all prices ±10-30%
inline void fluctuate_market_prices(Database* db) {
    ensure_market_tables(db);
    auto conn = db->get_pool()->acquire();
    if (!conn) return;
    
    static thread_local std::mt19937 rng(std::random_device{}());
    
    auto prices = get_all_prices(db);
    for (auto& p : prices) {
        // Random fluctuation: modifier changes by ±0.05 to ±0.15, clamped to 0.70..1.30
        std::uniform_real_distribution<double> delta_dist(-0.15, 0.15);
        // Trend adds momentum (mean-reverting toward 1.0)
        double mean_revert = (1.0 - p.modifier) * 0.1;
        double delta = delta_dist(rng) + mean_revert;
        
        double new_modifier = std::clamp(p.modifier + delta, 0.70, 1.30);
        double new_trend = new_modifier - p.modifier;
        int new_price = std::max(1, (int)(p.base_price * new_modifier));
        
        char esc_name[201];
        mysql_real_escape_string(conn->get(), esc_name, p.name.c_str(), p.name.size());
        
        std::string sql = "UPDATE commodity_prices SET current_price = " + std::to_string(new_price) + 
            ", price_modifier = " + std::to_string(new_modifier) +
            ", trend = " + std::to_string(new_trend) +
            ", last_updated = NOW() WHERE commodity_name = '" + esc_name + "' AND commodity_type = '" + p.type + "'";
        mysql_query(conn->get(), sql.c_str());
        
        // Record history
        std::string hist = "INSERT INTO commodity_price_history (commodity_name, commodity_type, price) VALUES ('" +
            std::string(esc_name) + "', '" + p.type + "', " + std::to_string(new_price) + ")";
        mysql_query(conn->get(), hist.c_str());
    }
    
    // Clean old history (keep 30 days)
    mysql_query(conn->get(), "DELETE FROM commodity_price_history WHERE recorded_at < DATE_SUB(NOW(), INTERVAL 30 DAY)");
    
    db->get_pool()->release(conn);
}

// ── Sparkline generator ─────────────────────────────────────────────────────

static std::string make_sparkline(const std::vector<PriceHistory>& history) {
    if (history.empty()) return "—";
    
    const char* blocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int min_p = history[0].price, max_p = history[0].price;
    for (const auto& h : history) {
        min_p = std::min(min_p, h.price);
        max_p = std::max(max_p, h.price);
    }
    if (min_p == max_p) return std::string(history.size(), '-');
    
    // Sample up to 8 points
    std::string spark;
    int step = std::max(1, (int)history.size() / 8);
    for (size_t i = 0; i < history.size(); i += step) {
        int idx = (int)(7.0 * (history[i].price - min_p) / (max_p - min_p));
        idx = std::clamp(idx, 0, 7);
        spark += blocks[idx];
    }
    return spark;
}

static std::string trend_arrow(double trend) {
    if (trend > 0.05) return "📈";
    if (trend < -0.05) return "📉";
    return "➡️";
}

static std::string pct_change(double modifier) {
    int pct = (int)((modifier - 1.0) * 100);
    if (pct >= 0) return "+" + std::to_string(pct) + "%";
    return std::to_string(pct) + "%";
}

// ── Command ─────────────────────────────────────────────────────────────────

inline Command* get_market_overview_command(Database* db) {
    static Command* cmd = new Command(
        "cmarket",
        "view fluctuating commodity market prices",
        "passive",
        {"commodity", "commodities", "prices"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_market_tables(db);
            initialize_market_prices(db);
            
            std::string sub = args.empty() ? "view" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "sell") {
                uint64_t uid = event.msg.author.id;
                db->ensure_user_exists(uid);
                
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify what to sell: `b.market sell <item name>`"));
                    return;
                }
                std::string item_name;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) item_name += " ";
                    item_name += args[i];
                }
                std::transform(item_name.begin(), item_name.end(), item_name.begin(), ::tolower);
                
                // Check fish market first, then ore
                auto fish_price = get_price(db, item_name, "fish");
                auto ore_price = get_price(db, item_name, "ore");
                
                CommodityPrice price;
                if (fish_price) price = *fish_price;
                else if (ore_price) price = *ore_price;
                else {
                    bronx::send_message(bot, event, bronx::error("\"" + item_name + "\" is not traded on the market"));
                    return;
                }
                
                // Find in inventory
                auto inventory = db->get_inventory(uid);
                bool sold = false;
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    std::string meta = item.metadata;
                    size_t name_pos = meta.find("\"name\":\"");
                    if (name_pos == std::string::npos) continue;
                    size_t start = name_pos + 8;
                    size_t end = meta.find("\"", start);
                    std::string stored_name = meta.substr(start, end - start);
                    std::string stored_lower = stored_name;
                    std::transform(stored_lower.begin(), stored_lower.end(), stored_lower.begin(), ::tolower);
                    
                    if (stored_lower == item_name || stored_lower.find(item_name) != std::string::npos) {
                        if (db->remove_item(uid, item.item_id, 1)) {
                            db->update_wallet(uid, price.current_price);
                            auto embed = bronx::success("sold **" + stored_name + "** at market rate: **$" + economy::format_number(price.current_price) + "** " + trend_arrow(price.trend) + " (" + pct_change(price.modifier) + " from base)");
                            bronx::send_message(bot, event, embed);
                            sold = true;
                            break;
                        }
                    }
                }
                if (!sold) {
                    bronx::send_message(bot, event, bronx::error("you don't have any \"" + item_name + "\" to sell"));
                }
                return;
            }
            
            if (sub == "history") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify an item: `b.market history <item>`"));
                    return;
                }
                std::string name;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) name += " ";
                    name += args[i];
                }
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                
                auto fish_h = get_price_history(db, name, "fish");
                auto ore_h = get_price_history(db, name, "ore");
                auto& hist = fish_h.empty() ? ore_h : fish_h;
                std::string type = fish_h.empty() ? "ore" : "fish";
                
                if (hist.empty()) {
                    bronx::send_message(bot, event, bronx::error("no price history for \"" + name + "\""));
                    return;
                }
                
                dpp::embed embed;
                embed.set_color(0x7B1FA2);
                embed.set_title("📊 Price History: " + name);
                
                std::string desc = "```\n";
                for (const auto& h : hist) {
                    auto ts = std::chrono::system_clock::to_time_t(h.time);
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%m/%d", std::localtime(&ts));
                    desc += std::string(buf) + "  $" + economy::format_number(h.price) + "\n";
                }
                desc += "```\n" + make_sparkline(hist);
                embed.set_description(desc);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Default: view prices
            auto fish_prices = get_all_prices(db, "fish");
            auto ore_prices = get_all_prices(db, "ore");
            
            dpp::embed embed;
            embed.set_color(0x7B1FA2);
            embed.set_title("📊 Commodity Market");
            embed.set_description("prices fluctuate daily — buy low, sell high!");
            
            if (!fish_prices.empty()) {
                std::string fish_str;
                for (const auto& p : fish_prices) {
                    auto hist = get_price_history(db, p.name, "fish", 7);
                    fish_str += trend_arrow(p.trend) + " **" + p.name + "** — $" + economy::format_number(p.current_price) + " (" + pct_change(p.modifier) + ") " + make_sparkline(hist) + "\n";
                }
                embed.add_field("🐟 Fish Market", fish_str, false);
            }
            
            if (!ore_prices.empty()) {
                std::string ore_str;
                for (const auto& p : ore_prices) {
                    auto hist = get_price_history(db, p.name, "ore", 7);
                    ore_str += trend_arrow(p.trend) + " **" + p.name + "** — $" + economy::format_number(p.current_price) + " (" + pct_change(p.modifier) + ") " + make_sparkline(hist) + "\n";
                }
                embed.add_field("⛏️ Ore Market", ore_str, false);
            }
            
            embed.set_footer(dpp::embed_footer().set_text("sell at market rates with b.market sell <item>"));
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_market_tables(db);
            initialize_market_prices(db);
            
            std::string sub = "view";
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) sub = ci_options[0].name;
            
            if (sub == "sell") {
                uint64_t uid = event.command.get_issuing_user().id;
                db->ensure_user_exists(uid);
                std::string item_name = std::get<std::string>(ci_options[0].options[0].value);
                std::transform(item_name.begin(), item_name.end(), item_name.begin(), ::tolower);
                
                auto fish_price = get_price(db, item_name, "fish");
                auto ore_price = get_price(db, item_name, "ore");
                CommodityPrice price;
                if (fish_price) price = *fish_price;
                else if (ore_price) price = *ore_price;
                else { event.reply(dpp::message().add_embed(bronx::error("not on the market")).set_flags(dpp::m_ephemeral)); return; }
                
                auto inventory = db->get_inventory(uid);
                bool sold = false;
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    std::string meta = item.metadata;
                    size_t np = meta.find("\"name\":\"");
                    if (np == std::string::npos) continue;
                    size_t s = np + 8, e = meta.find("\"", s);
                    std::string sn = meta.substr(s, e - s);
                    std::string sl = sn;
                    std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
                    if (sl == item_name || sl.find(item_name) != std::string::npos) {
                        if (db->remove_item(uid, item.item_id, 1)) {
                            db->update_wallet(uid, price.current_price);
                            event.reply(dpp::message().add_embed(bronx::success("sold **" + sn + "** for **$" + economy::format_number(price.current_price) + "** " + trend_arrow(price.trend))));
                            sold = true;
                            break;
                        }
                    }
                }
                if (!sold) event.reply(dpp::message().add_embed(bronx::error("you don't have that item")).set_flags(dpp::m_ephemeral));
                
            } else if (sub == "history") {
                std::string name = std::get<std::string>(ci_options[0].options[0].value);
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                auto fh = get_price_history(db, name, "fish");
                auto oh = get_price_history(db, name, "ore");
                auto& hist = fh.empty() ? oh : fh;
                if (hist.empty()) { event.reply(dpp::message().add_embed(bronx::error("no history")).set_flags(dpp::m_ephemeral)); return; }
                
                dpp::embed embed;
                embed.set_color(0x7B1FA2);
                embed.set_title("📊 " + name + " Price History");
                std::string desc = "```\n";
                for (auto& h : hist) {
                    auto ts = std::chrono::system_clock::to_time_t(h.time);
                    char buf[32]; std::strftime(buf, sizeof(buf), "%m/%d", std::localtime(&ts));
                    desc += std::string(buf) + "  $" + economy::format_number(h.price) + "\n";
                }
                desc += "```\n" + make_sparkline(hist);
                embed.set_description(desc);
                event.reply(dpp::message().add_embed(embed));
                
            } else { // view
                auto fish_prices = get_all_prices(db, "fish");
                auto ore_prices = get_all_prices(db, "ore");
                
                dpp::embed embed;
                embed.set_color(0x7B1FA2);
                embed.set_title("📊 Commodity Market");
                embed.set_description("prices fluctuate daily — buy low, sell high!");
                
                if (!fish_prices.empty()) {
                    std::string fs;
                    for (auto& p : fish_prices) {
                        auto hist = get_price_history(db, p.name, "fish", 7);
                        fs += trend_arrow(p.trend) + " **" + p.name + "** — $" + economy::format_number(p.current_price) + " (" + pct_change(p.modifier) + ") " + make_sparkline(hist) + "\n";
                    }
                    embed.add_field("🐟 Fish", fs, false);
                }
                if (!ore_prices.empty()) {
                    std::string os;
                    for (auto& p : ore_prices) {
                        auto hist = get_price_history(db, p.name, "ore", 7);
                        os += trend_arrow(p.trend) + " **" + p.name + "** — $" + economy::format_number(p.current_price) + " (" + pct_change(p.modifier) + ") " + make_sparkline(hist) + "\n";
                    }
                    embed.add_field("⛏️ Ores", os, false);
                }
                event.reply(dpp::message().add_embed(embed));
            }
        },
        // slash options
        {
            dpp::command_option(dpp::co_sub_command, "view", "view current market prices"),
            dpp::command_option(dpp::co_sub_command, "sell", "sell an item at market rate")
                .add_option(dpp::command_option(dpp::co_string, "item", "name of the item to sell", true)),
            dpp::command_option(dpp::co_sub_command, "history", "view price history for an item")
                .add_option(dpp::command_option(dpp::co_string, "item", "item name", true)),
        }
    );
    return cmd;
}

} // namespace passive
} // namespace commands
