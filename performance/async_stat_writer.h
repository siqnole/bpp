#pragma once
#include "../database/core/database.h"
#include "../database/operations/stats/stats_operations.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <mariadb/mysql.h>

// JSON parsing helper for config
#include <sstream>

namespace bronx {
namespace perf {

// ---------------------------------------------------------------------------
// Simple remote DB connection for stats replication to Aiven
// ---------------------------------------------------------------------------
class RemoteStatsConnection {
public:
    RemoteStatsConnection() : conn_(nullptr) {}
    ~RemoteStatsConnection() { disconnect(); }

    bool connect_from_config(const std::string& config_path) {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            std::cerr << "[remote_stats] Cannot open config: " << config_path << "\n";
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        
        // Simple JSON parsing for host, port, database, user, password
        auto get_value = [&json](const std::string& key) -> std::string {
            size_t pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = json.find(":", pos);
            if (pos == std::string::npos) return "";
            size_t start = json.find_first_not_of(" \t\n\"", pos + 1);
            if (start == std::string::npos) return "";
            // Handle numbers vs strings
            if (json[start] == '"') {
                start++;
                size_t end = json.find("\"", start);
                return json.substr(start, end - start);
            } else {
                size_t end = json.find_first_of(",}\n", start);
                return json.substr(start, end - start);
            }
        };
        
        host_ = get_value("host");
        port_ = std::stoi(get_value("port").empty() ? "3306" : get_value("port"));
        database_ = get_value("database");
        user_ = get_value("user");
        password_ = get_value("password");
        
        if (host_.empty() || host_ == "localhost" || host_ == "127.0.0.1") {
            std::cerr << "[remote_stats] Skipping - config points to localhost\n";
            return false;
        }
        
        return connect();
    }
    
    bool connect() {
        if (conn_) return true;
        
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            std::cerr << "[remote_stats] mysql_init failed\n";
            return false;
        }
        
        // Enable SSL for remote connections
        mysql_ssl_set(conn_, nullptr, nullptr, nullptr, nullptr, nullptr);
        
        unsigned int timeout = 5;
        mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(conn_, MYSQL_OPT_READ_TIMEOUT, &timeout);
        mysql_options(conn_, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
        
        if (!mysql_real_connect(conn_, host_.c_str(), user_.c_str(), password_.c_str(),
                                database_.c_str(), port_, nullptr, 0)) {
            std::cerr << "[remote_stats] Connection failed: " << mysql_error(conn_) << "\n";
            mysql_close(conn_);
            conn_ = nullptr;
            return false;
        }
        
        std::cout << "[remote_stats] Connected to " << host_ << ":" << port_ << "/" << database_ << "\n";
        ensure_tables();
        return true;
    }
    
