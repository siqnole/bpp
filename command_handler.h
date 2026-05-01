#pragma once
#include <dpp/dpp.h>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <mutex>
#include "command.h"
#include "embed_style.h"
#include "performance/async_stat_writer.h"
#include "database/operations/economy/history_operations.h"
#include "database/operations/economy/server_economy_operations.h"
#include "database/operations/user/privacy_operations.h"
#include "commands/daily_challenges/daily_stat_tracker.h"
#include "tui_logger.h"
// forward declare owner helper to avoid circular dependency
namespace commands { bool is_owner(uint64_t user_id); }

// global stats tracking used by CommandHandler and owner commands
namespace commands {
    struct BotStats {
        ::std::map<::std::string, uint64_t> command_usage;
        ::std::map<::std::string, uint64_t> error_counts;
        ::std::vector<double> ping_history;
        uint64_t total_commands = 0;
        uint64_t total_errors = 0;
        ::std::chrono::system_clock::time_point start_time;
        BotStats() : start_time(::std::chrono::system_clock::now()) {}
        void record_command(const ::std::string& cmd) {
            command_usage[cmd]++;
            total_commands++;
        }
        void record_error(const ::std::string& error_type) {
            error_counts[error_type]++;
            total_errors++;
        }
        void record_ping(double ping_ms) {
            ping_history.push_back(ping_ms);
            if (ping_history.size() > 10000) {
                ping_history.erase(ping_history.begin());
            }
        }
        double get_average_ping() const {
            if (ping_history.empty()) return 0.0;
            double sum = 0;
            for (double ping : ping_history) sum += ping;
            return sum / ping_history.size();
        }
    };
    extern BotStats global_stats;
}

#include "database/core/database.h"

// ============================================================================
// BAC — Bronx AntiCheat
// Detects macro / automation abuse by analysing command timing regularity.
// Strike system:  captcha → 5-min timeout → 10-min timeout → permanent ban
// ============================================================================
struct BACRecord {
    std::vector<std::chrono::steady_clock::time_point> timestamps;  // recent command times
    int strike = 0;                          // 0 = clean, 1-3 = escalating punishment
    bool captcha_pending = false;            // waiting for captcha answer
    std::string captcha_answer;              // expected answer
    std::string captcha_custom_id;           // button/modal custom id
    std::chrono::steady_clock::time_point captcha_sent_at;
    std::chrono::steady_clock::time_point timeout_until;  // command timeout expiry
    int consecutive_captcha_fails = 0;       // failed / ignored captchas in a row
};

class CommandHandler {
protected:
    std::map<std::string, Command*> commands;
    std::string prefix;
    bronx::db::Database* db_ = nullptr;                    // optional database pointer for global lists and cooldowns

    // BAC (Bronx AntiCheat) state per user
    std::unordered_map<uint64_t, BACRecord> bac_records_;
    std::mutex bac_mutex_;
    std::chrono::steady_clock::time_point last_bac_prune_ = std::chrono::steady_clock::now();
    static constexpr size_t BAC_MAX_RECORDS = 10000;  // hard cap on tracked users
    static constexpr int BAC_PRUNE_INTERVAL_MINUTES = 30;  // prune every 30 min

    // BAC tunables
    static constexpr int    BAC_HISTORY_SIZE          = 12;       // number of recent command timestamps to keep
    static constexpr int    BAC_MIN_SAMPLES           = 8;        // minimum commands before analysing intervals
    static constexpr double BAC_CV_THRESHOLD          = 0.08;     // coefficient-of-variation below this = suspiciously regular
    static constexpr int    BAC_SPAM_THRESHOLD        = 5;        // raw spam: commands within window before instant action
    static inline const std::chrono::seconds BAC_SPAM_WINDOW{10};
    static inline const std::chrono::minutes BAC_TIMEOUT_STRIKE1{5};
    static inline const std::chrono::minutes BAC_TIMEOUT_STRIKE2{10};
    static inline const std::chrono::seconds BAC_CAPTCHA_EXPIRY{60};  // captcha must be answered within 60s

public:
    CommandHandler(const std::string& prefix, bronx::db::Database* database = nullptr)
        : prefix(prefix), db_(database) {}
    
