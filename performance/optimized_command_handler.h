#pragma once
#include <dpp/dpp.h>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include "../command.h"
#include "../command_handler.h"
#include "../embed_style.h"
#include "../performance/cached_database.h"
#include "../performance/async_stat_writer.h"
#include "../performance/hybrid_database.h"
// forward declare owner helper to avoid circular dependency
namespace commands { bool is_owner(uint64_t user_id); }

// High-performance optimized command handler - inherits from CommandHandler for compatibility
class OptimizedCommandHandler : public CommandHandler {
private:
    std::unique_ptr<bronx::cache::CachedDatabase> cached_db_;
    bronx::perf::AsyncStatWriter* async_stat_writer_ = nullptr;
    bronx::hybrid::HybridDatabase* hybrid_db_ = nullptr;
    
    // Performance optimizations  
    mutable std::shared_mutex prefix_cache_mutex_;
    std::unordered_map<uint64_t, std::vector<std::string>> cached_all_prefixes_;
    std::chrono::steady_clock::time_point last_prefix_cache_cleanup_;

public:
    OptimizedCommandHandler(const std::string& prefix, bronx::db::Database* database = nullptr)
        : CommandHandler(prefix, database), last_prefix_cache_cleanup_(std::chrono::steady_clock::now()) {
        if (database) {
            cached_db_ = std::make_unique<bronx::cache::CachedDatabase>(database);
        }
    }

    // Set the async stat writer for non-blocking telemetry writes
    void set_async_stat_writer(bronx::perf::AsyncStatWriter* writer) {
        async_stat_writer_ = writer;
    }

    // Set the hybrid database for 3-tier caching
    void set_hybrid_database(bronx::hybrid::HybridDatabase* hdb) {
        hybrid_db_ = hdb;
    }

    // Get hybrid database for commands that need direct access
    bronx::hybrid::HybridDatabase* get_hybrid_db() { return hybrid_db_; }

    // Override to invalidate cached command state when it changes
    void notify_command_state_changed(uint64_t guild_id, const std::string& command, bool enabled) override {
        if (cached_db_) {
            cached_db_->invalidate_command_cache(guild_id, command);
        }
        // Invalidate the bulk guild toggle cache for this guild
        if (bronx::cache::global_cache) {
            bronx::cache::global_cache->invalidate_guild_toggles(guild_id);
        }
    }

    // Override to invalidate cached module state when it changes
    void notify_module_state_changed(uint64_t guild_id, const std::string& module, bool enabled) override {
        if (cached_db_) {
            cached_db_->invalidate_module_cache(guild_id, module);
        }
        if (bronx::cache::global_cache) {
            bronx::cache::global_cache->invalidate_guild_toggles(guild_id);
        }
    }

protected:
    // Override to use hybrid/cached database for whitelist check (sub-ms with LocalDB)
    bool check_global_whitelisted(uint64_t user_id) override {
        if (hybrid_db_) {
            return hybrid_db_->is_global_whitelisted(user_id);
        }
        if (cached_db_) {
            return cached_db_->is_global_whitelisted(user_id);
        }
        return CommandHandler::check_global_whitelisted(user_id);
    }

