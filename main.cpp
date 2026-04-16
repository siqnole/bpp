#include <dpp/dpp.h>
#include <iostream>
#include <csignal>
#include <execinfo.h>
#include <thread>
#include <chrono>
#include <exception>
#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#include "performance/optimized_command_handler.h"
#include "performance/cached_database.h" 
#include "performance/cache_manager.h"
#include "performance/xp_batch_writer.h"
#include "performance/async_stat_writer.h"
#include "performance/local_db.h"
#include "performance/write_batch_queue.h"
#include "performance/api_cache_client.h"
#include "performance/hybrid_database.h"
#include "performance/message_cache.h"
#include "performance/snipe_cache.h"
#include "commands/utility.h"
#include "commands/utility/autopurge.h"
#include "commands/fun.h"
#include "commands/games.h"
#include "commands/endgame.h"
#include "commands/help_new.h"
#include "commands/help_data.h"
#include "commands/guide.h"
#include "commands/economy.h"
#include "commands/leaderboard.h"
#include "commands/leveling.h"
#include "commands/leveling/xp_handler.h"
#include "commands/leveling/xpblacklist.h"
#include "commands/patch.h"
#include "commands/owner.h"
#include "commands/owner/log_beta.h"
#include "commands/owner/feature_command.h"
#include "commands/gambling.h"
#include "commands/moderation.h"
#include "commands/moderation_commands.h"
#include "commands/moderation/logconfig.h"
#include "commands/fishing/autofish_runner.h"
#include "commands/mining.h"
#include "commands/global_boss.h"
#include "commands/global_boss_raid.h"
#include "commands/setup.h"
#include "commands/passive.h"
#include "commands/social.h"
#include "commands/world_events.h"
#include "commands/server_economy.h"
#include "commands/stats_cmd.h"
#include "database/core/database.h"
#include "database/operations/stats/stats_operations.h"
#include "config_loader.h"
#include "commands/gambling/russian_roulette.h"
#include "server_logger.h"
#include "feature_gate.h"
#include "events/event_handlers.h"
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

using namespace bronx::db;
using namespace bronx;

// Flag to track when initial guild loading is complete.
// on_guild_create fires for EVERY existing guild on startup;
// we only send the welcome embed for guilds joined AFTER this flag is set.
std::atomic<bool> g_initial_load_complete{false};

// ANSI color codes for colorful journalctl output
#include "utils/colors.h"

// Global stats (unchanged)
namespace commands {
    BotStats global_stats;
}

// Crash signal handler - catches SIGSEGV, SIGABRT, SIGBUS, SIGFPE
static void crash_handler(int signum) {
    const char* signal_name = "UNKNOWN";
    switch (signum) {
        case SIGSEGV: signal_name = "SIGSEGV"; break;
        case SIGABRT: signal_name = "SIGABRT"; break;
        case SIGBUS:  signal_name = "SIGBUS"; break;
        case SIGFPE:  signal_name = "SIGFPE"; break;
    }
    std::cerr << clr::BOLD_RED << "*** " << signal_name << " received (signal " << signum << "), stack trace:" << clr::RESET << std::endl;
    
    void* bt[100];
    int cnt = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    
    std::cerr << clr::BOLD_RED << "*** Bot will restart via systemd" << clr::RESET << std::endl;
    std::cerr.flush();
    _exit(128 + signum);  // Exit with signal-specific code for systemd
}

