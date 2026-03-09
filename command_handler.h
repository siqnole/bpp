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
#include "database/operations/economy/history_operations.h"
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
        // They clicked the correct button — captcha passed
        rec.captcha_pending = false;
        rec.consecutive_captcha_fails = 0;
        rec.timestamps.clear();
        event.reply(dpp::message()
            .add_embed(bronx::create_embed(bronx::EMOJI_CHECK + " **BAC** — captcha passed! you're clear.", bronx::COLOR_SUCCESS))
            .set_flags(dpp::m_ephemeral));
        std::cerr << "\033[32m\u2714 BAC: user " << user_id << " passed captcha\033[0m\n";
    }

    void handle_message(dpp::cluster& bot, const dpp::message_create_t& event) {
        // Ignore bots
        if (event.msg.author.is_bot()) return;

        // BAC: global ban check
        uint64_t user_id = event.msg.author.id;
        if (db_) {
            if (!db_->is_global_whitelisted(user_id) && db_->is_global_blacklisted(user_id)) {
                std::cerr << "\033[33m\u26a0 \033[0mBAC: ignoring message from banned user " << user_id << "\n";
                return;
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
                }
                
                it->second->text_handler(bot, event, args);
            } catch (const std::exception& e) {
                // Record error
                commands::global_stats.record_error(std::string("command_error: ") + it->second->name);
                
                std::cerr << "\033[1;31m\u2718 Command handler exception for " << it->second->name << ": " << e.what() << "\033[0m" << std::endl;
                
                try {
                    dpp::embed error_embed = dpp::embed()
                        .set_description("✗ an error occurred while executing this command")
                        .set_color(0xE5989B)
                        .set_timestamp(time(0));
                    
                    bronx::send_message(bot, event, error_embed);
                } catch (...) {
                    std::cerr << "\033[31mFailed to send error message\033[0m" << std::endl;
                }
            } catch (...) {
                // Record unknown error
                commands::global_stats.record_error(std::string("unknown_error: ") + it->second->name);
                std::cerr << "\033[1;31m\u2718 Unknown exception in command handler for " << it->second->name << "\033[0m" << std::endl;
            }
        }
    }

    void handle_slash_command(dpp::cluster& bot, const dpp::slashcommand_t& event) {
        uint64_t user_id = event.command.get_issuing_user().id;
        if (db_) {
            if (!db_->is_global_whitelisted(user_id) && db_->is_global_blacklisted(user_id)) {
                std::cerr << "\033[33m\u26a0 \033[0mBAC: ignoring slash command from banned user " << user_id << "\n";
                return;
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
                }
                
                it->second->slash_handler(bot, event);
            } catch (const std::exception& e) {
                // Record error
                commands::global_stats.record_error(std::string("slash_error: ") + it->second->name);
                
                std::cerr << "\033[1;31m\u2718 Slash command handler exception for " << it->second->name << ": " << e.what() << "\033[0m" << std::endl;
                
                try {
                    dpp::embed error_embed = dpp::embed()
                        .set_description("✗ an error occurred while executing this command")
                        .set_color(0xE5989B)
                        .set_timestamp(time(0));
                    
                    bronx::safe_slash_reply(bot, event, error_embed);
                } catch (...) {
                    std::cerr << "\033[31mFailed to send slash command error reply\033[0m" << std::endl;
                }
            } catch (...) {
                // Record unknown error
                commands::global_stats.record_error(std::string("unknown_slash_error: ") + it->second->name);
                std::cerr << "\033[1;31m\u2718 Unknown exception in slash command handler for " << it->second->name << "\033[0m" << std::endl;
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
        if (db_ && db_->is_global_whitelisted(user_id)) return false;
        if (db_ && db_->is_global_blacklisted(user_id)) return true;

        std::lock_guard<std::mutex> lock(bac_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto& rec = bac_records_[user_id];

        // ---- If the user is currently timed out, block silently ----
        if (now < rec.timeout_until) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(rec.timeout_until - now).count();
            auto embed = bronx::create_embed(
                bronx::EMOJI_DENY + " **BAC** — you are timed out for suspicious activity\n"
                "try again in **" + std::to_string(remaining) + "s**", bronx::COLOR_ERROR);
            embed.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat • strike " + std::to_string(rec.strike) + "/3"));
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
                bronx::EMOJI_WARNING + " **BAC** — please solve the captcha sent to your DMs before using commands",
                bronx::COLOR_WARNING);
            embed.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat"));
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
            "🛡️ **Bronx AntiCheat (BAC)**\n\n"
            "suspicious activity detected on your account.\n"
            "please solve this to prove you're human:\n\n"
            "**What is `" + std::to_string(a) + " + " + std::to_string(b) + "`?**\n\n"
            "click the button with the correct answer within **60 seconds**.",
            bronx::COLOR_WARNING);
        captcha_embed.set_footer(dpp::embed_footer().set_text("BAC • strike " + std::to_string(rec.strike) + "/3"));

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
            bronx::EMOJI_WARNING + " **BAC** — suspicious activity detected\n"
            "a captcha has been sent to your DMs. solve it to continue using commands.",
            bronx::COLOR_WARNING);
        notify.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat"));
        if (msg_evt) {
            bronx::send_message(bot, *msg_evt, notify);
        } else if (slash_evt) {
            slash_evt->reply(dpp::message().add_embed(notify).set_flags(dpp::m_ephemeral));
        }
        std::cerr << "\033[33m\u26a0 BAC: captcha sent to user " << user_id << "\033[0m\n";
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
        std::cerr << "\033[33m\u26a0 BAC: user " << user_id << " failed captcha (wrong answer)\033[0m\n";
        bac_escalate(user_id, rec, bot, nullptr, nullptr, "failed captcha");
        event.reply(dpp::message()
            .add_embed(bronx::create_embed(bronx::EMOJI_DENY + " **BAC** — wrong answer. strike issued.", bronx::COLOR_ERROR))
            .set_flags(dpp::m_ephemeral));
    }

