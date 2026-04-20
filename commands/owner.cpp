// ============================================================================
// owner.cpp — All owner command implementations.
// Declarations are in owner.h (kept minimal for fast incremental builds).
// ============================================================================
#include "owner.h"

// Additional project headers needed only by implementations
#include "../database/operations/community/suggestion_operations.h"
#include "../database/operations/economy/history_operations.h"
#include "../database/operations/economy/gambling_verification.h"
#include "../performance/cache_manager.h"
#include "../performance/chart_renderer.h"
#include "../security/input_validation.h"
#include "../commands/market_state.h"
#include "economy_core.h"
#include "titles.h"
#include "owner/gambling_audit.h"

#include <dpp/version.h>
#include <fstream>
#include <dirent.h>
#include <sstream>
#include <iomanip>
#include <shared_mutex>
#include <sys/stat.h>
#include <unistd.h>

// Anonymous namespace for file-local helpers (replaces 'static' in headers)
namespace {

// helper to compute next weekly rotation boundary (UTC)
time_t next_rotation_time() {
    using namespace std::chrono;
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const int64_t week = 7 * 24 * 60 * 60;
    int64_t next = ((secs / week) + 1) * week;
    return static_cast<time_t>(next);
}

} // anonymous namespace

// ─── OStats paginator ───
static constexpr int OSTATS_TOTAL_PAGES = 8;

struct OStatsState {
    int current_page = 0; // 0-based, 0..OSTATS_TOTAL_PAGES-1
};
static std::map<uint64_t, OStatsState> ostats_states;
static std::recursive_mutex ostats_mutex;

// System memory / process helpers
struct ProcessInfo {
    long vm_rss_kb = 0;
    long vm_size_kb = 0;
    long vm_hwm_kb = 0;
    int  threads = 0;
};

static ProcessInfo get_process_info() {
    ProcessInfo info;
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0)    info.vm_rss_kb  = std::stol(line.substr(6));
        else if (line.rfind("VmSize:", 0) == 0) info.vm_size_kb = std::stol(line.substr(7));
        else if (line.rfind("VmHWM:", 0) == 0)  info.vm_hwm_kb  = std::stol(line.substr(6));
        else if (line.rfind("Threads:", 0) == 0) info.threads    = std::stoi(line.substr(8));
    }
    return info;
}

static int count_open_fds() {
    int count = 0;
    DIR* d = opendir("/proc/self/fd");
    if (d) {
        while (readdir(d)) count++;
        closedir(d);
        count -= 2; // . and ..
    }
    return count;
}

static std::string fmt_mem(long kb) {
    if (kb >= 1048576) return std::to_string(kb / 1024 / 1024) + " GB";
    if (kb >= 1024)    return std::to_string(kb / 1024) + " MB";
    return std::to_string(kb) + " KB";
}

// Helper to run a single SQL query that returns one numeric column, one row
static int64_t sql_count(bronx::db::Database* db, const std::string& sql) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return -1;
    if (mysql_query(conn->get(), sql.c_str()) != 0) { db->get_pool()->release(conn); return -1; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return -1; }
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

// Helper to run a SQL query returning multiple string columns, multiple rows
struct SqlRow { std::vector<std::string> cols; };
static std::vector<SqlRow> sql_query(bronx::db::Database* db, const std::string& sql) {
    std::vector<SqlRow> rows;
    auto conn = db->get_pool()->acquire();
    if (!conn) return rows;
    if (mysql_query(conn->get(), sql.c_str()) != 0) { db->get_pool()->release(conn); return rows; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return rows; }
    int ncols = mysql_num_fields(res);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        SqlRow r;
        for (int i = 0; i < ncols; i++) r.cols.push_back(row[i] ? row[i] : "0");
        rows.push_back(std::move(r));
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return rows;
}

