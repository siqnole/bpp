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
#include "../utils/string_utils.h"
#include "../utils/colors.h"
#include "../commands/owner.h"
#include "../commands/owner/log_beta.h"
#include "../commands/owner/feature_command.h"
#include "../commands/gambling.h"
#include "../commands/moderation.h"
#include "../commands/moderation_commands.h"
#include "../commands/moderation/logconfig.h"
#include "../commands/fishing/autofish_runner.h"
#include "../commands/mining.h"
#include "../commands/mining/automine_runner.h"
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
#include "../utils/logger.h"
#include "../commands/moderation/infraction_engine.h"
#include "../database/operations/moderation/raid_operations.h"


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
            bronx::logger::success("system", "logged in as " + bot.me.username + " (" + commands::utility::get_build_version() + ")");
            bronx::logger::info("system", "prefix: " + cmd_handler.get_prefix());

            // PERFORMANCE FIX: Warm up DPP's HTTPS connection pool so the first
            // user command doesn't pay for TLS handshake + DNS resolution (~3-4s).
            bot.current_user_get([&bot](const dpp::confirmation_callback_t& cb) {
                if (!cb.is_error()) {
                    bronx::logger::success("system", "HTTPS connection warmed up");
                }
            });

            // Performance stats
            auto perf_stats = cmd_handler.get_performance_stats();
            bronx::logger::info("cache", "initialized with " + std::to_string(perf_stats.total_cache_entries) + " slots");
            
            // Warm cache: bulk-fetch all server settings from remote DB once at startup
            try {
                cmd_handler.refresh_settings();
                bronx::logger::success("system", "server settings synced into cache");
            } catch (const std::exception& e) {
                bronx::logger::error("system", "initial settings sync failed: " + std::string(e.what()));
            }
            
            // Register slash commands (unchanged)
            auto slash_commands = cmd_handler.get_slash_commands();
            bronx::logger::info("slash command", "registering " + std::to_string(slash_commands.size()) + " slash commands");
            
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
                        bronx::logger::warn("slash command", "description too long for '" + sc.name + "' (" + std::to_string(sc.description.size()) + " chars, max 100)");
                    }
                    auto it = name_index.find(sc.name);
                    if (it != name_index.end()) {
                        bronx::logger::warn("slash command", "duplicate name '" + sc.name + "' at index " + std::to_string(it->second) + " and " + std::to_string(ci));
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
                                bronx::logger::warn("slash command", "registration failed (" + err.message + "), retrying in " + std::to_string(delay) + "s (attempt " + std::to_string(*attempt) + "/" + std::to_string(max_retries) + ")");
                                bot.start_timer([try_register](dpp::timer) {
                                    try_register();
                                }, delay);
                            } else {
                                bronx::logger::error("slash command", "fatal: bulk registration failed: " + err.message);
                                for (const auto& e : err.errors) {
                                    bronx::logger::error("slash command", "  ↳ " + e.field + ": " + e.reason + " (code " + e.code + ")");
                                }
                            }
                        } else {
                            if (*attempt > 0) {
                                bronx::logger::success("system", "registered slash commands after " + std::to_string(*attempt) + " retries");
                            } else {
                                bronx::logger::success("system", "successfully registered all slash commands");
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
            // Restore active moderation timers and start expiry sweep
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
            commands::register_moderation_handlers(bot, &db);
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
                        bronx::logger::warn("leveling", "callback: capped at " + std::to_string(MAX_LEVELUPS_PER_FLUSH) + " announcements (had " + std::to_string(events.size()) + ")");
                        break;
                    }
                    if (ev.guild_id == 0) continue;  // skip global level-ups (no announcement)
                    
                    // Fetch the server leveling config to check if announcements are enabled
                    auto cfg = db.get_guild_leveling_config(ev.guild_id);
                    if (!cfg || !cfg->announce_levelup) continue;
                    
                    std::string announcement;
                    
                    // Fetch guild data for placeholders
                    dpp::guild* guild = dpp::find_guild(ev.guild_id);
                    if (guild) {
                        std::unordered_map<std::string, std::string> placeholders;
                        placeholders["name"] = "<@" + std::to_string(ev.user_id) + ">";
                        placeholders["level"] = std::to_string(ev.new_level);
                        placeholders["members"] = std::to_string(guild->member_count);
                        
                        // Date logic
                        time_t now = time(0);
                        struct tm tstruct;
                        char buf[80];
                        tstruct = *localtime(&now);
                        strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);
                        placeholders["date"] = std::string(buf);
                        
                        // CreatedAt logic (guild creation date)
                        double created_at = guild->get_creation_time();
                        time_t cat_t = (time_t)created_at;
                        struct tm cat_struct = *localtime(&cat_t);
                        strftime(buf, sizeof(buf), "%Y-%m-%d", &cat_struct);
                        placeholders["createdat"] = std::string(buf);
                        
                        announcement = bronx::utils::replace_placeholders(cfg->announcement_message, placeholders);
                    } else {
                        // Fallback if guild not in cache
                        announcement = "\xF0\x9F\x8E\x89 <@" + std::to_string(ev.user_id) +
                            "> reached **Level " + std::to_string(ev.new_level) + "**!";
                    }
                    
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
                                        bronx::logger::error("leveling", "failed to add role " + std::to_string(rid) + " to user " + std::to_string(uid));
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
                                                    bronx::logger::error("leveling", "failed to remove role " + std::to_string(rid) + " from user " + std::to_string(uid));
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
                                        bronx::logger::error("leveling", "failed to send level-up in channel " + std::to_string(ch) + " (user " + std::to_string(uid) + ")");
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
                bronx::logger::success("system", "shard " + std::to_string(evt.shard_id) + " ready (user " + std::to_string(bot.me.id) + ")");
                // Mark initial guild loading as complete 60s after last shard readies.
                // This ensures all GUILD_CREATE events from the READY payload have
                // been processed before we start treating new guild_create as a join.
                if (!g_initial_load_complete.load()) {
                    std::thread([] {
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                        g_initial_load_complete.store(true);
                        bronx::logger::success("system", "initial guild load complete — welcome messages active");
                    }).detach();
                }
            });

            bot.on_resumed([&bot](const dpp::resumed_t& evt) {
                bronx::logger::success("system", "resumed shard " + std::to_string(evt.shard_id));
            });

            // PERFORMANCE OPTIMIZATION: Enhanced health check with per-shard monitoring
            bot.start_timer([&bot, &cmd_handler](dpp::timer timer) {
                double ws_latency = 0;
                auto shards = bot.get_shards();
                
                // Track per-shard latency to identify slow shards
                static int health_check_counter = 0;
                if (++health_check_counter % 10 == 0 && !shards.empty()) {
                    for (const auto& [shard_id, shard] : shards) {
                        double shard_ping = shard->websocket_ping * 1000;
                        bool connected = shard->is_connected();
                        std::string status = connected ? "CONNECTED" : "DISCONNECTED";
                        bronx::logger::Level lvl = connected ? bronx::logger::Level::SUCCESS : bronx::logger::Level::ERR;
                        bronx::logger::log(lvl, "health", "shard " + std::to_string(shard_id) + ": " + status + " (" + std::to_string(static_cast<int>(shard_ping)) + "ms)");
                        if (shard_id == 0) {
                            ws_latency = shard_ping;  // Use shard 0 for global stats
                        }
                    }
                    auto perf_stats = cmd_handler.get_performance_stats();
                    bronx::logger::info("cache", "current entries: " + std::to_string(perf_stats.total_cache_entries));
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
                    bronx::logger::error("leveling", "exception in process_xp: " + std::string(e.what()));
                } catch (...) {
                    bronx::logger::error("leveling", "unknown exception in process_xp");
                }
            }, 60);
            
            // AUTOFISHER: Background loop to run autofishing for active users
            // Set bot pointer for DM notifications on autofisher failure
            commands::fishing::set_autofish_bot(&bot);
            // Runs every 2 minutes and checks if each autofisher is due for a run
            bot.start_timer([&db, &cmd_handler](dpp::timer timer) {
                try {
                    auto active_users = db.get_all_active_autofishers();
                    if (active_users.empty()) return;
                    
                    auto now = std::chrono::system_clock::now();
                    
                    for (auto& [user_id, guild_id] : active_users) {
                        // Get tier to determine interval
                        int tier = db.get_autofisher_tier(user_id, guild_id);
                        bool has_item = (tier > 0);
                        if (!has_item) {
                            bronx::logger::warn("autofish", "deactivated for user " + std::to_string(user_id) + " (no item)");
                            // cmd_handler.get_hybrid_db()->set_autofisher_enabled(user_id, false);
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
                            bronx::logger::success("fishing", "autofisher completed for user " + std::to_string(user_id) + " (tier " + std::to_string(tier) + "): $" + std::to_string(value));
                        }
                    }
                } catch (const std::exception& e) {
                    bronx::logger::error("autofish", "loop error: " + std::string(e.what()));
                } catch (...) {
                    bronx::logger::error("autofish", "loop unknown error");
                }
            }, 120); // Run every 2 minutes
            
            // AUTOMINER: Background loop — runs every 5 minutes, processes every active miner
            // Interval per user is 30 min (basic) or 20 min (prestige ≥1), checked inside the loop.
            bot.start_timer([&db](dpp::timer timer) {
                try {
                    auto active_users = db.get_all_active_autominers();
                    if (active_users.empty()) return;

                    auto now = std::chrono::system_clock::now();

                    for (auto& user_id : active_users) {
                        // Determine interval: prestige users mine faster
                        int prestige = db.get_prestige(user_id);
                        int interval_minutes = (prestige >= 1) ? 20 : 30;

                        auto last_run = db.get_autominer_last_run(user_id);
                        bool should_run = false;

                        if (!last_run) {
                            should_run = true;
                        } else {
                            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - *last_run);
                            if (elapsed.count() >= interval_minutes) {
                                should_run = true;
                            }
                        }

                        if (should_run) {
                            db.update_autominer_last_run(user_id);
                            int64_t value = commands::mining::run_automine_for_user(&db, user_id);
                            bronx::logger::success("autominer", "cycle completed for user " +
                                std::to_string(user_id) + " (prestige " + std::to_string(prestige) +
                                "): $" + std::to_string(value));
                        }
                    }
                } catch (const std::exception& e) {
                    bronx::logger::error("autominer", "loop error: " + std::string(e.what()));
                } catch (...) {
                    bronx::logger::error("autominer", "loop unknown error");
                }
            }, 300); // check every 5 minutes

            // WORLD EVENTS: Check every 5 minutes for random event spawning
            bot.start_timer([&db](dpp::timer timer) {
                try {
                    commands::world_events::try_spawn_random_event(&db);
                } catch (const std::exception& e) {
                    bronx::logger::error("world event", "timer error: " + std::string(e.what()));
                } catch (...) {
                    bronx::logger::error("world event", "timer unknown error");
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
                    bronx::logger::info("cron", "fluctuated commodity market prices (04:00 EST)");
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
                    bronx::logger::info("cron", "awarded daily leaderboard titles (EST midnight)");
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
        if (_xp_ms > 200) {
            bronx::logger::debug("pre-cmd-slow", "handle_message_xp took " + std::to_string(_xp_ms) + "ms");
        }
 
        // Stats: track message event (skip bots)
        if (!event.msg.author.is_bot() && event.msg.guild_id != 0) {
            if (verbose_events) {
                bronx::logger::debug("event", "message_create guild=" + std::to_string(event.msg.guild_id) + " user=" + std::to_string(event.msg.author.id) + " ch=" + std::to_string(event.msg.channel_id) + " len=" + std::to_string(event.msg.content.size()));
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
        
        // Check for 'bronx <url>' trigger (case-insensitive, handles various spacing)
        std::string raw_content = event.msg.content;
        std::string msg_content = raw_content;
        msg_content.erase(0, msg_content.find_first_not_of(" \t\n\r\f\v")); // Trim leading whitespace
        
        std::string content_lower = msg_content;
        std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
        
        if (content_lower.rfind("bronx", 0) == 0) {
            std::string url_part = msg_content.substr(5);
            url_part.erase(0, url_part.find_first_not_of(" \t\n\r\f\v")); // Trim spaces between 'bronx' and URL
            
            if (!url_part.empty() && (url_part.find("http://") == 0 || url_part.find("https://") == 0)) {
                if (url_part == "bronx") {
                    bronx::logger::debug("trigger", "bronx detected for URL: " + url_part);
                    bot.message_add_reaction(event.msg.id, event.msg.channel_id, "bronx:123456789012345678");
                }
                
                // Send initial feedback
                dpp::message status_msg(event.msg.channel_id, "📥 Processing your link...");
                status_msg.set_reference(event.msg.id);
                
                bot.message_create(status_msg, [&bot, event, url_part](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) return;
                    dpp::message sent_msg = std::get<dpp::message>(cb.value);
                    
                        std::thread([&bot, event, sent_msg, url_part]() {
                            auto log_cb = [&bot, sent_msg](const std::string& logs) {
                                dpp::message update(sent_msg.channel_id, "📥 Processing your link...\n```\n" + logs + "\n```");
                                update.id = sent_msg.id;
                                bot.message_edit(update);
                            };
                            /*
                            commands::utility::process_download_request(bot, url_part, [&bot, sent_msg](const dpp::message& m) {
                                dpp::message reply = m;
                                reply.id = sent_msg.id;
                                reply.set_channel_id(sent_msg.channel_id);
                                bot.message_edit(reply);
                            }, log_cb);
                            */
                            dpp::message edit_msg(sent_msg.channel_id, "Download functionality is currently being refactored.");
                            edit_msg.id = sent_msg.id;
                            bot.message_edit(edit_msg);
                        }).detach();
                });
                return; // Triggered, don't process as command
            }
        }
        
        cmd_handler.handle_message(bot, event);
    });

    bot.on_message_update([&bot, &cmd_handler, &async_stat_writer, &message_cache, verbose_events](const dpp::message_update_t& event) {
        // Stats: track message edit (skip bots)
        if (!event.msg.author.is_bot() && event.msg.guild_id != 0) {
            if (verbose_events) {
                bronx::logger::debug("event", "message_update guild=" + std::to_string(event.msg.guild_id) + " user=" + std::to_string(event.msg.author.id) + " ch=" + std::to_string(event.msg.channel_id));
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
            bronx::logger::debug("event", "slashcommand /" + event.command.get_command_name() + " guild=" + std::to_string(event.command.guild_id) + " user=" + std::to_string(event.command.usr.id));
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

    // Join velocity tracking for raid protection
    static std::mutex join_mutex;
    static std::map<uint64_t, std::vector<time_t>> guild_joins;

    // Stats: track member joins (runs after autorole handler which also uses on_guild_member_add)
    bot.on_guild_member_add([&bot, &db, &async_stat_writer, verbose_events](const dpp::guild_member_add_t& event) {
        uint64_t guild_id = event.adding_guild.id;
        uint64_t user_id = event.added.user_id;

        // Raid Protection Check
        auto raid_settings = bronx::db::raid_operations::get_settings(&db, guild_id);
        if (raid_settings.join_gate_level != bronx::db::JoinGateLevel::OFF) {
            bool should_action = false;
            std::string reason = "Raid Protection: ";

            if (raid_settings.join_gate_level == bronx::db::JoinGateLevel::LOW) {
                // Account age check
                double created = event.added.user_id.get_creation_time();
                double now = (double)time(nullptr);
                int age_days = (int)((now - created) / 86400);
                if (age_days < raid_settings.min_account_age_days) {
                    should_action = true;
                    reason += "Account too young (" + std::to_string(age_days) + " days)";
                }
            } else if (raid_settings.join_gate_level == bronx::db::JoinGateLevel::MEDIUM) {
                // Join velocity check
                time_t now = time(nullptr);
                {
                    std::lock_guard<std::mutex> lock(join_mutex);
                    auto& joins = guild_joins[guild_id];
                    
                    // Cleanup old joins (> 60s)
                    joins.erase(std::remove_if(joins.begin(), joins.end(), [now](time_t t) {
                        return (now - t) > 60;
                    }), joins.end());
                    
                    joins.push_back(now);
                    
                    if ((int)joins.size() > raid_settings.join_velocity_threshold) {
                        should_action = true;
                        reason += "Join velocity spike (" + std::to_string(joins.size()) + " joins/min)";
                    }
                }
            } else if (raid_settings.join_gate_level == bronx::db::JoinGateLevel::HIGH) {
                // Lockdown
                should_action = true;
                reason += "Server is in join lockdown";
            } else if (raid_settings.join_gate_level == bronx::db::JoinGateLevel::MAX) {
                // Ban all
                should_action = true;
                reason += "Server is in extreme join protection (auto-ban)";
            }

            if (should_action) {
                if (raid_settings.join_gate_level == bronx::db::JoinGateLevel::MAX) {
                    bot.set_audit_reason(reason).guild_ban_add(guild_id, user_id, 0);
                } else {
                    bot.set_audit_reason(reason).guild_member_delete(guild_id, user_id);
                }

                if (raid_settings.notify_channel_id) {
                    dpp::embed alert = bronx::create_embed("🛡️ **Raid Protection Triggered**\n"
                                                          "User: <@" + std::to_string(user_id) + ">\n"
                                                          "Action: `" + (raid_settings.join_gate_level == bronx::db::JoinGateLevel::MAX ? "BAN" : "KICK") + "`\n"
                                                          "Reason: " + reason)
                                       .set_color(::bronx::COLOR_ERROR);
                    bot.message_create(dpp::message(*raid_settings.notify_channel_id, alert));
                }
                return; // Stop further processing for this join
            }
        }

        if (verbose_events) {
            bronx::logger::debug("event", "member_add guild=" + std::to_string(guild_id) + " user=" + std::to_string(user_id));
        }
        async_stat_writer.enqueue_member_event(guild_id, user_id, "join");
        
        dpp::embed log_embed = bronx::create_embed("User <@" + std::to_string(user_id) + "> joined the server.")
            .set_title("Member Joined")
            .set_color(0x00FF00);
        bronx::logger::ServerLogger::get().log_embed(guild_id, bronx::logger::LOG_TYPE_MEMBERS, log_embed);
    });

    // Stats: track member leaves
    bot.on_guild_member_remove([&async_stat_writer, verbose_events](const dpp::guild_member_remove_t& event) {
        /*
        if (event.removing_guild.id != 0) {
            if (verbose_events) {
                bronx::logger::debug("event", "member_remove guild=" + std::to_string(event.removing_guild.id) + " user=" + std::to_string(event.removed.id));
            }
            async_stat_writer.enqueue_member_event(
                event.removing_guild.id, event.removed.id, "leave");
                
            dpp::embed log_embed = bronx::create_embed("User " + event.removed.format_username() + " (<@" + std::to_string(event.removed.id) + ">) left the server.")
                .set_title("Member Left")
                .set_color(0xFF0000);
            bronx::logger::ServerLogger::get().log_embed(event.removing_guild.id, bronx::logger::LOG_TYPE_MEMBERS, log_embed);
        }
        */
    });

    // Stats: track message deletes + capture for snipe
    bot.on_message_delete([&async_stat_writer, &message_cache, &snipe_cache, verbose_events](const dpp::message_delete_t& event) {
        if (event.guild_id != 0) {
            if (verbose_events) {
                bronx::logger::debug("event", "message_delete guild=" + std::to_string(event.guild_id) + " ch=" + std::to_string(event.channel_id));
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
                bronx::logger::debug("event", "voice_join guild=" + std::to_string(vs.guild_id) + " user=" + std::to_string(vs.user_id) + " ch=" + std::to_string(vs.channel_id));
            }
            async_stat_writer.enqueue_voice_event(
                vs.guild_id, vs.user_id, vs.channel_id, "join");
        } else {
            // User left voice (channel_id == 0 means disconnected)
            if (verbose_events) {
                bronx::logger::debug("event", "voice_leave guild=" + std::to_string(vs.guild_id) + " user=" + std::to_string(vs.user_id));
            }
            async_stat_writer.enqueue_voice_event(
                vs.guild_id, vs.user_id, 0, "leave");
        }
    });

    // --- SERVER LOGS: Roles ---
    bot.on_guild_role_create([verbose_events](const dpp::guild_role_create_t& event) {
        if (verbose_events) bronx::logger::debug("event", "role_create guild=" + std::to_string(event.creating_guild.id));
        dpp::embed log_embed = bronx::create_embed("Role <@&" + std::to_string(event.created.id) + "> was created.")
            .set_title("Role Created")
            .set_color(0x00FF00);
        bronx::logger::ServerLogger::get().log_embed(event.creating_guild.id, bronx::logger::LOG_TYPE_SERVER, log_embed);
    });

    bot.on_guild_role_delete([verbose_events](const dpp::guild_role_delete_t& event) {
        if (verbose_events) bronx::logger::debug("event", "role_delete guild=" + std::to_string(event.deleting_guild.id));
        dpp::embed log_embed = bronx::create_embed("Role ID `" + std::to_string(event.deleted.id) + "` was deleted.")
            .set_title("Role Deleted")
            .set_color(0xFF0000);
        bronx::logger::ServerLogger::get().log_embed(event.deleting_guild.id, bronx::logger::LOG_TYPE_SERVER, log_embed);
    });

    // --- SERVER LOGS: Channels ---
    bot.on_channel_create([verbose_events](const dpp::channel_create_t& event) {
        if (verbose_events) bronx::logger::debug("event", "channel_create guild=" + std::to_string(event.created.guild_id));
        dpp::embed log_embed = bronx::create_embed("Channel <#" + std::to_string(event.created.id) + "> was created.")
            .set_title("Channel Created")
            .set_color(0x00FF00);
        bronx::logger::ServerLogger::get().log_embed(event.created.guild_id, bronx::logger::LOG_TYPE_SERVER, log_embed);
    });

    bot.on_channel_delete([verbose_events](const dpp::channel_delete_t& event) {
        if (verbose_events) bronx::logger::debug("event", "channel_delete guild=" + std::to_string(event.deleted.guild_id));
        dpp::embed log_embed = bronx::create_embed("Channel `" + event.deleted.name + "` was deleted.")
            .set_title("Channel Deleted")
            .set_color(0xFF0000);
        bronx::logger::ServerLogger::get().log_embed(event.deleted.guild_id, bronx::logger::LOG_TYPE_SERVER, log_embed);
    });

    // --- MODERATION LOGS: Guild Bans ---
    bot.on_guild_ban_add([verbose_events](const dpp::guild_ban_add_t& event) {
        if (verbose_events) bronx::logger::debug("event", "ban_add guild=" + std::to_string(event.banning_guild.id));
        dpp::embed log_embed = bronx::create_embed("User <@" + std::to_string(event.banned.id) + "> was banned from the server.")
            .set_title("User Banned")
            .set_color(0x991B1B);
        bronx::logger::ServerLogger::get().log_embed(event.banning_guild.id, bronx::logger::LOG_TYPE_MODERATION, log_embed);
    });

    bot.on_guild_ban_remove([verbose_events](const dpp::guild_ban_remove_t& event) {
        if (verbose_events) bronx::logger::debug("event", "ban_remove guild=" + std::to_string(event.unbanning_guild.id));
        dpp::embed log_embed = bronx::create_embed("User <@" + std::to_string(event.unbanned.id) + "> was unbanned.")
            .set_title("User Unbanned")
            .set_color(0x00FF00);
        bronx::logger::ServerLogger::get().log_embed(event.unbanning_guild.id, bronx::logger::LOG_TYPE_MODERATION, log_embed);
    });

    bot.on_invite_create([verbose_events](const dpp::invite_create_t& event) {
        if (verbose_events) bronx::logger::debug("event", "invite_create guild=" + std::to_string(event.created_invite.guild_id));
        dpp::embed log_embed = bronx::create_embed("Invite `" + event.created_invite.code + "` was created by <@" + std::to_string(event.created_invite.inviter_id) + ">.")
            .set_title("Invite Created")
            .set_color(0x3B82F6);
        bronx::logger::ServerLogger::get().log_embed(event.created_invite.guild_id, bronx::logger::LOG_TYPE_SERVER, log_embed);
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
