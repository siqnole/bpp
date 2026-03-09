#include "cooldown_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

bool Database::is_on_cooldown(uint64_t user_id, const std::string& command) {
    auto conn = pool_->acquire();
    
    const char* query = "SELECT expires_at FROM cooldowns WHERE user_id = ? AND command = ? AND expires_at > NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, query, strlen(query));
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)command.c_str();
    bind[1].buffer_length = command.length();
    
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);
    
    mysql_stmt_store_result(stmt);
    bool on_cooldown = mysql_stmt_num_rows(stmt) > 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return on_cooldown;
}

bool Database::set_cooldown(uint64_t user_id, const std::string& command, int seconds) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    const char* query = "INSERT INTO cooldowns (user_id, command, expires_at) "
                       "VALUES (?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND)) "
                       "ON DUPLICATE KEY UPDATE expires_at = DATE_ADD(NOW(), INTERVAL ? SECOND)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, query, strlen(query));
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)command.c_str();
    bind[1].buffer_length = command.length();
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = (char*)&seconds;
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&seconds;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}

std::optional<std::chrono::system_clock::time_point> Database::get_cooldown_expiry(uint64_t user_id, const std::string& command) {
    auto conn = pool_->acquire();
    
    const char* query = "SELECT UNIX_TIMESTAMP(expires_at) FROM cooldowns WHERE user_id = ? AND command = ? AND expires_at > NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, query, strlen(query));
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)command.c_str();
    bind[1].buffer_length = command.length();
    
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    int64_t timestamp = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&timestamp;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    std::optional<std::chrono::system_clock::time_point> expiry;
    if (mysql_stmt_fetch(stmt) == 0) {
        expiry = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return expiry;
}

bool Database::try_claim_cooldown(uint64_t user_id, const std::string& command, int seconds) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    // Start transaction
    mysql_query(conn->get(), "START TRANSACTION");
    
    // Try to get existing cooldown with FOR UPDATE lock
    const char* select_query = "SELECT UNIX_TIMESTAMP(expires_at) FROM cooldowns WHERE user_id = ? AND command = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, select_query, strlen(select_query));
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)command.c_str();
    bind[1].buffer_length = command.length();
    
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    int64_t timestamp = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&timestamp;
    
    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);
    
    bool row_exists = mysql_stmt_num_rows(stmt) > 0;
    bool is_expired = true;
    
    if (row_exists && mysql_stmt_fetch(stmt) == 0) {
        // Check if cooldown has expired
        auto now = std::chrono::system_clock::now();
        auto expiry_time = std::chrono::system_clock::from_time_t(timestamp);
        is_expired = (now >= expiry_time);
    }
    
    mysql_stmt_close(stmt);
    
    // If cooldown exists and hasn't expired, user is on cooldown - can't claim
    if (row_exists && !is_expired) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    
    // Either no cooldown exists or it's expired - set/update the cooldown
    const char* upsert_query = "INSERT INTO cooldowns (user_id, command, expires_at) "
                               "VALUES (?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND)) "
                               "ON DUPLICATE KEY UPDATE expires_at = DATE_ADD(NOW(), INTERVAL ? SECOND)";
    
    stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, upsert_query, strlen(upsert_query));
    
    MYSQL_BIND upsert_bind[4];
    memset(upsert_bind, 0, sizeof(upsert_bind));
    upsert_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    upsert_bind[0].buffer = (char*)&user_id;
    upsert_bind[0].is_unsigned = 1;
    upsert_bind[1].buffer_type = MYSQL_TYPE_STRING;
    upsert_bind[1].buffer = (char*)command.c_str();
    upsert_bind[1].buffer_length = command.length();
    upsert_bind[2].buffer_type = MYSQL_TYPE_LONG;
    upsert_bind[2].buffer = (char*)&seconds;
    upsert_bind[3].buffer_type = MYSQL_TYPE_LONG;
    upsert_bind[3].buffer = (char*)&seconds;
    
    mysql_stmt_bind_param(stmt, upsert_bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    
    if (success) {
        mysql_query(conn->get(), "COMMIT");
    } else {
        mysql_query(conn->get(), "ROLLBACK");
    }
    
    pool_->release(conn);
    return success;
}

} // namespace db
} // namespace bronx