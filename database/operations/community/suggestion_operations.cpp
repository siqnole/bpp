#include "suggestion_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// Implement Database member methods for suggestions

bool Database::add_suggestion(uint64_t user_id, const std::string& text, int64_t networth) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO suggestions (user_id, suggestion, networth) VALUES (?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        // capture statement-specific message to avoid stale errors
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_suggestion prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)text.c_str();
    bind[1].buffer_length = text.size();

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&networth;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("add_suggestion bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<Suggestion> Database::fetch_suggestions(const std::string& order_clause) {
    std::vector<Suggestion> results;
    auto conn = pool_->acquire();
    std::string query = "SELECT id, user_id, suggestion, networth, UNIX_TIMESTAMP(submitted_at), `read` FROM suggestions";
    if (!order_clause.empty()) {
        query += " ORDER BY " + order_clause;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
        log_error("fetch_suggestions prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("fetch_suggestions execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind result
    Suggestion s;
    long long ts = 0;
    my_bool read_val = 0;

    MYSQL_BIND bind_result[6];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &s.id;
    bind_result[0].is_unsigned = 1;
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &s.user_id;
    bind_result[1].is_unsigned = 1;
    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    static char textbuf[2048];
    bind_result[2].buffer = textbuf;
    bind_result[2].buffer_length = sizeof(textbuf);
    bind_result[2].length = nullptr;
    bind_result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[3].buffer = &s.networth;
    bind_result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[4].buffer = &ts;
    bind_result[5].buffer_type = MYSQL_TYPE_TINY;
    bind_result[5].buffer = &read_val;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("fetch_suggestions bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("fetch_suggestions store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        s.suggestion = std::string(textbuf);
        s.submitted_at = std::chrono::system_clock::from_time_t(ts);
        s.read = read_val != 0;
        results.push_back(s);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

bool Database::mark_suggestion_read(uint64_t suggestion_id) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE suggestions SET `read` = TRUE WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("mark_read prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&suggestion_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("mark_read bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_suggestion(uint64_t suggestion_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM suggestions WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_suggestion prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&suggestion_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("delete_suggestion bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

} // namespace db
} // namespace bronx
