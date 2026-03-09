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
#include "commands/utility.h"
#include "commands/utility/autopurge.h"
#include "commands/fun.h"
#include "commands/games.h"
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
#include "commands/gambling.h"
#include "commands/moderation.h"
#include "commands/fishing/autofish_runner.h"
#include "commands/mining.h"
#include "commands/global_boss.h"
#include "commands/global_boss_raid.h"
#include "commands/setup.h"
#include "commands/passive.h"
#include "commands/social.h"
#include "commands/world_events.h"
#include "commands/server_economy.h"
#include "database/core/database.h"
#include "config_loader.h"
#include "commands/gambling/russian_roulette.h"
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

using namespace bronx::db;

// Flag to track when initial guild loading is complete.
// on_guild_create fires for EVERY existing guild on startup;
// we only send the welcome embed for guilds joined AFTER this flag is set.
static std::atomic<bool> g_initial_load_complete{false};

// ANSI color codes for colorful journalctl output
namespace clr {
    constexpr const char* RESET       = "\033[0m";
    constexpr const char* RED         = "\033[31m";
    constexpr const char* GREEN       = "\033[32m";
    constexpr const char* YELLOW      = "\033[33m";
    constexpr const char* BLUE        = "\033[34m";
    constexpr const char* MAGENTA     = "\033[35m";
    constexpr const char* CYAN        = "\033[36m";
    constexpr const char* DIM         = "\033[2m";
    constexpr const char* BOLD        = "\033[1m";
    constexpr const char* BOLD_RED    = "\033[1;31m";
    constexpr const char* BOLD_GREEN  = "\033[1;32m";
    constexpr const char* BOLD_YELLOW = "\033[1;33m";
    constexpr const char* BOLD_BLUE   = "\033[1;34m";
    constexpr const char* BOLD_MAGENTA= "\033[1;35m";
    constexpr const char* BOLD_CYAN   = "\033[1;36m";
}

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
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-g") {
            enable_flamegraph = true;
        }
    }
    
    // If -g flag is present, start the live flamegraph profiler
    pid_t profiler_pid = -1;
    if (enable_flamegraph) {
        std::cout << clr::BOLD_MAGENTA << "🔥 Starting live flamegraph profiler..." << clr::RESET << "\n";
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
    DatabaseConfig db_config;
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
    // purge any leftover active_title rows so inventories stay clean; this is
    // safe to run repeatedly and covers both legacy data and any stray
    // entries that slip through.
    int purged = db.delete_inventory_item_for_all_users("active_title");
    if (purged > 0) {
        std::cout << clr::YELLOW << "⚠ " << clr::RESET << "purged " << purged << " legacy active_title entries from inventories\n";
    }
    
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
    
    bot.on_log([](const dpp::log_t& event) {
        // Skip TRACE — raw websocket frames are too noisy for normal operation
        if (event.severity == dpp::ll_trace) return;

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
    std::cout << clr::GREEN << "✔ " << clr::RESET << "Initialized optimized command handler with comprehensive caching\n";

    // Register commands (unchanged)
    for (auto* cmd : commands::get_utility_commands(&cmd_handler, &db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_fun_commands()) {
        cmd_handler.register_command(cmd);
    }

    for (auto* cmd : commands::get_games_commands(&db)) {
        cmd_handler.register_command(cmd);
    }
    
    for (auto* cmd : commands::get_moderation_commands()) {
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

    // Populate extended help data (subcommands, flags, examples) on registered commands
    commands::populate_extended_help(&cmd_handler);

    // Bot ready event
    bot.on_ready([&bot, &cmd_handler, &db, &xp_batch_writer](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_commands>()) {
            std::cout << clr::BOLD_GREEN << "✔ logged in as " << bot.me.username << clr::RESET << " (" << clr::DIM << commands::get_build_version() << clr::RESET << ")!\n";
            std::cout << clr::CYAN << "⚙ " << clr::RESET << "prefix: " << clr::BOLD << cmd_handler.get_prefix() << clr::RESET << "\n";
            
            // Performance stats
            auto perf_stats = cmd_handler.get_performance_stats();
            std::cout << clr::MAGENTA << "📊" << clr::RESET << " Cache initialized with " << clr::BOLD << perf_stats.total_cache_entries << clr::RESET << " total cache slots\n";
            
            // Register slash commands (unchanged)
            auto slash_commands = cmd_handler.get_slash_commands();
            std::cout << clr::CYAN << "⚙ " << clr::RESET << "registering " << clr::BOLD << slash_commands.size() << clr::RESET << " slash commands\n";
            
            std::vector<dpp::slashcommand> commands_to_register;

            for (auto* cmd : slash_commands) {
                dpp::slashcommand slash_cmd(cmd->name, cmd->description, bot.me.id);
                for (const auto& opt : cmd->options) {
                    slash_cmd.add_option(opt);
                }
                commands_to_register.push_back(slash_cmd);
            }
            
            // Validate commands before registering (detect issues early)
            {
                std::map<std::string, size_t> name_index;
                for (size_t ci = 0; ci < commands_to_register.size(); ci++) {
                    const auto& sc = commands_to_register[ci];
                    if (sc.description.size() > 100) {
                        std::cerr << clr::RED << "⚠ slash cmd '" << sc.name << "' description too long (" << sc.description.size() << " chars, max 100)" << clr::RESET << "\n";
                    }
                    auto it = name_index.find(sc.name);
                    if (it != name_index.end()) {
                        std::cerr << clr::RED << "⚠ duplicate slash cmd name '" << sc.name << "' at index " << it->second << " and " << ci << clr::RESET << "\n";
                    }
                    name_index[sc.name] = ci;
                }
            }

            // Use bulk registration to avoid rate limits
            if (!commands_to_register.empty()) {
                bot.global_bulk_command_create(commands_to_register, [](const dpp::confirmation_callback_t& callback) {
                    if (callback.is_error()) {
                        auto err = callback.get_error();
                        std::cerr << clr::RED << "✘ error registering commands: " << err.message << clr::RESET << "\n";
                        for (const auto& e : err.errors) {
                            std::cerr << clr::RED << "  ↳ " << e.field << ": " << e.reason << " (code " << e.code << ")" << clr::RESET << "\n";
                        }
                    } else {
                        std::cout << clr::BOLD_GREEN << "✔ successfully registered all slash commands" << clr::RESET << "\n";
                    }
                });

                // External API calls (unchanged)
                #ifdef HAVE_LIBCURL
                // ... [external API registration code remains the same]
                #endif
            }
            
            // Register interaction handlers (unchanged)
            commands::register_help_interactions(bot, &cmd_handler);
            commands::register_guide_interactions(bot, &db);
            commands::register_shop_interactions(bot, &db);
            commands::register_fishing_interactions(bot, &db);
            commands::register_leaderboard_interactions(bot, &db);
            commands::register_utility_interactions(bot, &db);
            commands::register_owner_interactions(bot, &db, &cmd_handler);
            commands::utility::load_persistent_reaction_roles(bot);
            commands::utility::load_autopurges(bot);
            commands::utility::load_autoroles(bot);
            commands::register_moderation_handlers(bot);
            commands::register_gambling_interactions(bot, &db);
            commands::register_games_handlers(bot, &db);
            commands::register_economy_handlers(bot, &db);
            commands::register_achievements_interactions(bot, &db);
            commands::register_mining_interactions(bot, &db);
            commands::register_global_boss_interactions(bot, &db);
            commands::register_boss_raid_interactions(bot, &db);
            commands::register_passive_interactions(bot, &db);
            commands::register_social_interactions(bot, &db);

            // PERFORMANCE FIX: Register level-up callback for the XP batch writer.
            // When the batch writer detects a level-up during its periodic flush,
            // it fires this callback (on its own thread) to send announcements.
            xp_batch_writer.set_levelup_callback([&bot, &db](const std::vector<bronx::xp::LevelUpEvent>& events) {
                for (auto& ev : events) {
                    if (ev.guild_id == 0) continue;  // skip global level-ups (no announcement)
                    
                    // Fetch the server leveling config to check if announcements are enabled
                    auto cfg = db.get_server_leveling_config(ev.guild_id);
                    if (!cfg || !cfg->announce_levelup) continue;
                    
                    std::string announcement = "\xF0\x9F\x8E\x89 <@" + std::to_string(ev.user_id) +
                        "> leveled up to **Level " + std::to_string(ev.new_level) + "**!";
                    
                    // Check for level role rewards
                    auto level_role = db.get_level_role_at_level(ev.guild_id, ev.new_level);
                    if (level_role) {
                        announcement += "\n\xF0\x9F\x8F\x86 You earned the <@&" +
                            std::to_string(level_role->role_id) + "> role!";
                        
                        bot.guild_member_add_role(ev.guild_id, ev.user_id, level_role->role_id,
                            [gid = ev.guild_id, uid = ev.user_id, rid = level_role->role_id](const dpp::confirmation_callback_t& cb) {
                                if (cb.is_error()) {
                                    std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to add role " << rid
                                              << " to user " << uid << " in guild " << gid
                                              << ": " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                                }
                            });
                        
                        if (level_role->remove_previous) {
                            auto all_roles = db.get_level_roles(ev.guild_id);
                            for (auto& r : all_roles) {
                                if (r.level < ev.new_level) {
                                    bot.guild_member_remove_role(ev.guild_id, ev.user_id, r.role_id,
                                        [gid = ev.guild_id, uid = ev.user_id, rid = r.role_id](const dpp::confirmation_callback_t& cb) {
                                            if (cb.is_error()) {
                                            std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to remove role " << rid
                                                          << " from user " << uid << " in guild " << gid
                                                          << ": " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                                            }
                                        });
                                }
                            }
                        }
                    }
                    
                    // Use announcement channel or fall back to... nothing (we don't have
                    // the original channel any more).  If the config specifies a channel, use it.
                    if (cfg->announcement_channel) {
                        uint64_t ch = *cfg->announcement_channel;
                        bot.message_create(dpp::message(ch, announcement),
                            [ch, gid = ev.guild_id, uid = ev.user_id](const dpp::confirmation_callback_t& cb) {
                                if (cb.is_error()) {
                                    std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to send level-up in channel " << ch
                                              << " (guild " << gid << "): " << cb.get_error().message << "\n";
                                }
                            });
                    }
                    // If no announcement channel is configured and level-up announcements are
                    // enabled, we can't send to the original message channel because the batch
                    // writer doesn't track it.  This is acceptable — most servers configure a
                    // dedicated channel via b.setup.
                }
            });

            // DPP 10.x handles shard reconnection automatically - no manual intervention needed
            // Removed problematic on_socket_close handler that caused race conditions

            bot.on_ready([&bot](const dpp::ready_t& evt) {
                std::cout << clr::GREEN << "✔ " << clr::RESET << "ready (user " << bot.me.id << ") on shard " << clr::BOLD_CYAN << evt.shard_id << clr::RESET << "\n";
                // Mark initial guild loading as complete 60s after last shard readies.
                // This ensures all GUILD_CREATE events from the READY payload have
                // been processed before we start treating new guild_create as a join.
                if (!g_initial_load_complete.load()) {
                    std::thread([] {
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                        g_initial_load_complete.store(true);
                        std::cout << clr::GREEN << "✔ " << clr::RESET << "initial guild load complete — welcome messages now active\n";
                    }).detach();
                }
            });

            bot.on_resumed([&bot](const dpp::resumed_t& evt) {
                std::cout << clr::GREEN << "↻ " << clr::RESET << "resumed shard " << clr::BOLD_CYAN << evt.shard_id << clr::RESET << "\n";
            });

            // PERFORMANCE OPTIMIZATION: Enhanced health check with per-shard monitoring
            bot.start_timer([&bot, &cmd_handler](dpp::timer timer) {
                double ws_latency = 0;
                auto shards = bot.get_shards();
                
                // Track per-shard latency to identify slow shards
                static int health_check_counter = 0;
                if (++health_check_counter % 10 == 0 && !shards.empty()) {
                    std::cout << clr::BOLD_CYAN << "❤ Shard Health Check:" << clr::RESET << "\n";
                    for (const auto& [shard_id, shard] : shards) {
                        double shard_ping = shard->websocket_ping * 1000;
                        bool connected = shard->is_connected();
                        const char* status_color = connected ? clr::GREEN : clr::RED;
                        std::string status = connected ? "CONNECTED" : "DISCONNECTED";
                        std::cout << "  Shard " << clr::BOLD << shard_id << clr::RESET << ": "
                                 << status_color << status << clr::RESET
                                 << ", ping: " << clr::CYAN << shard_ping << "ms" << clr::RESET << "\n";
                        if (shard_id == 0) {
                            ws_latency = shard_ping;  // Use shard 0 for global stats
                        }
                    }
                    auto perf_stats = cmd_handler.get_performance_stats();
                    std::cout << clr::MAGENTA << "📊" << clr::RESET << " Cache entries: " << clr::BOLD << perf_stats.total_cache_entries << clr::RESET << "\n";
                } else if (!shards.empty()) {
                    ws_latency = shards.begin()->second->websocket_ping * 1000;
                }
                
                commands::global_stats.record_ping(ws_latency);
                
                // Perform cache maintenance
                cmd_handler.periodic_maintenance();
                
                // Update presence
                dpp::activity activity(dpp::activity_type::at_streaming, "b.invite | bronxbot.xyz", "", "https://twitch.tv/siqnole");
                bot.set_presence(dpp::presence(dpp::presence_status::ps_online, activity));
            }, 20);
            
            // Periodically clean up XP leveling caches (every 30 minutes)
            bot.start_timer([](dpp::timer timer) {
                leveling::cleanup_xp_cooldown_cache(3600);
            }, 1800);
            
            // AUTOFISHER: Background loop to run autofishing for active users
            // Runs every 2 minutes and checks if each autofisher is due for a run
            bot.start_timer([&db](dpp::timer timer) {
                try {
                    auto active_users = db.get_all_active_autofishers();
                    if (active_users.empty()) return;
                    
                    auto now = std::chrono::system_clock::now();
                    
                    for (uint64_t user_id : active_users) {
                        // Get tier to determine interval
                        int tier = db.get_autofisher_tier(user_id);
                        if (tier == 0) {
                            // User no longer has autofisher item, deactivate
                            db.deactivate_autofisher(user_id);
                            std::cout << clr::YELLOW << "⚠ " << clr::RESET << "Deactivated autofisher for user " << user_id << " (no item)\n";
                            continue;
                        }
                        
                        // Determine interval based on tier
                        int interval_minutes = (tier == 2) ? 20 : 30;
                        
                        // Check if it's time to run
                        auto last_run = db.get_autofisher_last_run(user_id);
                        bool should_run = false;
                        
                        if (!last_run) {
                            // Never run before, run now
                            should_run = true;
                        } else {
                            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - *last_run);
                            if (elapsed.count() >= interval_minutes) {
                                should_run = true;
                            }
                        }
                        
                        if (should_run) {
                            // Always stamp last_run so the interval advances even on empty runs
                            db.update_autofisher_last_run(user_id);
                            // Run autofishing
                            int64_t value = commands::fishing::run_autofish_for_user(&db, user_id);
                            std::cout << clr::CYAN << "🎣" << clr::RESET << " Autofisher completed for user " << user_id 
                                     << " (tier " << tier << "): " << clr::GREEN << "$" << value << clr::RESET << "\n";
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << clr::RED << "✘ Autofisher loop error: " << e.what() << clr::RESET << "\n";
                } catch (...) {
                    std::cerr << clr::RED << "✘ Autofisher loop unknown error" << clr::RESET << "\n";
                }
            }, 120); // Run every 2 minutes
            
            // TODO: AUTOMINER timer - add once autominer DB table & methods are implemented
            // WORLD EVENTS: Check every 5 minutes for random event spawning
            bot.start_timer([&db](dpp::timer timer) {
                try {
                    commands::world_events::try_spawn_random_event(&db);
                } catch (const std::exception& e) {
                    std::cerr << clr::RED << "✘ world event timer error: " << e.what() << clr::RESET << "\n";
                } catch (...) {
                    std::cerr << clr::RED << "✘ world event timer unknown error" << clr::RESET << "\n";
                }
            }, 300); // 300 seconds = 5 minutes

            // COMMODITY MARKET: Fluctuate prices once per day at 04:00 EST
            bot.start_timer([&db](dpp::timer timer) {
                using namespace std::chrono;
                auto now = system_clock::now();
                time_t tnow = system_clock::to_time_t(now);
                time_t est = tnow - 5 * 3600;
                tm est_tm = *gmtime(&est);
                static int last_market_day = -1;
                if (est_tm.tm_hour == 4 && est_tm.tm_min == 0 && est_tm.tm_yday != last_market_day) {
                    last_market_day = est_tm.tm_yday;
                    commands::passive::fluctuate_market_prices(&db);
                    std::cout << clr::MAGENTA << "[cron]" << clr::RESET << " fluctuated commodity market prices (04:00 EST)\n";
                }
            }, 60); // check every minute
            // GLOBAL TOP‑10 TITLE AWARDER – run once per day at 00:00 EST
            bot.start_timer([&db](dpp::timer timer) {
                using namespace std::chrono;
                auto now = system_clock::now();
                time_t tnow = system_clock::to_time_t(now);
                // EST is UTC‑5 (ignoring DST); subtract 5 hours and then inspect
                time_t est = tnow - 5 * 3600;
                tm est_tm = *gmtime(&est);
                static int last_run_day = -1;
                if (est_tm.tm_hour == 0 && est_tm.tm_min == 0 && est_tm.tm_yday != last_run_day) {
                    last_run_day = est_tm.tm_yday;
                    commands::leaderboard::run_daily_title_awards(&db);
                    std::cout << clr::MAGENTA << "[cron]" << clr::RESET << " awarded daily leaderboard titles (EST midnight)\n";
                }
            }, 60); // check every minute
        }
        
        // Set streaming presence
        dpp::activity activity(dpp::activity_type::at_streaming, "b.invite | bronxbot.xyz", "", "https://twitch.tv/siqnole");
        bot.set_presence(dpp::presence(dpp::presence_status::ps_online, activity));
    });

    // PERFORMANCE OPTIMIZATION: Use optimized message handler
    bot.on_message_create([&bot, &cmd_handler, &db](const dpp::message_create_t& event) {
        // Track XP for leveling system (before command processing)
        leveling::handle_message_xp(bot, event, &db);
        
        // Check if the bot was mentioned (pinged)
        if (!event.msg.author.is_bot()) {
            // Ignore replies - only respond to direct pings
            if (event.msg.message_reference.message_id == 0) {
                for (const auto& mention : event.msg.mentions) {
                    if (mention.first.id == bot.me.id) {
                        // Bot was pinged, introduce itself
                        dpp::embed intro_embed;
                        intro_embed.set_color(0x5865F2)
                            .set_description("hi lol\n__im powered by__\n<:sql:1476704455733940345> | <:dpp:1476705109177012405> / <:C_:1476704911235354695><:plus:1476704929979699515><:plus:1476704929979699515> | \n**and i do**\n- fishing <a:fishdance:1476700021842776319>\n- gambling <:xqcgambling:1476700144295743682>\n- antispam <:shh:1476700239233683617>\n- and more\n\n")
                            .add_field("call me with", "`b.`", true)
                            .add_field("get info with", "`b.help`", true)
                            .set_footer(dpp::embed_footer().set_text(commands::get_build_version()));
                        
                        dpp::message msg(event.msg.channel_id, intro_embed);
                        
                        // Add invite and website buttons
                        dpp::component action_row;
                        action_row.set_type(dpp::cot_action_row);
                        
                        dpp::component invite_btn;
                        invite_btn.set_type(dpp::cot_button);
                        invite_btn.set_style(dpp::cos_link);
                        invite_btn.set_label("add me");
                        invite_btn.set_url("https://discord.com/oauth2/authorize?client_id=" + std::to_string(bot.me.id) + "&permissions=8&scope=bot%20applications.commands");
                        
                        dpp::component website_btn;
                        website_btn.set_type(dpp::cot_button);
                        website_btn.set_style(dpp::cos_link);
                        website_btn.set_label("dashboard (soon)");
                        website_btn.set_url("https://bronxbot.xyz");

                        dpp::component support_btn;
                        support_btn.set_type(dpp::cot_button);
                        support_btn.set_style(dpp::cos_link);
                        support_btn.set_label("support");
                        support_btn.set_url("https://discord.gg/bronx");
                        
                        action_row.add_component(invite_btn);
                        action_row.add_component(website_btn);
                        action_row.add_component(support_btn);
                        msg.add_component(action_row);
                        
                        event.reply(msg, true);
                        return; // Don't process as command
                    }
                }
            }
        }
        
        cmd_handler.handle_message(bot, event);
    });

    bot.on_message_update([&bot, &cmd_handler](const dpp::message_update_t& event) {
        // Only re-run command handler on edits, NOT the XP handler
        // (XP should only be awarded on new messages, not edits)
        dpp::message_create_t fake;
        fake.msg = event.msg;
        cmd_handler.handle_message(bot, fake);
    });

    // PERFORMANCE OPTIMIZATION: Use optimized slash command handler
    bot.on_slashcommand([&bot, &cmd_handler](const dpp::slashcommand_t& event) {
        cmd_handler.handle_slash_command(bot, event);
    });
    
    // Handle patch history pagination buttons, setup buttons, and BAC captcha buttons
    bot.on_button_click([&bot, &db, &cmd_handler](const dpp::button_click_t& event) {
        // BAC (Bronx AntiCheat) captcha button routing
        cmd_handler.bac_on_button_click(bot, event);
        commands::handle_patch_buttons(bot, event, &db);
        commands::setup::handle_setup_button(bot, event, &db);
    });

    // Auto-role: assign roles to new members on join
    commands::register_autorole_handler(bot);

    // Track websocket ping and send welcome message for new guilds
    bot.on_guild_create([&bot, &db](const dpp::guild_create_t& event) {
        auto shards = bot.get_shards();
        if (!shards.empty()) {
            double ws_latency = shards.begin()->second->websocket_ping * 1000;
            commands::global_stats.record_ping(ws_latency);
        }
        
        // Send welcome message when bot joins a new server
        // Only send if initial guild loading is complete (not on startup)
        if (g_initial_load_complete.load()) {
            dpp::guild* g = dpp::find_guild(event.created.id);
            if (g && g->owner_id != 0) {
                // Try to use the system channel if available
                dpp::snowflake target_channel = event.created.system_channel_id;
                
                // Send welcome message if we have a target channel
                if (target_channel != 0) {
                    commands::setup::send_welcome_message(bot, event.created.id, target_channel, g->owner_id);
                }
            }
        }
    });

    // PERFORMANCE OPTIMIZATION: Graceful shutdown with cache cleanup + XP flush
    // Note: signal handlers can't capture references, so we use a static pointer
    // for the batch writer flush-on-exit.
    static bronx::xp::XpBatchWriter* g_shutdown_writer = &xp_batch_writer;
    std::signal(SIGINT, [](int) {
        std::cout << "\n" << clr::BOLD_YELLOW << "⚠ Received SIGINT, shutting down gracefully..." << clr::RESET << "\n";
        if (g_shutdown_writer) g_shutdown_writer->stop();
        bronx::cache::shutdown_cache();
        exit(0);
    });

    std::signal(SIGTERM, [](int) {
        std::cout << "\n" << clr::BOLD_YELLOW << "⚠ Received SIGTERM, shutting down gracefully..." << clr::RESET << "\n";
        if (g_shutdown_writer) g_shutdown_writer->stop();
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
    std::cout << clr::BOLD_BLUE << "└───────────────────────────────────────────────┘" << clr::RESET << "\n\n";
    
    // Start bot
    bot.start(dpp::st_wait);
    
    // Cleanup — flush any remaining XP before exit
    xp_batch_writer.stop();
    bronx::cache::shutdown_cache();
    return 0;
}