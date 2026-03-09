#include "history_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// Implement Database member methods for command history

bool Database::log_history(uint64_t user_id, const std::string& entry_type,
                           const std::string& description, int64_t amount, int64_t balance_after) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO command_history (user_id, entry_type, description, amount, balance_after) VALUES (?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("log_history prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)entry_type.c_str();
    bind[1].buffer_length = entry_type.size();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)description.c_str();
    bind[2].buffer_length = description.size();

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&amount;

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&balance_after;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("log_history bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<HistoryEntry> Database::fetch_history(uint64_t user_id, int limit, int offset) {
    std::vector<HistoryEntry> results;
    auto conn = pool_->acquire();
    
    std::string query = "SELECT id, user_id, entry_type, description, amount, balance_after, "
                        "UNIX_TIMESTAMP(created_at) FROM command_history WHERE user_id = ? "
                        "ORDER BY created_at DESC LIMIT ? OFFSET ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
        log_error("fetch_history prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind parameters
    MYSQL_BIND bind_param[3];
    memset(bind_param, 0, sizeof(bind_param));
    
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    
    bind_param[1].buffer_type = MYSQL_TYPE_LONG;
    bind_param[1].buffer = (char*)&limit;
    
    bind_param[2].buffer_type = MYSQL_TYPE_LONG;
    bind_param[2].buffer = (char*)&offset;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        log_error("fetch_history bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("fetch_history execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind result
    HistoryEntry h;
    long long ts = 0;
    static char type_buf[16];
    static char desc_buf[512];
    unsigned long type_len = 0;
    unsigned long desc_len = 0;

    MYSQL_BIND bind_result[7];
    memset(bind_result, 0, sizeof(bind_result));
    
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &h.id;
    bind_result[0].is_unsigned = 1;
    
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &h.user_id;
    bind_result[1].is_unsigned = 1;
    
    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = type_buf;
    bind_result[2].buffer_length = sizeof(type_buf);
    bind_result[2].length = &type_len;
    
    bind_result[3].buffer_type = MYSQL_TYPE_STRING;
    bind_result[3].buffer = desc_buf;
    bind_result[3].buffer_length = sizeof(desc_buf);
    bind_result[3].length = &desc_len;
    
    bind_result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[4].buffer = &h.amount;
    
    bind_result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[5].buffer = &h.balance_after;
    
    bind_result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[6].buffer = &ts;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("fetch_history bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("fetch_history store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        h.entry_type = std::string(type_buf, type_len);
        h.description = std::string(desc_buf, desc_len);
        h.created_at = std::chrono::system_clock::from_time_t(ts);
        results.push_back(h);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

int Database::get_history_count(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT COUNT(*) FROM command_history WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_history_count prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        log_error("get_history_count bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_history_count execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    long long count = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &count;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_history_count bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return static_cast<int>(count);
}

bool Database::clear_history(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM command_history WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("clear_history prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("clear_history bind");
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
