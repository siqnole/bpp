#include "feature_flag_operations.h"
#include "../../core/database.h"
#include "../../../log.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ────────────────────────────────────────────────────────────────────────
// Database member functions — feature_flags table
// ────────────────────────────────────────────────────────────────────────

bool Database::set_feature_flag(const std::string& feature, const std::string& mode, const std::string& reason) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO feature_flags (feature_name, mode, reason) VALUES (?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE mode = VALUES(mode), reason = VALUES(reason)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_feature_flag prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)feature.c_str();
    bind[0].buffer_length = feature.size();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)mode.c_str();
    bind[1].buffer_length = mode.size();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)reason.c_str();
    bind[2].buffer_length = reason.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_feature_flag bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_feature_flag execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::tuple<std::string, std::string, std::string>> Database::get_all_feature_flags() {
    auto conn = pool_->acquire();
    std::vector<std::tuple<std::string, std::string, std::string>> result;
    const char* query = "SELECT feature_name, mode, reason FROM feature_flags";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_all_feature_flags prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_feature_flags execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    char name_buf[256]; unsigned long name_len = 0;
    char mode_buf[64];  unsigned long mode_len = 0;
    char reason_buf[1024]; unsigned long reason_len = 0;

    MYSQL_BIND res_bind[3];
    memset(res_bind, 0, sizeof(res_bind));

    res_bind[0].buffer_type = MYSQL_TYPE_STRING;
    res_bind[0].buffer = name_buf;
    res_bind[0].buffer_length = sizeof(name_buf);
    res_bind[0].length = &name_len;

    res_bind[1].buffer_type = MYSQL_TYPE_STRING;
    res_bind[1].buffer = mode_buf;
    res_bind[1].buffer_length = sizeof(mode_buf);
    res_bind[1].length = &mode_len;

    res_bind[2].buffer_type = MYSQL_TYPE_STRING;
    res_bind[2].buffer = reason_buf;
    res_bind[2].buffer_length = sizeof(reason_buf);
    res_bind[2].length = &reason_len;

    mysql_stmt_bind_result(stmt, res_bind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        result.emplace_back(
            std::string(name_buf, name_len),
            std::string(mode_buf, mode_len),
            std::string(reason_buf, reason_len)
        );
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::delete_feature_flag(const std::string& feature) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM feature_flags WHERE feature_name = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_feature_flag prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)feature.c_str();
    bind[0].buffer_length = feature.size();

    bool success = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    
    // Also delete whitelists for this feature
    if (success) {
        const char* wl_query = "DELETE FROM feature_flag_whitelist WHERE feature_name = ?";
        MYSQL_STMT* wl_stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(wl_stmt, wl_query, strlen(wl_query)) == 0) {
            mysql_stmt_bind_param(wl_stmt, bind);
            if (mysql_stmt_execute(wl_stmt) != 0) {
                log_error("delete_feature_flag wl execute");
            }
        } else {
            log_error("delete_feature_flag wl prepare");
        }
        mysql_stmt_close(wl_stmt);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

// ────────────────────────────────────────────────────────────────────────
// Database member functions — feature_flag_whitelist table
// ────────────────────────────────────────────────────────────────────────

bool Database::add_feature_flag_whitelist(const std::string& feature, uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* query = "INSERT IGNORE INTO feature_flag_whitelist (feature_name, guild_id) VALUES (?, ?)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("add_feature_flag_whitelist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)feature.c_str();
    bind[0].buffer_length = feature.size();

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;

    bool success = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    if (!success) {
        log_error("add_feature_flag_whitelist execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_feature_flag_whitelist(const std::string& feature, uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM feature_flag_whitelist WHERE feature_name = ? AND guild_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("remove_feature_flag_whitelist prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)feature.c_str();
    bind[0].buffer_length = feature.size();

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;

    bool success = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    if (!success) {
        log_error("remove_feature_flag_whitelist execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::pair<std::string, uint64_t>> Database::get_all_feature_flag_whitelists() {
    auto conn = pool_->acquire();
    std::vector<std::pair<std::string, uint64_t>> result;
    const char* query = "SELECT feature_name, guild_id FROM feature_flag_whitelist";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_all_feature_flag_whitelists prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_feature_flag_whitelists execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    char name_buf[256]; unsigned long name_len = 0;
    uint64_t guild_id;

    MYSQL_BIND res_bind[2];
    memset(res_bind, 0, sizeof(res_bind));

    res_bind[0].buffer_type = MYSQL_TYPE_STRING;
    res_bind[0].buffer = name_buf;
    res_bind[0].buffer_length = sizeof(name_buf);
    res_bind[0].length = &name_len;

    res_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    res_bind[1].buffer = (char*)&guild_id;
    res_bind[1].is_unsigned = 1;

    mysql_stmt_bind_result(stmt, res_bind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        result.emplace_back(std::string(name_buf, name_len), guild_id);
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ────────────────────────────────────────────────────────────────────────
// Namespace wrapper functions
// ────────────────────────────────────────────────────────────────────────

namespace feature_flag_operations {

    bool set_feature_flag(Database* db, const std::string& feature, const std::string& mode, const std::string& reason) {
        if (!db) return false;
        return db->set_feature_flag(feature, mode, reason);
    }

    std::vector<std::tuple<std::string, std::string, std::string>> get_all_feature_flags(Database* db) {
        if (!db) return {};
        return db->get_all_feature_flags();
    }

    bool delete_feature_flag(Database* db, const std::string& feature) {
        if (!db) return false;
        return db->delete_feature_flag(feature);
    }

    bool add_whitelist(Database* db, const std::string& feature, uint64_t guild_id) {
        if (!db) return false;
        return db->add_feature_flag_whitelist(feature, guild_id);
    }

    bool remove_whitelist(Database* db, const std::string& feature, uint64_t guild_id) {
        if (!db) return false;
        return db->remove_feature_flag_whitelist(feature, guild_id);
    }

    std::vector<std::pair<std::string, uint64_t>> get_all_whitelists(Database* db) {
        if (!db) return {};
        return db->get_all_feature_flag_whitelists();
    }

} // namespace feature_flag_operations
} // namespace db
} // namespace bronx