    void disconnect() {
        if (conn_) {
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }
    
    bool is_connected() const { return conn_ != nullptr; }

    // Keep-alive ping — call periodically to prevent connection timeout
    void ping() {
        if (!conn_) return;
        if (mysql_ping(conn_) != 0) {
            std::cerr << "[remote_stats] Keep-alive ping failed, reconnecting...\n";
            disconnect();
            connect();
        }
    }
    
    bool execute(const std::string& sql) {
        if (!conn_) {
            if (!connect()) return false;
        }
        
        if (mysql_query(conn_, sql.c_str()) != 0) {
            std::cerr << "[remote_stats] Query failed: " << mysql_error(conn_) << "\n";
            // Try to reconnect once
            disconnect();
            if (!connect()) return false;
            if (mysql_query(conn_, sql.c_str()) != 0) {
                std::cerr << "[remote_stats] Query failed after reconnect: " << mysql_error(conn_) << "\n";
                return false;
            }
        }
        return true;
    }

private:
    MYSQL* conn_;
    std::string host_, database_, user_, password_;
    int port_;
    
    void ensure_tables() {
        execute(R"(CREATE TABLE IF NOT EXISTS guild_voice_events (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            guild_id BIGINT UNSIGNED NOT NULL,
            user_id BIGINT UNSIGNED NOT NULL,
            channel_id BIGINT UNSIGNED NOT NULL,
            event_type VARCHAR(16) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_guild_time (guild_id, created_at)
        ))");
        execute(R"(CREATE TABLE IF NOT EXISTS guild_boost_events (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            guild_id BIGINT UNSIGNED NOT NULL,
            user_id BIGINT UNSIGNED NOT NULL,
            event_type VARCHAR(16) NOT NULL,
            boost_id VARCHAR(32) NOT NULL DEFAULT '',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_guild_time (guild_id, created_at)
        ))");
        execute(R"(CREATE TABLE IF NOT EXISTS guild_member_events (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            guild_id BIGINT UNSIGNED NOT NULL,
            user_id BIGINT UNSIGNED NOT NULL,
            event_type VARCHAR(16) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_guild_time (guild_id, created_at)
        ))");
        execute(R"(CREATE TABLE IF NOT EXISTS guild_message_events (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            guild_id BIGINT UNSIGNED NOT NULL,
            user_id BIGINT UNSIGNED NOT NULL,
            channel_id BIGINT UNSIGNED NOT NULL,
            event_type VARCHAR(16) NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_guild_time (guild_id, created_at)
        ))");
        execute(R"(CREATE TABLE IF NOT EXISTS guild_command_usage (
            guild_id BIGINT UNSIGNED NOT NULL,
            command_name VARCHAR(64) NOT NULL,
            channel_id BIGINT UNSIGNED NOT NULL,
            usage_date DATE NOT NULL,
            use_count INT UNSIGNED NOT NULL DEFAULT 1,
            PRIMARY KEY (guild_id, command_name, channel_id, usage_date),
            INDEX idx_guild_date (guild_id, usage_date)
        ))");
        execute(R"(CREATE TABLE IF NOT EXISTS guild_daily_stats (
            guild_id BIGINT UNSIGNED NOT NULL,
            channel_id VARCHAR(32) NOT NULL DEFAULT '__guild__',
            stat_date DATE NOT NULL,
            messages_count INT UNSIGNED NOT NULL DEFAULT 0,
            edits_count INT UNSIGNED NOT NULL DEFAULT 0,
            deletes_count INT UNSIGNED NOT NULL DEFAULT 0,
            joins_count INT UNSIGNED NOT NULL DEFAULT 0,
            leaves_count INT UNSIGNED NOT NULL DEFAULT 0,
            commands_count INT UNSIGNED NOT NULL DEFAULT 0,
            active_users INT UNSIGNED NOT NULL DEFAULT 0,
            PRIMARY KEY (guild_id, channel_id, stat_date),
            INDEX idx_guild_date (guild_id, stat_date)
        ))");

        // Migration: add missing columns if table was created with old schema
        execute("ALTER TABLE guild_daily_stats ADD COLUMN IF NOT EXISTS edits_count INT UNSIGNED NOT NULL DEFAULT 0 AFTER messages_count");
        execute("ALTER TABLE guild_daily_stats ADD COLUMN IF NOT EXISTS deletes_count INT UNSIGNED NOT NULL DEFAULT 0 AFTER edits_count");
        execute("ALTER TABLE guild_daily_stats ADD COLUMN IF NOT EXISTS joins_count INT UNSIGNED NOT NULL DEFAULT 0 AFTER deletes_count");
        execute("ALTER TABLE guild_daily_stats ADD COLUMN IF NOT EXISTS leaves_count INT UNSIGNED NOT NULL DEFAULT 0 AFTER joins_count");
        execute("ALTER TABLE guild_daily_stats ADD COLUMN IF NOT EXISTS active_users INT UNSIGNED NOT NULL DEFAULT 0 AFTER commands_count");

        // Ensure users table exists on Aiven for economy dashboard queries
        execute(R"(CREATE TABLE IF NOT EXISTS users (
            user_id BIGINT UNSIGNED PRIMARY KEY,
            wallet BIGINT NOT NULL DEFAULT 0,
            bank BIGINT NOT NULL DEFAULT 0,
            bank_limit BIGINT NOT NULL DEFAULT 5000,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        ))");

        // User activity daily — tracks messages, voice, commands per user per day
        execute(R"(CREATE TABLE IF NOT EXISTS user_activity_daily (
            guild_id BIGINT UNSIGNED NOT NULL,
            user_id BIGINT UNSIGNED NOT NULL,
            stat_date DATE NOT NULL,
            messages INT UNSIGNED NOT NULL DEFAULT 0,
            edits INT UNSIGNED NOT NULL DEFAULT 0,
            deletes INT UNSIGNED NOT NULL DEFAULT 0,
            commands_used INT UNSIGNED NOT NULL DEFAULT 0,
            voice_minutes INT UNSIGNED NOT NULL DEFAULT 0,
            PRIMARY KEY (guild_id, user_id, stat_date),
            INDEX idx_guild_date (guild_id, stat_date),
            INDEX idx_user_date (user_id, stat_date)
        ))");
    }
};