    virtual ~CommandHandler() = default;
    
    // Called when a guild command's enabled state changes; override in subclasses to invalidate caches
    virtual void notify_command_state_changed(uint64_t guild_id, const std::string& command, bool enabled) {
        // Default: no-op
        (void)guild_id; (void)command; (void)enabled;
    }

    // Called when a guild module's enabled state changes; override in subclasses to invalidate caches
    virtual void notify_module_state_changed(uint64_t guild_id, const std::string& module, bool enabled) {
        // Default: no-op
        (void)guild_id; (void)module; (void)enabled;
    }

    void register_command(Command* cmd) {
        commands[cmd->name] = cmd;
        for (const auto& alias : cmd->aliases) {
            commands[alias] = cmd;
        }
    }

    std::vector<std::string> split_args(const std::string& str) {
        std::vector<std::string> args;
        std::string current;
        bool in_quotes = false;

        for (char c : str) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ' ' && !in_quotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }

        if (!current.empty()) {
            args.push_back(current);
        }

        return args;
    }

    // BAC: handle a captcha button click (called from your component handler)
    void bac_handle_component(dpp::cluster& bot, const dpp::button_click_t& event) {
        uint64_t user_id = event.command.get_issuing_user().id;
        std::lock_guard<std::mutex> lock(bac_mutex_);
        auto it = bac_records_.find(user_id);
        if (it == bac_records_.end() || !it->second.captcha_pending) return;
        auto& rec = it->second;
        if (event.custom_id != rec.captcha_custom_id) return;
        // they clicked the correct button — captcha passed
        rec.captcha_pending = false;
        rec.consecutive_captcha_fails = 0;
        rec.timestamps.clear();
        event.reply(dpp::message()
            .add_embed(bronx::create_embed(bronx::EMOJI_CHECK + " **bac** — captcha passed! you're clear.", bronx::COLOR_SUCCESS))
            .set_flags(dpp::m_ephemeral));
        bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: user " + std::to_string(user_id) + " passed captcha");
    }

    void handle_message(dpp::cluster& bot, const dpp::message_create_t& event) {
        // Ignore bots
        if (event.msg.author.is_bot()) return;

        // BAC: global ban check
        uint64_t user_id = event.msg.author.id;
        if (db_) {
            if (!db_->is_global_whitelisted(user_id) && db_->is_global_blacklisted(user_id)) {
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: ignoring message from banned user " + std::to_string(user_id));
                return;
            }
            // Privacy opt-out check — completely ignore opted-out users
            // Exception: allow the privacy command itself so they can opt back in
            if (db_->is_opted_out(user_id)) {
                // peek at the command to see if it's the privacy command
                std::string content = event.msg.content;
                std::string content_lower = content;
                std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
                bool is_privacy_cmd = false;
                // check all possible prefixes
                std::vector<std::string> pfxs = {prefix};
                auto gps = db_->get_guild_prefixes(event.msg.guild_id);
                pfxs.insert(pfxs.end(), gps.begin(), gps.end());
                auto ups = db_->get_user_prefixes(user_id);
                pfxs.insert(pfxs.end(), ups.begin(), ups.end());
                for (auto& p : pfxs) {
                    std::string pl = p;
                    std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
                    if (content_lower.rfind(pl, 0) == 0) {
                        std::string after = content_lower.substr(pl.size());
                        // trim leading space
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

        // Prepare mutable content
        std::string content = event.msg.content;

        // Build list of prefixes to test (default + guild + user)
        std::vector<std::string> prefixes;
        prefixes.push_back(prefix);
        if (db_) {
            if (event.msg.guild_id != 0) {
                auto gps = db_->get_guild_prefixes(event.msg.guild_id);
                prefixes.insert(prefixes.end(), gps.begin(), gps.end());
            }
            auto ups = db_->get_user_prefixes(user_id);
            prefixes.insert(prefixes.end(), ups.begin(), ups.end());
        }

        // find longest matching prefix (case-insensitive)
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
            return; // no prefix matched
        }
        // remove the matched prefix from content (original-case length)
        content = content.substr(matched_prefix.size());

        // BAC (Bronx AntiCheat) — timing analysis & graduated strikes
        if (bac_check(user_id, bot, &event, nullptr)) {
            return; // user is timed-out, captcha-pending, or was just banned
        }

        // Extract command and args
        auto args = split_args(content);
        
        if (args.empty()) return;

        std::string cmd_name = args[0];
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
        
        args.erase(args.begin()); // Remove command name from args

        // Find and execute command
        auto it = commands.find(cmd_name);
        if (it != commands.end() && it->second->text_handler) {
            // check per-guild module/command toggles (defaults to enabled)
            if (db_ && event.msg.guild_id != 0) {
                uint64_t gid = event.msg.guild_id;
                uint64_t uid = event.msg.author.id;
                uint64_t cid = event.msg.channel_id;
                std::vector<uint64_t> roles;
                // Safely access member roles
                try {
                    if (event.msg.member.user_id != 0 && event.msg.member.user_id == uid) {
                        auto member_roles = event.msg.member.get_roles();
                        for (auto r : member_roles) {
                            roles.push_back(r);
                        }
                    }
                } catch (...) {
                    // If role access fails, continue without roles
                }
                if (!db_->is_guild_module_enabled(gid, it->second->category, uid, cid, roles) ||
                    !db_->is_guild_command_enabled(gid, it->second->name, uid, cid, roles)) {
                    bronx::send_message(bot, event,
                        bronx::error("`" + it->second->name + "` is disabled in this channel\nuse `b.commands` to see what's enabled"));
                    return;
                }
            }
            try {
                // Record command usage
                commands::global_stats.record_command(it->second->name);
                
                // Log command to history (for owner auditing)
                if (db_) {
                    bronx::db::history_operations::log_command(db_, event.msg.author.id, it->second->name);
                    db_->increment_stat(event.msg.author.id, "commands_used", 1);
                    ::commands::daily_challenges::track_daily_stat(db_, event.msg.author.id, "commands_today", 1);
                    // Update user_activity_daily for top-10 / leaderboard queries
                    if (event.msg.guild_id != 0) {
                        bronx::db::stats_operations::increment_user_daily_commands(db_, event.msg.guild_id, event.msg.author.id);
                    }
                    // Per-guild command tracking for dashboard
                    if (event.msg.guild_id != 0) {
                        bronx::db::server_economy_operations::log_server_command(db_, event.msg.guild_id, event.msg.author.id, it->second->name);
                        // Stats: per-command per-channel usage tracking
                        if (bronx::perf::g_stat_writer) {
                            bronx::perf::g_stat_writer->enqueue_command_usage(
                                event.msg.guild_id, it->second->name, event.msg.channel_id);
                        }
                    }
                }
                
                it->second->text_handler(bot, event, args);
            } catch (const std::exception& e) {
                // record error
                commands::global_stats.record_error(std::string("command_error: ") + it->second->name);
                
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "command handler exception for " + it->second->name + ": " + e.what());
                
                try {
                    dpp::embed error_embed = dpp::embed()
                        .set_description(bronx::EMOJI_DENY + " an error occurred while executing this command")
                        .set_color(0xE5989B)
                        .set_timestamp(time(0));
                    
                    bronx::send_message(bot, event, error_embed);
                } catch (...) {
                    bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "failed to send error message");
                }
            } catch (...) {
                // record unknown error
                commands::global_stats.record_error(std::string("unknown_error: ") + it->second->name);
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "unknown exception in command handler for " + it->second->name);
            }
        }
    }

    void handle_slash_command(dpp::cluster& bot, const dpp::slashcommand_t& event) {
        uint64_t user_id = event.command.get_issuing_user().id;
        if (db_) {
            if (!db_->is_global_whitelisted(user_id) && db_->is_global_blacklisted(user_id)) {
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: ignoring slash command from banned user " + std::to_string(user_id));
                return;
            }
            // Privacy opt-out check — allow only /privacy command
            if (db_->is_opted_out(user_id)) {
                std::string cmd_name = event.command.get_command_name();
                if (cmd_name != "privacy") {
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
        // BAC (Bronx AntiCheat) — timing analysis & graduated strikes
        if (bac_check(user_id, bot, nullptr, &event)) {
            return;
        }

        std::string cmd_name = event.command.get_command_name();
        
        auto it = commands.find(cmd_name);
        if (it != commands.end() && it->second->slash_handler) {
            // guild-level toggle check
            if (db_ && event.command.guild_id != 0) {
                uint64_t gid = event.command.guild_id;
                uint64_t uid = event.command.get_issuing_user().id;
                uint64_t cid = event.command.channel_id;
                std::vector<uint64_t> roles;
                // Safely access member roles
                try {
                    if (event.command.member.user_id != 0 && event.command.member.user_id == uid) {
                        auto member_roles = event.command.member.get_roles();
                        for (auto r : member_roles) {
                            roles.push_back(r);
                        }
                    }
                } catch (...) {
                    // If role access fails, continue without roles
                }
                if (!db_->is_guild_module_enabled(gid, it->second->category, uid, cid, roles) ||
                    !db_->is_guild_command_enabled(gid, it->second->name, uid, cid, roles)) {
                    event.reply(dpp::message()
                        .add_embed(bronx::error("`" + it->second->name + "` is disabled in this channel\nuse `b.commands` to see what's enabled"))
                        .set_flags(dpp::m_ephemeral));
                    return;
                }
            }
            try {
                // Record command usage
                commands::global_stats.record_command(it->second->name);
                
                // Log command to history (for owner auditing)
                if (db_) {
                    bronx::db::history_operations::log_command(db_, event.command.get_issuing_user().id, it->second->name);
                    db_->increment_stat(event.command.get_issuing_user().id, "commands_used", 1);
                    ::commands::daily_challenges::track_daily_stat(db_, event.command.get_issuing_user().id, "commands_today", 1);
                    // Update user_activity_daily for top-10 / leaderboard queries
                    if (event.command.guild_id != 0) {
                        bronx::db::stats_operations::increment_user_daily_commands(db_, event.command.guild_id, event.command.get_issuing_user().id);
                    }
                    // Per-guild command tracking for dashboard
                    if (event.command.guild_id != 0) {
                        bronx::db::server_economy_operations::log_server_command(db_, event.command.guild_id, event.command.get_issuing_user().id, it->second->name);
                        // Stats: per-command per-channel usage tracking
                        if (bronx::perf::g_stat_writer) {
                            bronx::perf::g_stat_writer->enqueue_command_usage(
                                event.command.guild_id, it->second->name, event.command.channel_id);
                        }
                    }
                }
                
                it->second->slash_handler(bot, event);
            } catch (const std::exception& e) {
                // record error
                commands::global_stats.record_error(std::string("slash_error: ") + it->second->name);
                
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "slash command handler exception for " + it->second->name + ": " + e.what());
                
                try {
                    dpp::embed error_embed = dpp::embed()
                        .set_description(bronx::EMOJI_DENY + " an error occurred while executing this command")
                        .set_color(0xE5989B)
                        .set_timestamp(time(0));
                    
                    bronx::safe_slash_reply(bot, event, error_embed);
                } catch (...) {
                    bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "failed to send slash command error reply");
                }
            } catch (...) {
                // record unknown error
                commands::global_stats.record_error(std::string("unknown_slash_error: ") + it->second->name);
                bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::ERROR, "unknown exception in slash command handler for " + it->second->name);
            }
        }
    }

    std::vector<Command*> get_slash_commands() {
        std::vector<Command*> slash_cmds;
        std::set<Command*> unique_commands;

        for (const auto& [name, cmd] : commands) {
            if (cmd->is_slash_command && unique_commands.find(cmd) == unique_commands.end()) {
                unique_commands.insert(cmd);
                slash_cmds.push_back(cmd);
            }
        }

        return slash_cmds;
    }

    std::map<std::string, std::vector<Command*>> get_commands_by_category() {
        std::map<std::string, std::vector<Command*>> categorized;
        std::set<Command*> unique_commands;

        for (const auto& [name, cmd] : commands) {
            if (unique_commands.find(cmd) == unique_commands.end()) {
                unique_commands.insert(cmd);
                categorized[cmd->category].push_back(cmd);
            }
        }

        return categorized;
    }

    const std::string& get_prefix() const { return prefix; }

