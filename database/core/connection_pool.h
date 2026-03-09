#pragma once
#include "types.h"
#include <mariadb/mysql.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace bronx {
namespace db {

class Connection {
public:
    Connection(MYSQL* conn) : conn_(conn) {}
    ~Connection() {
        if (conn_) {
            mysql_close(conn_);
        }
    }
    
    MYSQL* get() { return conn_; }
    operator bool() const { return conn_ != nullptr; }
    
    // Non-copyable, movable
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = default;
    Connection& operator=(Connection&&) = default;
    
private:
    MYSQL* conn_;
};

class ConnectionPool {
public:
    ConnectionPool(const DatabaseConfig& config);
    ~ConnectionPool();
    
    // Get connection from pool (blocks if none available)
    std::shared_ptr<Connection> acquire();
    
    // Return connection to pool
    void release(std::shared_ptr<Connection> conn);
    
    // Get pool statistics
    size_t available_connections() const;
    size_t total_connections() const;

    // control logging of each new connection (disabled by default)
    static void set_verbose_logging(bool on);
    static bool get_verbose_logging();
    
private:
    void create_connection();
    
    // configuration is stored globally (see connection_pool.cpp) to avoid
    // heap corruption issues; therefore the per-instance config_ member has
    // been removed.
    std::queue<std::shared_ptr<Connection>> pool_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t total_connections_ = 0;
    size_t max_connections_;
};

} // namespace db
} // namespace bronx