// ---------------------------------------------------------------------------
// AsyncStatWriter — batches synchronous telemetry writes (log_command,
// increment_stat, ensure_user_exists) off the gateway event threads.
//
// Instead of doing 3 blocking DB round-trips per command just for logging,
// we buffer the writes in memory and flush them every few seconds on a
// dedicated background thread.  This eliminates the #1 cause of slowness
// with remote databases.
// ---------------------------------------------------------------------------
class AsyncStatWriter {
public:
    explicit AsyncStatWriter(bronx::db::Database* db,
                             std::chrono::milliseconds flush_interval = std::chrono::milliseconds(3000),
                             bool verbose = false,
                             const std::string& remote_config_path = "")
        : db_(db), flush_interval_(flush_interval), verbose_(verbose) {
        // Optionally connect to remote database for stats replication
        if (!remote_config_path.empty()) {
            if (remote_stats_.connect_from_config(remote_config_path)) {
                std::cout << "[async_stat] Remote stats replication enabled\n";
            }
        }
    }

    ~AsyncStatWriter() { stop(); }

    // Expose remote connection for other components (e.g. WriteBatchQueue)
    RemoteStatsConnection& remote_connection() { return remote_stats_; }

    void start() {
        if (running_.exchange(true)) return;
        flush_thread_ = std::thread([this] { flush_loop(); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (flush_thread_.joinable()) flush_thread_.join();
        flush();  // final drain
    }

    // -----------------------------------------------------------------------
    // Enqueue methods — called from gateway event threads, MUST be fast.
    // -----------------------------------------------------------------------

    // Buffer a command history log entry
    void enqueue_log_command(uint64_t user_id, const std::string& command) {
        std::lock_guard<std::mutex> lk(log_mutex_);
        pending_logs_.push_back({user_id, command});
    }

    // Buffer a stat increment
    void enqueue_increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount) {
        std::lock_guard<std::mutex> lk(stat_mutex_);
        auto key = std::make_pair(user_id, stat_name);
        stat_deltas_[key] += amount;
    }

    // Mark a user as known to exist (avoid redundant ensure_user_exists calls)
    void mark_user_known(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(known_mutex_);
        known_users_.insert(user_id);
    }

    bool is_user_known(uint64_t user_id) {
        std::lock_guard<std::mutex> lk(known_mutex_);
        return known_users_.count(user_id) > 0;
    }

    // Buffer a guild member event (join / leave)
    void enqueue_member_event(uint64_t guild_id, uint64_t user_id, const std::string& event_type) {
        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[35m+member\033[0m "
                      << event_type << " guild=" << guild_id << " user=" << user_id << "\n";
        }
        std::lock_guard<std::mutex> lk(member_mutex_);
        pending_member_events_.push_back({guild_id, user_id, event_type});
    }

    // Buffer a guild message event (message / edit / delete)
    void enqueue_message_event(uint64_t guild_id, uint64_t user_id, uint64_t channel_id, const std::string& event_type) {
        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[36m+msg\033[0m "
                      << event_type << " guild=" << guild_id << " user=" << user_id
                      << " ch=" << channel_id << "\n";
        }
        std::lock_guard<std::mutex> lk(message_mutex_);
        pending_message_events_.push_back({guild_id, user_id, channel_id, event_type});
    }

    // Buffer a guild voice event (join / leave)
    void enqueue_voice_event(uint64_t guild_id, uint64_t user_id, uint64_t channel_id, const std::string& event_type) {
        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[34m+voice\033[0m "
                      << event_type << " guild=" << guild_id << " user=" << user_id
                      << " ch=" << channel_id << "\n";
        }
        std::lock_guard<std::mutex> lk(voice_mutex_);
        pending_voice_events_.push_back({guild_id, user_id, channel_id, event_type});
    }

    // Buffer a guild boost event (boost / unboost)
    void enqueue_boost_event(uint64_t guild_id, uint64_t user_id, const std::string& event_type, const std::string& boost_id = "") {
        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[35m+boost\033[0m "
                      << event_type << " guild=" << guild_id << " user=" << user_id
                      << " boost_id=" << boost_id << "\n";
        }
        std::lock_guard<std::mutex> lk(boost_mutex_);
        pending_boost_events_.push_back({guild_id, user_id, event_type, boost_id});
    }

    // Buffer a guild command usage increment
    void enqueue_command_usage(uint64_t guild_id, const std::string& command_name, uint64_t channel_id) {
        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[33m+cmd\033[0m "
                      << command_name << " guild=" << guild_id << " ch=" << channel_id << "\n";
        }
        std::lock_guard<std::mutex> lk(cmd_usage_mutex_);
        auto key = std::to_string(guild_id) + ":" + command_name + ":" + std::to_string(channel_id);
        pending_cmd_usage_[key] = {guild_id, command_name, channel_id,
                                   pending_cmd_usage_.count(key) ? pending_cmd_usage_[key].count + 1 : 1};
    }

    // Force an immediate flush
    void flush_now() { flush(); }

private:
    struct LogEntry {
        uint64_t user_id;
        std::string command;
    };

    struct MemberEvent {
        uint64_t guild_id;
        uint64_t user_id;
        std::string event_type;
    };

    struct MessageEvent {
        uint64_t guild_id;
        uint64_t user_id;
        uint64_t channel_id;
        std::string event_type;
    };

    struct VoiceEvent {
        uint64_t guild_id;
        uint64_t user_id;
        uint64_t channel_id;
        std::string event_type;
    };

    struct BoostEvent {
        uint64_t guild_id;
        uint64_t user_id;
        std::string event_type;
        std::string boost_id;
    };

    struct CmdUsageEntry {
        uint64_t guild_id;
        std::string command_name;
        uint64_t channel_id;
        int count;
    };

    // Key for stat deltas: (user_id, stat_name)
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, std::string>& p) const {
            auto h1 = std::hash<uint64_t>{}(p.first);
            auto h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 << 32);
        }
    };

    std::mutex log_mutex_;
    std::vector<LogEntry> pending_logs_;

    std::mutex stat_mutex_;
    std::unordered_map<std::pair<uint64_t, std::string>, int64_t, PairHash> stat_deltas_;

    std::mutex known_mutex_;
    std::unordered_set<uint64_t> known_users_;

    std::mutex member_mutex_;
    std::vector<MemberEvent> pending_member_events_;

    std::mutex message_mutex_;
    std::vector<MessageEvent> pending_message_events_;

    std::mutex voice_mutex_;
    std::vector<VoiceEvent> pending_voice_events_;

    std::mutex boost_mutex_;
    std::vector<BoostEvent> pending_boost_events_;

    std::mutex cmd_usage_mutex_;
    std::unordered_map<std::string, CmdUsageEntry> pending_cmd_usage_;

    bronx::db::Database* db_;
    RemoteStatsConnection remote_stats_;  // Aiven replication for dashboard
    std::chrono::milliseconds flush_interval_;
    bool verbose_{false};
    std::atomic<bool> running_{false};
    std::thread flush_thread_;
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::chrono::steady_clock::time_point last_ping_ = std::chrono::steady_clock::now();

    void flush_loop() {
        while (running_.load()) {
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, flush_interval_, [this] { return !running_.load(); });
            if (!running_.load()) break;
            lk.unlock();

            // Keep-alive: ping Aiven every ~30s to prevent connection drop
            auto now = std::chrono::steady_clock::now();
            if (now - last_ping_ > std::chrono::seconds(30)) {
                remote_stats_.ping();
                last_ping_ = now;
            }

            flush();
        }
    }

    void flush() {
        // Swap out pending data under lock, then process without holding lock
        std::vector<LogEntry> logs;
        {
            std::lock_guard<std::mutex> lk(log_mutex_);
            logs.swap(pending_logs_);
        }

        std::unordered_map<std::pair<uint64_t, std::string>, int64_t, PairHash> stats;
        {
            std::lock_guard<std::mutex> lk(stat_mutex_);
            stats.swap(stat_deltas_);
        }

        // Flush guild member events
        std::vector<MemberEvent> member_events;
        {
            std::lock_guard<std::mutex> lk(member_mutex_);
            member_events.swap(pending_member_events_);
        }

        // Flush guild message events
        std::vector<MessageEvent> msg_events;
        {
            std::lock_guard<std::mutex> lk(message_mutex_);
            msg_events.swap(pending_message_events_);
        }

        // Flush voice events
        std::vector<VoiceEvent> voice_events;
        {
            std::lock_guard<std::mutex> lk(voice_mutex_);
            voice_events.swap(pending_voice_events_);
        }

        // Flush boost events
        std::vector<BoostEvent> boost_events;
        {
            std::lock_guard<std::mutex> lk(boost_mutex_);
            boost_events.swap(pending_boost_events_);
        }

        // Flush command usage counters
        std::unordered_map<std::string, CmdUsageEntry> cmd_usage;
        {
            std::lock_guard<std::mutex> lk(cmd_usage_mutex_);
            cmd_usage.swap(pending_cmd_usage_);
        }

        // Nothing to do?
        if (logs.empty() && stats.empty() && member_events.empty() && msg_events.empty()
            && voice_events.empty() && boost_events.empty() && cmd_usage.empty()) return;

        if (verbose_) {
            std::cout << "\033[2m[\033[35mSTATS\033[2m]\033[0m \033[1mflush\033[0m"
                      << " logs=" << logs.size()
                      << " stats=" << stats.size()
                      << " members=" << member_events.size()
                      << " msgs=" << msg_events.size()
                      << " voice=" << voice_events.size()
                      << " boosts=" << boost_events.size()
                      << " cmds=" << cmd_usage.size() << "\n";
        }

        // Collect unique user IDs that need ensure_user_exists
        std::unordered_set<uint64_t> users_to_ensure;
        for (const auto& log : logs) {
            if (!is_user_known(log.user_id)) {
                users_to_ensure.insert(log.user_id);
            }
        }
        for (const auto& [key, _] : stats) {
            if (!is_user_known(key.first)) {
                users_to_ensure.insert(key.first);
            }
        }

        // Batch ensure_user_exists
        for (uint64_t uid : users_to_ensure) {
            try {
                db_->ensure_user_exists(uid);
                mark_user_known(uid);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] ensure_user_exists failed for " << uid << ": " << e.what() << "\n";
            }
        }

        // Flush command logs
        for (const auto& log : logs) {
            try {
                db_->log_history(log.user_id, "CMD", "ran ." + log.command);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_history failed: " << e.what() << "\n";
            }
        }

        // Flush stat increments
        for (const auto& [key, amount] : stats) {
            try {
                // Call increment_stat_raw to skip the internal ensure_user_exists
                // (we already ensured above in batch)
                db_->increment_stat(key.first, key.second, amount);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] increment_stat failed: " << e.what() << "\n";
            }
        }

        // Write member events
        for (const auto& ev : member_events) {
            try {
                bronx::db::stats_operations::log_member_event(db_, ev.guild_id, ev.user_id, ev.event_type);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_member_event failed: " << e.what() << "\n";
            }
            // Replicate to Aiven for dashboard
            if (remote_stats_.is_connected()) {
                std::string sql = "INSERT INTO guild_member_events (guild_id, user_id, event_type) VALUES ("
                    + std::to_string(ev.guild_id) + ", " + std::to_string(ev.user_id) + ", '" + ev.event_type + "')";
                remote_stats_.execute(sql);
            }
        }

        // Write message events
        for (const auto& ev : msg_events) {
            try {
                bronx::db::stats_operations::log_message_event(db_, ev.guild_id, ev.user_id, ev.channel_id, ev.event_type);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_message_event failed: " << e.what() << "\n";
            }
            // Replicate to Aiven for dashboard
            if (remote_stats_.is_connected()) {
                std::string sql = "INSERT INTO guild_message_events (guild_id, user_id, channel_id, event_type) VALUES ("
                    + std::to_string(ev.guild_id) + ", " + std::to_string(ev.user_id) + ", "
                    + std::to_string(ev.channel_id) + ", '" + ev.event_type + "')";
                remote_stats_.execute(sql);
            }
        }

        // Write voice events
        for (const auto& ev : voice_events) {
            try {
                bronx::db::stats_operations::log_voice_event(db_, ev.guild_id, ev.user_id, ev.channel_id, ev.event_type);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_voice_event failed: " << e.what() << "\n";
            }
            if (remote_stats_.is_connected()) {
                std::string sql = "INSERT INTO guild_voice_events (guild_id, user_id, channel_id, event_type) VALUES ("
                    + std::to_string(ev.guild_id) + ", " + std::to_string(ev.user_id) + ", "
                    + std::to_string(ev.channel_id) + ", '" + ev.event_type + "')";
                remote_stats_.execute(sql);
            }
        }

        // Write boost events
        for (const auto& ev : boost_events) {
            try {
                bronx::db::stats_operations::log_boost_event(db_, ev.guild_id, ev.user_id, ev.event_type, ev.boost_id);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] log_boost_event failed: " << e.what() << "\n";
            }
            if (remote_stats_.is_connected()) {
                std::string sql = "INSERT INTO guild_boost_events (guild_id, user_id, event_type, boost_id) VALUES ("
                    + std::to_string(ev.guild_id) + ", " + std::to_string(ev.user_id)
                    + ", '" + ev.event_type + "', '" + ev.boost_id + "')";
                remote_stats_.execute(sql);
            }
        }

