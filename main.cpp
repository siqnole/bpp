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
#include <sys/resource.h>
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
#include "media_manager.h"
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
#include "tui_logger.h"
#include "utils/colors.h"
#include "utils/logger.h"

using namespace bronx::db;
using namespace bronx;

// Global state
std::atomic<bool> g_initial_load_complete{false};
std::atomic<bool> g_running{true};

namespace commands {
    BotStats global_stats;
}

// crash signal handler
static void crash_handler(int signum) {
    const char* signal_name = "unknown";
    switch (signum) {
        case SIGSEGV: signal_name = "sigsegv"; break;
        case SIGABRT: signal_name = "sigabrt"; break;
        case SIGBUS:  signal_name = "sigbus"; break;
        case SIGFPE:  signal_name = "sigfpe"; break;
    }
    
    bronx::logger::critical("crash", signal_name);
    
    // still use cerr for actual stack trace as tui might be frozen
    std::cerr << "\n*** " << signal_name << " received ***\n";
    void* bt[100];
    int cnt = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    _exit(128 + signum);
}

static void terminate_handler() {
    bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "critical: uncaught exception");
    try {
        auto eptr = std::current_exception();
        if (eptr) std::rethrow_exception(eptr);
    } catch (const std::exception& e) {
        bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "error details: " + std::string(e.what()));
    }
    void* bt[100];
    int cnt = backtrace(bt, sizeof(bt)/sizeof(bt[0]));
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    std::abort();
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    bool enable_flamegraph = false;
    bool verbose_events = false;
    bool force_no_tui = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-g") enable_flamegraph = true;
        else if (arg == "-v" || arg == "--verbose") verbose_events = true;
        else if (arg == "--no-tui") force_no_tui = true;
    }

    bool use_tui = !force_no_tui && isatty(STDIN_FILENO);
    if (use_tui) {
        bronx::tui::TuiLogger::get().init();
        bronx::tui::TuiLogger::get().update_stats("status", "initializing...");
    }

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
    std::set_terminate(terminate_handler);

    std::string BOT_TOKEN;
    const char* env_token = std::getenv("BOT_TOKEN");
    if (!env_token) env_token = std::getenv("DISCORD_TOKEN");
    if (!env_token) env_token = std::getenv("TOKEN");
    BOT_TOKEN = env_token ? env_token : "";

    bronx::cache::initialize_cache();

    // 1. Database Configuration
    bronx::db::DatabaseConfig db_config;
    std::vector<std::string> config_paths = {"data/db_config.json", "../data/db_config.json"};
    for (const auto& path : config_paths) {
        db_config = bronx::load_database_config(path);
        if (db_config.user != "root") break;
    }

    // 2. Performance Layers (declared here for main scope accessibility)
    bronx::db::Database db(db_config);
    bronx::local::LocalDB local_db("/tmp/bronxbot_cache.db");
    bronx::perf::AsyncStatWriter async_stat_writer(&db, std::chrono::milliseconds(3000), verbose_events, "../data/db_config_aiven.json");
    bronx::batch::WriteBatchQueue write_batch(&db, &local_db, std::chrono::milliseconds(2000));
    bronx::xp::XpBatchWriter xp_batch_writer(&db, std::chrono::milliseconds(5000));
    bronx::snipe::MessageCache message_cache;
    bronx::snipe::SnipeCache snipe_cache(&db);
    bronx::api::ApiCacheClient api_client(&local_db);
    bronx::hybrid::HybridDatabase hybrid_db(&db, bronx::cache::global_cache.get(), &local_db, &write_batch, &api_client);

    // Static pointers for signal handler cleanup
    static bronx::xp::XpBatchWriter* s_xp = &xp_batch_writer;
    static bronx::perf::AsyncStatWriter* s_stat = &async_stat_writer;
    static bronx::batch::WriteBatchQueue* s_batch = &write_batch;
    static bronx::snipe::MessageCache* s_msg = &message_cache;
    static bronx::snipe::SnipeCache* s_snipe = &snipe_cache;

    std::signal(SIGINT, [](int) {
        g_running = false;
        bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "system: shutting down...");
        if (s_snipe) s_snipe->stop();
        if (s_msg) s_msg->stop();
        if (s_batch) s_batch->stop();
        if (s_xp) s_xp->stop();
        if (s_stat) s_stat->stop();
        bronx::cache::shutdown_cache();
        exit(0);
    });

    // Start background initialization thread
    std::thread init_thread([&, use_tui, BOT_TOKEN, verbose_events]() {
        try {
            if (use_tui) bronx::tui::TuiLogger::get().update_stats("status", "loading models...");
            if (!bronx::get_media_manager().init("data/ggml-base.en.bin")) {
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "system: media services failed to initialize");
            }

            if (use_tui) bronx::tui::TuiLogger::get().update_stats("status", "connecting dbs...");
            if (!db.connect()) {
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "system: fatal - remote database connection failed");
                return;
            }
            local_db.initialize();

            // Start performance writers
            async_stat_writer.start();
            bronx::perf::g_stat_writer = &async_stat_writer;
            write_batch.set_remote_connection(&async_stat_writer.remote_connection());
            write_batch.start();
            xp_batch_writer.start();
            leveling::set_xp_batch_writer(&xp_batch_writer);

            if (use_tui) bronx::tui::TuiLogger::get().update_stats("status", "preloading cache...");
            hybrid_db.refresh_all_settings();

            if (use_tui) bronx::tui::TuiLogger::get().update_stats("status", "starting bot...");
            
            uint32_t num_shards = 0;
            const char* shard_env = std::getenv("SHARD_COUNT");
            if (shard_env) {
                try { 
                    num_shards = std::stoul(shard_env); 
                    bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "init: overriding shard count to " + std::to_string(num_shards) + " from env");
                } catch (...) {
                    bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "init: failed to parse shard_count from env, using default");
                }
            }

            dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_members, num_shards, 0, 1);
            
            bot.on_log([use_tui](const dpp::log_t& event) {
                if (event.severity == dpp::ll_trace) return;
                
                bronx::tui::LogLevel l = bronx::tui::LogLevel::INFO;
                if (event.severity == dpp::ll_error) l = bronx::tui::LogLevel::ERROR;
                if (event.severity == dpp::ll_debug) l = bronx::tui::LogLevel::DEBUG;

                if (use_tui) {
                    bronx::tui::TuiLogger::get().add_log(l, "[" + std::to_string(static_cast<int>(event.severity)) + "] " + event.message);
                } else {
                    bronx::logger::Level lvl = bronx::logger::Level::INFO;
                    if (event.severity == dpp::ll_error) lvl = bronx::logger::Level::ERR;
                    if (event.severity == dpp::ll_warning) lvl = bronx::logger::Level::WARN;
                    if (event.severity == dpp::ll_debug) lvl = bronx::logger::Level::DEBUG;
                    if (event.severity == dpp::ll_trace) lvl = bronx::logger::Level::TRACE;

                    bronx::logger::log(lvl, "dpp", event.message);
                }
            });

            bot.on_ready([&bot, use_tui](const dpp::ready_t& event) {
                if (use_tui) {
                     bronx::tui::TuiLogger::get().update_stats("status", "online");
                     bronx::tui::TuiLogger::get().update_stats("bot", bot.me.username);
                     bronx::tui::TuiLogger::get().update_stats("shards", std::to_string(bot.numshards));
                }
            });

            OptimizedCommandHandler cmd_handler("b.", &db);
            cmd_handler.set_async_stat_writer(&async_stat_writer);
            cmd_handler.set_hybrid_database(&hybrid_db);

            // Register Commands
            for (auto* cmd : commands::get_utility_commands(&cmd_handler, &db, &snipe_cache)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_fun_commands()) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_games_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_endgame_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_moderation_commands()) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_manual_moderation_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_automod_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_economy_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_leaderboard_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_leveling_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_xpblacklist_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_patch_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_owner_commands(&cmd_handler, &db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_mining_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::global_boss::get_global_boss_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_boss_raid_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_passive_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_social_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::server_economy::get_server_economy_commands(&db)) cmd_handler.register_command(cmd);
            for (auto* cmd : commands::get_gambling_commands(&db)) cmd_handler.register_command(cmd);
            
            cmd_handler.register_command(commands::world_events::get_event_command(&db));
            cmd_handler.register_command(commands::create_help_command(&cmd_handler));
            cmd_handler.register_command(commands::create_guide_command(&db));
            cmd_handler.register_command(commands::gambling::get_russian_roulette_command(&db));
            // cmd_handler.register_command(commands::setup::create_setup_command(&db));
            cmd_handler.register_command(commands::moderation::get_log_command(&db));
            cmd_handler.register_command(commands::owner::get_log_beta_command(&db));
            cmd_handler.register_command(commands::get_feature_command(&db));

            for (auto* cmd : commands::get_stats_commands(&db)) cmd_handler.register_command(cmd);
            commands::populate_extended_help(&cmd_handler);

            // Give modules access to DB
            commands::utility::set_reactionrole_db(&db);
            commands::utility::set_autopurge_db(&db);
            commands::utility::set_prefix_db(&db);
            commands::utility::set_autorole_db(&db);

            register_event_handlers(bot, cmd_handler, db, async_stat_writer, message_cache, snipe_cache, xp_batch_writer, verbose_events);

            // ── Heartbeat Monitoring Task ───────────────────────────────────
            // Track uptime, memory usage, and guild counts in MariaDB every 60s
            auto start_time = std::chrono::steady_clock::now();
            bot.start_timer([&bot, &db, start_time](dpp::timer t) {
                auto now = std::chrono::steady_clock::now();
                uint64_t uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                
                // Get RAM usage (Resident Set Size) in MB
                uint64_t memory_mb = 0;
                struct rusage usage;
                if (getrusage(RUSAGE_SELF, &usage) == 0) {
                    memory_mb = usage.ru_maxrss / 1024; // KB to MB
                }

                size_t guild_count = dpp::get_guild_cache()->count();
                
                // Update heartbeat for all shards managed by this process
                auto shards = bot.get_shards();
                for (auto const& [shard_id, shard_ptr] : shards) {
                    db.update_heartbeat(shard_id, uptime_seconds, memory_mb, static_cast<int>(guild_count), "online");
                }
            }, 60);

            bot.start(dpp::st_wait);

        } catch (const std::exception& e) {
            bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "system: async init error - " + std::string(e.what()));
        }
    });

    if (use_tui) {
        bronx::tui::TuiLogger::get().run();
    } else {
        if (init_thread.joinable()) init_thread.join();
    }

    g_running = false;
    if (init_thread.joinable()) init_thread.join();

    return 0;
}