    // Override to use hybrid/cached database for blacklist check (sub-ms with LocalDB)
    bool check_global_blacklisted(uint64_t user_id) override {
        if (hybrid_db_) {
            return hybrid_db_->is_global_blacklisted(user_id);
        }
        if (cached_db_) {
            return cached_db_->is_global_blacklisted(user_id);
        }
        return CommandHandler::check_global_blacklisted(user_id);
    }

public:
    // Optimized prefix resolution with caching
    std::vector<std::string> get_all_prefixes(uint64_t user_id, uint64_t guild_id) {
        if (!cached_db_) {
            return {prefix};
        }
        
        // Check cache first (with periodic cleanup)
        {
            std::shared_lock lock(prefix_cache_mutex_);
            auto now = std::chrono::steady_clock::now();
            
            // Cleanup cache every 5 minutes
            if (now - last_prefix_cache_cleanup_ > std::chrono::minutes(5)) {
                lock.unlock();
                {
                    std::unique_lock exclusive_lock(prefix_cache_mutex_);
                    cached_all_prefixes_.clear();
                    last_prefix_cache_cleanup_ = now;
                }  // exclusive_lock released here before re-acquiring shared lock
                lock = std::shared_lock(prefix_cache_mutex_);
            }
            
            uint64_t cache_key = (guild_id << 32) | user_id;  // combine guild and user IDs
            auto it = cached_all_prefixes_.find(cache_key);
            if (it != cached_all_prefixes_.end()) {
                return it->second;
            }
        }
        
        // Cache miss - build prefix list using hybrid_db (fastest) or cached_db
        std::vector<std::string> all_prefixes;
        all_prefixes.push_back(prefix);
        
        if (guild_id != 0) {
            auto guild_prefixes = hybrid_db_ ? hybrid_db_->get_guild_prefixes(guild_id) 
                                             : cached_db_->get_guild_prefixes(guild_id);
            all_prefixes.insert(all_prefixes.end(), guild_prefixes.begin(), guild_prefixes.end());
        }
        
        auto user_prefixes = hybrid_db_ ? hybrid_db_->get_user_prefixes(user_id)
                                        : cached_db_->get_user_prefixes(user_id);
        all_prefixes.insert(all_prefixes.end(), user_prefixes.begin(), user_prefixes.end());
        
        // Cache the result
        {
            std::unique_lock lock(prefix_cache_mutex_);
            uint64_t cache_key = (guild_id << 32) | user_id;
            cached_all_prefixes_[cache_key] = all_prefixes;
        }
        
        return all_prefixes;
    }

    void handle_message(dpp::cluster& bot, const dpp::message_create_t& event) {
        // Ignore bots
        if (event.msg.author.is_bot()) return;

        auto _t0 = std::chrono::steady_clock::now();
        auto _elapsed = [&_t0]() -> double {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _t0).count();
        };

        uint64_t user_id = event.msg.author.id;
        uint64_t guild_id = event.msg.guild_id;
        
        // BAC: global ban check (use hybrid DB if available, else cached DB)
        if (hybrid_db_) {
            if (!hybrid_db_->is_global_whitelisted(user_id) && 
                hybrid_db_->is_global_blacklisted(user_id)) {
                return;
            }
        } else if (cached_db_) {
            // Check whitelist first (if whitelisted, skip ban check)
            if (!cached_db_->is_global_whitelisted(user_id) && 
                cached_db_->is_global_blacklisted(user_id)) {
                // User is banned by BAC, ignore silently
                return;
            }
        }
        
        // Privacy opt-out check — completely ignore opted-out users
        // Exception: allow the privacy command itself so they can opt back in
        if (db_) {
            if (db_->is_opted_out(user_id)) {
                // peek at the command to see if it's the privacy command
                std::string content_check = event.msg.content;
                std::string content_check_lower = content_check;
                std::transform(content_check_lower.begin(), content_check_lower.end(), content_check_lower.begin(), ::tolower);
                bool is_privacy_cmd = false;
                auto pfxs = get_all_prefixes(user_id, guild_id);
                for (auto& p : pfxs) {
                    std::string pl = p;
                    std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
                    if (content_check_lower.rfind(pl, 0) == 0) {
                        std::string after = content_check_lower.substr(pl.size());
                        while (!after.empty() && after[0] == ' ') after.erase(after.begin());
                        if (after.rfind("privacy", 0) == 0 || after.rfind("optin", 0) == 0 || after.rfind("opt-in", 0) == 0) {
                            is_privacy_cmd = true;
                            break;
                        }
                    }
                }
                if (!is_privacy_cmd) {
                    auto embed = bronx::create_embed(
                        "\xf0\x9f\x94\x92 **you have opted out of data collection**\n\n"
                        "the bot will not process your commands or collect any data.\n"
                        "to opt back in, use `b.privacy optin`",
                        bronx::COLOR_INFO);
                    bronx::send_message(bot, event, embed);
                    return;
                }
            }
        }
        double _t_ban = _elapsed();

