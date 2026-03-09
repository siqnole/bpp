#include "abuse_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

// ============================================================================
// Global blacklist/whitelist implementations
// ============================================================================

bool Database::add_global_blacklist(uint64_t user_id, const std::string& reason) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO global_blacklist (user_id, reason) VALUES (?, ?) "
                        "ON DUPLICATE KEY UPDATE reason = VALUES(reason)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_global_blacklist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    unsigned long reason_len = reason.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)reason.c_str();
    bind[1].buffer_length = reason.size();
    bind[1].length = &reason_len;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_global_blacklist execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_global_blacklist(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM global_blacklist WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_global_blacklist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_global_blacklist execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_global_blacklisted(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT 1 FROM global_blacklist WHERE user_id = ? LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_global_blacklisted prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);
    mysql_stmt_store_result(stmt);
    bool exists = mysql_stmt_num_rows(stmt) > 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return exists;
}

std::vector<BlacklistEntry> Database::get_global_blacklist() {
    std::vector<BlacklistEntry> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT user_id, COALESCE(reason, '') FROM global_blacklist";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_global_blacklist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_global_blacklist execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    uint64_t uid;
    char reason_buf[512];
    unsigned long reason_len = 0;
    my_bool reason_is_null = 0;
    MYSQL_BIND result_bind[2];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&uid;
    result_bind[0].is_unsigned = 1;
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = reason_buf;
    result_bind[1].buffer_length = sizeof(reason_buf);
    result_bind[1].length = &reason_len;
    result_bind[1].is_null = &reason_is_null;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        BlacklistEntry entry;
        entry.user_id = uid;
        entry.reason = reason_is_null ? "" : std::string(reason_buf, reason_len);
        out.push_back(entry);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

bool Database::add_global_whitelist(uint64_t user_id, const std::string& reason) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO global_whitelist (user_id, reason) VALUES (?, ?) "
                        "ON DUPLICATE KEY UPDATE reason = VALUES(reason)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_global_whitelist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    unsigned long reason_len = reason.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)reason.c_str();
    bind[1].buffer_length = reason.size();
    bind[1].length = &reason_len;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_global_whitelist execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_global_whitelist(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM global_whitelist WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_global_whitelist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_global_whitelist execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_global_whitelisted(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT 1 FROM global_whitelist WHERE user_id = ? LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_global_whitelisted prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);
    mysql_stmt_store_result(stmt);
    bool exists = mysql_stmt_num_rows(stmt) > 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return exists;
}

std::vector<WhitelistEntry> Database::get_global_whitelist() {
    std::vector<WhitelistEntry> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT user_id, COALESCE(reason, '') FROM global_whitelist";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_global_whitelist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_global_whitelist execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    uint64_t uid;
    char reason_buf[512];
    unsigned long reason_len = 0;
    my_bool reason_is_null = 0;
    MYSQL_BIND result_bind[2];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&uid;
    result_bind[0].is_unsigned = 1;
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = reason_buf;
    result_bind[1].buffer_length = sizeof(reason_buf);
    result_bind[1].length = &reason_len;
    result_bind[1].is_null = &reason_is_null;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        WhitelistEntry entry;
        entry.user_id = uid;
        entry.reason = reason_is_null ? "" : std::string(reason_buf, reason_len);
        out.push_back(entry);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

} // namespace db
} // namespace bronx

// C-style wrappers
namespace bronx {
namespace db {
namespace abuse_operations {
    bool add_global_blacklist(Database* db, uint64_t user_id, const std::string& reason) {
        return db->add_global_blacklist(user_id, reason);
    }
    bool remove_global_blacklist(Database* db, uint64_t user_id) {
        return db->remove_global_blacklist(user_id);
    }
    bool is_global_blacklisted(Database* db, uint64_t user_id) {
        return db->is_global_blacklisted(user_id);
    }
    std::vector<BlacklistEntry> get_global_blacklist(Database* db) {
        return db->get_global_blacklist();
    }
    bool add_global_whitelist(Database* db, uint64_t user_id, const std::string& reason) {
        return db->add_global_whitelist(user_id, reason);
    }
    bool remove_global_whitelist(Database* db, uint64_t user_id) {
        return db->remove_global_whitelist(user_id);
    }
    bool is_global_whitelisted(Database* db, uint64_t user_id) {
        return db->is_global_whitelisted(user_id);
    }
    std::vector<WhitelistEntry> get_global_whitelist(Database* db) {
        return db->get_global_whitelist();
    }
}
}
}
