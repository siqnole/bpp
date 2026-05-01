#include "telemetry.h"
#include "owner_utils.h"
#include "../../performance/cache_manager.h"
#include "../../performance/chart_renderer.h"
#include "../../utils/logger.h"
#include "../owner.h"
#include "../economy/helpers.h"
#include <fstream>
#include <dirent.h>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>

namespace commands {
using economy::format_number;
namespace owner {

const int OSTATS_TOTAL_PAGES = 9;
std::map<uint64_t, OStatsState> ostats_states;
std::recursive_mutex ostats_mutex;

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

dpp::message build_ostats_message(dpp::cluster& bot, bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
    OStatsState& state = ostats_states[owner_id];
    int page = state.current_page;

    static const std::vector<std::string> page_titles = {
        "bot overview",         // 0
        "cache & infrastructure", // 1
        "command stats",        // 2
        "economy overview",     // 3
        "fishing & inventory",  // 4
        "leveling & community", // 5
        "system & process",     // 6
        "render previews",      // 7
        "infrastructure health"  // 8
    };

    std::string desc;
    namespace ch = bronx::chart;
    std::string chart_image;

    if (page == 0) {
        auto now = std::chrono::system_clock::now();
        auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(now - commands::global_stats.start_time).count();
        uint64_t d = uptime_s / 86400, h = (uptime_s % 86400) / 3600, m = (uptime_s % 3600) / 60, s = uptime_s % 60;
        std::string uptime = std::to_string(d) + "d " + std::to_string(h) + "h " + std::to_string(m) + "m " + std::to_string(s) + "s";

        desc += "**uptime:** " + uptime + "\n";
        desc += "**bot:** " + bot.me.format_username() + " (`" + std::to_string((uint64_t)bot.me.id) + "`)\n";
        desc += "**dpp:** " DPP_VERSION_TEXT "\n";
        desc += "**c++:** " + std::to_string(__cplusplus) + "\n\n";

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

        desc += "**servers:** " + std::to_string(dpp::get_guild_cache()->count()) + "\n";
        desc += "**users:** " + std::to_string(dpp::get_user_cache()->count()) + "\n";
        desc += "**channels:** " + std::to_string(dpp::get_channel_cache()->count()) + "\n";
        desc += "**roles:** " + std::to_string(dpp::get_role_cache()->count()) + "\n";
        desc += "**emojis:** " + std::to_string(dpp::get_emoji_cache()->count()) + "\n";

    } else if (page == 1) {
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

        auto pool = db->get_pool();
        if (pool) {
            desc += "**db pool**\n";
            desc += "• available: " + std::to_string(pool->available_connections()) + "\n";
            desc += "• total: " + std::to_string(pool->total_connections()) + "\n\n";
        }

        auto bl = db->get_global_blacklist();
        auto wl = db->get_global_whitelist();
        desc += "**moderation**\n";
        desc += "• blacklisted users: " + std::to_string(bl.size()) + "\n";
        desc += "• whitelisted users: " + std::to_string(wl.size()) + "\n\n";

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
        desc += "**session totals**\n";
        desc += "• commands run: " + commands::format_number((int64_t)commands::global_stats.total_commands) + "\n";
        desc += "• errors: " + commands::format_number((int64_t)commands::global_stats.total_errors) + "\n";

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

        {
            std::vector<ch::BarItem> money_bars;
            money_bars.push_back({"wallets", total_wallets});
            money_bars.push_back({"banks", total_banks});
            chart_image = ch::render_horizontal_bar_chart("money supply", money_bars, 600, 140);
        }

        int64_t total_gambled = sql_count(db, "SELECT COALESCE(SUM(total_gambled),0) FROM users");
        int64_t total_won     = sql_count(db, "SELECT COALESCE(SUM(total_won),0) FROM users");
        int64_t total_lost    = sql_count(db, "SELECT COALESCE(SUM(total_lost),0) FROM users");
        desc += "**gambling**\n";
        desc += "• total gambled: $" + commands::format_number(total_gambled) + "\n";
        desc += "• total won: $" + commands::format_number(total_won) + "\n";
        desc += "• total lost: $" + commands::format_number(total_lost) + "\n";
        desc += "• house edge: $" + commands::format_number(total_lost - total_won) + "\n\n";

        int64_t gbl_games    = sql_count(db, "SELECT COALESCE(SUM(games_played),0) FROM gambling_stats");
        int64_t gbl_bet      = sql_count(db, "SELECT COALESCE(SUM(total_bet),0) FROM gambling_stats");
        int64_t gbl_big_win  = sql_count(db, "SELECT COALESCE(MAX(biggest_win),0) FROM gambling_stats");
        int64_t gbl_big_loss = sql_count(db, "SELECT COALESCE(MAX(biggest_loss),0) FROM gambling_stats");
        desc += "**gambling (detailed)**\n";
        desc += "• games played: " + commands::format_number(gbl_games) + "\n";
        desc += "• total bet: $" + commands::format_number(gbl_bet) + "\n";
        desc += "• biggest win: $" + commands::format_number(gbl_big_win) + "\n";
        desc += "• biggest loss: $" + commands::format_number(gbl_big_loss) + "\n\n";

        int64_t jackpot_pool = db->get_jackpot_pool();
        desc += "**jackpot pool:** $" + commands::format_number(jackpot_pool) + "\n";

        int64_t vip_count     = sql_count(db, "SELECT COUNT(*) FROM users WHERE vip=1");
        int64_t prestige_count = sql_count(db, "SELECT COUNT(*) FROM users WHERE prestige > 0");
        int64_t max_prestige  = sql_count(db, "SELECT COALESCE(MAX(prestige),0) FROM users");
        desc += "**vip users:** " + std::to_string(vip_count) + "\n";
        desc += "**prestige users:** " + std::to_string(prestige_count) + " (max: " + std::to_string(max_prestige) + ")\n";

        int64_t active_loans = sql_count(db, "SELECT COUNT(*) FROM user_loans WHERE remaining > 0");
        int64_t loan_balance = sql_count(db, "SELECT COALESCE(SUM(remaining),0) FROM user_loans WHERE remaining > 0");
        desc += "**active loans:** " + std::to_string(active_loans) + " ($" + commands::format_number(loan_balance) + " outstanding)\n";

        int64_t pending_trades = sql_count(db, "SELECT COUNT(*) FROM guild_trades WHERE status='pending'");
        int64_t completed_trades = sql_count(db, "SELECT COUNT(*) FROM guild_trades WHERE status='completed'");
        desc += "**trades:** " + std::to_string(pending_trades) + " pending, " + std::to_string(completed_trades) + " completed\n";

    } else if (page == 4) {
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

        int64_t auto_active  = sql_count(db, "SELECT COUNT(*) FROM user_autofishers WHERE active=1");
        int64_t auto_balance = sql_count(db, "SELECT COALESCE(SUM(balance),0) FROM autofishers");
        int64_t auto_stored  = sql_count(db, "SELECT COUNT(*) FROM autofish_storage");
        desc += "**autofishing**\n";
        desc += "• active: " + std::to_string(auto_active) + "\n";
        desc += "• total balance: $" + commands::format_number(auto_balance) + "\n";
        desc += "• stored fish: " + commands::format_number(auto_stored) + "\n\n";

        int64_t inv_items    = sql_count(db, "SELECT COALESCE(SUM(quantity),0) FROM inventory");
        int64_t inv_distinct = sql_count(db, "SELECT COUNT(DISTINCT item_id) FROM inventory");
        int64_t inv_users    = sql_count(db, "SELECT COUNT(DISTINCT user_id) FROM inventory");
        desc += "**inventory**\n";
        desc += "• total items: " + commands::format_number(inv_items) + "\n";
        desc += "• distinct items: " + std::to_string(inv_distinct) + "\n";
        desc += "• users with items: " + commands::format_number(inv_users) + "\n\n";

        int64_t baz_shares    = sql_count(db, "SELECT COALESCE(SUM(shares),0) FROM bazaar_stock");
        int64_t baz_invested  = sql_count(db, "SELECT COALESCE(SUM(total_invested),0) FROM bazaar_stock");
        int64_t baz_dividends = sql_count(db, "SELECT COALESCE(SUM(total_dividends),0) FROM bazaar_stock");
        desc += "**bazaar**\n";
        desc += "• total shares: " + commands::format_number(baz_shares) + "\n";
        desc += "• invested: $" + commands::format_number(baz_invested) + "\n";
        desc += "• dividends paid: $" + commands::format_number(baz_dividends) + "\n";

        int64_t active_claims = sql_count(db, "SELECT COUNT(*) FROM user_mining_claims WHERE expires_at > NOW()");
        desc += "\n**mining claims:** " + std::to_string(active_claims) + " active\n";

    } else if (page == 5) {
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

        int64_t active_giveaways = sql_count(db, "SELECT COUNT(*) FROM guild_giveaways WHERE active=1");
        int64_t guild_bal_total  = sql_count(db, "SELECT COALESCE(SUM(balance),0) FROM guild_balances");
        int64_t guild_donated    = sql_count(db, "SELECT COALESCE(SUM(total_donated),0) FROM guild_balances");
        desc += "**giveaways & guild economy**\n";
        desc += "• active giveaways: " + std::to_string(active_giveaways) + "\n";
        desc += "• guild balance total: $" + commands::format_number(guild_bal_total) + "\n";
        desc += "• total donated: $" + commands::format_number(guild_donated) + "\n\n";

        int64_t msg_evts_7d    = sql_count(db, "SELECT COUNT(*) FROM guild_message_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t voice_evts_7d  = sql_count(db, "SELECT COUNT(*) FROM guild_voice_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t member_evts_7d = sql_count(db, "SELECT COUNT(*) FROM guild_member_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        int64_t boost_evts_7d  = sql_count(db, "SELECT COUNT(*) FROM guild_boost_events WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)");
        desc += "**event tracking (7d)**\n";
        desc += "• message events: " + commands::format_number(msg_evts_7d) + "\n";
        desc += "• voice events: " + commands::format_number(voice_evts_7d) + "\n";
        desc += "• member events: " + commands::format_number(member_evts_7d) + "\n";
        desc += "• boost events: " + commands::format_number(boost_evts_7d) + "\n";

        int64_t total_pets = sql_count(db, "SELECT COUNT(*) FROM user_pets");
        desc += "\n**pets:** " + commands::format_number(total_pets) + " owned\n";

        int64_t total_crews = sql_count(db, "SELECT COUNT(*) FROM crews");
        int64_t crew_members = sql_count(db, "SELECT COUNT(*) FROM crew_members");
        desc += "**crews:** " + std::to_string(total_crews) + " (" + std::to_string(crew_members) + " members)\n";

    } else if (page == 6) {
        auto mem = get_process_info();
        desc += "**memory**\n";
        desc += "• rss: " + fmt_mem(mem.vm_rss_kb) + "\n";
        desc += "• peak rss: " + fmt_mem(mem.vm_hwm_kb) + "\n";
        desc += "• virtual: " + fmt_mem(mem.vm_size_kb) + "\n\n";

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

        struct stat st;
        if (stat("/proc/self/exe", &st) == 0) {
            double mb = st.st_size / 1048576.0;
            std::ostringstream oss; oss << std::fixed << std::setprecision(1) << mb;
            desc += "• binary size: " + oss.str() + " MB\n";
        }

        char hostname[256] = {};
        gethostname(hostname, sizeof(hostname));
        desc += "• hostname: " + std::string(hostname) + "\n\n";

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
        desc += "Active ephemeral preview environments from Render.\n\n";
        auto previews = sql_query(db, "SELECT branch, commit_sha, preview_url, status, created_at FROM site_previews WHERE status = 'active' ORDER BY created_at DESC LIMIT 10");
        
        if (previews.empty()) {
            desc += "No active previews found.";
        } else {
            for (const auto& p : previews) {
                std::string branch = p.cols[0];
                std::string sha = p.cols[1].substr(0, 7);
                std::string url = p.cols[2];
                std::string created = p.cols[4];
                desc += "• **" + branch + "** (`" + sha + "`)\n";
                desc += "  [view site](" + url + ") • " + created + "\n\n";
            }
        }
        int64_t total_active = sql_count(db, "SELECT COUNT(*) FROM site_previews WHERE status = 'active'");
        int64_t total_historical = sql_count(db, "SELECT COUNT(*) FROM site_previews");
        desc += "\n\n**summary**\n";
        desc += "• active: " + std::to_string(total_active) + "\n";
        desc += "• total historical: " + std::to_string(total_historical) + "\n";
    } else if (page == 8) {
        desc += "Real-time infrastructure observability and health metrics.\n\n";
        auto pool = db->get_pool();
        size_t avail = pool->available_connections();
        size_t total = pool->total_connections();
        double usage_pct = total > 0 ? (1.0 - (double)avail/total) * 100.0 : 0;

        auto start = std::chrono::steady_clock::now();
        db->execute("SELECT 1");
        auto end = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        desc += "**database engine (mariadb)**\n";
        desc += "• pool: " + std::to_string(avail) + "/" + std::to_string(total) + " available (" + commands::format_number((int64_t)usage_pct) + "% util)\n";
        desc += "• query latency: " + std::to_string(diff) + "μs\n";
        desc += "• server version: " + sql_query(db, "SELECT VERSION()")[0].cols[0] + "\n\n";

        double load[3];
        if (getloadavg(load, 3) != -1) {
            desc += "**system load**\n";
            desc += "• 1m: " + std::to_string(load[0]) + ", 5m: " + std::to_string(load[1]) + ", 15m: " + std::to_string(load[2]) + "\n";
        }

        auto proc_mem = get_process_info();
        desc += "• context switches: " + commands::format_number((int64_t)proc_mem.threads * 120) + " (est/s)\n";

        desc += "\n**network & shards**\n";
        desc += "• active shards: " + std::to_string(bot.get_shards().size()) + "\n";
        desc += "• gateway latency: " + std::to_string((int)(bot.get_shard(0)->websocket_ping * 1000)) + "ms\n";
        
        {
            std::vector<std::pair<std::string, std::string>> health_stats = {
                {"DB Latency", std::to_string(diff) + "us"},
                {"Pool Util", std::to_string((int)usage_pct) + "%"},
                {"Load 1m", std::to_string(load[0])},
                {"Threads", std::to_string(proc_mem.threads)}
            };
            chart_image = ch::render_summary_card("infrastructure health", health_stats, 600, 160);
        }
    }

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

    if (!chart_image.empty()) {
        std::string chart_filename = "chart_page" + std::to_string(page) + ".png";
        msg.add_file(chart_filename, chart_image);
        embed.set_image("attachment://" + chart_filename);
    }

    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);
    nav_row.add_component(dpp::component().set_type(dpp::cot_button).set_label("⏮").set_style(dpp::cos_secondary).set_id("ostats_first").set_disabled(page == 0));
    nav_row.add_component(dpp::component().set_type(dpp::cot_button).set_label("◀ previous").set_style(dpp::cos_primary).set_id("ostats_prev").set_disabled(page == 0));
    nav_row.add_component(dpp::component().set_type(dpp::cot_button).set_label(std::to_string(page + 1) + " / " + std::to_string(OSTATS_TOTAL_PAGES)).set_style(dpp::cos_secondary).set_id("ostats_page").set_disabled(true));
    nav_row.add_component(dpp::component().set_type(dpp::cot_button).set_label("next ▶").set_style(dpp::cos_primary).set_id("ostats_next").set_disabled(page >= OSTATS_TOTAL_PAGES - 1));
    nav_row.add_component(dpp::component().set_type(dpp::cot_button).set_label("⏭").set_style(dpp::cos_secondary).set_id("ostats_last").set_disabled(page >= OSTATS_TOTAL_PAGES - 1));
    msg.add_component(nav_row);

    dpp::component jump_row;
    jump_row.set_type(dpp::cot_action_row);
    dpp::component select;
    select.set_type(dpp::cot_selectmenu).set_placeholder("Jump to page…").set_id("ostats_jump");
    for (int i = 0; i < OSTATS_TOTAL_PAGES; i++) {
        select.add_select_option(dpp::select_option(std::to_string(i+1) + ". " + page_titles[i], std::to_string(i)).set_default(i == page));
    }
    jump_row.add_component(select);
    msg.add_component(jump_row);

    return msg;
}

bool handle_ostats_interaction(const dpp::interaction_create_t& event, dpp::cluster& bot, bronx::db::Database* db) {
    uint64_t user_id = event.command.usr.id;
    if (!commands::is_owner(user_id)) return false;

    std::string custom_id = event.command.type == dpp::it_component_button 
        ? std::get<dpp::component_interaction>(event.command.data).custom_id
        : "";
    
    if (event.command.type == dpp::it_modal_submit) return false;

    if (custom_id == "ostats_jump") {
        auto& select_data = std::get<dpp::component_interaction>(event.command.data);
        if (!select_data.values.empty()) {
            int target_page = std::stoi(select_data.values[0]);
            std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
            ostats_states[user_id].current_page = target_page;
            event.reply(dpp::ir_update_message, build_ostats_message(bot, db, user_id));
        }
        return true;
    }

    if (custom_id.rfind("ostats_", 0) == 0) {
        std::lock_guard<std::recursive_mutex> lock(ostats_mutex);
        OStatsState& st = ostats_states[user_id];
        if (custom_id == "ostats_first")     st.current_page = 0;
        else if (custom_id == "ostats_prev") st.current_page = std::max(0, st.current_page - 1);
        else if (custom_id == "ostats_next") st.current_page = std::min(OSTATS_TOTAL_PAGES - 1, st.current_page + 1);
        else if (custom_id == "ostats_last") st.current_page = OSTATS_TOTAL_PAGES - 1;
        else return false;
        
        event.reply(dpp::ir_update_message, build_ostats_message(bot, db, user_id));
        return true;
    }
    return false;
}

} // namespace owner
} // namespace commands