public:
    // Call this from your on_button_click handler to route BAC events
    void bac_on_button_click(dpp::cluster& bot, const dpp::button_click_t& event) {
        const auto& cid = event.custom_id;
        if (cid.rfind("bac_captcha_", 0) == 0) {
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
                db_->add_global_blacklist(user_id, "(BAC) " + reason + " — strike 3");
            }
            auto ban_embed = bronx::create_embed(
                "🛡️ **Bronx AntiCheat (BAC)**\n\n"
                "you have been **permanently banned** from using this bot for repeated suspicious activity.\n\n"
                "if you believe this was a mistake, please appeal in our support server.",
                bronx::COLOR_ERROR);
            ban_embed.set_title("BAC — Permanently Banned");
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
                bronx::EMOJI_DENY + " **BAC** — you have been permanently banned for repeated suspicious activity.\n"
                "check your DMs for appeal information.",
                bronx::COLOR_ERROR);
            notify.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat • strike 3/3"));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, notify);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(notify).set_flags(dpp::m_ephemeral));
            }
            std::cerr << "\033[1;31m\u26a0 BAC: user " << user_id << " permanently banned — " << reason << "\033[0m\n";
            bac_records_.erase(user_id);
        } else if (rec.strike == 2) {
            // ---- STRIKE 2: 10-minute timeout ----
            rec.timeout_until = now + BAC_TIMEOUT_STRIKE2;
            auto embed = bronx::create_embed(
                bronx::EMOJI_DENY + " **BAC** — strike **2/3**\n"
                "you have been timed out from commands for **10 minutes** for suspicious activity.\n"
                "next offence will result in a **permanent ban**.",
                bronx::COLOR_ERROR);
            embed.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat • " + reason));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            // DM warning
            auto dm_embed = bronx::create_embed(
                "🛡️ **Bronx AntiCheat (BAC)**\n\n"
                "**strike 2/3** — you've been timed out from commands for **10 minutes**.\n"
                "reason: " + reason + "\n\n"
                "⚠️ one more strike and you will be **permanently banned**.",
                bronx::COLOR_WARNING);
            bot.direct_message_create(user_id, dpp::message().add_embed(dm_embed));
            std::cerr << "\033[1;33m\u26a0 BAC: user " << user_id << " strike 2 (10min timeout) — " << reason << "\033[0m\n";
        } else {
            // ---- STRIKE 1: 5-minute timeout + captcha on return ----
            rec.timeout_until = now + BAC_TIMEOUT_STRIKE1;
            auto embed = bronx::create_embed(
                bronx::EMOJI_WARNING + " **BAC** — strike **1/3**\n"
                "you have been timed out from commands for **5 minutes** for suspicious activity.\n"
                "continued abuse will escalate to longer timeouts and eventually a ban.",
                bronx::COLOR_WARNING);
            embed.set_footer(dpp::embed_footer().set_text("Bronx AntiCheat • " + reason));
            if (msg_evt) {
                bronx::send_message(bot, *msg_evt, embed);
            } else if (slash_evt) {
                slash_evt->reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            // DM notice
            auto dm_embed = bronx::create_embed(
                "🛡️ **Bronx AntiCheat (BAC)**\n\n"
                "**strike 1/3** — you've been timed out from commands for **5 minutes**.\n"
                "reason: " + reason + "\n\n"
                "this is your first warning. please stop using macros or automated tools.",
                bronx::COLOR_WARNING);
            bot.direct_message_create(user_id, dpp::message().add_embed(dm_embed));
            std::cerr << "\033[33m\u26a0 BAC: user " << user_id << " strike 1 (5min timeout) — " << reason << "\033[0m\n";
        }
    }
};