protected:
    // Virtual methods for whitelist/blacklist checks — override in subclasses
    // to use cached database instead of raw DB
    virtual bool check_global_whitelisted(uint64_t user_id) {
        return db_ && db_->is_global_whitelisted(user_id);
    }
    
    virtual bool check_global_blacklisted(uint64_t user_id) {
        return db_ && db_->is_global_blacklisted(user_id);
    }

    // ========================================================================
    // BAC (Bronx AntiCheat) — core check run on every command invocation
    // Returns true if the command should be blocked.
    // ========================================================================
    bool bac_check(uint64_t user_id,
                   dpp::cluster& bot,
                   const dpp::message_create_t* msg_evt,
                   const dpp::slashcommand_t* slash_evt) {
        // owner & whitelisted users are always exempt
        if (commands::is_owner(user_id)) return false;
        if (check_global_whitelisted(user_id)) return false;
        if (check_global_blacklisted(user_id)) return true;

        std::lock_guard<std::mutex> lock(bac_mutex_);
        auto now = std::chrono::steady_clock::now();

        // Periodic pruning: remove stale BAC records to prevent unbounded memory growth
        auto since_prune = std::chrono::duration_cast<std::chrono::minutes>(now - last_bac_prune_);
        if (since_prune.count() >= BAC_PRUNE_INTERVAL_MINUTES || bac_records_.size() > BAC_MAX_RECORDS) {
            for (auto it = bac_records_.begin(); it != bac_records_.end();) {
                auto& rec_entry = it->second;
                // Remove records with no recent activity (no timestamps and no active strike/captcha)
                bool is_stale = rec_entry.timestamps.empty() && rec_entry.strike == 0
                                && !rec_entry.captcha_pending && now >= rec_entry.timeout_until;
                if (is_stale) {
                    it = bac_records_.erase(it);
                } else {
                    ++it;
                }
            }
            last_bac_prune_ = now;
        }

        auto& rec = bac_records_[user_id];

        // ---- If the user is currently timed out, block silently ----
        if (now < rec.timeout_until) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(rec.timeout_until - now).count();
            auto embed = bronx::create_embed(
                bronx::EMOJI_DENY + " **bac** — you are timed out for suspicious activity\n"
                "try again in **" + std::to_string(remaining) + "s**", bronx::COLOR_ERROR);
            embed.set_footer(dpp::embed_footer().set_text("bronx anticheat • strike " + std::to_string(rec.strike) + "/3"));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            return true;
        }

        // ---- If a captcha is pending, check expiry ----
        if (rec.captcha_pending) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - rec.captcha_sent_at).count();
            if (elapsed > (long)BAC_CAPTCHA_EXPIRY.count()) {
                // captcha expired — they didn't answer → treat as fail
                rec.captcha_pending = false;
                rec.consecutive_captcha_fails++;
                bac_escalate(user_id, rec, bot, msg_evt, slash_evt, "captcha expired");
                return true;
            }
            // still waiting
            auto embed = bronx::create_embed(
                bronx::EMOJI_WARNING + " **bac** — please solve the captcha sent to your dms before using commands",
                bronx::COLOR_WARNING);
            embed.set_footer(dpp::embed_footer().set_text("bronx anticheat"));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            return true;
        }

        // ---- Record timestamp ----
        rec.timestamps.push_back(now);
        // keep only the last BAC_HISTORY_SIZE entries
        if ((int)rec.timestamps.size() > BAC_HISTORY_SIZE) {
            rec.timestamps.erase(rec.timestamps.begin(),
                                 rec.timestamps.begin() + ((int)rec.timestamps.size() - BAC_HISTORY_SIZE));
        }

        // ---- Raw spam check (>5 commands in 10s) — instant escalation ----
        {
            int recent = 0;
            for (auto& ts : rec.timestamps) {
                if (now - ts <= BAC_SPAM_WINDOW) recent++;
            }
            if (recent > BAC_SPAM_THRESHOLD) {
                bac_escalate(user_id, rec, bot, msg_evt, slash_evt, "command spam");
                return true;
            }
        }

        // ---- Interval regularity analysis (macro detection) ----
        if ((int)rec.timestamps.size() >= BAC_MIN_SAMPLES) {
            // compute intervals in milliseconds
            std::vector<double> intervals;
            for (int i = 1; i < (int)rec.timestamps.size(); ++i) {
                double ms = std::chrono::duration<double, std::milli>(
                    rec.timestamps[i] - rec.timestamps[i - 1]).count();
                intervals.push_back(ms);
            }
            // compute mean
            double sum = 0;
            for (double v : intervals) sum += v;
            double mean = sum / intervals.size();
            // compute standard deviation
            double sq_sum = 0;
            for (double v : intervals) sq_sum += (v - mean) * (v - mean);
            double stddev = std::sqrt(sq_sum / intervals.size());
            // coefficient of variation
            double cv = (mean > 0) ? (stddev / mean) : 1.0;

            if (cv < BAC_CV_THRESHOLD && mean < 10000.0) {
                // suspiciously regular intervals — trigger captcha or escalate
                if (rec.strike == 0) {
                    // first offence → send captcha
                    bac_send_captcha(user_id, rec, bot, msg_evt, slash_evt);
                    return true;
                } else {
                    // already has a strike → escalate further
                    bac_escalate(user_id, rec, bot, msg_evt, slash_evt, "repeated macro-like activity");
                    return true;
                }
            }
        }

        return false; // all clear
    }

    // Generate and DM a captcha to the user
    void bac_send_captcha(uint64_t user_id, BACRecord& rec,
                          dpp::cluster& bot,
                          const dpp::message_create_t* msg_evt,
                          const dpp::slashcommand_t* slash_evt) {
        // generate a simple math captcha
        static std::mt19937 rng(std::random_device{}());
        int a = std::uniform_int_distribution<int>(10, 99)(rng);
        int b = std::uniform_int_distribution<int>(1, 50)(rng);
        int answer = a + b;

        rec.captcha_pending = true;
        rec.captcha_answer = std::to_string(answer);
        rec.captcha_custom_id = "bac_captcha_" + std::to_string(user_id) + "_" + std::to_string(answer);
        rec.captcha_sent_at = std::chrono::steady_clock::now();

        // build captcha DM
        auto captcha_embed = bronx::create_embed(
            "🛡️ **bronx anticheat (bac)**\n\n"
            "suspicious activity detected on your account.\n"
            "please solve this to prove you're human:\n\n"
            "**what is `" + std::to_string(a) + " + " + std::to_string(b) + "`?**\n\n"
            "click the button with the correct answer within **60 seconds**.",
            bronx::COLOR_WARNING);
        captcha_embed.set_footer(dpp::embed_footer().set_text("bac • strike " + std::to_string(rec.strike) + "/3"));

        // generate 3 wrong answers + 1 correct, shuffle
        std::vector<int> options;
        options.push_back(answer);
        while ((int)options.size() < 4) {
            int wrong = answer + std::uniform_int_distribution<int>(-15, 15)(rng);
            if (wrong != answer && wrong > 0 &&
                std::find(options.begin(), options.end(), wrong) == options.end()) {
                options.push_back(wrong);
            }
        }
        std::shuffle(options.begin(), options.end(), rng);

        dpp::component row;
        row.set_type(dpp::cot_action_row);
        for (int opt : options) {
            dpp::component btn;
            btn.set_type(dpp::cot_button);
            btn.set_label(std::to_string(opt));
            if (opt == answer) {
                btn.set_style(dpp::cos_primary);
                btn.set_id(rec.captcha_custom_id);
            } else {
                btn.set_style(dpp::cos_secondary);
                btn.set_id("bac_wrong_" + std::to_string(user_id) + "_" + std::to_string(opt));
            }
            row.add_component(btn);
        }

        dpp::message dm_msg;
        dm_msg.add_embed(captcha_embed);
        dm_msg.add_component(row);
        bot.direct_message_create(user_id, dm_msg);

        // notify in-channel
        auto notify = bronx::create_embed(
            bronx::EMOJI_WARNING + " **bac** — suspicious activity detected\n"
            "a captcha has been sent to your dms. solve it to continue using commands.",
            bronx::COLOR_WARNING);
        notify.set_footer(dpp::embed_footer().set_text("bronx anticheat"));
        if (msg_evt) {
            bronx::send_message(bot, *msg_evt, notify);
        } else if (slash_evt) {
            slash_evt->reply(dpp::message().add_embed(notify).set_flags(dpp::m_ephemeral));
        }
        bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: captcha sent to user " + std::to_string(user_id));
    }

    // Handle a wrong captcha button click
    void bac_handle_wrong_captcha(dpp::cluster& bot, const dpp::button_click_t& event) {
        uint64_t user_id = event.command.get_issuing_user().id;
        std::lock_guard<std::mutex> lock(bac_mutex_);
        auto it = bac_records_.find(user_id);
        if (it == bac_records_.end() || !it->second.captcha_pending) return;
        auto& rec = it->second;
        rec.captcha_pending = false;
        rec.consecutive_captcha_fails++;
        bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: user " + std::to_string(user_id) + " failed captcha (wrong answer)");
        bac_escalate(user_id, rec, bot, nullptr, nullptr, "failed captcha");
        event.reply(dpp::message()
            .add_embed(bronx::create_embed(bronx::EMOJI_DENY + " **bac** — wrong answer. strike issued.", bronx::COLOR_ERROR))
            .set_flags(dpp::m_ephemeral));
    }