// Build a single ostats page (0-based)
static dpp::message build_ostats_message(dpp::cluster& bot, bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
    OStatsState& state = ostats_states[owner_id];
    int page = state.current_page;

    static const std::vector<std::string> page_titles = {
        "Bot Overview",         // 0
        "Cache & Infrastructure", // 1
        "Command Stats",        // 2
        "Economy Overview",     // 3
        "Fishing & Inventory",  // 4
        "Leveling & Community", // 5
        "System & Process",     // 6
        "Render Previews",      // 7
    };

    std::string desc;
    namespace ch = bronx::chart;
    std::string chart_image;  // Store chart image for pages that need it

    if (page == 0) {
        // ── Page 1: Bot Overview ──
        auto now = std::chrono::system_clock::now();
        auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(now - commands::global_stats.start_time).count();
        uint64_t d = uptime_s / 86400, h = (uptime_s % 86400) / 3600, m = (uptime_s % 3600) / 60, s = uptime_s % 60;
        std::string uptime = std::to_string(d) + "d " + std::to_string(h) + "h " + std::to_string(m) + "m " + std::to_string(s) + "s";

        desc += "**uptime:** " + uptime + "\n";
        desc += "**bot:** " + bot.me.format_username() + " (`" + std::to_string((uint64_t)bot.me.id) + "`)\n";
        desc += "**dpp:** " DPP_VERSION_TEXT "\n";
        desc += "**c++:** " + std::to_string(__cplusplus) + "\n\n";

        // Shards
        auto shards = bot.get_shards();
        double total_ws = 0; int connected = 0;
        desc += "**shards:** " + std::to_string(shards.size()) + "\n";
        for (const auto& [sid, shard] : shards) {
            bool ready = shard->websocket_ping > 0;
            if (ready) { connected++; total_ws += shard->websocket_ping * 1000; }
            desc += "• shard " + std::to_string(sid) + ": " + (ready ? "🟢" : "⚫");
            if (ready) desc += " " + std::to_string((int)(shard->websocket_ping * 1000)) + "ms";
            desc += "\n";
        }
        double avg_ws = connected > 0 ? total_ws / connected : 0;
        desc += "\n**avg ping:** " + std::to_string((int)commands::global_stats.get_average_ping()) + "ms\n";
        desc += "**ws avg:** " + std::to_string((int)avg_ws) + "ms\n\n";

        // Discord object counts
        desc += "**servers:** " + std::to_string(dpp::get_guild_cache()->count()) + "\n";
        desc += "**users:** " + std::to_string(dpp::get_user_cache()->count()) + "\n";
        desc += "**channels:** " + std::to_string(dpp::get_channel_cache()->count()) + "\n";
        desc += "**roles:** " + std::to_string(dpp::get_role_cache()->count()) + "\n";
        desc += "**emojis:** " + std::to_string(dpp::get_emoji_cache()->count()) + "\n";

    } else if (page == 1) {
        // ── Page 2: Cache & Infrastructure ──
        if (bronx::cache::global_cache) {
            auto cs = bronx::cache::global_cache->get_stats();
            desc += "**cache manager**\n";
            desc += "• blacklist: " + std::to_string(cs.blacklist_entries) + "\n";
            desc += "• whitelist: " + std::to_string(cs.whitelist_entries) + "\n";
            desc += "• user prefixes: " + std::to_string(cs.user_prefixes_entries) + "\n";
            desc += "• guild prefixes: " + std::to_string(cs.guild_prefixes_entries) + "\n";
            desc += "• cooldowns: " + std::to_string(cs.cooldown_entries) + "\n";
            desc += "• modules: " + std::to_string(cs.module_entries) + "\n";
            desc += "• commands: " + std::to_string(cs.command_entries) + "\n";
            desc += "• guild toggles: " + std::to_string(cs.guild_toggle_entries) + "\n";
            desc += "• wallets: " + std::to_string(cs.wallet_entries) + "\n";
            desc += "• banks: " + std::to_string(cs.bank_entries) + "\n";
            desc += "• **total:** " + std::to_string(cs.total_entries) + "\n\n";
        } else {
            desc += "**cache manager:** not initialized\n\n";
        }

        // Connection pool
        auto pool = db->get_pool();
        if (pool) {
            desc += "**db pool**\n";
            desc += "• available: " + std::to_string(pool->available_connections()) + "\n";
            desc += "• total: " + std::to_string(pool->total_connections()) + "\n\n";
        }

        // Blacklist / whitelist counts
        auto bl = db->get_global_blacklist();
        auto wl = db->get_global_whitelist();
        desc += "**moderation**\n";
        desc += "• blacklisted users: " + std::to_string(bl.size()) + "\n";
        desc += "• whitelisted users: " + std::to_string(wl.size()) + "\n\n";

        // Config counts
        int64_t custom_prefixes = sql_count(db, "SELECT COUNT(DISTINCT guild_id) FROM guild_prefixes");
        int64_t disabled_cmds   = sql_count(db, "SELECT COUNT(*) FROM guild_command_settings WHERE enabled=0");
        int64_t disabled_mods   = sql_count(db, "SELECT COUNT(*) FROM guild_module_settings WHERE enabled=0");
        int64_t reaction_roles  = sql_count(db, "SELECT COUNT(*) FROM guild_reaction_roles");
        int64_t autopurges      = sql_count(db, "SELECT COUNT(*) FROM guild_autopurges");
        int64_t reminders       = sql_count(db, "SELECT COUNT(*) FROM user_reminders WHERE completed=0");

        desc += "**guild config**\n";
        desc += "• custom prefixes: " + std::to_string(custom_prefixes) + "\n";
        desc += "• disabled commands: " + std::to_string(disabled_cmds) + "\n";
        desc += "• disabled modules: " + std::to_string(disabled_mods) + "\n";
        desc += "• reaction roles: " + std::to_string(reaction_roles) + "\n";
        desc += "• autopurges: " + std::to_string(autopurges) + "\n";
        desc += "• active reminders: " + std::to_string(reminders) + "\n";

    } else if (page == 2) {
        // ── Page 3: Command Stats ──
        desc += "**session totals**\n";
        desc += "• commands run: " + commands::format_number((int64_t)commands::global_stats.total_commands) + "\n";
        desc += "• errors: " + commands::format_number((int64_t)commands::global_stats.total_errors) + "\n";

        // Rates
        auto now = std::chrono::system_clock::now();
        double uptime_hrs = std::chrono::duration_cast<std::chrono::seconds>(now - commands::global_stats.start_time).count() / 3600.0;
        if (uptime_hrs > 0) {
            double cmds_hr = commands::global_stats.total_commands / uptime_hrs;
            desc += "• commands/hour: " + std::to_string((int)cmds_hr) + "\n";
        }
        double err_rate = commands::global_stats.total_commands > 0
            ? (double)commands::global_stats.total_errors / commands::global_stats.total_commands * 100.0
            : 0.0;
        std::ostringstream oss; oss << std::fixed << std::setprecision(2) << err_rate;
        desc += "• error rate: " + oss.str() + "%\n\n";

        // Generate top commands chart
        if (!commands::global_stats.command_usage.empty()) {
            std::vector<ch::BarItem> bars;
            std::vector<std::pair<std::string, uint64_t>> sorted_cmds(
                commands::global_stats.command_usage.begin(), commands::global_stats.command_usage.end());
            std::sort(sorted_cmds.begin(), sorted_cmds.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            int count = 0;
            for (const auto& [cmd, uses] : sorted_cmds) {
                if (count++ >= 15) break;
                bars.push_back({cmd, (int64_t)uses});
                desc += std::to_string(count) + ". `" + cmd + "` — " + std::to_string(uses) + "\n";
            }
            chart_image = ch::render_horizontal_bar_chart("top commands (session)", bars, 600, 0);
            desc += "\n";
        }

        // Error breakdown
        if (!commands::global_stats.error_counts.empty()) {
            desc += "**error breakdown**\n";
            std::vector<std::pair<std::string, uint64_t>> sorted_errs(
                commands::global_stats.error_counts.begin(), commands::global_stats.error_counts.end());
            std::sort(sorted_errs.begin(), sorted_errs.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            int count = 0;
            for (const auto& [err, cnt] : sorted_errs) {
                if (count++ >= 10) break;
                desc += "• `" + err + "`: " + std::to_string(cnt) + "\n";
            }
        }

        // DB-persisted command stats
        int64_t db_total_cmds = sql_count(db, "SELECT COALESCE(SUM(uses),0) FROM command_stats");
        int64_t db_unique_cmds = sql_count(db, "SELECT COUNT(DISTINCT command_name) FROM command_stats");
        int64_t db_unique_users = sql_count(db, "SELECT COUNT(DISTINCT user_id) FROM command_stats");
        if (db_total_cmds > 0) {
            desc += "\n**all-time (db)**\n";
            desc += "• total uses: " + commands::format_number(db_total_cmds) + "\n";
            desc += "• unique commands: " + std::to_string(db_unique_cmds) + "\n";
            desc += "• unique users: " + std::to_string(db_unique_users) + "\n";
        }

    } else if (page == 3) {
        // ── Page 4: Economy Overview ──
        int64_t total_users   = sql_count(db, "SELECT COUNT(*) FROM users");
        int64_t total_wallets = sql_count(db, "SELECT COALESCE(SUM(wallet),0) FROM users");
        int64_t total_banks   = sql_count(db, "SELECT COALESCE(SUM(bank),0) FROM users");
        int64_t total_circ    = total_wallets + total_banks;
        int64_t avg_nw        = sql_count(db, "SELECT COALESCE(AVG(wallet+bank),0) FROM users");
        int64_t max_nw        = sql_count(db, "SELECT COALESCE(MAX(wallet+bank),0) FROM users");

        desc += "**users**\n";
        desc += "• registered: " + commands::format_number(total_users) + "\n";
        int64_t active_24h = sql_count(db, "SELECT COUNT(*) FROM users WHERE last_active >= DATE_SUB(NOW(), INTERVAL 1 DAY)");
        int64_t active_7d  = sql_count(db, "SELECT COUNT(*) FROM users WHERE last_active >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        desc += "• active (24h): " + commands::format_number(active_24h) + "\n";
        desc += "• active (7d): " + commands::format_number(active_7d) + "\n\n";

        desc += "**money supply**\n";
        desc += "• wallets: $" + commands::format_number(total_wallets) + "\n";
        desc += "• banks: $" + commands::format_number(total_banks) + "\n";
        desc += "• **circulation:** $" + commands::format_number(total_circ) + "\n";
        desc += "• avg net worth: $" + commands::format_number(avg_nw) + "\n";
        desc += "• max net worth: $" + commands::format_number(max_nw) + "\n\n";

        // Generate money supply chart
        {
            std::vector<ch::BarItem> money_bars;
            money_bars.push_back({"wallets", total_wallets});
            money_bars.push_back({"banks", total_banks});
            chart_image = ch::render_horizontal_bar_chart("money supply", money_bars, 600, 140);
        }

        // Gambling
        int64_t total_gambled = sql_count(db, "SELECT COALESCE(SUM(total_gambled),0) FROM users");
        int64_t total_won     = sql_count(db, "SELECT COALESCE(SUM(total_won),0) FROM users");
        int64_t total_lost    = sql_count(db, "SELECT COALESCE(SUM(total_lost),0) FROM users");
        desc += "**gambling**\n";
        desc += "• total gambled: $" + commands::format_number(total_gambled) + "\n";
        desc += "• total won: $" + commands::format_number(total_won) + "\n";
        desc += "• total lost: $" + commands::format_number(total_lost) + "\n";
        desc += "• house edge: $" + commands::format_number(total_lost - total_won) + "\n\n";

        // Gambling stats table
        int64_t gbl_games    = sql_count(db, "SELECT COALESCE(SUM(games_played),0) FROM gambling_stats");
        int64_t gbl_bet      = sql_count(db, "SELECT COALESCE(SUM(total_bet),0) FROM gambling_stats");
        int64_t gbl_big_win  = sql_count(db, "SELECT COALESCE(MAX(biggest_win),0) FROM gambling_stats");
        int64_t gbl_big_loss = sql_count(db, "SELECT COALESCE(MAX(biggest_loss),0) FROM gambling_stats");
        desc += "**gambling (detailed)**\n";
        desc += "• games played: " + commands::format_number(gbl_games) + "\n";
        desc += "• total bet: $" + commands::format_number(gbl_bet) + "\n";
        desc += "• biggest win: $" + commands::format_number(gbl_big_win) + "\n";
        desc += "• biggest loss: $" + commands::format_number(gbl_big_loss) + "\n\n";

        // Jackpot
        int64_t jackpot_pool = db->get_jackpot_pool();
        desc += "**jackpot pool:** $" + commands::format_number(jackpot_pool) + "\n";

        // VIP & prestige
        int64_t vip_count     = sql_count(db, "SELECT COUNT(*) FROM users WHERE vip=1");
        int64_t prestige_count = sql_count(db, "SELECT COUNT(*) FROM users WHERE prestige > 0");
        int64_t max_prestige  = sql_count(db, "SELECT COALESCE(MAX(prestige),0) FROM users");
        desc += "**vip users:** " + std::to_string(vip_count) + "\n";
        desc += "**prestige users:** " + std::to_string(prestige_count) + " (max: " + std::to_string(max_prestige) + ")\n";

        // Loans
        int64_t active_loans = sql_count(db, "SELECT COUNT(*) FROM user_loans WHERE remaining > 0");
        int64_t loan_balance = sql_count(db, "SELECT COALESCE(SUM(remaining),0) FROM user_loans WHERE remaining > 0");
        desc += "**active loans:** " + std::to_string(active_loans) + " ($" + commands::format_number(loan_balance) + " outstanding)\n";

        // Trades
        int64_t pending_trades = sql_count(db, "SELECT COUNT(*) FROM guild_trades WHERE status='pending'");
        int64_t completed_trades = sql_count(db, "SELECT COUNT(*) FROM guild_trades WHERE status='completed'");
        desc += "**trades:** " + std::to_string(pending_trades) + " pending, " + std::to_string(completed_trades) + " completed\n";

    } else if (page == 4) {
        // ── Page 5: Fishing & Inventory ──
        int64_t total_fish     = sql_count(db, "SELECT COUNT(*) FROM fish_catches");
        int64_t unsold_fish    = sql_count(db, "SELECT COUNT(*) FROM user_fish_catches WHERE sold=0");
        int64_t unsold_value   = sql_count(db, "SELECT COALESCE(SUM(value),0) FROM user_fish_catches WHERE sold=0");
        int64_t legendary_fish = sql_count(db, "SELECT COUNT(*) FROM user_fish_catches WHERE rarity='legendary'");
        int64_t mutated_fish   = sql_count(db, "SELECT COUNT(*) FROM user_fish_catches WHERE rarity='mutated'");

        desc += "**fishing**\n";
        desc += "• total caught: " + commands::format_number(total_fish) + "\n";
        desc += "• unsold: " + commands::format_number(unsold_fish) + " ($" + commands::format_number(unsold_value) + ")\n";
        desc += "• legendary: " + commands::format_number(legendary_fish) + "\n";
        desc += "• mutated: " + commands::format_number(mutated_fish) + "\n\n";

        // Rarity breakdown
        auto rarity_rows = sql_query(db, "SELECT rarity, COUNT(*) as cnt FROM user_fish_catches GROUP BY rarity ORDER BY cnt DESC LIMIT 10");
        if (!rarity_rows.empty()) {
            desc += "**fish by rarity**\n";
            std::vector<ch::BarItem> rarity_bars;
            for (const auto& r : rarity_rows) {
                int64_t cnt = (int64_t)std::stoll(r.cols[1]);
                desc += "• " + r.cols[0] + ": " + commands::format_number(cnt) + "\n";
                rarity_bars.push_back({r.cols[0], cnt});
            }
            chart_image = ch::render_horizontal_bar_chart("fish by rarity", rarity_bars, 600, 0);
            desc += "\n";
        }

        // Autofishing
        int64_t auto_active  = sql_count(db, "SELECT COUNT(*) FROM user_autofishers WHERE active=1");
        int64_t auto_balance = sql_count(db, "SELECT COALESCE(SUM(balance),0) FROM autofishers");
        int64_t auto_stored  = sql_count(db, "SELECT COUNT(*) FROM autofish_storage");
        desc += "**autofishing**\n";
        desc += "• active: " + std::to_string(auto_active) + "\n";
        desc += "• total balance: $" + commands::format_number(auto_balance) + "\n";
        desc += "• stored fish: " + commands::format_number(auto_stored) + "\n\n";

        // Inventory
        int64_t inv_items    = sql_count(db, "SELECT COALESCE(SUM(quantity),0) FROM inventory");
        int64_t inv_distinct = sql_count(db, "SELECT COUNT(DISTINCT item_id) FROM inventory");
        int64_t inv_users    = sql_count(db, "SELECT COUNT(DISTINCT user_id) FROM inventory");
        desc += "**inventory**\n";
        desc += "• total items: " + commands::format_number(inv_items) + "\n";
        desc += "• distinct items: " + std::to_string(inv_distinct) + "\n";
        desc += "• users with items: " + commands::format_number(inv_users) + "\n\n";

        // Bazaar
        int64_t baz_shares    = sql_count(db, "SELECT COALESCE(SUM(shares),0) FROM bazaar_stock");
        int64_t baz_invested  = sql_count(db, "SELECT COALESCE(SUM(total_invested),0) FROM bazaar_stock");
        int64_t baz_dividends = sql_count(db, "SELECT COALESCE(SUM(total_dividends),0) FROM bazaar_stock");
        desc += "**bazaar**\n";
        desc += "• total shares: " + commands::format_number(baz_shares) + "\n";
        desc += "• invested: $" + commands::format_number(baz_invested) + "\n";
        desc += "• dividends paid: $" + commands::format_number(baz_dividends) + "\n";

        // Mining claims
        int64_t active_claims = sql_count(db, "SELECT COUNT(*) FROM user_mining_claims WHERE expires_at > NOW()");
        desc += "\n**mining claims:** " + std::to_string(active_claims) + " active\n";

    } else if (page == 5) {
        // ── Page 6: Leveling & Community ──
        int64_t xp_users     = sql_count(db, "SELECT COUNT(*) FROM user_xp");
        int64_t xp_total     = sql_count(db, "SELECT COALESCE(SUM(total_xp),0) FROM user_xp");
        int64_t xp_max_level = sql_count(db, "SELECT COALESCE(MAX(level),0) FROM user_xp");
        int64_t lv_servers   = sql_count(db, "SELECT COUNT(*) FROM guild_leveling_config WHERE enabled=1");
        int64_t lv_roles     = sql_count(db, "SELECT COUNT(*) FROM level_roles");

        desc += "**leveling**\n";
        desc += "• users with xp: " + commands::format_number(xp_users) + "\n";
        desc += "• total xp earned: " + commands::format_number(xp_total) + "\n";
        desc += "• max level: " + std::to_string(xp_max_level) + "\n";
        desc += "• servers with leveling: " + std::to_string(lv_servers) + "\n";
        desc += "• level roles: " + std::to_string(lv_roles) + "\n\n";

        // Community
        int64_t sugg_total  = sql_count(db, "SELECT COUNT(*) FROM suggestions");
        int64_t sugg_unread = sql_count(db, "SELECT COUNT(*) FROM suggestions WHERE `read`=0");
        int64_t bug_total   = sql_count(db, "SELECT COUNT(*) FROM bug_reports");
        int64_t bug_unread  = sql_count(db, "SELECT COUNT(*) FROM bug_reports WHERE `read`=0");
        int64_t bug_open    = sql_count(db, "SELECT COUNT(*) FROM bug_reports WHERE resolved=0");
        int64_t patch_count = db->get_patch_count();

        desc += "**community**\n";
        desc += "• suggestions: " + std::to_string(sugg_total) + " (" + std::to_string(sugg_unread) + " unread)\n";
        desc += "• bug reports: " + std::to_string(bug_total) + " (" + std::to_string(bug_unread) + " unread, " + std::to_string(bug_open) + " open)\n";
        desc += "• patch notes: " + std::to_string(patch_count) + "\n\n";

        // Giveaways / guild economy
        int64_t active_giveaways = sql_count(db, "SELECT COUNT(*) FROM guild_giveaways WHERE active=1");
        int64_t guild_bal_total  = sql_count(db, "SELECT COALESCE(SUM(balance),0) FROM guild_balances");
        int64_t guild_donated    = sql_count(db, "SELECT COALESCE(SUM(total_donated),0) FROM guild_balances");
        desc += "**giveaways & guild economy**\n";
        desc += "• active giveaways: " + std::to_string(active_giveaways) + "\n";
        desc += "• guild balance total: $" + commands::format_number(guild_bal_total) + "\n";
        desc += "• total donated: $" + commands::format_number(guild_donated) + "\n\n";

        // Event tracking
        int64_t msg_evts_7d    = sql_count(db, "SELECT COUNT(*) FROM guild_message_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t voice_evts_7d  = sql_count(db, "SELECT COUNT(*) FROM guild_voice_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t member_evts_7d = sql_count(db, "SELECT COUNT(*) FROM guild_member_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t boost_evts_7d  = sql_count(db, "SELECT COUNT(*) FROM guild_boost_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        desc += "**event tracking (7d)**\n";
        desc += "• message events: " + commands::format_number(msg_evts_7d) + "\n";
        desc += "• voice events: " + commands::format_number(voice_evts_7d) + "\n";
        desc += "• member events: " + commands::format_number(member_evts_7d) + "\n";
        desc += "• boost events: " + commands::format_number(boost_evts_7d) + "\n";

        // Pets
        int64_t total_pets = sql_count(db, "SELECT COUNT(*) FROM user_pets");
        desc += "\n**pets:** " + commands::format_number(total_pets) + " owned\n";

        // Crews
        int64_t total_crews = sql_count(db, "SELECT COUNT(*) FROM crews");
        int64_t crew_members = sql_count(db, "SELECT COUNT(*) FROM crew_members");
        desc += "**crews:** " + std::to_string(total_crews) + " (" + std::to_string(crew_members) + " members)\n";

    } else if (page == 6) {
        // ── Page 7: System & Process ──
        auto mem = get_process_info();
        desc += "**memory**\n";
        desc += "• rss: " + fmt_mem(mem.vm_rss_kb) + "\n";
        desc += "• peak rss: " + fmt_mem(mem.vm_hwm_kb) + "\n";
        desc += "• virtual: " + fmt_mem(mem.vm_size_kb) + "\n\n";

        // Create memory summary card
        {
            std::vector<std::pair<std::string, std::string>> mem_stats = {
                {"RSS", fmt_mem(mem.vm_rss_kb)},
                {"Peak", fmt_mem(mem.vm_hwm_kb)},
                {"Virtual", fmt_mem(mem.vm_size_kb)},
                {"Threads", std::to_string(mem.threads)}
            };
            chart_image = ch::render_summary_card("process memory & stats", mem_stats, 600, 160);
        }

        desc += "**process**\n";
        desc += "• threads: " + std::to_string(mem.threads) + "\n";
        desc += "• open fds: " + std::to_string(count_open_fds()) + "\n";

        // executable size
        struct stat st;
        if (stat("/proc/self/exe", &st) == 0) {
            double mb = st.st_size / 1048576.0;
            std::ostringstream oss; oss << std::fixed << std::setprecision(1) << mb;
            desc += "• binary size: " + oss.str() + " MB\n";
        }

        char hostname[256] = {};
        gethostname(hostname, sizeof(hostname));
        desc += "• hostname: " + std::string(hostname) + "\n\n";

        // Ping percentiles
        if (commands::global_stats.ping_history.size() >= 5) {
            auto sorted_pings = commands::global_stats.ping_history;
            std::sort(sorted_pings.begin(), sorted_pings.end());
            size_t n = sorted_pings.size();
            desc += "**ping percentiles** (" + std::to_string(n) + " samples)\n";
            desc += "• min: " + std::to_string((int)sorted_pings[0]) + "ms\n";
            desc += "• p50: " + std::to_string((int)sorted_pings[n/2]) + "ms\n";
            desc += "• p95: " + std::to_string((int)sorted_pings[(size_t)(n*0.95)]) + "ms\n";
            desc += "• p99: " + std::to_string((int)sorted_pings[(size_t)(n*0.99)]) + "ms\n";
            desc += "• max: " + std::to_string((int)sorted_pings[n-1]) + "ms\n\n";
        }

        // DB table sizes
        auto table_rows = sql_query(db,
            "SELECT table_name, table_rows, ROUND(data_length/1024/1024, 2) AS data_mb "
            "FROM information_schema.tables WHERE table_schema='bronxbot' "
            "ORDER BY data_length DESC LIMIT 10");
        if (!table_rows.empty()) {
            desc += "**top tables by size**\n";
            std::vector<ch::BarItem> table_bars;
            for (const auto& r : table_rows) {
                desc += "• " + r.cols[0] + ": ~" + r.cols[1] + " rows (" + r.cols[2] + " MB)\n";
                try {
                    table_bars.push_back({r.cols[0], (int64_t)std::stoll(r.cols[1])});
                } catch (...) {}
            }
            if (!table_bars.empty() && chart_image.empty()) {
                chart_image = ch::render_horizontal_bar_chart("db table sizes (rows)", table_bars, 600, 0);
            }
        }
    } else if (page == 7) {
        // ── Page 8: Render Previews ──
        desc += "Active ephemeral preview environments from Render.\n\n";

        auto previews = sql_query(db, "SELECT branch, commit_sha, preview_url, status, created_at FROM site_previews WHERE status = 'active' ORDER BY created_at DESC LIMIT 10");
        
        if (previews.empty()) {
            desc += "No active previews found.";
        } else {
            for (const auto& p : previews) {
                // p.cols order: branch (0), commit_sha (1), preview_url (2), status (3), created_at (4)
                std::string branch = p.cols[0];
                std::string sha = p.cols[1].substr(0, 7);
                std::string url = p.cols[2];
                std::string created = p.cols[4];

                desc += "• **" + branch + "** (`" + sha + "`)\n";
                desc += "  [view site](" + url + ") • " + created + "\n\n";
            }
            if (previews.size() >= 10) {
                desc += "*Showing latest 10 active previews.*";
            }
        }

        // Display totals
        int64_t total_active = sql_count(db, "SELECT COUNT(*) FROM site_previews WHERE status = 'active'");
        int64_t total_historical = sql_count(db, "SELECT COUNT(*) FROM site_previews");
        desc += "\n\n**summary**\n";
        desc += "• active: " + std::to_string(total_active) + "\n";
        desc += "• total historical: " + std::to_string(total_historical) + "\n";
    }

    // Truncate if too long
    if (desc.size() > 4000) desc = desc.substr(0, 3997) + "...";

    auto embed = bronx::create_embed(desc);
    embed.set_title("📊 " + page_titles[page]);
    embed.set_color(0xFF6B35);
    embed.set_footer(dpp::embed_footer().set_text(
        "Page " + std::to_string(page + 1) + " / " + std::to_string(OSTATS_TOTAL_PAGES)
        + " • " + page_titles[page]));
    embed.set_timestamp(time(0));

    dpp::message msg;
    msg.add_embed(embed);

    // Add chart image if generated for this page
    if (!chart_image.empty()) {
        std::string chart_filename = "chart_page" + std::to_string(page) + ".png";
        msg.add_file(chart_filename, chart_image);
        embed.set_image("attachment://" + chart_filename);
    }

    // Navigation buttons
    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);

    dpp::component first_btn;
    first_btn.set_type(dpp::cot_button).set_label("⏮").set_style(dpp::cos_secondary)
        .set_id("ostats_first").set_disabled(page == 0);
    nav_row.add_component(first_btn);

    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button).set_label("◀ Prev").set_style(dpp::cos_primary)
        .set_id("ostats_prev").set_disabled(page == 0);
    nav_row.add_component(prev_btn);

    // Page indicator (disabled button as label)
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button)
        .set_label(std::to_string(page + 1) + " / " + std::to_string(OSTATS_TOTAL_PAGES))
        .set_style(dpp::cos_secondary).set_id("ostats_page").set_disabled(true);
    nav_row.add_component(page_btn);

    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button).set_label("Next ▶").set_style(dpp::cos_primary)
        .set_id("ostats_next").set_disabled(page >= OSTATS_TOTAL_PAGES - 1);
    nav_row.add_component(next_btn);

    dpp::component last_btn;
    last_btn.set_type(dpp::cot_button).set_label("⏭").set_style(dpp::cos_secondary)
        .set_id("ostats_last").set_disabled(page >= OSTATS_TOTAL_PAGES - 1);
    nav_row.add_component(last_btn);

    msg.add_component(nav_row);

    // Quick-jump row — select menu for pages
    dpp::component jump_row;
    jump_row.set_type(dpp::cot_action_row);
    dpp::component select;
    select.set_type(dpp::cot_selectmenu).set_placeholder("Jump to page…").set_id("ostats_jump");
    for (int i = 0; i < OSTATS_TOTAL_PAGES; i++) {
        select.add_select_option(
            dpp::select_option(std::to_string(i+1) + ". " + page_titles[i], std::to_string(i))
                .set_default(i == page));
    }
    jump_row.add_component(select);
    msg.add_component(jump_row);

    return msg;
}

// Pagination state for suggestions view
struct SuggestState {
    int current_page = 0;
    std::string order_by = "submitted_at"; // column name
    bool asc = false;
};
static std::map<uint64_t, SuggestState> suggest_states;
static std::recursive_mutex suggest_mutex;

// Pagination state for server list view
struct ServerListState {
    int current_page = 0;
    std::string sort_by = "name"; // name, members, id
    bool asc = true;
};
static std::map<uint64_t, ServerListState> server_list_states;
static std::recursive_mutex server_list_mutex;

// Pagination state for command history view
struct CmdHistoryState {
    int current_page = 0;
    uint64_t target_user = 0;  // the user whose history we're viewing
    std::string filter = "";    // optional filter by entry_type (CMD, BAL, FSH, etc)
};
static std::map<uint64_t, CmdHistoryState> cmdhistory_states;
static std::recursive_mutex cmdhistory_mutex;

// Helper to format a history entry for display
static std::string format_history_entry(const bronx::db::HistoryEntry& e) {
    std::string result = "[**" + e.entry_type + "**] " + e.description;
    if (e.amount != 0) {
        std::string sign = e.amount > 0 ? "+" : "";
        result += " (" + sign + commands::format_number(e.amount) + ")";
    }
    return result;
}

// Helper to format timestamp as relative time
static std::string format_relative_time(std::chrono::system_clock::time_point tp) {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - tp).count();
    
    if (diff < 60) return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    return std::to_string(diff / 86400) + "d ago";
}

