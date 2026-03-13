#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../mining/mining_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <mutex>

using namespace bronx::db;

namespace commands {
namespace passive {

// ============================================================================
// MINING CLAIMS — Passive ore production
// ============================================================================
// Purchase a claim on a specific ore vein. It passively produces 1-3 of that
// ore every 4 hours. Higher rarity veins cost more. Claims expire after 7
// days unless renewed.
//
// Subcommands:
//   /claim buy <ore>    — purchase a mining claim
//   /claim collect      — collect produced ores (coins)
//   /claim renew <id>   — renew an expiring claim
//   /claim list         — view your active claims
//   /claim abandon <id> — abandon a claim
// ============================================================================

// Claim costs by ore rarity tier
struct ClaimTier {
    std::string rarity;
    int64_t cost;
    int64_t renew_cost;
    int yield_min;
    int yield_max;
    std::string emoji;
};

static const std::vector<ClaimTier> claim_tiers = {
    {"common",    10000,    5000,   1, 3, "🟤"},
    {"uncommon",  50000,    25000,  1, 3, "🔵"},
    {"rare",      200000,   100000, 1, 2, "💜"},
    {"epic",      1000000,  500000, 1, 2, "🟠"},
    {"legendary", 5000000,  2500000,1, 1, "⭐"},
};

static const ClaimTier& get_claim_tier(const std::string& rarity) {
    for (const auto& t : claim_tiers) {
        if (t.rarity == rarity) return t;
    }
    return claim_tiers[0];
}

// Max claims per user
static const int MAX_CLAIMS = 5;

// Table creation
static bool g_claim_tables_created = false;
static std::mutex g_claim_mutex;

static void ensure_claim_tables(Database* db) {
    if (g_claim_tables_created) return;
    std::lock_guard<std::mutex> lock(g_claim_mutex);
    if (g_claim_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS mining_claims ("
        "  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  ore_name VARCHAR(100) NOT NULL,"
        "  ore_emoji VARCHAR(32) NOT NULL DEFAULT '⛏️',"
        "  rarity VARCHAR(20) NOT NULL DEFAULT 'common',"
        "  yield_min INT NOT NULL DEFAULT 1,"
        "  yield_max INT NOT NULL DEFAULT 3,"
        "  ore_value INT NOT NULL DEFAULT 10,"
        "  purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL,"
        "  last_collect TIMESTAMP NULL DEFAULT NULL,"
        "  INDEX idx_user (user_id),"
        "  INDEX idx_expires (expires_at)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_claim_tables_created = true;
}

// ── DB helpers ──────────────────────────────────────────────────────────────

struct ClaimInfo {
    uint64_t id;
    std::string ore_name;
    std::string ore_emoji;
    std::string rarity;
    int yield_min;
    int yield_max;
    int ore_value;
    std::chrono::system_clock::time_point expires_at;
    std::optional<std::chrono::system_clock::time_point> last_collect;
};

static std::vector<ClaimInfo> get_user_claims(Database* db, uint64_t user_id) {
    std::vector<ClaimInfo> claims;
    auto conn = db->get_pool()->acquire();
    if (!conn) return claims;
    
    // Only return non-expired claims
    std::string sql = "SELECT id, ore_name, ore_emoji, rarity, yield_min, yield_max, ore_value, expires_at, last_collect "
                      "FROM mining_claims WHERE user_id = " + std::to_string(user_id) + " AND expires_at > NOW() ORDER BY id";
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                ClaimInfo c;
                c.id = std::stoull(row[0]);
                c.ore_name = row[1];
                c.ore_emoji = row[2];
                c.rarity = row[3];
                c.yield_min = std::stoi(row[4]);
                c.yield_max = std::stoi(row[5]);
                c.ore_value = std::stoi(row[6]);
                
                struct tm tm = {};
                strptime(row[7], "%Y-%m-%d %H:%M:%S", &tm);
                c.expires_at = std::chrono::system_clock::from_time_t(timegm(&tm));
                
                if (row[8]) {
                    struct tm tm2 = {};
                    strptime(row[8], "%Y-%m-%d %H:%M:%S", &tm2);
                    c.last_collect = std::chrono::system_clock::from_time_t(timegm(&tm2));
                }
                claims.push_back(c);
            }
            mysql_free_result(res);
        }
    }
    db->get_pool()->release(conn);
    return claims;
}

static bool create_claim(Database* db, uint64_t user_id, const std::string& ore_name, const std::string& ore_emoji, 
                          const std::string& rarity, int yield_min, int yield_max, int ore_value) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    char esc_name[201], esc_emoji[65], esc_rarity[41];
    mysql_real_escape_string(conn->get(), esc_name, ore_name.c_str(), ore_name.size());
    mysql_real_escape_string(conn->get(), esc_emoji, ore_emoji.c_str(), ore_emoji.size());
    mysql_real_escape_string(conn->get(), esc_rarity, rarity.c_str(), rarity.size());
    
