#include "prefix_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

// --------------------------------------------------------------------------
// user prefixes
// --------------------------------------------------------------------------

bool Database::add_user_prefix(uint64_t user_id, const std::string& prefix) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    const char* query = "INSERT IGNORE INTO user_prefixes (user_id, prefix) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_user_prefix prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)prefix.c_str();
    bind[1].buffer_length = prefix.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_user_prefix execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_user_prefix(uint64_t user_id, const std::string& prefix) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM user_prefixes WHERE user_id = ? AND prefix = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_user_prefix prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)prefix.c_str();
    bind[1].buffer_length = prefix.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_user_prefix execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::string> Database::get_user_prefixes(uint64_t user_id) {
    std::vector<std::string> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT prefix FROM user_prefixes WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_user_prefixes prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_user_prefixes execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char buf[64]; unsigned long len = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = buf;
    result_bind[0].buffer_length = sizeof(buf);
    result_bind[0].length = &len;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(buf, len);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// --------------------------------------------------------------------------
// guild prefixes
// --------------------------------------------------------------------------

bool Database::add_guild_prefix(uint64_t guild_id, const std::string& prefix) {
    auto conn = pool_->acquire();
    const char* query = "INSERT IGNORE INTO guild_prefixes (guild_id, prefix) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_guild_prefix prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)prefix.c_str();
    bind[1].buffer_length = prefix.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_guild_prefix execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_guild_prefix(uint64_t guild_id, const std::string& prefix) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM guild_prefixes WHERE guild_id = ? AND prefix = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_guild_prefix prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)prefix.c_str();
    bind[1].buffer_length = prefix.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_guild_prefix execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::string> Database::get_guild_prefixes(uint64_t guild_id) {
    std::vector<std::string> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT prefix FROM guild_prefixes WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_guild_prefixes prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_guild_prefixes execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char buf[64]; unsigned long len = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = buf;
    result_bind[0].buffer_length = sizeof(buf);
    result_bind[0].length = &len;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(buf, len);
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
namespace prefix_operations {
    bool add_user_prefix(Database* db, uint64_t user_id, const std::string& prefix) {
        return db->add_user_prefix(user_id, prefix);
    }
    bool remove_user_prefix(Database* db, uint64_t user_id, const std::string& prefix) {
        return db->remove_user_prefix(user_id, prefix);
    }
    std::vector<std::string> get_user_prefixes(Database* db, uint64_t user_id) {
        return db->get_user_prefixes(user_id);
    }
    bool add_guild_prefix(Database* db, uint64_t guild_id, const std::string& prefix) {
        return db->add_guild_prefix(guild_id, prefix);
    }
    bool remove_guild_prefix(Database* db, uint64_t guild_id, const std::string& prefix) {
        return db->remove_guild_prefix(guild_id, prefix);
    }
    std::vector<std::string> get_guild_prefixes(Database* db, uint64_t guild_id) {
        return db->get_guild_prefixes(guild_id);
    }
}
}
}
