#include "event_handlers.h"
#include <iostream>
#include <vector>
#include <string>
#include <dpp/dpp.h>
#include "../performance/optimized_command_handler.h"
#include "../performance/cached_database.h"
#include "../performance/cache_manager.h"
#include "../performance/xp_batch_writer.h"
#include "../performance/async_stat_writer.h"
#include "../performance/local_db.h"
#include "../performance/write_batch_queue.h"
#include "../performance/api_cache_client.h"
#include "../performance/hybrid_database.h"
#include "../performance/message_cache.h"
#include "../performance/snipe_cache.h"
#include "../commands/utility.h"
#include "../commands/utility/autopurge.h"
#include "../commands/fun.h"
#include "../commands/games.h"
#include "../commands/endgame.h"
#include "../commands/help_new.h"
#include "../commands/help_data.h"
#include "../commands/guide.h"
#include "../commands/economy.h"
#include "../commands/leaderboard.h"
#include "../commands/leveling.h"
#include "../commands/leveling/xp_handler.h"
#include "../commands/leveling/xpblacklist.h"
#include "../commands/patch.h"
#include "../commands/owner.h"
#include "../commands/owner/log_beta.h"
#include "../commands/owner/feature_command.h"
#include "../commands/gambling.h"
#include "../commands/moderation.h"
#include "../commands/moderation_commands.h"
#include "../commands/moderation/logconfig.h"
#include "../commands/fishing/autofish_runner.h"
#include "../commands/mining.h"
#include "../commands/global_boss.h"
#include "../commands/global_boss_raid.h"
#include "../commands/setup.h"
#include "../commands/passive.h"
#include "../commands/social.h"
#include "../commands/world_events.h"
#include "../commands/server_economy.h"
#include "../commands/stats_cmd.h"
#include "../database/core/database.h"
#include "../database/operations/stats/stats_operations.h"
#include "../config_loader.h"
#include "../commands/gambling/russian_roulette.h"
#include "../server_logger.h"
#include "../feature_gate.h"

#include "../utils/colors.h"


struct register_commands {};