        // Write command usage counters
        for (const auto& [key, entry] : cmd_usage) {
            std::string sql = "INSERT INTO guild_command_usage (guild_id, command_name, channel_id, usage_date, use_count) VALUES ('"
                + std::to_string(entry.guild_id) + "', '" + entry.command_name + "', '"
                + std::to_string(entry.channel_id) + "', CURDATE(), " + std::to_string(entry.count)
                + ") ON DUPLICATE KEY UPDATE use_count = use_count + " + std::to_string(entry.count);
            try {
                db_->execute(sql);
            } catch (const std::exception& e) {
                std::cerr << "[async_stat] increment_command_usage failed: " << e.what() << "\n";
            }
            // Replicate to Aiven for dashboard
            if (remote_stats_.is_connected()) {
                remote_stats_.execute(sql);
            }
        }

        if (!logs.empty() || !stats.empty() || !member_events.empty() || !msg_events.empty()
            || !voice_events.empty() || !boost_events.empty() || !cmd_usage.empty()) {
            // Only log occasionally to avoid spam
            static std::atomic<uint64_t> flush_count{0};
            if (++flush_count % 20 == 1) {
                std::cerr << "[async_stat] flushed " << logs.size() << " logs, "
                          << stats.size() << " stat updates, "
                          << member_events.size() << " member events, "
                          << msg_events.size() << " msg events, "
                          << voice_events.size() << " voice events, "
                          << boost_events.size() << " boost events, "
                          << cmd_usage.size() << " cmd usage\n";
            }
        }
    }
};

// global pointer — set from main.cpp so command_handler.h can enqueue
// guild command usage without requiring constructor changes
inline AsyncStatWriter* g_stat_writer = nullptr;

} // namespace perf
} // namespace bronx