// Helper for constructing the message for command history view
static constexpr int HISTORY_PER_PAGE = 10;
static dpp::message build_cmdhistory_message(bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
    CmdHistoryState& state = cmdhistory_states[owner_id];
    
    if (state.target_user == 0) {
        auto embed = bronx::create_embed("No user selected. Use `.cmdh <user>` to view history.");
        dpp::message msg;
        msg.add_embed(embed);
        return msg;
    }
    
    int total = db->get_history_count(state.target_user);
    int pages = std::max(1, (total + HISTORY_PER_PAGE - 1) / HISTORY_PER_PAGE);
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;
    
    int offset = state.current_page * HISTORY_PER_PAGE;
    auto entries = db->fetch_history(state.target_user, HISTORY_PER_PAGE, offset);
    
    std::string desc;
    if (entries.empty()) {
        desc = "no history recorded for this user";
    } else {
        for (const auto& e : entries) {
            desc += format_history_entry(e);
            desc += " _" + format_relative_time(e.created_at) + "_\n";
        }
    }
    desc += "\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(pages) + "_";
    desc += " • _" + std::to_string(total) + " total entries_";
    
    auto embed = bronx::create_embed(desc)
        .set_title("Command History for <@" + std::to_string(state.target_user) + ">")
        .set_color(0x7289DA);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Navigation row
    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ Previous")
        .set_style(dpp::cos_secondary)
        .set_id("cmdh_nav_prev")
        .set_disabled(pages <= 1);
    nav_row.add_component(prev_btn);
    
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("Next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("cmdh_nav_next")
        .set_disabled(pages <= 1);
    nav_row.add_component(next_btn);
    
    dpp::component refresh_btn;
    refresh_btn.set_type(dpp::cot_button)
        .set_label("🔄 Refresh")
        .set_style(dpp::cos_primary)
        .set_id("cmdh_refresh");
    nav_row.add_component(refresh_btn);
    
    dpp::component clear_btn;
    clear_btn.set_type(dpp::cot_button)
        .set_label("🗑 Clear All")
        .set_style(dpp::cos_danger)
        .set_id("cmdh_clear")
        .set_disabled(total == 0);
    nav_row.add_component(clear_btn);
    
    msg.add_component(nav_row);
    
    return msg;
}

// helper for constructing the message for an owner's suggestions view
// keep per-page small so total action rows (per-item + navigation) never exceed
// Discord's limit of 5 rows. 3 items/page gives 3 rows for items + 2 nav rows.
static constexpr int SUGGESTIONS_PER_PAGE = 1;
static dpp::message build_suggestions_message(bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
    SuggestState& state = suggest_states[owner_id];
    // determine order clause
    std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
    auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
    int per_page = SUGGESTIONS_PER_PAGE;
    int total = suggestions.size();
    int pages = (total + per_page - 1) / per_page;
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;

    int start = state.current_page * per_page;
    int end = std::min(start + per_page, total);

    std::string desc;
    if (total == 0) {
        desc = "no suggestions yet";
    } else {
        for (int i = start; i < end; ++i) {
            const auto& s = suggestions[i];
            desc += "**" + std::to_string(s.id) + "** ";
            desc += s.read ? bronx::EMOJI_CHECK : "🆕";
            desc += " <@" + std::to_string(s.user_id) + "> ";
            desc += "(`" + commands::format_number(s.networth) + "`)";
            desc += "\n" + s.suggestion;
            if (i < end - 1) desc += "\n\n";
        }
    }
    desc += "\n\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(std::max(1, pages)) + "_";

    auto embed = bronx::create_embed(desc).set_title("User Suggestions");
    // footer omitted; owner knows context

    dpp::message msg;
    msg.add_embed(embed);

    // build action rows: one per suggestion for read/delete
    for (int i = start; i < end; ++i) {
        const auto& s = suggestions[i];
        dpp::component row;
        row.set_type(dpp::cot_action_row);
        // mark read button
        dpp::component read_btn;
        read_btn.set_type(dpp::cot_button)
            .set_label("Mark Read")
            .set_style(dpp::cos_primary)
            .set_id("suggest_read_" + std::to_string(s.id))
            .set_disabled(s.read);
        row.add_component(read_btn);
        // delete button
        dpp::component del_btn;
        del_btn.set_type(dpp::cot_button)
            .set_label("Delete")
            .set_style(dpp::cos_danger)
            .set_id("suggest_del_" + std::to_string(s.id));
        row.add_component(del_btn);
        msg.add_component(row);
    }

    // navigation row (prev/next/goto/delete)
    dpp::component nav_row1;
    nav_row1.set_type(dpp::cot_action_row);
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ Previous")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_nav_prev");
        // wrapping means button is always enabled
    nav_row1.add_component(prev_btn);
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("Next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_nav_next");
        // always enabled to allow wraparound
    nav_row1.add_component(next_btn);

    // goto button (shows a modal to jump to a specific page)
    dpp::component goto_btn;
    goto_btn.set_type(dpp::cot_button)
        .set_label("Goto")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_goto")
        .set_disabled(total == 0);
    nav_row1.add_component(goto_btn);

    // delete-page button (remove all suggestions on current page)
    dpp::component delete_page_btn;
    delete_page_btn.set_type(dpp::cot_button)
        .set_label("Delete Page")
        .set_style(dpp::cos_danger)
        .set_id("suggest_delete_page")
        .set_disabled(total == 0);
    nav_row1.add_component(delete_page_btn);

    msg.add_component(nav_row1);

    // sort row
    std::string date_label = "Date" + std::string(state.order_by == "submitted_at" ? (state.asc ? " ▲" : " ▼") : "");
    std::string net_label = "Balance" + std::string(state.order_by == "networth" ? (state.asc ? " ▲" : " ▼") : "");
    std::string alpha_label = "Alphabetical" + std::string(state.order_by == "suggestion" ? (state.asc ? " ▲" : " ▼") : "");

    dpp::component nav_row2;
    nav_row2.set_type(dpp::cot_action_row);
    dpp::component sort_date;
    sort_date.set_type(dpp::cot_button)
        .set_label(date_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_date");
    nav_row2.add_component(sort_date);

    dpp::component sort_net;
    sort_net.set_type(dpp::cot_button)
        .set_label(net_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_networth");
    nav_row2.add_component(sort_net);

    dpp::component sort_alpha;
    sort_alpha.set_type(dpp::cot_button)
        .set_label(alpha_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_alpha");
    nav_row2.add_component(sort_alpha);

    msg.add_component(nav_row2);
    return msg;
}

// Helper for building paginated server list message  
// Discord allows max 5 action rows per message:
// - 3 rows for leave buttons (1 per server)
// - 1 row for navigation (prev/next/refresh)
// - 1 row for sorting (name/members/id)
static constexpr int SERVERS_PER_PAGE = 3;
static dpp::message build_servers_message(dpp::cluster& bot, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
    ServerListState& state = server_list_states[owner_id];
    
    // Get all guilds from cache
    std::vector<dpp::guild*> guilds;
    dpp::cache<dpp::guild>* guild_cache = dpp::get_guild_cache();
    
    // Safely iterate through the cache
    {
        std::shared_lock l(guild_cache->get_mutex());
        for (auto& [id, guild_ptr] : guild_cache->get_container()) {
            if (guild_ptr) {
                guilds.push_back(guild_ptr);
            }
        }
    }
    
    // Sort guilds based on current sort settings
    std::sort(guilds.begin(), guilds.end(), [&state](dpp::guild* a, dpp::guild* b) {
        bool result = false;
        if (state.sort_by == "name") {
            result = a->name < b->name;
        } else if (state.sort_by == "members") {
            result = a->member_count < b->member_count;
        } else if (state.sort_by == "id") {
            result = a->id < b->id;
        }
        return state.asc ? result : !result;
    });
    
    int total = guilds.size();
    int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;
    
    int start = state.current_page * SERVERS_PER_PAGE;
    int end = std::min(start + SERVERS_PER_PAGE, total);
    
    // Build description
    std::string desc;
    if (total == 0) {
        desc = "no servers found";
    } else {
        for (int i = start; i < end; ++i) {
            auto* g = guilds[i];
            desc += "**" + g->name + "**\n";
            desc += "• ID: `" + std::to_string(g->id) + "`\n";
            desc += "• Members: " + std::to_string(g->member_count) + "\n";
            desc += "• Owner: <@" + std::to_string(g->owner_id) + ">\n";
            if (i < end - 1) desc += "\n";
        }
    }
    desc += "\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(pages) + "_";
    desc += " • _" + std::to_string(total) + " total servers_";
    
    auto embed = bronx::create_embed(desc).set_title("Server List");
    embed.set_color(0x5865F2);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Add leave buttons for each server on current page
    for (int i = start; i < end; ++i) {
        auto* g = guilds[i];
        dpp::component row;
        row.set_type(dpp::cot_action_row);
        
        dpp::component leave_btn;
        leave_btn.set_type(dpp::cot_button)
            .set_label("Leave " + g->name.substr(0, std::min(40, (int)g->name.length())))
            .set_style(dpp::cos_danger)
            .set_id("serverlist_leave_" + std::to_string(g->id));
        row.add_component(leave_btn);
        
        msg.add_component(row);
    }
    
    // Navigation row
    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ Previous")
        .set_style(dpp::cos_secondary)
        .set_id("serverlist_nav_prev")
        .set_disabled(pages <= 1);
    nav_row.add_component(prev_btn);
    
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("Next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("serverlist_nav_next")
        .set_disabled(pages <= 1);
    nav_row.add_component(next_btn);
    
    dpp::component refresh_btn;
    refresh_btn.set_type(dpp::cot_button)
        .set_label("🔄 Refresh")
        .set_style(dpp::cos_primary)
        .set_id("serverlist_refresh");
    nav_row.add_component(refresh_btn);
    
    msg.add_component(nav_row);
    
    // Sort row
    std::string name_label = "Name" + std::string(state.sort_by == "name" ? (state.asc ? " ▲" : " ▼") : "");
    std::string members_label = "Members" + std::string(state.sort_by == "members" ? (state.asc ? " ▲" : " ▼") : "");
    std::string id_label = "ID" + std::string(state.sort_by == "id" ? (state.asc ? " ▲" : " ▼") : "");
    
    dpp::component sort_row;
    sort_row.set_type(dpp::cot_action_row);
    
    dpp::component sort_name;
    sort_name.set_type(dpp::cot_button)
        .set_label(name_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_name");
    sort_row.add_component(sort_name);
    
    dpp::component sort_members;
    sort_members.set_type(dpp::cot_button)
        .set_label(members_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_members");
    sort_row.add_component(sort_members);
    
    dpp::component sort_id;
    sort_id.set_type(dpp::cot_button)
        .set_label(id_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_id");
    sort_row.add_component(sort_id);
    
    msg.add_component(sort_row);
    
    return msg;
}

namespace commands {

// get_owner_ids() and is_owner() are defined inline in owner.h

// parse a mention or raw numeric string into a snowflake ID
// parse_snowflake() and parse_scope_args() are defined inline in owner.h


std::vector<Command*> get_owner_commands(CommandHandler* handler, bronx::db::Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Owner Stats command (bot statistics) — paginated
    static Command stats("ostats", "view detailed bot statistics (owner only)", "owner", {"ownerstatistics", "botinfo"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }

            uint64_t uid = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
                // Initialize / reset state
                OStatsState& state = ostats_states[uid];

                // Allow jumping to a page: .ostats 3
                if (!args.empty()) {
                    try {
                        int p = std::stoi(args[0]) - 1;
                        if (p >= 0 && p < OSTATS_TOTAL_PAGES) state.current_page = p;
                    } catch (...) {}
                } else {
                    state.current_page = 0;
                }

                msg = build_ostats_message(bot, db, uid);
            }
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&stats);

    // cleanup command to remove obsolete active_title entries from every user's inventory
    static Command cleantitles("cleantitles", "purge legacy active_title items from all inventories", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner")));
                return;
            }
            int removed = db->delete_inventory_item_for_all_users("active_title");
            bot.message_create(dpp::message(event.msg.channel_id,
                bronx::success("removed " + std::to_string(removed) + " active_title entries")));
        },
        nullptr, {});
    cmds.push_back(&cleantitles);

    // title database management (dynamic titles that can be added/edited at runtime)
    static Command titledb("titledb", "manage dynamic titles (add/edit/remove/list)", "owner", {"edittitles"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("usage: titledb <list|add|edit|remove> [...]")));
                return;
            }
            std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            if (action == "list") {
                auto dyn = db->get_dynamic_titles();
                std::string desc = "**dynamic titles**\n\n";
                if (dyn.empty()) {
                    desc += "(none)";
                } else {
                    time_t expiry = next_rotation_time();
                    std::string expstr = expiry ? " (expires <t:" + std::to_string(expiry) + ":R>)" : "";
                    for (auto &t : dyn) {
                        desc += t.item_id + " : " + t.display + " ($" + format_number(t.price) + ")";
                        if (t.rotation_slot) desc += " slot=" + std::to_string(t.rotation_slot);
                        if (t.purchase_limit) desc += " limit=" + std::to_string(t.purchase_limit);
                        if (t.rotation_slot) desc += expstr;
                        desc += "\n";
                    }
                }
                bot.message_create(dpp::message(event.msg.channel_id, bronx::create_embed(desc)));
                return;
            }
            if (action == "remove") {
                if (args.size() < 2) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("usage: titledb remove <item_id>")));
                    return;
                }
                if (db->delete_dynamic_title(args[1])) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("removed title " + args[1])));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("failed to remove title (check ID?)")));
                }
                return;
            }
            if (action == "add" || action == "edit") {
                // syntax: add <item_id> <display> <price> <shop_desc> [slot] [limit]
                if (args.size() < 5) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("usage: titledb " + action + " <item_id> <display> <price> <shop_desc> [rotation_slot] [purchase_limit]")));
                    return;
                }
                commands::TitleDef t;
                t.item_id = args[1];
                t.display = args[2];
                try {
                    t.price = parse_amount(args[3], INT64_MAX);
                } catch (...) { t.price = 0; }
                t.shop_desc = args[4];
                t.rotation_slot = 0;
                t.purchase_limit = 0;
                if (args.size() >= 6) t.rotation_slot = std::stoi(args[5]);
                if (args.size() >= 7) t.purchase_limit = std::stoi(args[6]);
                bool ok = (action == "add") ? db->create_dynamic_title(t)
                                              : db->update_dynamic_title(t);
                if (ok) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("title " + t.item_id + " " + (action=="add"?"added":"updated"))));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("database operation failed")));
                }
                return;
            }
            bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("unknown action for titledb")));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            if (!is_owner(uid)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto get_str = [&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            std::string action = get_str("action");
            if (action.empty() || action == "list") {
                auto dyn = db->get_dynamic_titles();
                std::string desc = "**dynamic titles**\n\n";
                if (dyn.empty()) desc += "(none)";
                else {
                    for (auto &t : dyn) {
                        desc += t.item_id + " : " + t.display + " ($" + format_number(t.price) + ")";
                        if (t.rotation_slot) desc += " slot=" + std::to_string(t.rotation_slot);
                        if (t.purchase_limit) desc += " limit=" + std::to_string(t.purchase_limit);
                        desc += "\n";
                    }
                }
                event.reply(dpp::message().add_embed(bronx::create_embed(desc)));
                return;
            }
            if (action == "remove") {
                std::string id = get_str("item_id");
                if (id.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("need item_id")));
                    return;
                }
                if (db->delete_dynamic_title(id))
                    event.reply(dpp::message().add_embed(bronx::success("removed " + id)));
                else
                    event.reply(dpp::message().add_embed(bronx::error("failed to remove")));
                return;
            }
            if (action == "add" || action == "edit") {
                std::string id = get_str("item_id");
                std::string disp = get_str("display");
                std::string price_str = get_str("price");
                std::string desc = get_str("shop_desc");
                int slot = 0; try { slot = std::stoi(get_str("slot")); } catch(...){}
                int limit = 0; try { limit = std::stoi(get_str("limit")); } catch(...){}
                if (id.empty() || disp.empty() || price_str.empty() || desc.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("missing required fields")));
                    return;
                }
                commands::TitleDef t;
                t.item_id = id;
                t.display = disp;
                try { t.price = parse_amount(price_str, INT64_MAX); } catch(...) { t.price = 0; }
                t.shop_desc = desc;
                t.rotation_slot = slot;
                t.purchase_limit = limit;
                bool ok = (action == "add") ? db->create_dynamic_title(t) : db->update_dynamic_title(t);
                if (ok)
                    event.reply(dpp::message().add_embed(bronx::success("title " + id + " " + (action=="add"?"added":"updated"))));
                else
                    event.reply(dpp::message().add_embed(bronx::error("database error")));
                return;
            }
            event.reply(dpp::message().add_embed(bronx::error("unknown action")));
        }, { dpp::command_option(dpp::co_string,"action","list|add|edit|remove",false)
            .add_choice(dpp::command_option_choice("list","list"))
            .add_choice(dpp::command_option_choice("add","add"))
            .add_choice(dpp::command_option_choice("edit","edit"))
            .add_choice(dpp::command_option_choice("remove","remove")),
           dpp::command_option(dpp::co_string,"item_id","id of title",false),
           dpp::command_option(dpp::co_string,"display","display text",false),
           dpp::command_option(dpp::co_string,"price","price",false),
           dpp::command_option(dpp::co_string,"shop_desc","shop description",false),
           dpp::command_option(dpp::co_integer,"slot","rotation slot",false),
           dpp::command_option(dpp::co_integer,"limit","purchase limit",false) });
    cmds.push_back(&titledb);
    
    // Servers list command (paginated with leave buttons)
    static Command servers_cmd("servers", "view and manage all servers the bot is in (owner only)", "owner", {"serverlist", "guilds"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            
            uint64_t owner_id = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
                // Initialize state if missing
                if (server_list_states.find(owner_id) == server_list_states.end()) {
                    server_list_states[owner_id] = {};
                }
                
                // Build and send the server list
                msg = build_servers_message(bot, owner_id);
            }
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&servers_cmd);
    
    // mysql eval command
    static Command mysql_cmd("mysql", "execute arbitrary mysql statement (owner only)", "owner", {"sql"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                std::string help = "usage: mysql <SQL statement>\n";
                help += "\ncommon examples:\n";
                help += "```sql\n";
                help += "SELECT * FROM tablename;          -- grab info initially\n";
                help += "SHOW TABLES;\n";
                help += "SHOW COLUMNS FROM tablename;\n";
                help += "ALTER TABLE tablename ADD COLUMN newcol VARCHAR(255);\n";
                help += "```";
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error(help)));
                return;
            }
            std::string sql;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) sql += " ";
                sql += args[i];
            }

            // SECURITY FIX: Use direct MySQL C API instead of popen() shell injection vector.
            // This eliminates shell metacharacter injection and avoids exposing
            // credentials on the command line (visible in /proc/*/cmdline).
            std::string result;
            auto conn = db->get_pool()->acquire();
            if (!conn) {
                result = "failed to acquire database connection";
            } else {
                if (mysql_query(conn->get(), sql.c_str()) == 0) {
                    MYSQL_RES* res = mysql_store_result(conn->get());
                    if (res) {
                        unsigned int num_fields = mysql_num_fields(res);
                        MYSQL_FIELD* fields = mysql_fetch_fields(res);
                        // Header row
                        for (unsigned int i = 0; i < num_fields; i++) {
                            if (i) result += "\t";
                            result += fields[i].name;
                        }
                        result += "\n";
                        // Data rows (limit to 50 rows to avoid message overflow)
                        int row_count = 0;
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res)) && row_count < 50) {
                            for (unsigned int i = 0; i < num_fields; i++) {
                                if (i) result += "\t";
                                result += row[i] ? row[i] : "NULL";
                            }
                            result += "\n";
                            row_count++;
                        }
                        uint64_t total_rows = mysql_num_rows(res);
                        if (total_rows > 50) {
                            result += "... (" + std::to_string(total_rows - 50) + " more rows)\n";
                        }
                        mysql_free_result(res);
                    } else {
                        // Non-SELECT statement (INSERT/UPDATE/DELETE/ALTER/etc.)
                        uint64_t affected = mysql_affected_rows(conn->get());
                        result = "OK, " + std::to_string(affected) + " rows affected";
                    }
                } else {
                    result = "MySQL error: ";
                    result += mysql_error(conn->get());
                }
                db->get_pool()->release(conn);
            }
            if (result.empty()) result = "(no output)";
            // Truncate to fit Discord message limit
            if (result.size() > 1900) result = result.substr(0, 1900) + "\n... (truncated)";
            bot.message_create(dpp::message(event.msg.channel_id, "```" + result + "```"));
        }
    );
    cmds.push_back(&mysql_cmd);
    
    // machine learning settings commands
    static Command mlstatus("mlstatus", "show ML configuration settings (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
 
            // ── Market regime block ──
            std::string market_block = db->get_market_state_report();
            if (market_block.empty()) {
                market_block = "⚫ **market regime:** not yet classified\n"
                               "  run `mlclassify` to perform the first classification\n";
            }
 
            // ── ML settings block (unchanged) ──
            auto list = db->list_ml_settings();
            std::string settings_block;
            if (list.empty()) {
                settings_block = "*(no ML settings stored)*\n";
            } else {
                for (auto& p : list) {
                    settings_block += p.first + " = " + p.second + "\n";
                }
            }
 
            // ── Price effect report block ──
            int hours = 24;
            if (!args.empty()) {
                try { hours = std::stoi(args[0]); } catch(...) {}
            }
            std::string effect = db->get_ml_effect_report(hours);
 
            std::string msgtxt =
                "**— market state —**\n" + market_block +
                "\n**— ml settings —**\n" + settings_block +
                "\n**— price adjustments (last " + std::to_string(hours) + "h) —**\n" + effect;
 
            bot.message_create(dpp::message(event.msg.channel_id, bronx::info(msgtxt)));
        });
    cmds.push_back(&mlstatus);

    // list of known ML configuration keys with descriptions; update as features are added
    static Command mlclassify("mlclassify",
        "run market state classifier and update regime (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
 
            // optional min_samples argument (default 50)
            int min_samples = 50;
            if (!args.empty()) {
                try { min_samples = std::stoi(args[0]); } catch(...) {}
                if (min_samples < 1) min_samples = 1;
            }
 
            std::string result = db->classify_market_state(min_samples);
            bot.message_create(dpp::message(event.msg.channel_id, bronx::info(
                "**market classifier result** (min_samples=" +
                std::to_string(min_samples) + "):\n" + result +
                "\nrun `mlstatus` for full regime report."
            )));
        });
    cmds.push_back(&mlclassify);

    static const std::vector<std::pair<std::string,std::string>> ml_keys = {
        {"tune_scale",             "price tuning scale factor (float); overwritten by classifier unless tune_scale_override is set"},
        {"catch_win_prob",         "(example) chance of winning fishing; not actively used"},
        {"profit_floor",           "minimum profit threshold for ML adjustments (supports k/m suffix)"},
        {"bait_delta_cap",         "maximum per-run bait price change (int; use k/m as needed)"},
        {"bait_price_min",         "minimum allowable bait price (int, clamped; suffix _<level> for per-rarity)"},
        {"bait_price_max",         "maximum allowable bait price (int, clamped; suffix _<level> for per-rarity)"},
        // ── new market-state keys ──
        {"market_stable_band",     "fraction of profit_target within which market is STABLE (default 0.15 = ±15%)"},
        {"market_inflation_band",  "fraction above target at which market enters INFLATION (default 0.60 = 60%)"},
        {"market_critical_band",   "fraction deviation at which market enters CRITICAL state (default 1.50 = 150%)"},
        {"tune_scale_override",    "set to any value to prevent classifier from overwriting tune_scale (manual lock)"},
        {"tune_decay_override",    "set to any value to prevent classifier from overwriting tune_decay (manual lock)"},
    };

    auto normalize_ml_value = [](const std::string &raw)->std::string {
        // support optional < or > prefix, and k/m/b suffixes for thousands/millions/billions
        if (raw.empty()) return raw;
        size_t idx = 0;
        char op = '\0';
        if (raw[0] == '<' || raw[0] == '>') {
            op = raw[0];
            idx = 1;
        }
        std::string core = raw.substr(idx);
        // trim spaces
        while (!core.empty() && isspace((unsigned char)core.front())) core.erase(core.begin());
        while (!core.empty() && isspace((unsigned char)core.back())) core.pop_back();
        if (core.empty()) return raw;
        // detect suffix
        char suf = '\0';
        if (core.size() > 1) {
            char last = tolower(core.back());
            if (last=='k' || last=='m' || last=='b') {
                suf = last;
                core.pop_back();
            }
        }
        // attempt numeric parse
        try {
            if (core.find('.') != std::string::npos) {
                double f = std::stod(core);
                if (suf == 'k') f *= 1e3;
                else if (suf == 'm') f *= 1e6;
                else if (suf == 'b') f *= 1e9;
                std::string out = std::to_string(f);
                if (op) out = std::string(1, op) + out;
                return out;
            } else {
                long long n = std::stoll(core);
                if (suf == 'k') n *= 1000;
                else if (suf == 'm') n *= 1000000;
                else if (suf == 'b') n *= 1000000000;
                std::string out = std::to_string(n);
                if (op) out = std::string(1, op) + out;
                return out;
            }
        } catch(...) {
            return raw; // fallback to unmodified
        }
    };

    static Command mlset("mlset", "set an ML configuration key/value (owner only)", "owner", {}, false,
        [db,normalize_ml_value](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.size() < 2) {
                // build usage/help embed including list of known keys
                std::string help = "usage: mlset <key> <value>\n\n*available keys:*\n";
                for (auto &p : ml_keys) {
                    help += "• `" + p.first + "` - " + p.second + "\n";
                }
                help += "\nuse `mlstatus` to view current values. \n";
                help += "(you can also specify `bait_price_min_3` etc. to limit a particular bait level)";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error(help)));
                return;
            }
            std::string key = args[0];
            std::string val;
            for (size_t i=1;i<args.size();++i){ val += args[i]; if(i+1<args.size()) val += " "; }
            std::string norm = normalize_ml_value(val);
            bool stored = db->set_ml_setting(key,norm);
            if (stored) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("setting stored: " + norm)));
                // if this is a price bound, immediately clamp existing shop prices
                std::string lk = key;
                std::string prefix;
                int level = 0;
                // detect optional level suffix
                size_t pos = lk.find_last_of('_');
                if (pos != std::string::npos && pos + 1 < lk.size()) {
                    std::string suffix = lk.substr(pos+1);
                    bool all_digits = std::all_of(suffix.begin(), suffix.end(), ::isdigit);
                    if (all_digits) {
                        level = std::stoi(suffix);
                        prefix = lk.substr(0,pos);
                    }
                }
                if (prefix.empty()) prefix = lk; // no suffix
                if (prefix == "bait_price_max" || prefix == "bait_price_min") {
                    // build SQL clamp depending on min/max
                    std::string op = (prefix == "bait_price_max") ? "LEAST" : "GREATEST";
                    std::string value = norm;
                    std::string q;
                    if (level <= 0) {
                        q = "UPDATE shop_items SET price = " + op + "(price, " + value + ") WHERE category='bait'";
                    } else {
                        q = "UPDATE shop_items SET price = " + op + "(price, " + value + ") WHERE category='bait' AND level=" + std::to_string(level);
                    }
                    if (!db->execute(q)) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to clamp existing prices")));
                    }
                }
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to store setting")));
            }
        });
    cmds.push_back(&mlset);

    static Command mldelete("mldelete", "remove an ML configuration key (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.size() != 1) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: mldelete <key>")));
                return;
            }
            if (db->delete_ml_setting(args[0])) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("setting removed")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to remove setting")));
            }
        });
    cmds.push_back(&mldelete);

    static Command invdbg("invdebug", "enable/disable inventory debug logging (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.empty()) {
                // no arguments: toggle the current state
                bool cur = db->get_inventory_debug();
                db->set_inventory_debug(!cur);
                bool now = db->get_inventory_debug();
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(std::string("inventory debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = args[0]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_inventory_debug(true);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("inventory debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_inventory_debug(false);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("inventory debug disabled")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: invdebug [on|off]")));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto param = event.get_parameter("mode");
            if (!std::holds_alternative<std::string>(param) || std::get<std::string>(param).empty()) {
                // toggle current state when no explicit mode provided
                bool cur = db->get_inventory_debug();
                db->set_inventory_debug(!cur);
                bool now = db->get_inventory_debug();
                event.reply(dpp::message().add_embed(bronx::success(std::string("inventory debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = std::get<std::string>(param);
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_inventory_debug(true);
                event.reply(dpp::message().add_embed(bronx::success("inventory debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_inventory_debug(false);
                event.reply(dpp::message().add_embed(bronx::success("inventory debug disabled")));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("usage: /invdebug [mode:on|off]")));
            }
        }, { dpp::command_option(dpp::co_string, "mode", "on or off (omit to toggle)", false) });
    cmds.push_back(&invdbg);

    static Command dbdebug("dbdebug", "enable/disable verbose database connection logging (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.empty()) {
                bool cur = db->get_connection_debug();
                db->set_connection_debug(!cur);
                bool now = db->get_connection_debug();
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(std::string("connection debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = args[0]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_connection_debug(true);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("connection debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_connection_debug(false);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("connection debug disabled")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: dbdebug [on|off]")));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto param = event.get_parameter("mode");
            if (!std::holds_alternative<std::string>(param) || std::get<std::string>(param).empty()) {
                bool cur = db->get_connection_debug();
                db->set_connection_debug(!cur);
                bool now = db->get_connection_debug();
                event.reply(dpp::message().add_embed(bronx::success(std::string("connection debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = std::get<std::string>(param);
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_connection_debug(true);
                event.reply(dpp::message().add_embed(bronx::success("connection debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_connection_debug(false);
                event.reply(dpp::message().add_embed(bronx::success("connection debug disabled")));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("usage: /dbdebug [mode:on|off]")));
            }
        }, { dpp::command_option(dpp::co_string, "mode", "on or off (omit to toggle)", false) });
    cmds.push_back(&dbdebug);

    // Presence command
    static Command presence("presence", "change bot presence/status (owner only)", "owner", {"status", "activity"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            
            if (args.empty()) {
                ::std::string description = "**presence guide**\n\n";
                description += "**types:**\n";
                description += "• `online` - Online (green)\n";
                description += "• `idle` - Idle (yellow)\n";
                description += "• `dnd` - Do Not Disturb (red)\n";
                description += "• `invisible` - Invisible/Offline\n\n";
                description += "**activity types:**\n";
                description += "• `playing` - Playing <text>\n";
                description += "• `listening` - Listening to <text>\n";
                description += "• `watching` - Watching <text>\n";
                description += "• `streaming` - Streaming <text> <url>\n";
                description += "• `competing` - Competing in <text>\n\n";
                description += "**examples:**\n";
                description += "```\n";
                description += "b.presence online playing with commands\n";
                description += "b.presence dnd listening to music\n";
                description += "b.presence idle watching you\n";
                description += "b.presence online streaming https://twitch.tv/example Live!\n";
                description += "```";
                
                auto embed = bronx::create_embed(description);
                embed.set_color(0x7289DA);
                bronx::add_invoker_footer(embed, event.msg.author);
                
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Usage: `b.presence <status> <activity_type> <text> [url]`")));
                return;
            }
            
            ::std::string status_str = args[0];
            ::std::string activity_type = args[1];
            
            // Parse status
            dpp::presence_status status = dpp::ps_online;
            if (status_str == "idle") status = dpp::ps_idle;
            else if (status_str == "dnd") status = dpp::ps_dnd;
            else if (status_str == "invisible" || status_str == "offline") status = dpp::ps_invisible;
            
            // Parse activity type
            dpp::activity_type type = dpp::at_game;
            if (activity_type == "listening") type = dpp::at_listening;
            else if (activity_type == "watching") type = dpp::at_watching;
            else if (activity_type == "streaming") type = dpp::at_streaming;
            else if (activity_type == "competing") type = dpp::at_competing;
            
            // Get activity text (remaining args)
            ::std::string text;
            ::std::string url;
            
            if (type == dpp::at_streaming && args.size() >= 4) {
                // For streaming: status type url text...
                url = args[2];
                for (size_t i = 3; i < args.size(); i++) {
                    text += args[i];
                    if (i < args.size() - 1) text += " ";
                }
            } else {
                // For other types: status type text...
                for (size_t i = 2; i < args.size(); i++) {
                    text += args[i];
                    if (i < args.size() - 1) text += " ";
                }
            }
            
            if (text.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Activity text cannot be empty!")));
                return;
            }
            
            // Set presence
            bot.set_presence(dpp::presence(status, type, text));
            
            // Confirm
            ::std::string status_name = status_str;
            ::std::string type_name = activity_type;
            
            ::std::string description = "**presence updated**\n\n";
            description += "**status:** " + status_name + "\n";
            description += "**activity:** " + type_name + " " + text;
            if (!url.empty()) {
                description += "\n**url:** " + url;
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_color(0x7289DA);
            bronx::add_invoker_footer(embed, event.msg.author);
            bot.message_create(dpp::message(event.msg.channel_id, embed));
            return;
        });
    cmds.push_back(&presence);

    // Suggestions management (owner only)
    static Command suggestions_cmd("suggestions", "view and manage user suggestions (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }

            uint64_t owner_id = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
                // initialize state if missing
                if (suggest_states.find(owner_id) == suggest_states.end()) {
                    suggest_states[owner_id] = {};
                }

                // build and send the suggestions list
                msg = build_suggestions_message(db, owner_id);
            }
            // ensure the message is sent to the correct channel (build_suggestions_message
            // doesn't know about channels)
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&suggestions_cmd);

    // Command history viewer (owner only)
    static Command cmdhistory_cmd("cmdh", "view command history and balance changes for a user (owner only)", "owner", {"cmdhistory", "history"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            
            uint64_t owner_id = event.msg.author.id;
            bool target_invalid = false;
            bool no_target = false;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
                // Initialize state if missing
                if (cmdhistory_states.find(owner_id) == cmdhistory_states.end()) {
                    cmdhistory_states[owner_id] = {};
                }
                
                CmdHistoryState& state = cmdhistory_states[owner_id];
                
                // If a user argument is provided, set the target
                if (!args.empty()) {
                    uint64_t target_id = 0;
                    // Check mentions first
                    if (!event.msg.mentions.empty()) {
                        target_id = event.msg.mentions[0].first.id;
                    } else {
                        // Try to parse as snowflake
                        target_id = parse_snowflake(args[0]);
                    }
                    
                    if (target_id == 0) {
                        target_invalid = true;
                    } else {
                        state.target_user = target_id;
                        state.current_page = 0;  // reset to first page when switching users
                    }
                }
                
                if (!target_invalid) {
                    if (state.target_user == 0) {
                        no_target = true;
                    } else {
                        // Build the history view
                        msg = build_cmdhistory_message(db, owner_id);
                    }
                }
            }
            
            if (target_invalid) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid user. Usage: `.cmdh <@user>` or `.cmdh <user_id>`")));
                return;
            }
            if (no_target) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `.cmdh <@user>` or `.cmdh <user_id>` to view a user's command history")));
                return;
            }
            
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&cmdhistory_cmd);

    // Give money command (owner only) with payout syntax and alias
    static Command* givemoney = new Command("givemoney", "add or remove money from users (owner only)", "owner", {"payout"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Usage: `givemoney <user(s)/all/everyone> <amount>` (you may also prefix with add/remove as before)")));
                return;
            }

            // optionally consume a leading add/remove token for backwards compatibility
            size_t arg_index = 0;
            ::std::string action = "";
            if (args.size() >= 2) {
                ::std::string first = args[0];
                std::transform(first.begin(), first.end(), first.begin(), ::tolower);
                if (first == "add" || first == "remove") {
                    action = first;
                    arg_index = 1;
                    if (arg_index >= args.size() - 1) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Usage: `givemoney <user(s)/all/everyone> <amount>`")));
                        return;
                    }
                }
            }

            // Amount is always the last argument
            ::std::string amount_str = args.back();
            bool neg_from_string = false;
            if (!amount_str.empty() && amount_str[0] == '-') {
                neg_from_string = true;
                amount_str = amount_str.substr(1);
            }
            int64_t amount;
            try {
                amount = parse_amount(amount_str, INT64_MAX);
            } catch (const ::std::exception& e) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error(::std::string("Invalid amount: ") + e.what())));
                return;
            }
            if (neg_from_string) {
                amount = -amount;
            }
            if (action == "remove") {
                amount = -::std::abs(amount);
            }

            // Check for forbidden payout targets (roles, servers)
            // Check for role mentions
            if (!event.msg.mention_roles.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Payouts to roles are not permitted for security reasons.")));
                return;
            }
            
            // Check for forbidden keywords
            for (size_t i = arg_index; i + 1 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "server" || lower == "guild" || lower == "role") {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::error("Payouts to entire servers or roles are not permitted for security reasons.")));
                    return;
                }
            }

            // determine if applying to everyone
            bool apply_to_all = false;
            for (size_t i = arg_index; i + 1 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "all" || lower == "everyone") {
                    apply_to_all = true;
                    break;
                }
            }

            if (apply_to_all) {
                auto user_ids = db->get_all_user_ids();
                if (user_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::error("No users found in database.")));
                    return;
                }
                int success_count = 0;
                for (uint64_t user_id : user_ids) {
                    if (db->update_wallet(user_id, amount).has_value()) {
                        success_count++;
                    }
                }
                ::std::string description;
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to **" + ::std::to_string(success_count) + "** users!";
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from **" + ::std::to_string(success_count) + "** users!";
                }
                auto embed = bronx::create_embed(description);
                embed.set_color(0x43B581);
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            // Handle multiple users - collect all user IDs from args (except the last one which is amount)
            ::std::vector<uint64_t> target_ids;
            
            // First, add all mentioned users
            for (const auto& mention : event.msg.mentions) {
                target_ids.push_back(mention.first.id);
            }
            
            // Then parse any user IDs from args (all args except the last one).
            // Only treat an argument as an ID if it is entirely numeric; plaintext
            // names (e.g. "123abc") will be ignored here and must be mentioned or
            // resolved by Discord so that event.msg.mentions catches them.
            for (size_t i = arg_index; i + 1 < args.size(); i++) {
                const std::string& tok = args[i];
                if (tok.empty())
                    continue;
                bool all_digits = std::all_of(tok.begin(), tok.end(), ::isdigit);
                if (!all_digits)
                    continue; // not a pure number, probably a username starting with digit
                try {
                    uint64_t user_id = ::std::stoull(tok);
                    // Check if not already added from mentions
                    if (::std::find(target_ids.begin(), target_ids.end(), user_id) == target_ids.end()) {
                        target_ids.push_back(user_id);
                    }
                } catch (...) {
                    // Shouldn't happen since we checked digits, but just in case
                }
            }
            
            if (target_ids.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("No valid users found. Please mention users or provide valid user IDs.")));
                return;
            }
            
            // Apply to all target users
            int success_count = 0;
            ::std::string users_list = "";
            
            for (uint64_t target_id : target_ids) {
                auto result = db->update_wallet(target_id, amount);
                if (result.has_value()) {
                    success_count++;
                    users_list += "<@" + ::std::to_string(target_id) + "> ";
                }
            }
            
            if (success_count == 0) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Failed to update any user wallets.")));
                return;
            }
            
            ::std::string description;
            if (target_ids.size() == 1) {
                // Single user - show new balance
                auto new_balance = db->get_wallet(target_ids[0]);
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to <@" + ::std::to_string(target_ids[0]) + ">!\nnew balance: $" + format_number(new_balance);
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from <@" + ::std::to_string(target_ids[0]) + ">!\nnew balance: $" + format_number(new_balance);
                }
            } else {
                // Multiple users
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to **" + ::std::to_string(success_count) + "** user(s):\n" + users_list;
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from **" + ::std::to_string(success_count) + "** user(s):\n" + users_list;
                }
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_color(0x43B581);
            bronx::add_invoker_footer(embed, event.msg.author);
            
            bot.message_create(dpp::message(event.msg.channel_id, embed));
        });
    cmds.push_back(givemoney);

    // Give item command (owner only) with similar syntax to givemoney
    static Command* giveitem = new Command("giveitem", "add or remove items from users (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.size() < 3) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `giveitem <user(s)/all/everyone> <item_id> <quantity>` (optionally prefix add/remove)")));
                return;
            }

            size_t arg_index = 0;
            ::std::string action = "";
            if (args.size() >= 3) {
                ::std::string first = args[0];
                std::transform(first.begin(), first.end(), first.begin(), ::tolower);
                if (first == "add" || first == "remove") {
                    action = first;
                    arg_index = 1;
                    if (arg_index >= args.size() - 2) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Usage: `giveitem <user(s)/all/everyone> <item_id> <quantity>`")));
                        return;
                    }
                }
            }

            ::std::string item_id = args[args.size() - 2];
            ::std::string qty_str = args.back();
            int quantity = 0;
            try {
                quantity = std::stoi(qty_str);
            } catch (...) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid quantity.")));
                return;
            }
            if (action == "remove") quantity = -std::abs(quantity);

            bool apply_to_all = false;
            for (size_t i = arg_index; i + 2 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "all" || lower == "everyone") {
                    apply_to_all = true;
                    break;
                }
            }

            ::std::vector<uint64_t> target_ids;
            if (!apply_to_all) {
                for (const auto& mention : event.msg.mentions) {
                    target_ids.push_back(mention.first.id);
                }
                for (size_t i = arg_index; i + 2 < args.size(); ++i) {
                    const std::string& tok = args[i];
                    if (tok.empty()) continue;
                    bool all_digits = std::all_of(tok.begin(), tok.end(), ::isdigit);
                    if (!all_digits) continue;
                    try {
                        uint64_t user_id = ::std::stoull(tok);
                        if (::std::find(target_ids.begin(), target_ids.end(), user_id) == target_ids.end()) {
                            target_ids.push_back(user_id);
                        }
                    } catch (...) {}
                }
                if (target_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("No valid users found. Mention users or provide numeric IDs.")));
                    return;
                }
            }

            if (apply_to_all) {
                target_ids = db->get_all_user_ids();
                if (target_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("No users found in database to give items to.")));
                    return;
                }
            }

            int success_count = 0;
            for (uint64_t uid : target_ids) {
                bool ok;
                if (quantity >= 0) {
                    ok = db->add_item(uid, item_id, "other", quantity, "", 1);
                } else {
                    ok = db->remove_item(uid, item_id, -quantity);
                }
                if (ok) success_count++;
            }

            ::std::string description;
            if (apply_to_all) {
                if (target_ids.empty()) {
                    description = "no users available";
                } else if (success_count == 0) {
                    description = "attempted to give items but none of the users could be updated (check DB logs)";
                } else if (quantity >= 0) {
                    description = "gave **" + ::std::to_string(quantity) + "x " + item_id + "** to **" + ::std::to_string(success_count) + "** users!";
                } else {
                    description = "removed **" + ::std::to_string(-quantity) + "x " + item_id + "** from **" + ::std::to_string(success_count) + "** users!";
                }
            } else if (target_ids.size() == 1) {
                if (quantity >= 0)
                    description = "gave <@" + ::std::to_string(target_ids[0]) + "> **" + ::std::to_string(quantity) + "x " + item_id + "**";
                else
                    description = "removed <@" + ::std::to_string(target_ids[0]) + "> **" + ::std::to_string(-quantity) + "x " + item_id + "**";
            } else {
                if (quantity >= 0)
                    description = "gave **" + ::std::to_string(quantity) + "x " + item_id + "** to **" + ::std::to_string(success_count) + "** users";
                else
                    description = "removed **" + ::std::to_string(-quantity) + "x " + item_id + "** from **" + ::std::to_string(success_count) + "** users";
            }
            auto embed = bronx::create_embed(description);
            embed.set_color(0x43B581);
            bronx::add_invoker_footer(embed, event.msg.author);
            bot.message_create(dpp::message(event.msg.channel_id, embed));
        });
    cmds.push_back(giveitem);

    // BAC (Bronx AntiCheat) — global ban management
    static Command* blacklist_cmd = new Command("bac", "manage BAC global bans (owner only)", "owner", {"blacklist"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.bac <add|remove|list> -u <user> [-r <reason>] [-s]`")));
                return;
            }
            ::std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);

            auto parse_id = [&](const ::std::string& str) -> uint64_t {
                ::std::string idstr = str;
                if (idstr.find("<@") == 0 || idstr.find("<@!") == 0) {
                    size_t start = idstr.find_first_of("0123456789");
                    size_t end = idstr.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        idstr = idstr.substr(start, end - start + 1);
                    }
                }
                return ::std::stoull(idstr);
            };

            if (action == "list") {
                auto list = db->get_global_blacklist();
                if (list.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::info("\U0001f6e1\ufe0f BAC ban list is empty.")));
                } else {
                    ::std::string out = "";
                    for (const auto& entry : list) {
                        out += "<@" + ::std::to_string(entry.user_id) + ">";
                        if (!entry.reason.empty()) {
                            out += " — " + entry.reason;
                        }
                        out += "\n";
                    }
                    dpp::embed eb = bronx::info("\U0001f6e1\ufe0f BAC — Banned Users");
                    eb.set_description(out);
                    bot.message_create(dpp::message(event.msg.channel_id, eb));
                }
                return;
            }

            if (action != "add" && action != "remove") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid action. Use add, remove or list.")));
                return;
            }

            // find -u flag, -r flag, and -s (silent) flag
            uint64_t target_id = 0;
            ::std::string reason = "";
            bool silent = false;
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "-s") {
                    silent = true;
                } else if (args[i] == "-u" && i + 1 < args.size()) {
                    try {
                        target_id = parse_id(args[i+1]);
                    } catch (...) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Invalid user identifier.")));
                        return;
                    }
                    ++i; // skip the value
                } else if (args[i] == "-r" && i + 1 < args.size()) {
                    // Collect all remaining args after -r as reason
                    for (size_t j = i + 1; j < args.size(); ++j) {
                        if (args[j] == "-u" || args[j] == "-s") break; // stop if we hit another flag
                        if (!reason.empty()) reason += " ";
                        reason += args[j];
                    }
                    break; // done parsing
                }
            }
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You must specify a user with `-u <user>`.")));
                return;
            }

            bool ok = false;
            if (action == "add") {
                ok = db->add_global_blacklist(target_id, reason.empty() ? "(BAC) manual ban" : "(BAC) " + reason);
                if (ok) {
                    if (!silent) {
                        // Send DM to banned user about appeal with embed and button
                        auto ban_embed = bronx::create_embed(
                            "\U0001f6e1\ufe0f **Bronx AntiCheat (BAC)**\n\n"
                            "you have been **banned** from using this bot.\n\n"
                            "if you believe this was a mistake or would like to appeal, please join our support server.",
                            bronx::COLOR_ERROR);
                        ban_embed.set_title("BAC — Banned");
                        dpp::message dm_msg;
                        dm_msg.add_embed(ban_embed);
                        dm_msg.add_component(
                            dpp::component()
                                .set_type(dpp::cot_action_row)
                                .add_component(
                                    dpp::component()
                                        .set_type(dpp::cot_button)
                                        .set_label("appeal in support server")
                                        .set_style(dpp::cos_link)
                                        .set_url(bronx::SUPPORT_SERVER_URL)
                                )
                        );
                        bot.direct_message_create(target_id, dm_msg);
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::success("\U0001f6e1\ufe0f BAC — user banned. they have been sent a DM with appeal information.")));
                    } else {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::success("\U0001f6e1\ufe0f BAC — user silently banned. no DM was sent.")));
                    }
                }
            } else if (action == "remove") {
                ok = db->remove_global_blacklist(target_id);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("\U0001f6e1\ufe0f BAC — user unbanned.")));
            }
            if (!ok) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed. Check logs.")));
            }
        });
    cmds.push_back(blacklist_cmd);

    // Global whitelist management
    static Command* whitelist_cmd = new Command("whitelist", "manage global command whitelist (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.whitelist <add|remove|list> -u <user> [-r <reason>]`")));
                return;
            }
            ::std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);

            auto parse_id = [&](const ::std::string& str) -> uint64_t {
                ::std::string idstr = str;
                if (idstr.find("<@") == 0 || idstr.find("<@!") == 0) {
                    size_t start = idstr.find_first_of("0123456789");
                    size_t end = idstr.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        idstr = idstr.substr(start, end - start + 1);
                    }
                }
                return ::std::stoull(idstr);
            };

            if (action == "list") {
                auto list = db->get_global_whitelist();
                if (list.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::info("Global whitelist is empty.")));
                } else {
                    ::std::string out = "";
                    for (const auto& entry : list) {
                        out += "<@" + ::std::to_string(entry.user_id) + ">";
                        if (!entry.reason.empty()) {
                            out += " - " + entry.reason;
                        }
                        out += "\n";
                    }
                    dpp::embed eb = bronx::info("Whitelisted users");
                    eb.set_description(out);
                    bot.message_create(dpp::message(event.msg.channel_id, eb));
                }
                return;
            }

            if (action != "add" && action != "remove") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid action. Use add, remove or list.")));
                return;
            }

            // find -u flag and -r flag
            uint64_t target_id = 0;
            ::std::string reason = "";
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "-u" && i + 1 < args.size()) {
                    try {
                        target_id = parse_id(args[i+1]);
                    } catch (...) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Invalid user identifier.")));
                        return;
                    }
                    ++i; // skip the value
                } else if (args[i] == "-r" && i + 1 < args.size()) {
                    // Collect all remaining args after -r as reason
                    for (size_t j = i + 1; j < args.size(); ++j) {
                        if (args[j] == "-u") break; // stop if we hit another flag
                        if (!reason.empty()) reason += " ";
                        reason += args[j];
                    }
                    break; // done parsing
                }
            }
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You must specify a user with `-u <user>`.")));
                return;
            }

            bool ok = false;
            if (action == "add") {
                ok = db->add_global_whitelist(target_id, reason);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("User added to global whitelist.")));
            } else if (action == "remove") {
                ok = db->remove_global_whitelist(target_id);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("User removed from global whitelist.")));
            }
            if (!ok) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed. Check logs.")));
            }
        });
    cmds.push_back(whitelist_cmd);

    // per-guild module toggle (enable/disable entire category)
    static Command* module_cmd = new Command("module", "enable or disable a module. scope: -u <user> -r <role> -c <channel> -e (exclusive)", "utility", {}, true,
        [db, handler](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.module <name> <enable|disable> [-c <channel>] [-u <user>] [-r <role>] [-e]`")));
                return;
            }
            std::string mod = args[0];
            std::string action = args[1];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            bool enable;
            if (action == "enable") enable = true;
            else if (action == "disable") enable = false;
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Action must be 'enable' or 'disable'.")));
                return;
            }
            std::string scope_type;
            uint64_t scope_id;
            bool exclusive;
            if (!parse_scope_args(args, 2, scope_type, scope_id, exclusive)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid scope. Use `-c <channel>`, `-u <user>`, `-r <role>`, `-e` (exclusive, only with channel)")));
                return;
            }
            // Prevent disabling the owner or utility modules to avoid lockout
            if (!enable && (mod == "owner" || mod == "utility") && scope_type == "guild") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Cannot disable the '" + mod + "' module - this would prevent you from managing permissions!")));
                return;
            }
            bool ok = db->set_guild_module_enabled(event.msg.guild_id, mod, enable, scope_type, scope_id, exclusive);
            if (ok) {
                if (handler) handler->notify_module_state_changed(event.msg.guild_id, mod, enable);
                std::string msg = std::string("Module '") + mod + " " + (enable ? "enabled" : "disabled");
                if (scope_type != "guild") {
                    msg += " for ";
                    msg += scope_type + " ";
                    msg += "`" + std::to_string(scope_id) + "`";
                    if (exclusive) msg += " (exclusive)";
                }
                msg += ".";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(msg)));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed.")));
            }
        },
        // slash handler
        [db, handler](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }
            auto get_str=[&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            auto get_bool=[&](const std::string &name)->bool {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<bool>(p)) return std::get<bool>(p);
                return false;
            };
            std::string mod = get_str("module");
            std::string action = get_str("action");
            std::string scope = get_str("scope");
            std::string target = get_str("target");
            bool exclusive = get_bool("exclusive");
            if (!mod.empty() && !action.empty()) {
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                bool enable;
                if (action == "enable") enable = true;
                else if (action == "disable") enable = false;
                else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Action must be 'enable' or 'disable'.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string scope_type;
                uint64_t scope_id;
                if (scope.empty()) {
                    scope_type = "guild";
                    scope_id = 0;
                } else {
                    std::vector<std::string> tmp = {scope, target};
                    bool excl_dummy;
                    if (!parse_scope_args(tmp, 0, scope_type, scope_id, excl_dummy)) {
                        event.reply(dpp::ir_channel_message_with_source,
                            dpp::message().add_embed(bronx::error("Invalid scope/type or target.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                // Validate exclusive mode
                if (exclusive && scope_type != "channel") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Exclusive mode (-e) can only be used with channel scope.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                // Prevent disabling the owner or utility modules to avoid lockout
                if (!enable && (mod == "owner" || mod == "utility") && scope_type == "guild") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + mod + "' module - this would prevent you from managing permissions!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                bool ok = db->set_guild_module_enabled(gid, mod, enable, scope_type, scope_id, exclusive);
                if (ok) {
                    if (handler) handler->notify_module_state_changed(gid, mod, enable);
                    std::string msg = std::string("Module '") + mod + " " + (enable ? "enabled" : "disabled");
                    if (scope_type != "guild") {
                        msg += " for ";
                        msg += scope_type + " ";
                        msg += "`" + std::to_string(scope_id) + "`";
                        if (exclusive) msg += " (exclusive)";
                    }
                    msg += ".";
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success(msg)).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Database operation failed.")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            auto categories = handler->get_commands_by_category();
            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder("select a module")
                  .set_id("owner_mod_select_" + std::to_string(uid));
            for (const auto& [cat, cmds] : categories) {
                select.add_select_option(dpp::select_option(cat, cat));
            }
            dpp::message msg;
            msg.add_component(dpp::component().add_component(select));
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
        }, std::vector<dpp::command_option>{
            dpp::command_option(dpp::co_string, "module", "module name (leave blank for interactive)", false),
            dpp::command_option(dpp::co_string, "action", "enable or disable", false),
            dpp::command_option(dpp::co_string, "scope", "guild/channel/role/user", false),
            dpp::command_option(dpp::co_string, "target", "ID or mention of channel/role/user", false),
            dpp::command_option(dpp::co_boolean, "exclusive", "exclusive mode (only for channels)", false)
        });
    cmds.push_back(module_cmd);

    // per-guild command toggle
    static Command* toggle_cmd = new Command("command", "enable or disable a command. scope: -u <user> -r <role> -c <channel> -e (exclusive)", "utility", {}, true,
        [db, handler](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.command <name> <enable|disable> [-c <channel>] [-u <user>] [-r <role>] [-e]`")));
                return;
            }
            std::string cmd = args[0];
            std::string action = args[1];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            bool enable;
            if (action == "enable") enable = true;
            else if (action == "disable") enable = false;
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Action must be 'enable' or 'disable'.")));
                return;
            }
            std::string scope_type;
            uint64_t scope_id;
            bool exclusive;
            if (!parse_scope_args(args, 2, scope_type, scope_id, exclusive)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid scope. Use `-c <channel>`, `-u <user>`, `-r <role>`, `-e` (exclusive, only with channel)")));
                return;
            }
            // Prevent disabling critical permission commands to avoid lockout
            if (!enable && (cmd == "module" || cmd == "command") && scope_type == "guild") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Cannot disable the '" + cmd + "' command - this would prevent you from re-enabling it!")));
                return;
            }
            bool ok = db->set_guild_command_enabled(event.msg.guild_id, cmd, enable, scope_type, scope_id, exclusive);
            if (ok) {
                // Invalidate cache so the change takes effect immediately
                handler->notify_command_state_changed(event.msg.guild_id, cmd, enable);
                
                std::string msg = std::string("Command '") + cmd + " " + (enable ? "enabled" : "disabled");
                if (scope_type != "guild") {
                    msg += " for ";
                    msg += scope_type + " ";
                    msg += "`" + std::to_string(scope_id) + "`";
                    if (exclusive) msg += " (exclusive)";
                }
                msg += ".";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(msg)));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed.")));
            }
        },
        // slash handler
        [db, handler](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }
            auto get_str=[&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            auto get_bool=[&](const std::string &name)->bool {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<bool>(p)) return std::get<bool>(p);
                return false;
            };
            std::string cmd = get_str("command");
            std::string action = get_str("action");
            std::string scope = get_str("scope");
            std::string target = get_str("target");
            bool exclusive = get_bool("exclusive");
            if (!cmd.empty() && !action.empty()) {
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                bool enable;
                if (action == "enable") enable = true;
                else if (action == "disable") enable = false;
                else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Action must be 'enable' or 'disable'.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string scope_type;
                uint64_t scope_id;
                if (scope.empty()) {
                    scope_type = "guild";
                    scope_id = 0;
                } else {
                    std::vector<std::string> tmp = {scope, target};
                    bool excl_dummy;
                    if (!parse_scope_args(tmp, 0, scope_type, scope_id, excl_dummy)) {
                        event.reply(dpp::ir_channel_message_with_source,
                            dpp::message().add_embed(bronx::error("Invalid scope/type or target.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                // Validate exclusive mode
                if (exclusive && scope_type != "channel") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Exclusive mode (-e) can only be used with channel scope.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                // Prevent disabling critical permission commands to avoid lockout
                if (!enable && (cmd == "module" || cmd == "command") && scope_type == "guild") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + cmd + "' command - this would prevent you from re-enabling it!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                bool ok = db->set_guild_command_enabled(gid, cmd, enable, scope_type, scope_id, exclusive);
                if (ok) {
                    // Invalidate cache so the change takes effect immediately
                    handler->notify_command_state_changed(gid, cmd, enable);
                    
                    std::string msg = std::string("Command '") + cmd + " " + (enable ? "enabled" : "disabled");
                    if (scope_type != "guild") {
                        msg += " for ";
                        msg += scope_type + " ";
                        msg += "`" + std::to_string(scope_id) + "`";
                        if (exclusive) msg += " (exclusive)";
                    }
                    msg += ".";
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success(msg)).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Database operation failed.")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            auto categories = handler->get_commands_by_category();
            // collect unique command names
            std::set<std::string> seen;
            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder("select a command")
                  .set_id("owner_cmd_select_" + std::to_string(uid));
            for (auto& [cat, cmds] : categories) {
                for (auto* cmd : cmds) {
                    if (seen.insert(cmd->name).second) {
                        select.add_select_option(dpp::select_option(cmd->name, cmd->name));
                    }
                }
            }
            dpp::message msg;
            msg.add_component(dpp::component().add_component(select));
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
        }, std::vector<dpp::command_option>{
            dpp::command_option(dpp::co_string, "command", "command name (leave blank for interactive)", false),
            dpp::command_option(dpp::co_string, "action", "enable or disable", false),
            dpp::command_option(dpp::co_string, "scope", "guild/channel/role/user", false),
            dpp::command_option(dpp::co_string, "target", "ID or mention of channel/role/user", false),
            dpp::command_option(dpp::co_boolean, "exclusive", "exclusive mode (only for channels)", false)
        });
    cmds.push_back(toggle_cmd);

    // show current module/command permission settings for this guild
    static Command* permissions_cmd = new Command("permissions", "show all module and command permission settings for this guild", "utility", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& /*args*/) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            uint64_t gid = event.msg.guild_id;

            // helper to format a scope mention
            auto fmt_scope = [](const std::string& scope_type, uint64_t scope_id) -> std::string {
                if (scope_type == "channel") return "<#" + std::to_string(scope_id) + ">";
                if (scope_type == "role")    return "<@&" + std::to_string(scope_id) + ">";
                if (scope_type == "user")    return "<@" + std::to_string(scope_id) + ">";
                return std::to_string(scope_id);
            };

            // Gather data
            auto mod_settings = db->get_all_module_settings(gid);
            auto cmd_settings = db->get_all_command_settings(gid);
            auto mod_scopes   = db->get_all_module_scope_settings(gid);
            auto cmd_scopes   = db->get_all_command_scope_settings(gid);

            bool has_any = !mod_settings.empty() || !cmd_settings.empty() || !mod_scopes.empty() || !cmd_scopes.empty();
            if (!has_any) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::info("No custom permission settings configured for this server. All modules and commands are at their defaults.")));
                return;
            }

            std::string desc;

            // Module guild-wide settings
            if (!mod_settings.empty()) {
                desc += "**__Module Settings (guild-wide)__**\n";
                for (const auto& ms : mod_settings) {
                    desc += (ms.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + ms.module + "** — " + (ms.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            // Module scoped overrides
            if (!mod_scopes.empty()) {
                desc += "**__Module Scope Overrides__**\n";
                for (const auto& s : mod_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + s.name + "** → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
                desc += "\n";
            }

            // Command guild-wide settings
            if (!cmd_settings.empty()) {
                desc += "**__Command Settings (guild-wide)__**\n";
                for (const auto& cs : cmd_settings) {
                    desc += (cs.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + cs.command + "` — " + (cs.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            // Command scoped overrides
            if (!cmd_scopes.empty()) {
                desc += "**__Command Scope Overrides__**\n";
                for (const auto& s : cmd_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + s.name + "` → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
            }

            // Truncate if too long for a single embed (Discord limit ~4096)
            if (desc.size() > 4000) {
                desc = desc.substr(0, 3990) + "\n*...truncated*";
            }

            auto embed = bronx::create_embed(desc);
            embed.set_title("⚙️ Permission Settings");
            embed.set_color(0x5865F2);
            embed.set_footer(dpp::embed_footer().set_text("Use 'module' and 'command' to modify these settings"));
            embed.set_timestamp(time(0));
            bronx::add_invoker_footer(embed, event.msg.author);

            dpp::message msg(event.msg.channel_id, "");
            msg.add_embed(embed);
            msg.set_reference(event.msg.id);
            bot.message_create(msg);
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto fmt_scope = [](const std::string& scope_type, uint64_t scope_id) -> std::string {
                if (scope_type == "channel") return "<#" + std::to_string(scope_id) + ">";
                if (scope_type == "role")    return "<@&" + std::to_string(scope_id) + ">";
                if (scope_type == "user")    return "<@" + std::to_string(scope_id) + ">";
                return std::to_string(scope_id);
            };

            auto mod_settings = db->get_all_module_settings(gid);
            auto cmd_settings = db->get_all_command_settings(gid);
            auto mod_scopes   = db->get_all_module_scope_settings(gid);
            auto cmd_scopes   = db->get_all_command_scope_settings(gid);

            bool has_any = !mod_settings.empty() || !cmd_settings.empty() || !mod_scopes.empty() || !cmd_scopes.empty();
            if (!has_any) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::info("No custom permission settings configured for this server. All modules and commands are at their defaults.")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string desc;

            if (!mod_settings.empty()) {
                desc += "**__Module Settings (guild-wide)__**\n";
                for (const auto& ms : mod_settings) {
                    desc += (ms.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + ms.module + "** — " + (ms.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            if (!mod_scopes.empty()) {
                desc += "**__Module Scope Overrides__**\n";
                for (const auto& s : mod_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + s.name + "** → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
                desc += "\n";
            }

            if (!cmd_settings.empty()) {
                desc += "**__Command Settings (guild-wide)__**\n";
                for (const auto& cs : cmd_settings) {
                    desc += (cs.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + cs.command + "` — " + (cs.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            if (!cmd_scopes.empty()) {
                desc += "**__Command Scope Overrides__**\n";
                for (const auto& s : cmd_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + s.name + "` → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
            }

            if (desc.size() > 4000) {
                desc = desc.substr(0, 3990) + "\n*...truncated*";
            }

            auto embed = bronx::create_embed(desc);
            embed.set_title("⚙️ Permission Settings");
            embed.set_color(0x5865F2);
            embed.set_footer(dpp::embed_footer().set_text("Use /module and /command to modify these settings"));
            embed.set_timestamp(time(0));

            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
        });
    cmds.push_back(permissions_cmd);

    // Purge all data for a user (wipe from every table, cascading from users row)
    static Command purgeuser_cmd("purgeuser", "completely wipe a user's data from the database (owner only)", "owner", {"wipeuser", "clearuser", "resetuser"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("usage: purgeuser <user_id or @mention> [confirm]\n"
                                 "pass `confirm` to skip the warning and delete immediately.")));
                return;
            }

            uint64_t target_id = parse_snowflake(args[0]);
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("could not parse a valid user ID from `" + args[0] + "`")));
                return;
            }

            // Safety: require explicit "confirm" flag
            bool confirmed = (args.size() >= 2 && args[1] == "confirm");
            if (!confirmed) {
                std::string warn = bronx::EMOJI_WARNING + " **this will permanently delete ALL data** for <@" + std::to_string(target_id) + "> (`" + std::to_string(target_id) + "`):\n\n"
                    "economy, inventory, fish catches, autofisher, bazaar, XP, gambling stats, "
                    "command history, cooldowns, wishlists, trades, suggestions, bug reports, "
                    "reminders, and every other record tied to this user.\n\n"
                    "**this action cannot be undone.**\n\n"
                    "to proceed, run:\n```\npurgeuser " + std::to_string(target_id) + " confirm\n```";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::create_embed(warn)));
                return;
            }

            // Build ordered delete queries.  Most child tables cascade from users,
            // but we explicitly delete from tables that might lack a FK or where
            // cascading order matters, then finish with the users row itself.
            std::string uid = std::to_string(target_id);
            std::vector<std::string> queries = {
                // Tables that may lack ON DELETE CASCADE or have unusual FK chains
                "DELETE FROM user_autofish_storage WHERE user_id = " + uid,
                "DELETE FROM user_autofishers WHERE user_id = " + uid,
                // The main delete — cascades to most child tables
                "DELETE FROM users WHERE user_id = " + uid,
            };

            int ok = db->execute_batch(queries);
            int total = static_cast<int>(queries.size());

            if (ok == total) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("purged all data for `" + uid + "` (" + std::to_string(ok) + "/" + std::to_string(total) + " statements succeeded)")));
            } else if (ok > 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::create_embed(bronx::EMOJI_WARNING + " partial purge: " + std::to_string(ok) + "/" + std::to_string(total) + " statements succeeded for `" + uid + "`. check logs for errors.")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("purge failed for `" + uid + "`: " + db->get_last_error())));
            }
        });
    cmds.push_back(&purgeuser_cmd);

    // Gambling audit command
    auto* gambling_audit = commands::owner::get_gambling_audit_owner_command(db);
    if (gambling_audit) {
        cmds.push_back(gambling_audit);
    }

    return cmds;
}

// Owner-specific interaction handlers (currently used by suggestions paginator)
void register_owner_interactions(dpp::cluster& bot, bronx::db::Database* db, CommandHandler* handler) {
    // dropdown for module/command selection
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        std::string custom_id = event.custom_id;
        // Only handle owner-related select menus, let other handlers process their own
        if (custom_id.rfind("owner_mod_select_", 0) != 0 && custom_id.rfind("owner_cmd_select_", 0) != 0
            && custom_id != "ostats_jump") {
            return;
        }
        uint64_t uid = event.command.get_issuing_user().id;
        if (!is_owner(uid)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this interaction is restricted to the bot owner")).set_flags(dpp::m_ephemeral));
            return;
        }

        // ── ostats page-jump select ──
        if (custom_id == "ostats_jump") {
            std::string choice = event.values.empty() ? "0" : event.values[0];
            int target_page = 0;
            try { target_page = std::stoi(choice); } catch (...) {}
            if (target_page < 0) target_page = 0;
            if (target_page >= OSTATS_TOTAL_PAGES) target_page = OSTATS_TOTAL_PAGES - 1;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
                ostats_states[uid].current_page = target_page;
                msg = build_ostats_message(bot, db, uid);
            }
            event.reply(dpp::ir_update_message, msg);
            return;
        }

        std::string choice = event.values.empty() ? "" : event.values[0];
        if (custom_id.rfind("owner_mod_select_",0)==0) {
            // produce ephemeral enable/disable buttons for module
            dpp::component row;
            dpp::component en;
            en.set_type(dpp::cot_button).set_label("Enable").set_style(dpp::cos_success)
                .set_id("owner_module_enable_" + choice + "_" + std::to_string(uid));
            dpp::component dis;
            dis.set_type(dpp::cot_button).set_label("Disable").set_style(dpp::cos_danger)
                .set_id("owner_module_disable_" + choice + "_" + std::to_string(uid));
            row.add_component(en);
            row.add_component(dis);
            dpp::message msg;
            msg.add_component(row);
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
            return;
        } else if (custom_id.rfind("owner_cmd_select_",0)==0) {
            dpp::component row;
            dpp::component en;
            en.set_type(dpp::cot_button).set_label("Enable").set_style(dpp::cos_success)
                .set_id("owner_command_enable_" + choice + "_" + std::to_string(uid));
            dpp::component dis;
            dis.set_type(dpp::cot_button).set_label("Disable").set_style(dpp::cos_danger)
                .set_id("owner_command_disable_" + choice + "_" + std::to_string(uid));
            row.add_component(en);
            row.add_component(dis);
            dpp::message msg;
            msg.add_component(row);
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
            return;
        }
    });

    bot.on_button_click([&bot, db, handler](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        // handle module/command toggle buttons separately
        if (custom_id.rfind("owner_module_enable_",0)==0 || custom_id.rfind("owner_module_disable_",0)==0 ||
            custom_id.rfind("owner_command_enable_",0)==0 || custom_id.rfind("owner_command_disable_",0)==0) {
            // parse name and uid from tail
            bool is_module = (custom_id.find("owner_module_") == 0);
            bool enable = (custom_id.find("_enable_") != std::string::npos);
            // strip prefix up to action
            size_t start = custom_id.find(enable ? "_enable_" : "_disable_") + (enable ? 8 : 9);
            std::string rest = custom_id.substr(start);
            size_t sep = rest.rfind('_');
            if (sep == std::string::npos) return;
            std::string name = rest.substr(0, sep);
            uint64_t uid2 = std::stoull(rest.substr(sep+1));
            if (event.command.get_issuing_user().id != uid2) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this interaction isn't for you")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            // Prevent disabling critical commands/modules to avoid lockout
            if (!enable) {
                if (is_module && (name == "owner" || name == "utility")) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + name + "' module - this would prevent you from managing permissions!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!is_module && (name == "module" || name == "command")) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + name + "' command - this would prevent you from re-enabling it!")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }
            bool ok = false;
            if (is_module) {
                ok = db->set_guild_module_enabled(gid, name, enable);
            } else {
                ok = db->set_guild_command_enabled(gid, name, enable);
            }
            if (ok) {
                // Invalidate cache so the change takes effect immediately
                if (is_module && handler) {
                    handler->notify_module_state_changed(gid, name, enable);
                }
                if (!is_module && handler) {
                    handler->notify_command_state_changed(gid, name, enable);
                }
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success(std::string("") + (is_module?"Module":"Command") + " '" + name + " " + (enable?"enabled":"disabled") + ".")).set_flags(dpp::m_ephemeral));
            } else {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Database operation failed.")).set_flags(dpp::m_ephemeral));
            }
            return;
        }
        // only handle suggestion-related, serverlist, cmdhistory, and ostats buttons; otherwise ignore entirely so they
        // don't collide with other modules (e.g. gambling interactions)
        if (custom_id.rfind("suggest_", 0) != 0 && custom_id.rfind("serverlist_", 0) != 0
            && custom_id.rfind("cmdh_", 0) != 0 && custom_id.rfind("ostats_", 0) != 0) {
            return;
        }
        uint64_t user_id = event.command.get_issuing_user().id;
        if (!is_owner(user_id)) {
            // ignore or send ephemeral error
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this interaction is restricted to the bot owner"))
                    .set_flags(dpp::m_ephemeral));
            return;
        }

        // ── ostats paginator buttons ──
        if (custom_id.rfind("ostats_", 0) == 0) {
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
                OStatsState& st = ostats_states[user_id];
                if (custom_id == "ostats_first")     st.current_page = 0;
                else if (custom_id == "ostats_prev") st.current_page = std::max(0, st.current_page - 1);
                else if (custom_id == "ostats_next") st.current_page = std::min(OSTATS_TOTAL_PAGES - 1, st.current_page + 1);
                else if (custom_id == "ostats_last") st.current_page = OSTATS_TOTAL_PAGES - 1;
                else return; // "ostats_page" disabled button, ignore

                msg = build_ostats_message(bot, db, user_id);
            }
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        // ensure state exists
        SuggestState* suggest_state_ptr;
        {
            std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
            suggest_state_ptr = &suggest_states[user_id];
        }
        SuggestState& state = *suggest_state_ptr;

        if (custom_id == "suggest_nav_prev" || custom_id == "suggest_nav_next") {
            // fetch current suggestions to compute pages
            std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
            auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
            int per_page = SUGGESTIONS_PER_PAGE;
            int total = suggestions.size();
            int pages = (total + per_page - 1) / per_page;
            if (pages == 0) pages = 1;

            if (custom_id == "suggest_nav_prev") {
                if (state.current_page > 0) state.current_page--;
                else state.current_page = pages - 1; // wrap to end
            } else if (custom_id == "suggest_nav_next") {
                if (state.current_page < pages - 1) state.current_page++;
                else state.current_page = 0; // wrap to start
            }

            dpp::message msg = build_suggestions_message(db, user_id);
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        
        // global delete-page button
        if (custom_id == "suggest_delete_page") {
            std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
            auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
            int per_page = SUGGESTIONS_PER_PAGE;
            int total = suggestions.size();
            int start = state.current_page * per_page;
            int end = std::min(start + per_page, total);
            for (int i = start; i < end; ++i) {
                bronx::db::suggestion_operations::delete_suggestion(db, suggestions[i].id);
            }
            // adjust current page if necessary
            int pages = (total + per_page - 1) / per_page;
            if (state.current_page >= pages && pages > 0) {
                state.current_page = pages - 1;
            }
            dpp::message msg = build_suggestions_message(db, user_id);
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        
        // goto modal trigger
        if (custom_id == "suggest_goto") {
            dpp::interaction_modal_response modal("suggest_goto_modal", "Go to page");
            modal.add_component(
                dpp::component()
                    .set_label("Page number")
                    .set_id("page_input")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Enter page number")
                    .set_min_length(1)
                    .set_max_length(10)
                    .set_text_style(dpp::text_short)
            );
            event.dialog(modal);
            return;
        }

        if (custom_id.find("suggest_sort_") == 0) {
            std::string field;
            if (custom_id == "suggest_sort_date") field = "submitted_at";
            else if (custom_id == "suggest_sort_networth") field = "networth";
            else if (custom_id == "suggest_sort_alpha") field = "suggestion";
            if (field.empty()) return;
            if (state.order_by == field) {
                // toggle direction
                state.asc = !state.asc;
            } else {
                state.order_by = field;
                state.asc = false;
                state.current_page = 0;
            }
            dpp::message msg = build_suggestions_message(db, user_id);
            event.reply(dpp::ir_update_message, msg);
            return;
        }

        if (custom_id.find("suggest_read_") == 0) {
            uint64_t sid = std::stoull(custom_id.substr(strlen("suggest_read_")));
            bronx::db::suggestion_operations::mark_read(db, sid);
            dpp::message msg = build_suggestions_message(db, user_id);
            event.reply(dpp::ir_update_message, msg);
            return;
        }

        if (custom_id.find("suggest_del_") == 0) {
            uint64_t sid = std::stoull(custom_id.substr(strlen("suggest_del_")));
            bronx::db::suggestion_operations::delete_suggestion(db, sid);
            // if the item was on a page, we might need to adjust current_page
            dpp::message msg = build_suggestions_message(db, user_id);
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        
        // Server list navigation handlers
        if (custom_id == "serverlist_nav_prev" || custom_id == "serverlist_nav_next" || custom_id == "serverlist_refresh") {
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
                // ensure state exists
                if (server_list_states.find(user_id) == server_list_states.end()) {
                    server_list_states[user_id] = {};
                }
                ServerListState& srv_state = server_list_states[user_id];
                
                // Get total pages
                int total = dpp::get_guild_cache()->count();
                int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
                
                if (custom_id == "serverlist_nav_prev") {
                    if (srv_state.current_page > 0) srv_state.current_page--;
                    else srv_state.current_page = pages - 1;
                } else if (custom_id == "serverlist_nav_next") {
                    if (srv_state.current_page < pages - 1) srv_state.current_page++;
                    else srv_state.current_page = 0;
                }
                // serverlist_refresh just rebuilds without changing page
                
                msg = build_servers_message(bot, user_id);
            }
            event.reply(dpp::ir_update_message, msg);
            return;
        }
        
        // Server list sort handlers
        if (custom_id.find("serverlist_sort_") == 0) {
            dpp::message msg;
            bool has_msg = false;
            {
                std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
                if (server_list_states.find(user_id) == server_list_states.end()) {
                    server_list_states[user_id] = {};
                }
                ServerListState& srv_state = server_list_states[user_id];
                
                std::string field;
                if (custom_id == "serverlist_sort_name") field = "name";
                else if (custom_id == "serverlist_sort_members") field = "members";
                else if (custom_id == "serverlist_sort_id") field = "id";
                
                if (!field.empty()) {
                    if (srv_state.sort_by == field) {
                        srv_state.asc = !srv_state.asc;
                    } else {
                        srv_state.sort_by = field;
                        srv_state.asc = true;
                        srv_state.current_page = 0;
                    }
                    
                    msg = build_servers_message(bot, user_id);
                    has_msg = true;
                }
            }
            if (has_msg) {
                event.reply(dpp::ir_update_message, msg);
            }
            return;
        }
        
        // Server leave handler
        if (custom_id.find("serverlist_leave_") == 0) {
            uint64_t guild_id = std::stoull(custom_id.substr(strlen("serverlist_leave_")));
            dpp::guild* g = dpp::find_guild(guild_id);
            std::string guild_name = g ? g->name : "Unknown";
            
            std::cout << "[LEAVE ATTEMPT] User: " << user_id 
                      << " Guild: " << guild_name 
                      << " ID: " << guild_id << "\n";
            
            // Acknowledge the interaction immediately with a thinking message
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            
            // Leave the guild (using current_user_leave_guild, not guild_delete)
            bot.current_user_leave_guild(guild_id, [&bot, event, user_id, guild_name, guild_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    // Log the error details
                    std::cout << "[LEAVE ERROR] Guild ID: " << guild_id 
                              << " Name: " << guild_name 
                              << " Error: " << callback.get_error().message 
                              << " HTTP: " << callback.http_info.status << "\n";
                    
                    // Edit original message to show error with details
                    std::string err_details = "Failed to leave **" + guild_name + "**\n" + callback.get_error().message;
                    dpp::message err_msg = dpp::message().add_embed(bronx::error(err_details));
                    bot.interaction_response_edit(event.command.token, err_msg);
                } else {
                    // Successfully left
                    std::cout << "[LEAVE SUCCESS] Left guild: " << guild_name << " (ID: " << guild_id << ")\n";
                    
                    // Refresh the list
                    dpp::message msg;
                    {
                        std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
                        if (server_list_states.find(user_id) == server_list_states.end()) {
                            server_list_states[user_id] = {};
                        }
                        
                        // Adjust page if needed (in case we left the last server on the page)
                        int total = dpp::get_guild_cache()->count();
                        int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
                        if (server_list_states[user_id].current_page >= pages && pages > 0) {
                            server_list_states[user_id].current_page = pages - 1;
                        }
                        
                        msg = build_servers_message(bot, user_id);
                    }
                    // Edit the original message with updated list
                    bot.interaction_response_edit(event.command.token, msg);
                    
                    // Send ephemeral confirmation as follow-up
                    bot.interaction_followup_create(event.command.token,
                        dpp::message().add_embed(bronx::success("Left server: **" + guild_name + "**"))
                        .set_flags(dpp::m_ephemeral));
                }
            });
            return;
        }
        
        // Command history navigation handlers
        if (custom_id == "cmdh_nav_prev" || custom_id == "cmdh_nav_next" || custom_id == "cmdh_refresh") {
            bool no_user = false;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
                // ensure cmdhistory state exists
                if (cmdhistory_states.find(user_id) == cmdhistory_states.end()) {
                    cmdhistory_states[user_id] = {};
                }
                CmdHistoryState& hstate = cmdhistory_states[user_id];
                
                if (hstate.target_user == 0) {
                    no_user = true;
                } else {
                    int total = db->get_history_count(hstate.target_user);
                    int pages = std::max(1, (total + HISTORY_PER_PAGE - 1) / HISTORY_PER_PAGE);
                    
                    if (custom_id == "cmdh_nav_prev") {
                        if (hstate.current_page > 0) hstate.current_page--;
                        else hstate.current_page = pages - 1;  // wrap to end
                    } else if (custom_id == "cmdh_nav_next") {
                        if (hstate.current_page < pages - 1) hstate.current_page++;
                        else hstate.current_page = 0;  // wrap to start
                    }
                    // refresh button: page stays the same
                    
                    msg = build_cmdhistory_message(db, user_id);
                }
            }
            if (no_user) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("No user selected")).set_flags(dpp::m_ephemeral));
            } else {
                event.reply(dpp::ir_update_message, msg);
            }
            return;
        }
        
        // Command history clear button
        if (custom_id == "cmdh_clear") {
            bool no_user = false;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
                if (cmdhistory_states.find(user_id) == cmdhistory_states.end()) {
                    cmdhistory_states[user_id] = {};
                }
                CmdHistoryState& hstate = cmdhistory_states[user_id];
                
                if (hstate.target_user == 0) {
                    no_user = true;
                } else {
                    db->clear_history(hstate.target_user);
                    hstate.current_page = 0;
                    msg = build_cmdhistory_message(db, user_id);
                }
            }
            if (no_user) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("No user selected")).set_flags(dpp::m_ephemeral));
            } else {
                event.reply(dpp::ir_update_message, msg);
            }
            return;
        }
    });

    // Handle modal submissions for suggestions (goto page)
    bot.on_form_submit([&bot, db](const dpp::form_submit_t& event) {
        if (event.custom_id != "suggest_goto_modal") {
            return;
        }
        uint64_t user_id = event.command.get_issuing_user().id;
        if (!is_owner(user_id)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this interaction is restricted to the bot owner")).set_flags(dpp::m_ephemeral));
            return;
        }
        // ensure state exists
        SuggestState* form_state_ptr;
        {
            std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
            form_state_ptr = &suggest_states[user_id];
        }
        SuggestState& state = *form_state_ptr;

        if (event.components.empty()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("no page number provided")).set_flags(dpp::m_ephemeral));
            return;
        }
        std::string page_str;
        try {
            page_str = std::get<std::string>(event.components[0].value);
        } catch (...) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("invalid page number")).set_flags(dpp::m_ephemeral));
            return;
        }
        int page = 0;
        try {
            page = std::stoi(page_str) - 1;
        } catch (...) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("could not parse page number")).set_flags(dpp::m_ephemeral));
            return;
        }
        if (page < 0) page = 0;
        // clamp against total pages and update state under lock
        dpp::message msg;
        {
            std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
            std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
            auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
            int per_page = 5;
            int total = suggestions.size();
            int pages = (total + per_page - 1) / per_page;
            if (pages == 0) pages = 1;
            if (page >= pages) page = pages - 1;
            state.current_page = page;
            msg = build_suggestions_message(db, user_id);
        }
        event.reply(dpp::ir_update_message, msg);
    });
}

} // namespace commands
