#pragma once
/**
 * safe_db.h — Type-safe MySQL prepared statement wrapper
 *
 * Eliminates the ~30-line boilerplate of manual MYSQL_STMT + MYSQL_BIND
 * while preventing SQL injection by design (no string concatenation).
 *
 * Usage:
 *   using bronx::security::SafeDB;
 *
 *   // Non-SELECT (INSERT/UPDATE/DELETE):
 *   bool ok = SafeDB::execute(db, "UPDATE users SET coins = ? WHERE user_id = ?",
 *                             new_coins, user_id);
 *
 *   // SELECT returning rows:
 *   auto rows = SafeDB::select(db, "SELECT name, coins FROM users WHERE user_id = ?",
 *                              user_id);
 *   for (auto& row : rows) {
 *       std::string name = row[0];
 *       std::string coins = row[1];
 *   }
 *
 *   // SELECT single row:
 *   auto row = SafeDB::select_one(db, "SELECT coins FROM users WHERE user_id = ?", user_id);
 *   if (row) { int64_t coins = std::stoll((*row)[0]); }
 *
 *   // Get affected row count:
 *   uint64_t affected = 0;
 *   SafeDB::execute(db, "DELETE FROM temp WHERE expired < NOW()", &affected);
 *   // (pass nullptr for affected_rows if you don't need it)
 */

#include <string>
#include <vector>
#include <optional>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <mariadb/mysql.h>
#include "../database/core/database.h"

namespace bronx {
namespace security {

// ── RAII helpers ────────────────────────────────────────────────

/// RAII guard: acquires connection from pool, releases on destruction
class ConnectionGuard {
public:
    explicit ConnectionGuard(bronx::db::Database* db)
        : pool_(db->get_pool()), conn_(pool_->acquire()) {}
    ~ConnectionGuard() {
        if (conn_) pool_->release(conn_);
    }
    MYSQL* get() { return conn_ ? conn_->get() : nullptr; }
    explicit operator bool() const { return conn_ && conn_->get(); }
    // Non-copyable
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
private:
    bronx::db::ConnectionPool* pool_;
    std::shared_ptr<bronx::db::Connection> conn_;
};

/// RAII guard for MYSQL_STMT — calls mysql_stmt_close() on destruction
class StmtGuard {
public:
    explicit StmtGuard(MYSQL* mysql) : stmt_(mysql_stmt_init(mysql)) {}
    ~StmtGuard() {
        if (stmt_) mysql_stmt_close(stmt_);
    }
    MYSQL_STMT* get() { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;
private:
    MYSQL_STMT* stmt_;
};

// ── Bind trait: maps C++ types → MYSQL_TYPE ────────────────────

namespace detail {

// Base case: unsupported type
template<typename T, typename = void>
struct BindTrait {
    static_assert(sizeof(T) == 0,
        "SafeDB: unsupported parameter type. Use int, int64_t, uint64_t, double, or std::string.");
};

// int32_t / int
template<>
struct BindTrait<int32_t> {
    static void bind(MYSQL_BIND& b, int32_t& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_LONG;
        b.buffer = &val;
        b.is_unsigned = false;
    }
};

// uint32_t
template<>
struct BindTrait<uint32_t> {
    static void bind(MYSQL_BIND& b, uint32_t& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_LONG;
        b.buffer = &val;
        b.is_unsigned = true;
    }
};

// int64_t
template<>
struct BindTrait<int64_t> {
    static void bind(MYSQL_BIND& b, int64_t& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_LONGLONG;
        b.buffer = &val;
        b.is_unsigned = false;
    }
};

// uint64_t
template<>
struct BindTrait<uint64_t> {
    static void bind(MYSQL_BIND& b, uint64_t& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_LONGLONG;
        b.buffer = &val;
        b.is_unsigned = true;
    }
};

// double
template<>
struct BindTrait<double> {
    static void bind(MYSQL_BIND& b, double& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_DOUBLE;
        b.buffer = &val;
    }
};

// std::string
template<>
struct BindTrait<std::string> {
    static void bind(MYSQL_BIND& b, std::string& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = val.data();
        b.buffer_length = val.size();
    }
};

// const char* → treat as string
template<>
struct BindTrait<const char*> {
    static void bind(MYSQL_BIND& b, const char*& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = const_cast<char*>(val);
        b.buffer_length = std::strlen(val);
    }
};

// bool
template<>
struct BindTrait<bool> {
    static void bind(MYSQL_BIND& b, bool& val) {
        std::memset(&b, 0, sizeof(b));
        b.buffer_type = MYSQL_TYPE_TINY;
        b.buffer = &val;
        b.is_unsigned = true;
    }
};

// ── Recursive bind helper ──────────────────────────────────────

inline void bind_params(MYSQL_BIND* /*binds*/, size_t /*index*/) {
    // Base case: no more params
}

template<typename T, typename... Rest>
void bind_params(MYSQL_BIND* binds, size_t index, T& val, Rest&... rest) {
    BindTrait<std::decay_t<T>>::bind(binds[index], val);
    bind_params(binds, index + 1, rest...);
}

} // namespace detail

// ── SafeDB public API ──────────────────────────────────────────

class SafeDB {
public:
    /**
     * Execute a non-SELECT prepared statement (INSERT/UPDATE/DELETE/ALTER…).
     *
     * @param db          Database instance
     * @param query       SQL with ? placeholders
     * @param args        Bound parameters (type-safe)
     * @return true on success
     *
     * Overload with affected_rows output:
     *   bool execute(db, query, &affected, args...)
     */
    template<typename... Args>
    static bool execute(bronx::db::Database* db, const std::string& query, Args... args) {
        return execute_impl(db, query, nullptr, args...);
    }