// Handle uncaught exceptions
static void terminate_handler() {
    std::cerr << clr::BOLD_RED << "*** Uncaught exception, attempting stack trace:" << clr::RESET << std::endl;
    try {
        auto eptr = std::current_exception();
        if (eptr) {
            std::rethrow_exception(eptr);
        }
    } catch (const std::exception& e) {
        std::cerr << clr::BOLD_RED << "*** Exception: " << e.what() << clr::RESET << std::endl;
    } catch (...) {
        std::cerr << clr::BOLD_RED << "*** Unknown exception type" << clr::RESET << std::endl;
    }
    
    void* bt[100];
    int cnt = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    
    std::cerr << clr::BOLD_RED << "*** Bot will restart via systemd" << clr::RESET << std::endl;
    std::cerr.flush();
    std::abort();
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    bool enable_flamegraph = false;
    bool verbose_events = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-g") {
            enable_flamegraph = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose_events = true;
        }
    }
    if (verbose_events) {
        std::cout << clr::BOLD_MAGENTA << "verbose mode enabled — all events will be logged" << clr::RESET << "\n";
    }
    
    // If -g flag is present, start the live flamegraph profiler
    pid_t profiler_pid = -1;
    if (enable_flamegraph) {
        std::cout << clr::BOLD_MAGENTA << "Starting live flamegraph profiler..." << clr::RESET << "\n";
        std::cout.flush();
        
        profiler_pid = fork();
        if (profiler_pid == 0) {
            // Child process: run the flamegraph generator script
            // Need to get parent PID after fork - child's parent will be the main bot process
            pid_t bot_pid = getppid();
            char pid_str[32];
            snprintf(pid_str, sizeof(pid_str), "%d", bot_pid);
            
            // Redirect stdout/stderr to /dev/null so profiler output doesn't pollute bot logs
            if (!freopen("/dev/null", "w", stdout)) _exit(1);
            if (!freopen("/dev/null", "w", stderr)) _exit(1);
            
            execl("/home/siqnole/Documents/code/bpp/scripts/live_flamegraph.sh", 
                  "live_flamegraph.sh", 
                  pid_str,
                  nullptr);
            // If execl returns, it failed
            std::cerr << clr::RED << "Failed to start flamegraph profiler: " << strerror(errno) << clr::RESET << "\n";
            exit(1);
        } else if (profiler_pid > 0) {
            std::cout << clr::GREEN << "✔ " << clr::RESET << "Flamegraph profiler started (PID: " << profiler_pid << ")\n";
            std::cout << clr::BOLD_CYAN << "🌐 Open http://localhost:8899 in your browser to view the live flamegraph" << clr::RESET << "\n";
            std::cout.flush();
        } else {
            std::cerr << clr::RED << "Failed to fork flamegraph profiler: " << strerror(errno) << clr::RESET << "\n";
        }
    }
    
    // Install crash handlers for various fatal signals
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
    
    // Set up terminate handler for uncaught exceptions
    std::set_terminate(terminate_handler);

    // Configuration (unchanged)
    std::string BOT_TOKEN;
    const char* env_token = std::getenv("BOT_TOKEN");
    if (!env_token || std::string(env_token).empty()) {
        env_token = std::getenv("DISCORD_TOKEN");
    }
    if (!env_token || std::string(env_token).empty()) {
        env_token = std::getenv("TOKEN");
    }
    if (env_token && std::string(env_token).size() > 0) {
        BOT_TOKEN = std::string(env_token);
        std::cout << clr::GREEN << "✔ " << clr::RESET << "Loaded BOT_TOKEN from environment variable.\n";
    } else {
        BOT_TOKEN = "token_here";
        std::cout << clr::YELLOW << "⚠ " << clr::RESET << "Using hardcoded BOT_TOKEN.\n";
    }
    const std::string PREFIX = "b.";
    
    // PERFORMANCE OPTIMIZATION: Initialize cache system first
    bronx::cache::initialize_cache();
    std::cout << clr::GREEN << "✔ " << clr::RESET << "High-performance cache system initialized\n";
    
    // Load database configuration (unchanged)
    bronx::db::DatabaseConfig db_config;
    std::vector<std::string> config_paths = {
        "data/db_config.json",
        "../data/db_config.json",
        "/home/siqnole/Documents/code/bpp/data/db_config.json"
    };
    
    bool config_loaded = false;
    for (const auto& path : config_paths) {
        db_config = bronx::load_database_config(path);
        if (db_config.user != "root") {
            std::cout << clr::GREEN << "✔ " << clr::RESET << "loaded database config from: " << clr::CYAN << path << clr::RESET << "\n";
            config_loaded = true;
            break;
        }
    }
    
    if (!config_loaded) {
        std::cerr << clr::YELLOW << "⚠ warning: could not load config file, using defaults" << clr::RESET << "\n";
    }
    
    std::cout << clr::DIM << "database config: " << db_config.user << "@" << db_config.host 
              << ":" << db_config.port << "/" << db_config.database << clr::RESET << "\n";
    
    // Initialize database
    Database db(db_config);
    if (!db.connect()) {
        std::cerr << clr::BOLD_RED << "✘ failed to connect to database!" << clr::RESET << "\n";
        bronx::cache::shutdown_cache();
        return 1;
    }
    std::cout << clr::BOLD_GREEN << "✔ database connected successfully" << clr::RESET << "\n";
    // NOTE: active_title purge removed — it was clearing everyone's equipped
    // titles on every bot restart, causing titles to disappear from leaderboards
    // and profiles.  Equipped titles are now preserved across restarts.
    
    // Enable debug modes if requested
    if (std::getenv("INVENTORY_DEBUG")) {
        db.set_inventory_debug(true);
        std::cerr << clr::MAGENTA << "🔍 inventory debug enabled" << clr::RESET << "\n";
    }
    // Check for truthy values (1, true, yes, on) - not just existence
    auto is_env_truthy = [](const char* name) {
        const char* val = std::getenv(name);
        if (!val) return false;
        return (val[0] == '1' || val[0] == 't' || val[0] == 'T' || 
                val[0] == 'y' || val[0] == 'Y');
    };
    if (is_env_truthy("DB_CONN_DEBUG") || is_env_truthy("DB_DEBUG")) {
        db.set_connection_debug(true);
        std::cerr << clr::MAGENTA << "🔍 database connection debug enabled" << clr::RESET << "\n";
    }
    
    // Run price tuning at startup
    if (!db.tune_bait_prices_from_logs(50)) {
        std::cerr << clr::YELLOW << "⚠ warning: price tuning failed or no data available" << clr::RESET << "\n";
    }

    // Give modules access to the database
    commands::utility::set_reactionrole_db(&db);
    commands::utility::set_autopurge_db(&db);
    commands::utility::set_prefix_db(&db);
    commands::utility::set_autorole_db(&db);
    
    // PERFORMANCE FIX: Create the XP batch writer that moves blocking DB writes
    // off the shard gateway threads.  XP is accumulated in memory and flushed to
    // MySQL every 5 seconds on a dedicated background thread.
    bronx::xp::XpBatchWriter xp_batch_writer(&db, std::chrono::milliseconds(5000));
    leveling::set_xp_batch_writer(&xp_batch_writer);
    xp_batch_writer.start();
    std::cout << clr::GREEN << "✔ " << clr::RESET << "XP batch writer started " << clr::DIM << "(flush interval: 5s)" << clr::RESET << "\n";
    
    // PERFORMANCE FIX: Async stat/log writer — moves synchronous log_command()
    // and increment_stat() calls off the gateway threads.  These were doing
    // 3 blocking DB round-trips per command, adding 150-300ms to every response.
    // Pass Aiven config for dual-write: local DB for bot, Aiven for dashboard
    std::string aiven_config_path = "../data/db_config_aiven.json";
    bronx::perf::AsyncStatWriter async_stat_writer(&db, std::chrono::milliseconds(3000), verbose_events, aiven_config_path);
    async_stat_writer.start();
    bronx::perf::g_stat_writer = &async_stat_writer;  // expose for command_handler.h
    std::cout << clr::GREEN << "✔ " << clr::RESET << "Async stat writer started " << clr::DIM << "(flush interval: 3s" << (verbose_events ? ", verbose" : "") << ")" << clr::RESET << "\n";
    
    // ========================================================================
    // HYBRID PERFORMANCE LAYER — Local SQLite + batch writes + API cache
    // Eliminates 40-100ms remote DB round-trips for the hottest data paths.
    // ========================================================================
    
    // 1. Local SQLite cache — sub-ms reads for user data, inventory, stats, shop
    bronx::local::LocalDB local_db("/tmp/bronxbot_cache.db");
    if (local_db.initialize()) {
        std::cout << clr::GREEN << "✔ " << clr::RESET << "Local SQLite cache initialized " 
                  << clr::DIM << "(/tmp/bronxbot_cache.db, WAL mode)" << clr::RESET << "\n";
    } else {
        std::cerr << clr::YELLOW << "⚠ " << clr::RESET << "Local SQLite cache failed to initialize, using remote-only\n";
    }
    
    // 2. Write batch queue — non-blocking wallet/bank/inventory/stat writes
    bronx::batch::WriteBatchQueue write_batch(&db, &local_db, std::chrono::milliseconds(2000));
    // Wire up Aiven dual-write: economy mutations go to both local + Aiven for dashboard
    write_batch.set_remote_connection(&async_stat_writer.remote_connection());
    write_batch.start();
    std::cout << clr::GREEN << "✔ " << clr::RESET << "Write batch queue started " 
              << clr::DIM << "(flush interval: 2s, Aiven dual-write: "
              << (async_stat_writer.remote_connection().is_connected() ? "enabled" : "disabled")
              << ")" << clr::RESET << "\n";
    
    // 3. API cache client — fetch leaderboards/aggregations from the site API
    bronx::api::ApiCacheClient api_client(&local_db);
    std::cout << clr::GREEN << "✔ " << clr::RESET << "API cache client initialized " 
              << clr::DIM << "(" << api_client.get_base_url() << ")" << clr::RESET << "\n";
    
    // 4. Hybrid database — unified layer combining all caching tiers
    bronx::hybrid::HybridDatabase hybrid_db(&db, bronx::cache::global_cache.get(), 
                                           &local_db, &write_batch, &api_client);
    std::cout << clr::BOLD_GREEN << "✔ " << clr::RESET << "Hybrid database layer active " 
              << clr::DIM << "(local SQLite → memory cache → remote DB)" << clr::RESET << "\n";
    
    // PERFORMANCE OPTIMIZATION: Preload ALL guild toggles, prefixes, blacklist into memory
    // This eliminates the 800+ms cold-start penalty on the first command per guild.
    // Instead of lazily loading 4 DB queries (200ms each) on first access, we bulk-load
    // everything at startup so toggle checks are instant (sub-ms in-memory).
    {
        auto start = std::chrono::steady_clock::now();
        hybrid_db.refresh_all_settings();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << clr::GREEN << "✔ " << clr::RESET << "Preloaded guild settings into cache "
                  << clr::DIM << "(" << elapsed << "ms)" << clr::RESET << "\n";
    }
    
    // PERFORMANCE OPTIMIZATION: Optimal shard count based on guild count
    // Discord recommends ~1 shard per 1000-2500 guilds
    // Reduced from 8 to 4 to prevent connection churn and improve stability
    uint32_t shard_count = 4;  // Reduced from 8 - over-sharding causes connection issues
    
    std::cout << clr::CYAN << "⚙ " << clr::RESET << "Initializing bot with " << clr::BOLD << shard_count << clr::RESET << " shards for optimal operation\n";
    
    dpp::cluster bot(BOT_TOKEN, 
                     dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_members, 
                     shard_count,
                     0,
                     1,
                     false,
                     dpp::cache_policy::cpol_default);
    
    // Initialize Webhook Logger
    bronx::logger::ServerLogger::get().init(&bot, &db);

    // Initialize Feature Gate — runtime kill-switch / beta gating system
    bronx::FeatureGate::get().init(&db);

    
    bot.on_log([&async_stat_writer](const dpp::log_t& event) {
        // Skip TRACE — raw websocket frames are too noisy for normal operation
        if (event.severity == dpp::ll_trace) return;

        // Intercept GUILD_APPLIED_BOOSTS_UPDATE from DPP's "Unhandled event" debug messages
        if (event.severity == dpp::ll_debug && event.message.find("GUILD_APPLIED_BOOSTS_UPDATE") != std::string::npos) {
            // Parse the raw JSON payload: {"d":{"ended":bool,"guild_id":"...","id":"...","user_id":"..."},...}
            auto json_start = event.message.find('{');
            if (json_start != std::string::npos) {
                try {
                    auto raw = event.message.substr(json_start);
                    // extract guild_id
                    auto extract = [&raw](const std::string& key) -> std::string {
                        auto pos = raw.find("\"" + key + "\":");
                        if (pos == std::string::npos) return "";
                        pos = raw.find('"', pos + key.size() + 3);
                        if (pos == std::string::npos) return "";
                        auto end = raw.find('"', pos + 1);
                        if (end == std::string::npos) return "";
                        return raw.substr(pos + 1, end - pos - 1);
                    };
                    std::string guild_id_str = extract("guild_id");
                    std::string user_id_str  = extract("user_id");
                    std::string boost_id_str = extract("id");
                    bool ended = (raw.find("\"ended\":true") != std::string::npos);

                    if (!guild_id_str.empty() && !user_id_str.empty()) {
                        uint64_t gid = std::stoull(guild_id_str);
                        uint64_t uid = std::stoull(user_id_str);
                        async_stat_writer.enqueue_boost_event(
                            gid, uid, ended ? "unboost" : "boost", boost_id_str);
                    }
                } catch (...) {
                    // silently ignore parse failures
                }
            }
        }

        // Intercept VOICE_CHANNEL_START_TIME_UPDATE for precise voice start/end timing
        // {"d":{"guild_id":"...","id":"...","voice_start_time":1234567890|null},...}
        if (event.severity == dpp::ll_debug && event.message.find("VOICE_CHANNEL_START_TIME_UPDATE") != std::string::npos) {
            auto json_start = event.message.find('{');
            if (json_start != std::string::npos) {
                try {
                    auto raw = event.message.substr(json_start);
                    auto extract = [&raw](const std::string& key) -> std::string {
                        auto pos = raw.find("\"" + key + "\":");
                        if (pos == std::string::npos) return "";
                        pos = raw.find('"', pos + key.size() + 3);
                        if (pos == std::string::npos) return "";
                        auto end = raw.find('"', pos + 1);
                        if (end == std::string::npos) return "";
                        return raw.substr(pos + 1, end - pos - 1);
                    };
                    std::string guild_id_str   = extract("guild_id");
                    std::string channel_id_str = extract("id");
                    bool is_start = (raw.find("\"voice_start_time\":null") == std::string::npos
                                     && raw.find("voice_start_time") != std::string::npos);

                    if (!guild_id_str.empty() && !channel_id_str.empty()) {
                        uint64_t gid = std::stoull(guild_id_str);
                        uint64_t cid = std::stoull(channel_id_str);
                        // We log channel-level voice activity (not user-specific here)
                        // User-specific voice is already tracked via voice_state_update
                        // This event indicates channel voice activity started/stopped
                        async_stat_writer.enqueue_voice_event(
                            gid, 0, cid, is_start ? "channel_active" : "channel_inactive");
                    }
                } catch (...) {
                    // silently ignore parse failures
                }
            }
        }

        // Severity label + color
        const char* severity_str = "???";
        const char* severity_color = clr::RESET;
        switch (event.severity) {
            case dpp::ll_trace:    severity_str = "TRACE";    severity_color = clr::DIM;          break;
            case dpp::ll_debug:    severity_str = "DEBUG";    severity_color = clr::CYAN;         break;
            case dpp::ll_info:     severity_str = "INFO";     severity_color = clr::GREEN;        break;
            case dpp::ll_warning:  severity_str = "WARNING";  severity_color = clr::BOLD_YELLOW;  break;
            case dpp::ll_error:    severity_str = "ERROR";    severity_color = clr::RED;          break;
            case dpp::ll_critical: severity_str = "CRITICAL"; severity_color = clr::BOLD_RED;     break;
        }

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char timebuf[64];
        std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

        // For errors, enrich the message with context about common Discord error codes
        if (event.severity >= dpp::ll_error) {
            std::string msg = event.message;

            // Try to extract the error code from the message
            // DPP formats REST errors as "Error: CODE: Message" or similar
            std::string extra_context;
            if (msg.find("50013") != std::string::npos) {
                extra_context = " [Missing Permissions — bot lacks a required permission (Send Messages / Embed Links / Manage Roles / etc.) in the target channel or guild. Check channel permission overrides.]";
            } else if (msg.find("50001") != std::string::npos) {
                extra_context = " [Missing Access — bot cannot see/access the target channel. Check channel visibility permissions.]";
            } else if (msg.find("50008") != std::string::npos) {
                extra_context = " [Cannot send messages in a non-text channel]";
            } else if (msg.find("50007") != std::string::npos) {
                extra_context = " [Cannot send messages to this user — DMs are disabled]";
            } else if (msg.find("10003") != std::string::npos) {
                extra_context = " [Unknown Channel — the channel may have been deleted]";
            } else if (msg.find("10008") != std::string::npos) {
                extra_context = " [Unknown Message — the message may have been deleted]";
            } else if (msg.find("40060") != std::string::npos) {
                extra_context = " [Interaction already acknowledged]";
            } else if (msg.find("10062") != std::string::npos) {
                extra_context = " [Unknown Interaction — interaction token expired (>3s)]";
            }

            std::cerr << clr::DIM << "[" << timebuf << "] " << clr::RESET
                      << severity_color << severity_str << ": " << msg << extra_context << clr::RESET << "\n";
        } else {
            std::cout << clr::DIM << "[" << timebuf << "] " << clr::RESET
                      << severity_color << severity_str << clr::RESET << ": " << event.message << "\n";
        }
    });

    // PERFORMANCE OPTIMIZATION: Use optimized command handler with caching
    OptimizedCommandHandler cmd_handler(PREFIX, &db);
    cmd_handler.set_async_stat_writer(&async_stat_writer);
    cmd_handler.set_hybrid_database(&hybrid_db);
    std::cout << clr::GREEN << "✔ " << clr::RESET << "Initialized optimized command handler with hybrid caching\n";

    // Initialize message content cache and snipe (deleted messages) cache
    bronx::snipe::MessageCache message_cache;
    bronx::snipe::SnipeCache snipe_cache(&db);
    std::cout << clr::GREEN << "✔ " << clr::RESET << "Initialized snipe message cache\n";

    // Register commands (unchanged)
    for (auto* cmd : commands::get_utility_commands(&cmd_handler, &db, &snipe_cache)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_fun_commands()) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_games_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_endgame_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_moderation_commands()) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_manual_moderation_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_automod_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_economy_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_leaderboard_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_leveling_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_xpblacklist_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_patch_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_owner_commands(&cmd_handler, &db)) {
        cmd_handler.register_command(cmd);
    }
    // `cleantitles` owner command added by modifications

    for (auto* cmd : commands::get_mining_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_global_boss_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_boss_raid_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_passive_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_social_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    // Server economy admin commands
    for (auto* cmd : commands::server_economy::get_server_economy_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    // World events command
    cmd_handler.register_command(commands::world_events::get_event_command(&db));

    cmd_handler.register_command(commands::create_help_command(&cmd_handler));
    cmd_handler.register_command(commands::create_guide_command(&db));
    cmd_handler.register_command(commands::gambling::get_russian_roulette_command(&db));
    cmd_handler.register_command(commands::setup::create_setup_command(&db));
    cmd_handler.register_command(commands::moderation::get_log_command(&db));
    cmd_handler.register_command(commands::owner::get_log_beta_command(&db));
    cmd_handler.register_command(commands::get_feature_command(&db));

    // Stats commands (visual chart-based server statistics)
    for (auto* cmd : commands::get_stats_commands(&db)) {
        cmd_handler.register_command(cmd);
    }

    // Populate extended help data (subcommands, flags, examples) on registered commands
    commands::populate_extended_help(&cmd_handler);

    // Bot ready event
    register_event_handlers(bot, cmd_handler, db, async_stat_writer, message_cache, snipe_cache, xp_batch_writer, verbose_events);

    // PERFORMANCE OPTIMIZATION: Graceful shutdown with cache cleanup + XP flush
    // Note: signal handlers can't capture references, so we use a static pointer
    // for the batch writer flush-on-exit.
    static bronx::xp::XpBatchWriter* g_shutdown_writer = &xp_batch_writer;
    static bronx::perf::AsyncStatWriter* g_shutdown_stat_writer = &async_stat_writer;
    static bronx::batch::WriteBatchQueue* g_shutdown_batch = &write_batch;
    static bronx::snipe::MessageCache* g_shutdown_msg_cache = &message_cache;
    static bronx::snipe::SnipeCache* g_shutdown_snipe_cache = &snipe_cache;
    std::signal(SIGINT, [](int) {
        std::cout << "\n" << clr::BOLD_YELLOW << "⚠ Received SIGINT, shutting down gracefully..." << clr::RESET << "\n";
        if (g_shutdown_snipe_cache) g_shutdown_snipe_cache->stop();
        if (g_shutdown_msg_cache) g_shutdown_msg_cache->stop();
        if (g_shutdown_batch) g_shutdown_batch->stop();
        if (g_shutdown_writer) g_shutdown_writer->stop();
        if (g_shutdown_stat_writer) g_shutdown_stat_writer->stop();
        bronx::cache::shutdown_cache();
        exit(0);
    });

    std::signal(SIGTERM, [](int) {
        std::cout << "\n" << clr::BOLD_YELLOW << "⚠ Received SIGTERM, shutting down gracefully..." << clr::RESET << "\n";
        if (g_shutdown_snipe_cache) g_shutdown_snipe_cache->stop();
        if (g_shutdown_msg_cache) g_shutdown_msg_cache->stop();
        if (g_shutdown_batch) g_shutdown_batch->stop();
        if (g_shutdown_writer) g_shutdown_writer->stop();
        if (g_shutdown_stat_writer) g_shutdown_stat_writer->stop();
        bronx::cache::shutdown_cache();
        exit(0);
    });

    std::cout << "\n" << clr::BOLD_BLUE << "┌───────────────────────────────────────────────┐" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::BOLD << "  🚀 Starting bronxbot with " << shard_count << " shards..." << clr::RESET;
    // pad to fill the box
    std::cout << std::string(16, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "├───────────────────────────────────────────────┤" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::MAGENTA << "  Performance optimizations active:" << clr::RESET << std::string(12, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Connection pooling (pool_size=" << db_config.pool_size << ")" << std::string(8, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Batched schema migrations" << std::string(14, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Cached XP blacklist + leveling config" << std::string(2, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Thread-local RNG" << std::string(23, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Compiled with -O2 optimizations" << std::string(8, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " TTL caches for prefixes/toggles" << std::string(8, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Per-shard performance monitoring" << std::string(8, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " XP batch writer (non-blocking)" << std::string(9, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Async stat/log writer" << std::string(18, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " ensure_user_exists cache" << std::string(15, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Idle-based pool health (no ping)" << std::string(7, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Local SQLite cache (sub-ms reads)" << std::string(6, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Write batch queue (2s flush)" << std::string(11, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " API cache client (site API)" << std::string(12, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "│" << clr::RESET << clr::GREEN << "   ✔" << clr::RESET << " Hybrid DB (3-tier read/write)" << std::string(10, ' ') << clr::BOLD_BLUE << "│" << clr::RESET << "\n";
    std::cout << clr::BOLD_BLUE << "└───────────────────────────────────────────────┘" << clr::RESET << "\n\n";
    
    // Start bot with retry on transient 503 / gateway fetch failures.
    // Discord occasionally returns 503 "upstream connect error" when the
    // bot calls GET /gateway/bot during startup.  DPP logs it as CRITICAL
    // and returns from start(), so we simply retry with exponential backoff.
    {
        constexpr int max_gateway_retries = 5;
        constexpr int base_delay_seconds  = 5;   // 5, 10, 20, 40, 80

        for (int attempt = 0; attempt < max_gateway_retries; ++attempt) {
            if (attempt > 0) {
                int delay = base_delay_seconds * (1 << (attempt - 1));
                std::cerr << clr::YELLOW << "⚠ " << clr::RESET
                          << "Gateway connection failed, retrying in " << delay
                          << "s (attempt " << (attempt + 1) << "/" << max_gateway_retries << ")\n";
                std::this_thread::sleep_for(std::chrono::seconds(delay));
            }

            try {
                bot.start(dpp::st_wait);
                break;  // clean exit from start() means the bot ran and then stopped
            } catch (const dpp::connection_exception& e) {
                std::cerr << clr::RED << "✘ " << clr::RESET
                          << "Gateway connection exception: " << e.what() << "\n";
                if (attempt + 1 >= max_gateway_retries) {
                    std::cerr << clr::BOLD_RED << "✘ All gateway retries exhausted, giving up." << clr::RESET << "\n";
                }
            } catch (const std::exception& e) {
                std::cerr << clr::RED << "✘ " << clr::RESET
                          << "bot.start() exception: " << e.what() << "\n";
                if (attempt + 1 >= max_gateway_retries) {
                    std::cerr << clr::BOLD_RED << "✘ All gateway retries exhausted, giving up." << clr::RESET << "\n";
                }
            }
        }
    }
    
    // Cleanup — flush any remaining data before exit
    snipe_cache.stop();
    message_cache.stop();
    write_batch.stop();
    xp_batch_writer.stop();
    async_stat_writer.stop();
    bronx::cache::shutdown_cache();
    return 0;
}