        // Prepare mutable content
        std::string content = event.msg.content;

        // OPTIMIZED: Get all prefixes in one cached call
        auto prefixes = get_all_prefixes(user_id, guild_id);
        double _t_prefix = _elapsed();

        // Find longest matching prefix (case-insensitive)
        std::string matched_prefix;
        std::string content_lower = content;
        std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
        
        for (auto &p : prefixes) {
            if (p.empty()) continue;
            std::string p_lower = p;
            std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), ::tolower);
            if (content_lower.rfind(p_lower, 0) == 0) {
                if (p_lower.size() > matched_prefix.size()) {
                    matched_prefix = p_lower;
                }
            }
        }
        
        if (matched_prefix.empty()) {
            return; // No prefix matched
        }
        
        // Remove the matched prefix from content
        content = content.substr(matched_prefix.size());

        // BAC (Bronx AntiCheat) — timing analysis & graduated strikes
        if (bac_check(user_id, bot, &event, nullptr)) {
            return;
        }
        double _t_bac = _elapsed();

        // Extract command and args
        auto args = split_args(content);
        
        if (args.empty()) return;

        std::string cmd_name = args[0];
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
        
        args.erase(args.begin());

        // Find command
        auto it = commands.find(cmd_name);
        if (it == commands.end() || !it->second->text_handler) {
            return;
        }
        
        // OPTIMIZED: Check per-guild module/command toggles with caching
        if (guild_id != 0) {
            uint64_t cid = event.msg.channel_id;
            std::vector<uint64_t> roles;
            
            // Safely access member roles
            // In large servers or with slow network, member data may not be populated
            // Permission checks will fall through to guild defaults if roles unavailable
            try {
                if (event.msg.member.user_id != 0) {
                    auto member_roles = event.msg.member.get_roles();
                    for (auto r : member_roles) {
                        roles.push_back(r);
                    }
                }
            } catch (...) {
                // If role access fails, continue without roles (will use guild defaults)
            }
            
            // Use hybrid DB (fastest) or cached DB for toggle checks
            if (hybrid_db_) {
                if (!hybrid_db_->is_guild_module_enabled(guild_id, it->second->category, user_id, cid, roles) ||
                    !hybrid_db_->is_guild_command_enabled(guild_id, it->second->name, user_id, cid, roles)) {
                    return;
                }
            } else if (cached_db_) {
                if (!cached_db_->is_guild_module_enabled(guild_id, it->second->category, user_id, cid, roles) ||
                    !cached_db_->is_guild_command_enabled(guild_id, it->second->name, user_id, cid, roles)) {
                    return;
                }
            }
        }
        double _t_toggle = _elapsed();
        
        try {
            // Record command usage
            commands::global_stats.record_command(it->second->name);
            
            // PERFORMANCE: Log command and increment stats asynchronously
            // This avoids 3 blocking DB round-trips per command on the gateway thread
            if (async_stat_writer_) {
                async_stat_writer_->enqueue_log_command(event.msg.author.id, it->second->name);
                async_stat_writer_->enqueue_increment_stat(event.msg.author.id, "commands_used", 1);
                // Per-guild command tracking for dashboard stats
                if (guild_id != 0) {
                    async_stat_writer_->enqueue_command_usage(
                        guild_id, it->second->name, event.msg.channel_id);
                }
            } else if (cached_db_) {
                // Fallback to synchronous if no async writer
                bronx::db::history_operations::log_command(cached_db_->get_raw_db(), event.msg.author.id, it->second->name);
                cached_db_->increment_stat(event.msg.author.id, "commands_used", 1);
            }
            double _t_log = _elapsed();

            // --- DEBUG TIMING: print pre-handler breakdown ---
            std::cerr << "\033[2m[cmd-timing] b." << it->second->name
                      << "  ban=" << _t_ban << "ms"
                      << "  prefix=" << (_t_prefix - _t_ban) << "ms"
                      << "  bac=" << (_t_bac - _t_prefix) << "ms"
                      << "  toggles=" << (_t_toggle - _t_bac) << "ms"
                      << "  log=" << (_t_log - _t_toggle) << "ms"
                      << "  pre-total=" << _t_log << "ms\033[0m\n";
            
            it->second->text_handler(bot, event, args);

            double _t_handler = _elapsed();
            std::cerr << "\033[2m[cmd-timing] b." << it->second->name
                      << "  handler=" << (_t_handler - _t_log) << "ms"
                      << "  TOTAL=" << _t_handler << "ms\033[0m\n";
        } catch (const std::exception& e) {
            // Record error
            commands::global_stats.record_error(std::string("command_error: ") + it->second->name);
            
            std::cerr << "\033[1;31m\u2718 Command handler exception for " << it->second->name << ": " << e.what() << "\033[0m" << std::endl;
            
            try {
                dpp::embed error_embed = dpp::embed()
                    .set_description(bronx::EMOJI_DENY + " an error occurred while executing this command")
                    .set_color(0xE5989B);
                    
                dpp::message error_msg;
                error_msg.add_embed(error_embed);
                std::string cmd_name = it->second->name;
                bot.message_create(error_msg.set_channel_id(event.msg.channel_id),
                    [ch = event.msg.channel_id, gid = event.msg.guild_id, cmd_name](const dpp::confirmation_callback_t& cb) {
                        if (cb.is_error()) {
                            std::cerr << "\033[31m[command_error]\033[0m failed to send error for '" << cmd_name
                                      << "' in channel " << ch << " (guild " << gid
                                      << "): " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                        }
                    });
            } catch (...) {
                // If we can't send error message, just log it
                std::cerr << "\033[31mFailed to send error message for command " << it->second->name << "\033[0m" << std::endl;
            }
        }
    }

    void handle_slash_command(dpp::cluster& bot, const dpp::slashcommand_t& event) {
        auto _t0 = std::chrono::steady_clock::now();
        auto _elapsed = [&_t0]() -> double {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _t0).count();
        };

        uint64_t user_id = event.command.get_issuing_user().id;
        uint64_t guild_id = event.command.guild_id;
        
        // BAC CHECK FIRST (before any command processing)
        if (hybrid_db_) {
            if (!hybrid_db_->is_global_whitelisted(user_id) && 
                hybrid_db_->is_global_blacklisted(user_id)) {
                event.reply(dpp::message("You are banned from using this bot by BAC (Bronx AntiCheat).").set_flags(dpp::m_ephemeral));
                return;
            }
        } else if (cached_db_) {
            if (!cached_db_->is_global_whitelisted(user_id) && 
                cached_db_->is_global_blacklisted(user_id)) {
                event.reply(dpp::message("You are banned from using this bot by BAC (Bronx AntiCheat).").set_flags(dpp::m_ephemeral));
                return;
            }
        }
        
        // Privacy opt-out check — allow only /privacy command
        if (db_) {
            if (db_->is_opted_out(user_id)) {
                std::string peek_cmd = event.command.get_command_name();
                if (peek_cmd != "privacy") {
                    auto embed = bronx::create_embed(
                        "\xf0\x9f\x94\x92 **you have opted out of data collection**\n\n"
                        "the bot will not process your commands or collect any data.\n"
                        "to opt back in, use `/privacy optin`",
                        bronx::COLOR_INFO);
                    event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
                    return;
                }
            }
        }
        double _t_ban = _elapsed();
        
        std::string cmd_name = event.command.get_command_name();
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
        
        std::cout << "\033[36m/\033[0m " << cmd_name << " \033[2mfrom user " << user_id << "\033[0m\n";
        
        auto it = commands.find(cmd_name);
        if (it == commands.end()) {
            std::cerr << "\033[33m\u26a0 [SLASH]\033[0m Command not found: /" << cmd_name << "\n";
            event.reply(dpp::message("Unknown command.").set_flags(dpp::m_ephemeral));
            return;
        }
        if (!it->second->slash_handler) {
            std::cerr << "\033[33m\u26a0 [SLASH]\033[0m Command has no slash handler: /" << cmd_name << "\n";
            event.reply(dpp::message("This command doesn't support slash commands.").set_flags(dpp::m_ephemeral));
            return;
        }
        
        // BAC (Bronx AntiCheat) — timing analysis & graduated strikes
        if (bac_check(user_id, bot, nullptr, &event)) {
            return;
        }
        double _t_bac = _elapsed();
        
        // Guild-specific module/command checks
        if (guild_id != 0) {
            uint64_t channel_id = event.command.channel_id;
            std::vector<uint64_t> roles; // TODO: Extract roles from slash command event
            
            if (hybrid_db_) {
                if (!hybrid_db_->is_guild_module_enabled(guild_id, it->second->category, user_id, channel_id, roles) ||
                    !hybrid_db_->is_guild_command_enabled(guild_id, it->second->name, user_id, channel_id, roles)) {
                    event.reply(dpp::message("This command is disabled in this server.").set_flags(dpp::m_ephemeral));
                    return;
                }
            } else if (cached_db_) {
                if (!cached_db_->is_guild_module_enabled(guild_id, it->second->category, user_id, channel_id, roles) ||
                    !cached_db_->is_guild_command_enabled(guild_id, it->second->name, user_id, channel_id, roles)) {
                    event.reply(dpp::message("This command is disabled in this server.").set_flags(dpp::m_ephemeral));
                    return;
                }
            }
        }
        double _t_toggle = _elapsed();
        
        try {
            commands::global_stats.record_command(it->second->name);
            
            // PERFORMANCE: Log command and increment stats asynchronously
            if (async_stat_writer_) {
                async_stat_writer_->enqueue_log_command(event.command.get_issuing_user().id, it->second->name);
                async_stat_writer_->enqueue_increment_stat(event.command.get_issuing_user().id, "commands_used", 1);
                // Per-guild command tracking for dashboard stats
                if (guild_id != 0) {
                    async_stat_writer_->enqueue_command_usage(
                        guild_id, it->second->name, event.command.channel_id);
                }
            } else if (cached_db_) {
                bronx::db::history_operations::log_command(cached_db_->get_raw_db(), event.command.get_issuing_user().id, it->second->name);
                cached_db_->increment_stat(event.command.get_issuing_user().id, "commands_used", 1);
            }
            double _t_log = _elapsed();

            // --- DEBUG TIMING: print pre-handler breakdown ---
            std::cerr << "\033[2m[cmd-timing] /" << cmd_name
                      << "  ban=" << _t_ban << "ms"
                      << "  bac=" << (_t_bac - _t_ban) << "ms"
                      << "  toggles=" << (_t_toggle - _t_bac) << "ms"
                      << "  log=" << (_t_log - _t_toggle) << "ms"
                      << "  pre-total=" << _t_log << "ms\033[0m\n";
            
            it->second->slash_handler(bot, event);

            double _t_handler = _elapsed();
            std::cerr << "\033[2m[cmd-timing] /" << cmd_name
                      << "  handler=" << (_t_handler - _t_log) << "ms"
                      << "  TOTAL=" << _t_handler << "ms\033[0m\n";
        } catch (const std::exception& e) {
            commands::global_stats.record_error(std::string("slash_command_error: ") + it->second->name);
            std::cerr << "\033[1;31m\u2718 Slash command exception for " << it->second->name << ": " << e.what() << "\033[0m" << std::endl;
            
            try {
                event.reply(dpp::message("An error occurred while executing this command.").set_flags(dpp::m_ephemeral));
            } catch (...) {
                std::cerr << "\033[31mFailed to send slash command error response\033[0m" << std::endl;
            }
        }
    }

    // Performance monitoring
    struct PerformanceStats {
        size_t cached_prefix_entries;
        size_t total_cache_entries;
        double cache_hit_ratio;
        std::chrono::milliseconds avg_command_time;
    };
    
    PerformanceStats get_performance_stats() const {
        PerformanceStats stats{};
        
        if (hybrid_db_) {
            auto hstats = const_cast<bronx::hybrid::HybridDatabase*>(hybrid_db_)->get_hybrid_stats();
            stats.total_cache_entries = hstats.memory_cache_entries + 
                hstats.local_user_entries + hstats.local_inventory_entries + 
                hstats.local_stat_entries + hstats.local_shop_entries;
            stats.cache_hit_ratio = stats.total_cache_entries > 100 ? 0.90 : 0.0;
        } else if (cached_db_) {
            auto cache_stats = cached_db_->get_cache_stats();
            stats.total_cache_entries = cache_stats.total_entries;
            // Estimate cache hit ratio based on total entries vs database calls
            // This is approximate - in production you'd want proper metrics
            stats.cache_hit_ratio = cache_stats.total_entries > 1000 ? 0.85 : 0.0;
        }
        
        {
            std::shared_lock lock(prefix_cache_mutex_);
            stats.cached_prefix_entries = cached_all_prefixes_.size();
        }
        
        return stats;
    }
    
    // Cache management methods
    void invalidate_user_cache(uint64_t user_id) {
        if (hybrid_db_) {
            hybrid_db_->invalidate_user(user_id);
        } else if (cached_db_) {
            cached_db_->invalidate_user_cache(user_id);
        }
        
        // Clear prefix cache entries for this user
        std::unique_lock lock(prefix_cache_mutex_);
        for (auto it = cached_all_prefixes_.begin(); it != cached_all_prefixes_.end();) {
            uint64_t cached_user_id = it->first & 0xFFFFFFFF;
            if (cached_user_id == user_id) {
                it = cached_all_prefixes_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void invalidate_guild_cache(uint64_t guild_id) {
        if (hybrid_db_) {
            hybrid_db_->invalidate_guild(guild_id);
        } else if (cached_db_) {
            cached_db_->invalidate_guild_cache(guild_id);
        }
        
        // Clear prefix cache entries for this guild
        std::unique_lock lock(prefix_cache_mutex_);
        for (auto it = cached_all_prefixes_.begin(); it != cached_all_prefixes_.end();) {
            uint64_t cached_guild_id = it->first >> 32;
            if (cached_guild_id == guild_id) {
                it = cached_all_prefixes_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void periodic_maintenance() {
        if (hybrid_db_) {
            hybrid_db_->periodic_cleanup();
        } else if (cached_db_) {
            cached_db_->periodic_cleanup();
        }
        
        // Clean up prefix cache
        std::unique_lock lock(prefix_cache_mutex_);
        auto now = std::chrono::steady_clock::now();
        if (now - last_prefix_cache_cleanup_ > std::chrono::minutes(5)) {
            cached_all_prefixes_.clear();
            last_prefix_cache_cleanup_ = now;
        }
    }

    // Bulk-refresh all dashboard-editable settings from the remote DB.
    // Call this on a separate timer (e.g. every 60s).
    void refresh_settings() {
        if (hybrid_db_) {
            hybrid_db_->refresh_all_settings();
            std::unique_lock lock(prefix_cache_mutex_);
            cached_all_prefixes_.clear();
        } else if (cached_db_) {
            cached_db_->refresh_all_settings();
            std::unique_lock lock(prefix_cache_mutex_);
            cached_all_prefixes_.clear();
        }
    }

    // Existing methods adapted...
    const std::string& get_prefix() const { return prefix; }
    
    std::vector<Command*> get_slash_commands() {
        std::vector<Command*> slash_cmds;
        std::set<Command*> seen;
        for (auto& [name, cmd] : commands) {
            if (cmd->is_slash_command && seen.find(cmd) == seen.end()) {
                slash_cmds.push_back(cmd);
                seen.insert(cmd);
            }
        }
        return slash_cmds;
    }
};

// Helper to migrate from old CommandHandler to OptimizedCommandHandler
class CommandHandlerMigration {
public:
    static std::unique_ptr<OptimizedCommandHandler> migrate_from_legacy(
        const std::string& prefix, bronx::db::Database* database) {
        return std::make_unique<OptimizedCommandHandler>(prefix, database);
    }
};