public:
    // Call this from your on_button_click handler to route BAC events
    void bac_on_button_click(dpp::cluster& bot, const dpp::button_click_t& event) {
        const auto& cid = event.custom_id;
        // Handle custom who-sent button
        if (cid.rfind("who_sent_", 0) == 0) {
            std::string id_str = cid.substr(std::string("who_sent_").size());
            uint64_t uid = 0;
            try { uid = std::stoull(id_str); } catch (...) { return; }
            dpp::message reply_msg;
            reply_msg.add_embed(bronx::create_embed("Message was sent by <@" + std::to_string(uid) + ">.", bronx::COLOR_INFO));
            event.reply(reply_msg.set_flags(dpp::m_ephemeral));
            return;
        } else if (cid.rfind("bac_captcha_", 0) == 0) {
            bac_handle_component(bot, event);
        } else if (cid.rfind("bac_wrong_", 0) == 0) {
            bac_handle_wrong_captcha(bot, event);
        }
    }

private:
    // Escalate punishment: strike 1→captcha+5min, strike 2→10min, strike 3→permanent ban
    void bac_escalate(uint64_t user_id, BACRecord& rec,
                      dpp::cluster& bot,
                      const dpp::message_create_t* msg_evt,
                      const dpp::slashcommand_t* slash_evt,
                      const std::string& reason) {
        rec.strike++;
        rec.timestamps.clear();
        auto now = std::chrono::steady_clock::now();

        if (rec.strike >= 3) {
            // ---- STRIKE 3: permanent ban ----
            if (db_) {
                db_->remove_global_whitelist(user_id);  // ensure ban is not bypassed
                db_->add_global_blacklist(user_id, "(bac) " + reason + " — strike 3");
            }
            auto ban_embed = bronx::create_embed(
                "🛡️ **bronx anticheat (bac)**\n\n"
                "you have been **permanently banned** from using this bot for repeated suspicious activity.\n\n"
                "if you believe this was a mistake, please appeal in our support server.",
                bronx::COLOR_ERROR);
            ban_embed.set_title("bac — permanently banned");
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
            bot.direct_message_create(user_id, dm_msg);

            auto notify = bronx::create_embed(
                bronx::EMOJI_DENY + " **bac** — you have been permanently banned for repeated suspicious activity.\n"
                "check your dms for appeal information.",
                bronx::COLOR_ERROR);
            notify.set_footer(dpp::embed_footer().set_text("bronx anticheat • strike 3/3"));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, notify);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(notify).set_flags(dpp::m_ephemeral));
            }
            bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: user " + std::to_string(user_id) + " permanently banned — " + reason);
            bac_records_.erase(user_id);
        } else if (rec.strike == 2) {
            // ---- STRIKE 2: 10-minute timeout ----
            rec.timeout_until = now + BAC_TIMEOUT_STRIKE2;
            auto embed = bronx::create_embed(
                bronx::EMOJI_DENY + " **bac** — strike **2/3**\n"
                "you have been timed out from commands for **10 minutes** for suspicious activity.\n"
                "next offence will result in a **permanent ban**.",
                bronx::COLOR_ERROR);
            embed.set_footer(dpp::embed_footer().set_text("bronx anticheat • " + reason));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            // DM warning
            auto dm_embed = bronx::create_embed(
                "🛡️ **bronx anticheat (bac)**\n\n"
                "**strike 2/3** — you've been timed out from commands for **10 minutes**.\n"
                "reason: " + reason + "\n\n"
                + bronx::EMOJI_WARNING + " one more strike and you will be **permanently banned**.",
                bronx::COLOR_WARNING);
            bot.direct_message_create(user_id, dpp::message().add_embed(dm_embed));
            bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: user " + std::to_string(user_id) + " strike 2 (10min timeout) — " + reason);
        } else {
            // ---- STRIKE 1: 5-minute timeout + captcha on return ----
            rec.timeout_until = now + BAC_TIMEOUT_STRIKE1;
            auto embed = bronx::create_embed(
                bronx::EMOJI_WARNING + " **bac** — strike **1/3**\n"
                "you have been timed out from commands for **5 minutes** for suspicious activity.\n"
                "continued abuse will escalate to longer timeouts and eventually a ban.",
                bronx::COLOR_WARNING);
            embed.set_footer(dpp::embed_footer().set_text("bronx anticheat • " + reason));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            // dm notice
            auto dm_embed = bronx::create_embed(
                "🛡️ **bronx anticheat (bac)**\n\n"
                "**strike 1/3** — you've been timed out from commands for **5 minutes**.\n"
                "reason: " + reason + "\n\n"
                "this is your first warning. please stop using macros or automated tools.",
                bronx::COLOR_WARNING);
            bot.direct_message_create(user_id, dpp::message().add_embed(dm_embed));
            bronx::tui::TuiLogger::get().add_log(bronx::tui::LogLevel::INFO, "bac: user " + std::to_string(user_id) + " strike 1 (5min timeout) — " + reason);
        }
    }
};