    /**
     * Execute non-SELECT with affected_rows output.
     * First arg after query must be uint64_t* for affected rows.
     */
    template<typename... Args>
    static bool execute_counted(bronx::db::Database* db, const std::string& query,
                                uint64_t* affected_rows, Args... args) {
        return execute_impl(db, query, affected_rows, args...);
    }

    /**
     * Execute a SELECT prepared statement and return all rows.
     * Each row is a vector of strings (NULL → empty string "").
     *
     * @param db          Database instance
     * @param query       SQL SELECT with ? placeholders
     * @param args        Bound parameters
     * @return vector of rows; empty on error or no results
     */
    template<typename... Args>
    static std::vector<std::vector<std::string>> select(
            bronx::db::Database* db, const std::string& query, Args... args) {
        std::vector<std::vector<std::string>> results;

        ConnectionGuard conn(db);
        if (!conn) return results;

        StmtGuard stmt(conn.get());
        if (!stmt) return results;

        if (mysql_stmt_prepare(stmt.get(), query.c_str(), query.size()) != 0)
            return results;

        // Bind input parameters
        constexpr size_t N = sizeof...(Args);
        if constexpr (N > 0) {
            MYSQL_BIND binds[N];
            std::memset(binds, 0, sizeof(binds));
            detail::bind_params(binds, 0, args...);
            if (mysql_stmt_bind_param(stmt.get(), binds) != 0)
                return results;
        }

        if (mysql_stmt_execute(stmt.get()) != 0)
            return results;

        // Get result metadata
        MYSQL_RES* meta = mysql_stmt_result_metadata(stmt.get());
        if (!meta) return results; // no result set

        unsigned int num_cols = mysql_num_fields(meta);

        // Store result for random access
        mysql_stmt_store_result(stmt.get());

        // Prepare output binds
        std::vector<MYSQL_BIND> out_binds(num_cols);
        std::vector<std::vector<char>> buffers(num_cols);
        std::vector<unsigned long> lengths(num_cols);
        std::vector<my_bool> nulls(num_cols);
        std::memset(out_binds.data(), 0, num_cols * sizeof(MYSQL_BIND));

        MYSQL_FIELD* fields = mysql_fetch_fields(meta);
        for (unsigned int i = 0; i < num_cols; i++) {
            // Allocate generous buffer per column
            size_t buf_size = (fields[i].max_length > 0)
                              ? fields[i].max_length + 1
                              : 1024;
            buffers[i].resize(buf_size);
            out_binds[i].buffer_type = MYSQL_TYPE_STRING; // fetch everything as string
            out_binds[i].buffer = buffers[i].data();
            out_binds[i].buffer_length = buf_size;
            out_binds[i].length = &lengths[i];
            out_binds[i].is_null = &nulls[i];
        }

        mysql_stmt_bind_result(stmt.get(), out_binds.data());

        // Fetch rows
        while (mysql_stmt_fetch(stmt.get()) == 0) {
            std::vector<std::string> row;
            row.reserve(num_cols);
            for (unsigned int i = 0; i < num_cols; i++) {
                if (nulls[i]) {
                    row.emplace_back("");
                } else {
                    row.emplace_back(buffers[i].data(), lengths[i]);
                }
            }
            results.push_back(std::move(row));
        }

        mysql_free_result(meta);
        return results;
    }

    /**
     * Execute a SELECT and return the first row, or std::nullopt if no results.
     */
    template<typename... Args>
    static std::optional<std::vector<std::string>> select_one(
            bronx::db::Database* db, const std::string& query, Args... args) {
        auto rows = select(db, query, args...);
        if (rows.empty()) return std::nullopt;
        return std::move(rows[0]);
    }

    /**
     * Get the last error message from a failed operation.
     * This is a convenience — callers can also check db->get_last_error().
     */
    static std::string last_error(bronx::db::Database* db) {
        return db->get_last_error();
    }

private:
    template<typename... Args>
    static bool execute_impl(bronx::db::Database* db, const std::string& query,
                             uint64_t* affected_rows, Args&... args) {
        ConnectionGuard conn(db);
        if (!conn) return false;

        StmtGuard stmt(conn.get());
        if (!stmt) return false;

        if (mysql_stmt_prepare(stmt.get(), query.c_str(), query.size()) != 0)
            return false;

        constexpr size_t N = sizeof...(Args);
        if constexpr (N > 0) {
            MYSQL_BIND binds[N];
            std::memset(binds, 0, sizeof(binds));
            detail::bind_params(binds, 0, args...);
            if (mysql_stmt_bind_param(stmt.get(), binds) != 0)
                return false;
        }

        if (mysql_stmt_execute(stmt.get()) != 0)
            return false;

        if (affected_rows) {
            *affected_rows = mysql_stmt_affected_rows(stmt.get());
        }
        return true;
    }
};

} // namespace security
} // namespace bronx