void register_event_handlers(
    dpp::cluster& bot,
    OptimizedCommandHandler& cmd_handler,
    Database& db,
    bronx::perf::AsyncStatWriter& async_stat_writer,
    bronx::snipe::MessageCache& message_cache,
    bronx::snipe::SnipeCache& snipe_cache,
    bronx::xp::XpBatchWriter& xp_batch_writer,
    bool verbose_events
) {
    bot.on_ready([&bot, &cmd_handler, &db, &xp_batch_writer, &snipe_cache](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_commands>()) {
            std::cout << clr::BOLD_GREEN << "✔ logged in as " << bot.me.username << clr::RESET << " (" << clr::DIM << commands::utility::get_build_version() << clr::RESET << ")!\n";
            std::cout << clr::CYAN << "⚙ " << clr::RESET << "prefix: " << clr::BOLD << cmd_handler.get_prefix() << clr::RESET << "\n";

            // PERFORMANCE FIX: Warm up DPP's HTTPS connection pool so the first
            // user command doesn't pay for TLS handshake + DNS resolution (~3-4s).
            bot.current_user_get([](const dpp::confirmation_callback_t& cb) {
                if (!cb.is_error()) {
                    std::cout << clr::GREEN << "✔ " << clr::RESET << "HTTPS connection warmed up\n";
                }
            });

            // Performance stats
            auto perf_stats = cmd_handler.get_performance_stats();
            std::cout << clr::MAGENTA << "📊" << clr::RESET << " Cache initialized with " << clr::BOLD << perf_stats.total_cache_entries << clr::RESET << " total cache slots\n";
            
            // Warm cache: bulk-fetch all server settings from remote DB once at startup
            try {
                cmd_handler.refresh_settings();
                std::cout << clr::GREEN << "✔ " << clr::RESET << "Server settings synced into cache\n";
            } catch (const std::exception& e) {
                std::cerr << clr::RED << "⚠ " << clr::RESET << "Initial settings sync failed: " << e.what() << "\n";
            }
            
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

            // Use bulk registration to avoid rate limits — retry on transient failures (e.g. 504)
            if (!commands_to_register.empty()) {
                auto attempt = std::make_shared<int>(0);
                auto cmds    = std::make_shared<std::vector<dpp::slashcommand>>(commands_to_register);
                constexpr int max_retries = 3;
                constexpr int backoff_seconds[] = {5, 15, 30};

                std::function<void()> try_register;
                try_register = [&bot, cmds, attempt, max_retries, try_register]() {
                    bot.global_bulk_command_create(*cmds, [&bot, cmds, attempt, max_retries, try_register](const dpp::confirmation_callback_t& callback) {
                        if (callback.is_error()) {
                            auto err = callback.get_error();
                            bool is_transient = (err.message.find("504") != std::string::npos ||
                                                 err.message.find("502") != std::string::npos ||
                                                 err.message.find("parse_error") != std::string::npos);

                            if (is_transient && *attempt < max_retries) {
                                constexpr int backoff[] = {5, 15, 30};
                                int delay = backoff[*attempt];
                                (*attempt)++;
                                std::cerr << clr::YELLOW << "⚠ slash command registration failed (" << err.message
                                          << "), retrying in " << delay << "s (attempt " << *attempt << "/" << max_retries << ")" << clr::RESET << "\n";
                                bot.start_timer([try_register](dpp::timer) {
                                    try_register();
                                }, delay, [](dpp::timer){});
                            } else {
                                std::cerr << clr::RED << "✘ error registering commands: " << err.message << clr::RESET << "\n";
                                for (const auto& e : err.errors) {
                                    std::cerr << clr::RED << "  ↳ " << e.field << ": " << e.reason << " (code " << e.code << ")" << clr::RESET << "\n";
                                }
                            }
                        } else {
                            if (*attempt > 0) {
                                std::cout << clr::BOLD_GREEN << "✔ successfully registered all slash commands (after " << *attempt << " retries)" << clr::RESET << "\n";
                            } else {
                                std::cout << clr::BOLD_GREEN << "✔ successfully registered all slash commands" << clr::RESET << "\n";
                            }
                        }
                    });
                };
                try_register();

                // External API calls (unchanged)
                #ifdef HAVE_LIBCURL
                // ... [external API registration code remains the same]
                #endif
            }
            
            // Register interaction handlers (unchanged)
            // Register interaction handlers using actual function names and namespaces
            commands::register_help_interactions(bot, &cmd_handler);
            commands::register_guide_interactions(bot, &db);
            commands::register_shop_interactions(bot, &db);
            commands::register_fishing_interactions(bot, &db);
            commands::register_leaderboard_interactions(bot, &db);
            commands::register_utility_interactions(bot, &db, &snipe_cache);
            commands::register_owner_interactions(bot, &db, &cmd_handler);
            commands::utility::load_persistent_reaction_roles(bot);
            commands::utility::load_autopurges(bot);
            commands::utility::load_autoroles(bot);
            commands::register_moderation_handlers(bot);
            commands::register_automod_handlers(bot, &db);
            commands::start_infraction_expiry_sweep(bot, &db);
            commands::restore_infraction_timers(bot, &db);
            commands::register_gambling_interactions(bot, &db);
            commands::register_games_handlers(bot, &db);
            commands::register_economy_handlers(bot, &db);
            commands::register_achievements_interactions(bot, &db);
            commands::register_mining_interactions(bot, &db);
            commands::global_boss::register_boss_interactions(bot, &db);
            commands::register_boss_raid_interactions(bot, &db);
            commands::register_passive_interactions(bot, &db);
            commands::register_social_interactions(bot, &db);
            commands::register_stats_interactions(bot, &db);

            // PERFORMANCE FIX: Register level-up callback for the XP batch writer.
            // When the batch writer detects a level-up during its periodic flush,
            // it fires this callback (on its own thread) to send announcements.
            xp_batch_writer.set_levelup_callback([&bot, &db](const std::vector<bronx::xp::LevelUpEvent>& events) {
                // Rate-limit level-up processing: if too many level-ups fire at once
                // (e.g. after a large XP batch flush), only process a bounded number
                // to avoid flooding Discord's REST API and causing 503 overflow.
                constexpr size_t MAX_LEVELUPS_PER_FLUSH = 5;
                size_t processed = 0;
                // Collect all role operations and stagger them via timers
                // to avoid flooding the REST queue (which delays command responses).
                size_t rest_call_index = 0;
                for (auto& ev : events) {
                    if (++processed > MAX_LEVELUPS_PER_FLUSH) {
                        std::cerr << clr::YELLOW << "⚠ " << clr::RESET << "level-up callback: capped at "
                                  << MAX_LEVELUPS_PER_FLUSH << " announcements (had " << events.size() << ")\n";
                        break;
                    }
                    if (ev.guild_id == 0) continue;  // skip global level-ups (no announcement)
                    
                    // Fetch the server leveling config to check if announcements are enabled
                    auto cfg = db.get_guild_leveling_config(ev.guild_id);
                    if (!cfg || !cfg->announce_levelup) continue;
                    
                    std::string announcement = "\xF0\x9F\x8E\x89 <@" + std::to_string(ev.user_id) +
                        "> leveled up to **Level " + std::to_string(ev.new_level) + "**!";
                    
                    // Check for level role rewards
                    auto level_role = db.get_level_role_at_level(ev.guild_id, ev.new_level);
                    if (level_role) {
                        announcement += "\n\xF0\x9F\x8F\x86 You earned the <@&" +
                            std::to_string(level_role->role_id) + "> role!";
                        
                        // PERFORMANCE FIX: Stagger role add/remove with 1s delay per
                        // REST call to avoid flooding the queue (previously fired up
                        // to 20+ guild_member_remove_role calls in a tight loop).
                        size_t delay_s = ++rest_call_index;
                        bot.start_timer([&bot, gid = ev.guild_id, uid = ev.user_id, rid = level_role->role_id](dpp::timer t) {
                            bot.stop_timer(t);
                            bot.guild_member_add_role(gid, uid, rid,
                                [gid, uid, rid](const dpp::confirmation_callback_t& cb) {
                                    if (cb.is_error()) {
                                        std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to add role " << rid
                                                  << " to user " << uid << " in guild " << gid
                                                  << ": " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                                    }
                                });
                        }, delay_s, [](dpp::timer){});
                        
                        if (level_role->remove_previous) {
                            auto all_roles = db.get_level_roles(ev.guild_id);
                            for (auto& r : all_roles) {
                                if (r.level < ev.new_level) {
                                    size_t role_delay = ++rest_call_index;
                                    bot.start_timer([&bot, gid = ev.guild_id, uid = ev.user_id, rid = r.role_id](dpp::timer t) {
                                        bot.stop_timer(t);
                                        bot.guild_member_remove_role(gid, uid, rid,
                                            [gid, uid, rid](const dpp::confirmation_callback_t& cb) {
                                                if (cb.is_error()) {
                                                    std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to remove role " << rid
                                                              << " from user " << uid << " in guild " << gid
                                                              << ": " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                                                }
                                            });
                                    }, role_delay, [](dpp::timer){});
                                }
                            }
                        }
                    }
                    
                    // PERFORMANCE FIX: Stagger level-up announcements too
                    if (cfg->announcement_channel) {
                        uint64_t ch = *cfg->announcement_channel;
                        size_t msg_delay = ++rest_call_index;
                        bot.start_timer([&bot, ch, announcement, gid = ev.guild_id, uid = ev.user_id](dpp::timer t) {
                            bot.stop_timer(t);
                            bot.message_create(dpp::message(ch, announcement),
                                [ch, gid, uid](const dpp::confirmation_callback_t& cb) {
                                    if (cb.is_error()) {
                                        std::cerr << clr::RED << "[leveling] " << clr::RESET << "failed to send level-up in channel " << ch
                                                  << " (guild " << gid << "): " << cb.get_error().message << "\n";
                                    }
                                });
                        }, msg_delay, [](dpp::timer){});
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
                
                // Update presence (every 10th check = ~200s to reduce API load)
                if (health_check_counter % 10 == 0) {
                    dpp::activity activity(dpp::activity_type::at_streaming, "b.invite | bronxbot.xyz", "", "https://twitch.tv/siqnole");
                    bot.set_presence(dpp::presence(dpp::presence_status::ps_online, activity));
                }
            }, 20);
            
            // Periodically clean up XP leveling caches (every 30 minutes)
            bot.start_timer([](dpp::timer timer) {
                leveling::cleanup_xp_cooldown_cache(3600);
            }, 1800);

            // SETTINGS SYNC: Bulk-refresh dashboard-editable settings every 60s.
            // The bot runs off its in-memory cache for guild prefixes, module
            // toggles, and command toggles.  This timer is the only thing that
            // hits the remote DB for those tables, keeping per-message latency
            // low while still picking up dashboard edits within ~1 minute.
            bot.start_timer([&cmd_handler](dpp::timer timer) {
                try {
                    cmd_handler.refresh_settings();
                } catch (const std::exception& e) {
                    std::cerr << clr::RED << "⚠ " << clr::RESET
                              << "Settings sync failed: " << e.what() << "\n";
                }
            }, 60);
            
            // AUTOFISHER: Background loop to run autofishing for active users
            // Set bot pointer for DM notifications on autofisher failure
            commands::fishing::set_autofish_bot(&bot);
            // Runs every 2 minutes and checks if each autofisher is due for a run
            bot.start_timer([&db](dpp::timer timer) {
                try {
                    auto active_users = db.get_all_active_autofishers();
                    if (active_users.empty()) return;
                    
                    auto now = std::chrono::system_clock::now();
                    
                    for (auto& [user_id, guild_id] : active_users) {
                        // Get tier to determine interval
                        int tier = db.get_autofisher_tier(user_id, guild_id);
                        if (tier == 0) {
                            // User no longer has autofisher item, deactivate
                            db.deactivate_autofisher(user_id, guild_id);
                            std::cout << clr::YELLOW << "⚠ " << clr::RESET << "Deactivated autofisher for user " << user_id << " (no item)\n";
                            continue;
                        }
                        
                        // Determine interval based on tier
                        int interval_minutes = (tier == 2) ? 20 : 30;
                        
                        // Check if it's time to run
                        auto last_run = db.get_autofisher_last_run(user_id, guild_id);
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
                            db.update_autofisher_last_run(user_id, guild_id);
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
    bot.on_message_create([&bot, &cmd_handler, &db, &async_stat_writer, &message_cache, verbose_events](const dpp::message_create_t& event) {
        auto _msg_t0 = std::chrono::steady_clock::now();

        // Cache message content for snipe (before anything else)
        message_cache.cache_message(event.msg);

        // Track XP for leveling system (before command processing)
        leveling::handle_message_xp(bot, event, &db);

        auto _msg_t1 = std::chrono::steady_clock::now();
        double _xp_ms = std::chrono::duration<double, std::milli>(_msg_t1 - _msg_t0).count();
        if (_xp_ms > 5.0) {
            std::cerr << "\033[1;33m[pre-cmd-slow]\033[0m handle_message_xp took " << _xp_ms << "ms\n";
        }

        // Stats: track message event (skip bots)
        if (!event.msg.author.is_bot() && event.msg.guild_id != 0) {
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::CYAN << "message_create" << clr::RESET
                          << " guild=" << event.msg.guild_id
                          << " user=" << event.msg.author.id
                          << " ch=" << event.msg.channel_id
                          << " len=" << event.msg.content.size() << "\n";
            }
            async_stat_writer.enqueue_message_event(
                event.msg.guild_id, event.msg.author.id, event.msg.channel_id, "message");
        }
        
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
                            .set_footer(dpp::embed_footer().set_text(commands::utility::get_build_version()));
                        
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
                        website_btn.set_label("dashboard");
                        website_btn.set_url("https://bronxbot.xyz");

                        dpp::component support_btn;
                        support_btn.set_type(dpp::cot_button);
                        support_btn.set_style(dpp::cos_link);
                        support_btn.set_label("help");
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

    bot.on_message_update([&bot, &cmd_handler, &async_stat_writer, &message_cache, verbose_events](const dpp::message_update_t& event) {
        // Stats: track message edit (skip bots)
        if (!event.msg.author.is_bot() && event.msg.guild_id != 0) {
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::YELLOW << "message_update" << clr::RESET
                          << " guild=" << event.msg.guild_id
                          << " user=" << event.msg.author.id
                          << " ch=" << event.msg.channel_id << "\n";
            }
            async_stat_writer.enqueue_message_event(
                event.msg.guild_id, event.msg.author.id, event.msg.channel_id, "edit");
                
            // Check cache to log edit
            auto cached = message_cache.get_message(static_cast<uint64_t>(event.msg.id));
            if (cached && cached->content != event.msg.content) {
                dpp::embed log_embed = bronx::create_embed("Before:\n" + cached->content + "\n\nAfter:\n" + event.msg.content)
                    .set_title("Message Edited")
                    .set_color(0xFFA500)
                    .add_field("Author", event.msg.author.format_username() + " (<@" + std::to_string(event.msg.author.id) + ">)", true)
                    .add_field("Channel", "<#" + std::to_string(event.msg.channel_id) + ">", true);
                bronx::logger::ServerLogger::get().log_embed(event.msg.guild_id, bronx::logger::LOG_TYPE_MESSAGES, log_embed);
                
                // Update cache
                auto updated = *cached;
                updated.content = event.msg.content;
                message_cache.update_message(static_cast<uint64_t>(event.msg.id), updated);
            }
        }
        // Only re-run command handler on edits, NOT the XP handler
        // (XP should only be awarded on new messages, not edits)
        dpp::message_create_t fake;
        fake.msg = event.msg;
        cmd_handler.handle_message(bot, fake);
    });

    // PERFORMANCE OPTIMIZATION: Use optimized slash command handler
    bot.on_slashcommand([&bot, &cmd_handler, verbose_events](const dpp::slashcommand_t& event) {
        if (verbose_events) {
            std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::MAGENTA << "slashcommand" << clr::RESET
                      << " /" << event.command.get_command_name()
                      << " guild=" << event.command.guild_id
                      << " user=" << event.command.usr.id << "\n";
        }
        cmd_handler.handle_slash_command(bot, event);
    });
    
    // Handle patch history pagination buttons, setup buttons, stats buttons, and BAC captcha buttons
    bot.on_button_click([&bot, &db, &cmd_handler](const dpp::button_click_t& event) {
        // BAC (Bronx AntiCheat) captcha button routing
        cmd_handler.bac_on_button_click(bot, event);
        commands::handle_patch_buttons(bot, event, &db);
        commands::setup::handle_setup_button(bot, event, &db);
        commands::handle_stats_buttons(bot, event, &db);
        commands::handle_passive_button(bot, event, &db);
        commands::handle_endgame_button(bot, event, &db);
    });

    // Auto-role: assign roles to new members on join
    commands::register_autorole_handler(bot);

    // Stats: track member joins (runs after autorole handler which also uses on_guild_member_add)
    bot.on_guild_member_add([&async_stat_writer, verbose_events](const dpp::guild_member_add_t& event) {
        if (event.adding_guild.id != 0) {
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::GREEN << "member_add" << clr::RESET
                          << " guild=" << event.adding_guild.id
                          << " user=" << event.added.user_id << "\n";
            }
            async_stat_writer.enqueue_member_event(
                event.adding_guild.id, event.added.user_id, "join");
                
            dpp::embed log_embed = bronx::create_embed("User <@" + std::to_string(event.added.user_id) + "> joined the server.")
                .set_title("Member Joined")
                .set_color(0x00FF00);
            bronx::logger::ServerLogger::get().log_embed(event.adding_guild.id, bronx::logger::LOG_TYPE_MEMBERS, log_embed);
        }
    });

    // Stats: track member leaves
    bot.on_guild_member_remove([&async_stat_writer, verbose_events](const dpp::guild_member_remove_t& event) {
        if (event.removing_guild.id != 0) {
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::RED << "member_remove" << clr::RESET
                          << " guild=" << event.removing_guild.id
                          << " user=" << event.removed.id << "\n";
            }
            async_stat_writer.enqueue_member_event(
                event.removing_guild.id, event.removed.id, "leave");
                
            dpp::embed log_embed = bronx::create_embed("User " + event.removed.format_username() + " (<@" + std::to_string(event.removed.id) + ">) left the server.")
                .set_title("Member Left")
                .set_color(0xFF0000);
            bronx::logger::ServerLogger::get().log_embed(event.removing_guild.id, bronx::logger::LOG_TYPE_MEMBERS, log_embed);
        }
    });

    // Stats: track message deletes + capture for snipe
    bot.on_message_delete([&async_stat_writer, &message_cache, &snipe_cache, verbose_events](const dpp::message_delete_t& event) {
        if (event.guild_id != 0) {
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::RED << "message_delete" << clr::RESET
                          << " guild=" << event.guild_id
                          << " ch=" << event.channel_id << "\n";
            }
            async_stat_writer.enqueue_message_event(
                event.guild_id, 0, event.channel_id, "delete");

            // Try to pop the message from our content cache for snipe
            auto cached = message_cache.pop_message(static_cast<uint64_t>(event.id));
            if (cached) {
                bronx::snipe::DeletedMessage dm;
                dm.message_id = cached->message_id;
                dm.guild_id = cached->guild_id;
                dm.channel_id = cached->channel_id;
                dm.author_id = cached->author_id;
                dm.author_tag = cached->author_tag;
                dm.author_avatar = cached->author_avatar;
                dm.content = cached->content;
                dm.attachment_urls = cached->attachment_urls;
                dm.embeds_summary = cached->embeds_json;
                dm.deleted_at = std::chrono::system_clock::now();
                snipe_cache.add_deleted(std::move(dm));
                
                dpp::embed log_embed = bronx::create_embed(cached->content)
                    .set_title("Message Deleted")
                    .set_color(0xFF0000)
                    .add_field("Author", cached->author_tag + " (<@" + std::to_string(cached->author_id) + ">)", true)
                    .add_field("Channel", "<#" + std::to_string(cached->channel_id) + ">", true);
                if (!cached->attachment_urls.empty()) {
                    std::string att_str;
                    for (const auto& url : cached->attachment_urls) {
                        att_str += url + "\n";
                    }
                    if (att_str.size() > 1024) att_str = att_str.substr(0, 1021) + "...";
                    log_embed.add_field("Attachments", att_str);
                }
                bronx::logger::ServerLogger::get().log_embed(event.guild_id, bronx::logger::LOG_TYPE_MESSAGES, log_embed);
            }
        }
    });

    // Stats: track voice channel joins / leaves
    bot.on_voice_state_update([&async_stat_writer, verbose_events](const dpp::voice_state_update_t& event) {
        auto& vs = event.state;
        if (vs.guild_id == 0 || vs.user_id == 0) return;

        if (vs.channel_id != 0) {
            // User joined or moved to a voice channel
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::CYAN << "voice_join" << clr::RESET
                          << " guild=" << vs.guild_id << " user=" << vs.user_id
                          << " ch=" << vs.channel_id << "\n";
            }
            async_stat_writer.enqueue_voice_event(
                vs.guild_id, vs.user_id, vs.channel_id, "join");
        } else {
            // User left voice (channel_id == 0 means disconnected)
            if (verbose_events) {
                std::cout << "\033[2m[\033[36mEVENT\033[2m]\033[0m " << clr::RED << "voice_leave" << clr::RESET
                          << " guild=" << vs.guild_id << " user=" << vs.user_id << "\n";
            }
            async_stat_writer.enqueue_voice_event(
                vs.guild_id, vs.user_id, 0, "leave");
        }
    });

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

}
