#include "connection_pool.h"
#include <iostream>
#include <cstring>

namespace bronx {
namespace db {

// global copy of configuration so connection parameters survive even if the
// ConnectionPool object is damaged on the heap (belt-and-suspenders safety).
static DatabaseConfig g_config;

// verify that a string contains only human-readable characters; used to detect
// corruption of the host name before passing it to the MySQL library.
static bool is_printable(const std::string &s) {
    for (unsigned char c : s) {
        if (c < 32 || c > 126) return false;
    }
    return true;
}

// global toggle for verbose connection output
static bool g_verbose_logging = false;

// Helper: create a fresh MySQL connection using the global config.
// Returns nullptr on failure.
static MYSQL* make_new_connection() {
    if (!is_printable(g_config.host)) {
        std::cerr << "ConnectionPool: invalid host string, resetting to 'localhost'\n";
        g_config.host = "localhost";
    }

    MYSQL* mysql = mysql_init(nullptr);
    if (!mysql) {
        std::cerr << "Failed to initialize MySQL connection\n";
        return nullptr;
    }

    unsigned int timeout = g_config.timeout_seconds;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    my_bool reconnect = 1;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);

    // Enable TCP keepalive for remote connections to detect dead connections
    // faster and avoid long hangs on broken pipes
    if (g_config.host != "localhost" && g_config.host != "127.0.0.1") {
        my_bool ssl_enforce = 1;
        mysql_options(mysql, MYSQL_OPT_SSL_ENFORCE, &ssl_enforce);
    }

    if (g_verbose_logging) {
        std::cerr << "  connecting to host '" << g_config.host
                  << "' port " << g_config.port << "\n";
    }

    if (!mysql_real_connect(mysql, g_config.host.c_str(), g_config.user.c_str(),
                           g_config.password.c_str(), g_config.database.c_str(),
                           g_config.port, nullptr, CLIENT_MULTI_STATEMENTS)) {
        std::cerr << "Failed to connect to database: " << mysql_error(mysql) << "\n";
        mysql_close(mysql);
        return nullptr;
    }
    mysql_set_character_set(mysql, "utf8mb4");
    return mysql;
}

// ---------------------------------------------------------------------------
// ConnectionPool — real connection pool with reuse
// ---------------------------------------------------------------------------

ConnectionPool::ConnectionPool(const DatabaseConfig& config)
    : max_connections_(config.pool_size > 0 ? config.pool_size : 25) {
    g_config = config;
    g_verbose_logging = config.log_connections;

    // Pre-create connections so the first queries are fast.
    // For remote databases we need more ready connections to absorb
    // concurrent requests without blocking on new SSL handshakes.
    size_t eager = std::min<size_t>(8, max_connections_);
    for (size_t i = 0; i < eager; ++i) {
        MYSQL* m = make_new_connection();
        if (m) {
            pool_.push(std::make_shared<Connection>(m));
            total_connections_++;
        }
    }
    if (g_verbose_logging || true) {
        std::cout << "ConnectionPool: pre-created " << pool_.size()
                  << " connections (max " << max_connections_ << ")\n";
    }
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        pool_.pop();  // shared_ptr destructor calls mysql_close
    }
}

void ConnectionPool::create_connection() {
    // Used internally — caller must NOT hold mutex_.
    MYSQL* m = make_new_connection();
    if (m) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(std::make_shared<Connection>(m));
        total_connections_++;
    }
}

std::shared_ptr<Connection> ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Fast path: grab a pooled connection
    while (!pool_.empty()) {
        auto conn = pool_.front();
        pool_.pop();

        if (!conn || !conn->get()) {
            total_connections_ = (total_connections_ > 0 ? total_connections_ - 1 : 0);
            continue;
        }

        // Only ping connections that have been idle for >60 seconds.
        // For recently-used connections, skip the expensive network round-trip.
        auto idle_time = std::chrono::steady_clock::now() - conn->last_used();
        bool needs_ping = idle_time > std::chrono::seconds(60);

        if (needs_ping) {
            // Unlock during network I/O to avoid holding the pool mutex
            lock.unlock();
            bool alive = (mysql_ping(conn->get()) == 0);
            lock.lock();

            if (!alive) {
                if (g_verbose_logging) {
                    std::cerr << "ConnectionPool::acquire() dropped stale connection (failed ping after "
                              << std::chrono::duration_cast<std::chrono::seconds>(idle_time).count() << "s idle)\n";
                }
                total_connections_ = (total_connections_ > 0 ? total_connections_ - 1 : 0);
                continue;
            }
        }

        conn->touch();
        if (g_verbose_logging) {
            std::cerr << "ConnectionPool::acquire() reused pooled connection"
                      << (needs_ping ? " (pinged)" : " (fast)") << "\n";
        }
        return conn;
    }

    // Pool empty — create a new connection if we haven't hit the limit.
    // Even if we're at max, we still create one (to avoid deadlock), but log a warning.
    if (total_connections_ >= max_connections_) {
        std::cerr << "ConnectionPool: at max connections (" << max_connections_
                  << "), creating overflow connection\n";
    }
    lock.unlock();  // don't hold mutex during network I/O

    MYSQL* m = make_new_connection();
    if (!m) {
        return nullptr;
    }

    lock.lock();
    total_connections_++;
    lock.unlock();

    return std::make_shared<Connection>(m);
}

void ConnectionPool::release(std::shared_ptr<Connection> conn) {
    if (!conn || !conn->get()) return;

    conn->touch();  // mark as recently used
    std::lock_guard<std::mutex> lock(mutex_);

    // Return the connection to the pool if below capacity
    if (pool_.size() < max_connections_) {
        pool_.push(std::move(conn));
        if (g_verbose_logging) {
            std::cerr << "ConnectionPool::release() returned to pool (size "
                      << pool_.size() << ")\n";
        }
    } else {
        // Over capacity — let shared_ptr destructor close it
        total_connections_ = (total_connections_ > 0 ? total_connections_ - 1 : 0);
        if (g_verbose_logging) {
            std::cerr << "ConnectionPool::release() pool full, closing connection\n";
        }
    }
}

size_t ConnectionPool::available_connections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.size();
}

void ConnectionPool::set_verbose_logging(bool on) {
    g_verbose_logging = on;
}

bool ConnectionPool::get_verbose_logging() {
    return g_verbose_logging;
}

size_t ConnectionPool::total_connections() const {
    return total_connections_;
}

} // namespace db
} // namespace bronx