    std::string sql = "INSERT INTO mining_claims (user_id, ore_name, ore_emoji, rarity, yield_min, yield_max, ore_value, expires_at) VALUES (" +
        std::to_string(user_id) + ", '" + esc_name + "', '" + esc_emoji + "', '" + esc_rarity + "', " +
        std::to_string(yield_min) + ", " + std::to_string(yield_max) + ", " + std::to_string(ore_value) +
        ", DATE_ADD(NOW(), INTERVAL 7 DAY))";
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0;
    db->get_pool()->release(conn);
    return ok;
}

static bool renew_claim(Database* db, uint64_t claim_id, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    std::string sql = "UPDATE mining_claims SET expires_at = DATE_ADD(NOW(), INTERVAL 7 DAY) WHERE id = " +
        std::to_string(claim_id) + " AND user_id = " + std::to_string(user_id);
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0 && mysql_affected_rows(conn->get()) > 0;
    db->get_pool()->release(conn);
    return ok;
}

static bool abandon_claim(Database* db, uint64_t claim_id, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    std::string sql = "DELETE FROM mining_claims WHERE id = " + std::to_string(claim_id) + " AND user_id = " + std::to_string(user_id);
    bool ok = mysql_query(conn->get(), sql.c_str()) == 0 && mysql_affected_rows(conn->get()) > 0;
    db->get_pool()->release(conn);
    return ok;
}

static void update_claim_collect(Database* db, uint64_t claim_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return;
    std::string sql = "UPDATE mining_claims SET last_collect = NOW() WHERE id = " + std::to_string(claim_id);
    mysql_query(conn->get(), sql.c_str());
    db->get_pool()->release(conn);
}

// ── Command ─────────────────────────────────────────────────────────────────

