#include "modmail_operations.h"
#include "../../core/database.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ---------------------------------------------------------------------------
// get_modmail_config
// ---------------------------------------------------------------------------
std::optional<ModmailConfig> Database::get_modmail_config(uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT category_id, staff_role_id, log_channel_id, enabled "
                       "FROM guild_modmail_config WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_in[0].buffer = &guild_id;
    bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t category_id, staff_role_id, log_channel_id;
    int8_t enabled;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));

    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[0].buffer = &category_id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[1].buffer = &staff_role_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_out[2].buffer = &log_channel_id;
    bind_out[3].buffer_type = MYSQL_TYPE_TINY;
    bind_out[3].buffer = &enabled;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailConfig> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailConfig config;
        config.guild_id = guild_id;
        config.category_id = category_id;
        config.staff_role_id = staff_role_id;
        config.log_channel_id = log_channel_id;
        config.enabled = (enabled != 0);
        result = config;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// set_modmail_config
// ---------------------------------------------------------------------------
bool Database::set_modmail_config(const ModmailConfig& config) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO guild_modmail_config (guild_id, category_id, staff_role_id, log_channel_id, enabled) "
                       "VALUES (?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE "
                       "category_id = VALUES(category_id), staff_role_id = VALUES(staff_role_id), "
                       "log_channel_id = VALUES(log_channel_id), enabled = VALUES(enabled)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    uint64_t gid = config.guild_id;
    uint64_t cid = config.category_id;
    uint64_t srid = config.staff_role_id;
    uint64_t lcid = config.log_channel_id;
    int8_t en = config.enabled ? 1 : 0;

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &gid; bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &cid; bind[1].is_unsigned = true;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG; bind[2].buffer = &srid; bind[2].is_unsigned = true;
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG; bind[3].buffer = &lcid; bind[3].is_unsigned = true;
    bind[4].buffer_type = MYSQL_TYPE_TINY;     bind[4].buffer = &en;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// get_any_active_modmail_thread
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_any_active_modmail_thread(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, guild_id, thread_id, created_at FROM modmail_threads "
                       "WHERE user_id = ? AND status = 'open' LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &user_id; bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, guild_id, thread_id;
    MYSQL_TIME created_at;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &guild_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[2].buffer = &thread_id;
    bind_out[3].buffer_type = MYSQL_TYPE_TIMESTAMP; bind_out[3].buffer = &created_at;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = "open";
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// get_active_modmail_thread_by_user
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_active_modmail_thread_by_user(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, thread_id, created_at FROM modmail_threads "
                       "WHERE user_id = ? AND guild_id = ? AND status = 'open' LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[2];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &user_id; bind_in[0].is_unsigned = true;
    bind_in[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[1].buffer = &guild_id; bind_in[1].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, thread_id;
    MYSQL_TIME created_at;
    MYSQL_BIND bind_out[3];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &thread_id;
    bind_out[2].buffer_type = MYSQL_TYPE_TIMESTAMP; bind_out[2].buffer = &created_at;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = "open";
        // Convert MYSQL_TIME to time_point if needed, but for now we skip complex conversions
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// get_modmail_thread_by_id
// ---------------------------------------------------------------------------
std::optional<ModmailThread> Database::get_modmail_thread_by_id(uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT id, guild_id, user_id, status FROM modmail_threads WHERE thread_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_in[0].buffer = &thread_id; bind_in[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind_in);
    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t id, guild_id, user_id;
    char status[20] = {0};
    unsigned long status_len;
    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));
    bind_out[0].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[0].buffer = &id;
    bind_out[1].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[1].buffer = &guild_id;
    bind_out[2].buffer_type = MYSQL_TYPE_LONGLONG; bind_out[2].buffer = &user_id;
    bind_out[3].buffer_type = MYSQL_TYPE_STRING;   bind_out[3].buffer = status; bind_out[3].buffer_length = sizeof(status); bind_out[3].length = &status_len;

    mysql_stmt_bind_result(stmt, bind_out);
    
    std::optional<ModmailThread> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ModmailThread thread;
        thread.id = id;
        thread.guild_id = guild_id;
        thread.user_id = user_id;
        thread.thread_id = thread_id;
        thread.status = std::string(status, status_len);
        result = thread;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ---------------------------------------------------------------------------
// create_modmail_thread
// ---------------------------------------------------------------------------
bool Database::create_modmail_thread(uint64_t guild_id, uint64_t user_id, uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO modmail_threads (guild_id, user_id, thread_id, status) VALUES (?, ?, ?, 'open')";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &guild_id;  bind[0].is_unsigned = true;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &user_id;   bind[1].is_unsigned = true;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG; bind[2].buffer = &thread_id; bind[2].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// close_modmail_thread
// ---------------------------------------------------------------------------
bool Database::close_modmail_thread(uint64_t thread_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;

    const char* query = "UPDATE modmail_threads SET status = 'closed', closed_at = CURRENT_TIMESTAMP WHERE thread_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query))) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &thread_id; bind[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ============================================================================
// Free-function wrappers
// ============================================================================

std::optional<ModmailConfig> get_modmail_config(Database* db, uint64_t guild_id) {
    return db->get_modmail_config(guild_id);
}

bool set_modmail_config(Database* db, const ModmailConfig& config) {
    return db->set_modmail_config(config);
}

std::optional<ModmailThread> get_any_active_modmail_thread(Database* db, uint64_t user_id) {
    return db->get_any_active_modmail_thread(user_id);
}

std::optional<ModmailThread> get_active_modmail_thread_by_user(Database* db, uint64_t user_id, uint64_t guild_id) {
    return db->get_active_modmail_thread_by_user(user_id, guild_id);
}

std::optional<ModmailThread> get_modmail_thread_by_id(Database* db, uint64_t thread_id) {
    return db->get_modmail_thread_by_id(thread_id);
}

bool create_modmail_thread(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t thread_id) {
    return db->create_modmail_thread(guild_id, user_id, thread_id);
}

bool close_modmail_thread(Database* db, uint64_t thread_id) {
    return db->close_modmail_thread(thread_id);
}

} // namespace db
} // namespace bronx