inline Command* get_claim_command(Database* db) {
    static Command* cmd = new Command(
        "claim",
        "manage mining claims for passive ore income",
        "passive",
        {"claims", "mc"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_claim_tables(db);
            uint64_t uid = event.msg.author.id;
            db->ensure_user_exists(uid);
            
            std::string sub = args.empty() ? "list" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "buy" || sub == "purchase") {
                auto claims = get_user_claims(db, uid);
                if ((int)claims.size() >= MAX_CLAIMS) {
                    bronx::send_message(bot, event, bronx::error("you already have " + std::to_string(MAX_CLAIMS) + " active claims! abandon one first"));
                    return;
                }
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify an ore: `b.claim buy <ore name>`"));
                    return;
                }
                std::string ore_name;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) ore_name += " ";
                    ore_name += args[i];
                }
                std::transform(ore_name.begin(), ore_name.end(), ore_name.begin(), ::tolower);
                
                // Find matching ore
                const mining::OreType* found_ore = nullptr;
                for (const auto& o : mining::ore_types) {
                    std::string oname = o.name;
                    std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
                    if (oname == ore_name || oname.find(ore_name) != std::string::npos) {
                        found_ore = &o;
                        break;
                    }
                }
                if (!found_ore) {
                    bronx::send_message(bot, event, bronx::error("unknown ore type \"" + ore_name + "\""));
                    return;
                }
                
                // Determine rarity and cost
                std::string rarity = mining::get_ore_rarity(found_ore->name);
                const auto& tier = get_claim_tier(rarity);
                int64_t cost = tier.cost;
                
                int64_t wallet = db->get_wallet(uid);
                if (wallet < cost) {
                    bronx::send_message(bot, event, bronx::error("you need **$" + economy::format_number(cost) + "** to buy this claim (have $" + economy::format_number(wallet) + ")"));
                    return;
                }
                
                db->update_wallet(uid, -cost);
                int avg_value = (found_ore->min_value + found_ore->max_value) / 2;
                create_claim(db, uid, found_ore->name, found_ore->emoji, rarity, tier.yield_min, tier.yield_max, avg_value);
                
                auto embed = bronx::success("⛏️ purchased a **" + found_ore->name + "** mining claim!\n"
                    "💰 Cost: **$" + economy::format_number(cost) + "**\n"
                    "📦 Yields: " + std::to_string(tier.yield_min) + "-" + std::to_string(tier.yield_max) + " ores × $" + economy::format_number(avg_value) + " per 4h\n"
                    "⏰ Expires in 7 days (renewable)");
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "collect" || sub == "claim") {
                auto claims = get_user_claims(db, uid);
                if (claims.empty()) {
                    bronx::send_message(bot, event, bronx::error("you don't have any active mining claims! buy one with `b.claim buy <ore>`"));
                    return;
                }
                
                static thread_local std::mt19937 rng(std::random_device{}());
                int64_t total_income = 0;
                int collected_count = 0;
                int not_ready = 0;
                std::string details;
                auto now = std::chrono::system_clock::now();
                
                for (auto& c : claims) {
                    bool ready = true;
                    if (c.last_collect) {
                        auto next = *c.last_collect + std::chrono::hours(4);
                        if (now < next) {
                            ready = false;
                            not_ready++;
                        }
                    }
                    if (ready) {
                        std::uniform_int_distribution<int> yield_dist(c.yield_min, c.yield_max);
                        int yield = yield_dist(rng);
                        int64_t income = yield * c.ore_value;
                        total_income += income;
                        collected_count++;
                        update_claim_collect(db, c.id);
                        details += c.ore_emoji + " **" + c.ore_name + "** × " + std::to_string(yield) + " = $" + economy::format_number(income) + "\n";
                    }
                }
                
                if (collected_count == 0) {
                    auto next_ready = std::chrono::system_clock::time_point::max();
                    for (auto& c : claims) {
                        if (c.last_collect) {
                            auto next = *c.last_collect + std::chrono::hours(4);
                            if (next < next_ready) next_ready = next;
                        }
                    }
                    auto ts = std::chrono::system_clock::to_time_t(next_ready);
                    bronx::send_message(bot, event, bronx::error("no claims ready to collect! next one <t:" + std::to_string(ts) + ":R>"));
                    return;
                }
                
                db->update_wallet(uid, total_income);
                auto embed = bronx::success("⛏️ collected from **" + std::to_string(collected_count) + "** claims!\n\n" + details + "\n💰 Total: **$" + economy::format_number(total_income) + "**");
                if (not_ready > 0) {
                    embed.set_footer(dpp::embed_footer().set_text(std::to_string(not_ready) + " claim(s) not ready yet"));
                }
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "renew") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify claim ID: `b.claim renew <id>`"));
                    return;
                }
                uint64_t claim_id;
                try { claim_id = std::stoull(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid claim ID"));
                    return;
                }
                auto claims = get_user_claims(db, uid);
                const ClaimInfo* target = nullptr;
                for (const auto& c : claims) {
                    if (c.id == claim_id) { target = &c; break; }
                }
                if (!target) {
                    bronx::send_message(bot, event, bronx::error("claim not found or doesn't belong to you"));
                    return;
                }
                const auto& tier = get_claim_tier(target->rarity);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < tier.renew_cost) {
                    bronx::send_message(bot, event, bronx::error("need **$" + economy::format_number(tier.renew_cost) + "** to renew (have $" + economy::format_number(wallet) + ")"));
                    return;
                }
                db->update_wallet(uid, -tier.renew_cost);
                renew_claim(db, claim_id, uid);
                auto embed = bronx::success("⛏️ **" + target->ore_name + "** claim renewed for 7 more days!\n💰 Cost: $" + economy::format_number(tier.renew_cost));
                bronx::send_message(bot, event, embed);
                
            } else if (sub == "abandon" || sub == "delete") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify claim ID: `b.claim abandon <id>`"));
                    return;
                }
                uint64_t claim_id;
                try { claim_id = std::stoull(args[1]); } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid claim ID"));
                    return;
                }
                if (abandon_claim(db, claim_id, uid)) {
                    bronx::send_message(bot, event, bronx::success("claim #" + std::to_string(claim_id) + " abandoned"));
                } else {
                    bronx::send_message(bot, event, bronx::error("claim not found or doesn't belong to you"));
                }
                
            } else { // list
                auto claims = get_user_claims(db, uid);
                if (claims.empty()) {
                    bronx::send_message(bot, event, bronx::error("you don't have any mining claims! buy one with `b.claim buy <ore>`"));
                    return;
                }
                
                dpp::embed embed;
                embed.set_color(0xFFA726);
                embed.set_title("⛏️ " + event.msg.author.username + "'s Mining Claims");
                
                std::string desc;
                auto now = std::chrono::system_clock::now();
                for (const auto& c : claims) {
                    auto exp_ts = std::chrono::system_clock::to_time_t(c.expires_at);
                    bool ready = true;
                    if (c.last_collect) {
                        auto next = *c.last_collect + std::chrono::hours(4);
                        ready = (now >= next);
                    }
                    
                    desc += "**#" + std::to_string(c.id) + "** " + c.ore_emoji + " **" + c.ore_name + "** (" + c.rarity + ")\n";
                    desc += "  📦 " + std::to_string(c.yield_min) + "-" + std::to_string(c.yield_max) + " × $" + economy::format_number(c.ore_value) + "/cycle";
                    desc += ready ? (" " + bronx::EMOJI_CHECK) : std::string(" ⏳");
                    desc += " • expires <t:" + std::to_string(exp_ts) + ":R>\n";
                }
                desc += "\n*Claims: " + std::to_string(claims.size()) + "/" + std::to_string(MAX_CLAIMS) + "*";
                embed.set_description(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_claim_tables(db);
            uint64_t uid = event.command.get_issuing_user().id;
            db->ensure_user_exists(uid);
            
            std::string sub = "list";
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) sub = ci_options[0].name;
            
            if (sub == "buy") {
                auto claims = get_user_claims(db, uid);
                if ((int)claims.size() >= MAX_CLAIMS) {
                    event.reply(dpp::message().add_embed(bronx::error("max " + std::to_string(MAX_CLAIMS) + " claims reached!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string ore_name = std::get<std::string>(ci_options[0].options[0].value);
                std::transform(ore_name.begin(), ore_name.end(), ore_name.begin(), ::tolower);
                
                const mining::OreType* found_ore = nullptr;
                for (const auto& o : mining::ore_types) {
                    std::string oname = o.name;
                    std::transform(oname.begin(), oname.end(), oname.begin(), ::tolower);
                    if (oname == ore_name || oname.find(ore_name) != std::string::npos) {
                        found_ore = &o;
                        break;
                    }
                }
                if (!found_ore) {
                    event.reply(dpp::message().add_embed(bronx::error("unknown ore type")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string rarity = mining::get_ore_rarity(found_ore->name);
                const auto& tier = get_claim_tier(rarity);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < tier.cost) {
                    event.reply(dpp::message().add_embed(bronx::error("need **$" + economy::format_number(tier.cost) + "**")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, -tier.cost);
                int avg_value = (found_ore->min_value + found_ore->max_value) / 2;
                create_claim(db, uid, found_ore->name, found_ore->emoji, rarity, tier.yield_min, tier.yield_max, avg_value);
                auto embed = bronx::success("⛏️ purchased **" + found_ore->name + "** claim!\n💰 $" + economy::format_number(tier.cost) + " • Yields " + std::to_string(tier.yield_min) + "-" + std::to_string(tier.yield_max) + " × $" + economy::format_number(avg_value) + "/4h • 7 day duration");
                event.reply(dpp::message().add_embed(embed));
                
            } else if (sub == "collect") {
                auto claims = get_user_claims(db, uid);
                if (claims.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("no active claims")).set_flags(dpp::m_ephemeral));
                    return;
                }
                static thread_local std::mt19937 rng(std::random_device{}());
                int64_t total = 0;
                int count = 0;
                auto now = std::chrono::system_clock::now();
                std::string details;
                for (auto& c : claims) {
                    bool ready = !c.last_collect || (now >= *c.last_collect + std::chrono::hours(4));
                    if (ready) {
                        std::uniform_int_distribution<int> d(c.yield_min, c.yield_max);
                        int y = d(rng);
                        int64_t inc = y * c.ore_value;
                        total += inc;
                        count++;
                        update_claim_collect(db, c.id);
                        details += c.ore_emoji + " " + c.ore_name + " ×" + std::to_string(y) + " = $" + economy::format_number(inc) + "\n";
                    }
                }
                if (count == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("no claims ready yet")).set_flags(dpp::m_ephemeral));
                    return;
                }
                db->update_wallet(uid, total);
                event.reply(dpp::message().add_embed(bronx::success("⛏️ collected from " + std::to_string(count) + " claims\n\n" + details + "\n💰 **$" + economy::format_number(total) + "**")));
                
            } else if (sub == "renew") {
                int64_t claim_id = std::get<int64_t>(ci_options[0].options[0].value);
                auto claims = get_user_claims(db, uid);
                const ClaimInfo* target = nullptr;
                for (const auto& c : claims) { if (c.id == (uint64_t)claim_id) { target = &c; break; } }
                if (!target) { event.reply(dpp::message().add_embed(bronx::error("claim not found")).set_flags(dpp::m_ephemeral)); return; }
                const auto& tier = get_claim_tier(target->rarity);
                int64_t wallet = db->get_wallet(uid);
                if (wallet < tier.renew_cost) { event.reply(dpp::message().add_embed(bronx::error("need $" + economy::format_number(tier.renew_cost))).set_flags(dpp::m_ephemeral)); return; }
                db->update_wallet(uid, -tier.renew_cost);
                renew_claim(db, claim_id, uid);
                event.reply(dpp::message().add_embed(bronx::success("⛏️ claim renewed for 7 days! ($" + economy::format_number(tier.renew_cost) + ")")));
                
            } else if (sub == "abandon") {
                int64_t claim_id = std::get<int64_t>(ci_options[0].options[0].value);
                if (abandon_claim(db, claim_id, uid)) {
                    event.reply(dpp::message().add_embed(bronx::success("claim abandoned")));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("claim not found")).set_flags(dpp::m_ephemeral));
                }
                
            } else { // list
                auto claims = get_user_claims(db, uid);
                if (claims.empty()) { event.reply(dpp::message().add_embed(bronx::error("no active claims")).set_flags(dpp::m_ephemeral)); return; }
                dpp::embed embed;
                embed.set_color(0xFFA726);
                embed.set_title("⛏️ Mining Claims");
                std::string desc;
                auto now = std::chrono::system_clock::now();
                for (const auto& c : claims) {
                    auto exp_ts = std::chrono::system_clock::to_time_t(c.expires_at);
                    bool ready = !c.last_collect || (now >= *c.last_collect + std::chrono::hours(4));
                    desc += "**#" + std::to_string(c.id) + "** " + c.ore_emoji + " **" + c.ore_name + "** (" + c.rarity + ")" + (ready ? (" " + bronx::EMOJI_CHECK) : std::string(" ⏳")) + "\n";
                    desc += "  " + std::to_string(c.yield_min) + "-" + std::to_string(c.yield_max) + " × $" + economy::format_number(c.ore_value) + " • expires <t:" + std::to_string(exp_ts) + ":R>\n";
                }
                embed.set_description(desc);
                event.reply(dpp::message().add_embed(embed));
            }
        },
        // slash options
        {
            dpp::command_option(dpp::co_sub_command, "buy", "purchase a mining claim")
                .add_option(dpp::command_option(dpp::co_string, "ore", "ore type to claim", true)),
            dpp::command_option(dpp::co_sub_command, "collect", "collect ore income from all claims"),
            dpp::command_option(dpp::co_sub_command, "renew", "renew an expiring claim (+7 days)")
                .add_option(dpp::command_option(dpp::co_integer, "id", "claim ID to renew", true)),
            dpp::command_option(dpp::co_sub_command, "list", "view your active claims"),
            dpp::command_option(dpp::co_sub_command, "abandon", "abandon a mining claim")
                .add_option(dpp::command_option(dpp::co_integer, "id", "claim ID to abandon", true)),
        }
    );
    return cmd;
}

} // namespace passive
} // namespace commands
