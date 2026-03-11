#include "xp_blacklist_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {
namespace xp_blacklist_operations {

// ============================================================================
// CHANNEL BLACKLIST
// ============================================================================

bool add_channel_blacklist(Database* db, uint64_t guild_id, uint64_t channel_id, uint64_t added_by, const std::string& reason) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO xp_blacklist_channels (guild_id, channel_id, added_by, reason) VALUES (?, ?, ?, ?) "
                       "ON DUPLICATE KEY UPDATE reason=VALUES(reason)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&channel_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&added_by;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)reason.c_str();
    bind[3].buffer_length = reason.size();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool remove_channel_blacklist(Database* db, uint64_t guild_id, uint64_t channel_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM xp_blacklist_channels WHERE guild_id = ? AND channel_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&channel_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool is_channel_blacklisted(Database* db, uint64_t guild_id, uint64_t channel_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "SELECT COUNT(*) FROM xp_blacklist_channels WHERE guild_id = ? AND channel_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        db->get_pool()->release(conn);
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&channel_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND resbind[1];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t count = 0;
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &count;
    resbind[0].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return count > 0;
}

std::vector<XPBlacklistChannel> get_blacklisted_channels(Database* db, uint64_t guild_id) {
    std::vector<XPBlacklistChannel> result;
    auto conn = db->get_pool()->acquire();
    if (!conn) return result;
    
    const char* query = "SELECT id, guild_id, channel_id, added_by, reason, UNIX_TIMESTAMP(created_at) "
                       "FROM xp_blacklist_channels WHERE guild_id = ? ORDER BY created_at DESC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND resbind[6];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t id, gid, cid, added_by;
    char reason[256];
    unsigned long reason_len;
    my_bool reason_null;
    uint64_t timestamp;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &id;
    resbind[0].is_unsigned = 1;
    
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[1].is_unsigned = 1;
    
    resbind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[2].buffer = &cid;
    resbind[2].is_unsigned = 1;
    
    resbind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[3].buffer = &added_by;
    resbind[3].is_unsigned = 1;
    
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = reason;
    resbind[4].buffer_length = sizeof(reason);
    resbind[4].length = &reason_len;
    resbind[4].is_null = &reason_null;
    
    resbind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[5].buffer = &timestamp;
    resbind[5].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        XPBlacklistChannel entry;
        entry.id = id;
        entry.guild_id = gid;
        entry.channel_id = cid;
        entry.added_by = added_by;
        entry.reason = reason_null ? "" : std::string(reason, reason_len);
        entry.created_at = std::chrono::system_clock::from_time_t(timestamp);
        result.push_back(entry);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

// ============================================================================
// ROLE BLACKLIST
// ============================================================================

bool add_role_blacklist(Database* db, uint64_t guild_id, uint64_t role_id, uint64_t added_by, const std::string& reason) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO xp_blacklist_roles (guild_id, role_id, added_by, reason) VALUES (?, ?, ?, ?) "
                       "ON DUPLICATE KEY UPDATE reason=VALUES(reason)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&role_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&added_by;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)reason.c_str();
    bind[3].buffer_length = reason.size();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool remove_role_blacklist(Database* db, uint64_t guild_id, uint64_t role_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM xp_blacklist_roles WHERE guild_id = ? AND role_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&role_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool is_role_blacklisted(Database* db, uint64_t guild_id, uint64_t role_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "SELECT COUNT(*) FROM xp_blacklist_roles WHERE guild_id = ? AND role_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        db->get_pool()->release(conn);
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&role_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND resbind[1];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t count = 0;
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &count;
    resbind[0].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return count > 0;
}

std::vector<XPBlacklistRole> get_blacklisted_roles(Database* db, uint64_t guild_id) {
    std::vector<XPBlacklistRole> result;
    auto conn = db->get_pool()->acquire();
    if (!conn) return result;
    
    const char* query = "SELECT id, guild_id, role_id, added_by, reason, UNIX_TIMESTAMP(created_at) "
                       "FROM xp_blacklist_roles WHERE guild_id = ? ORDER BY created_at DESC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND resbind[6];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t id, gid, rid, added_by;
    char reason[256];
    unsigned long reason_len;
    my_bool reason_null;
    uint64_t timestamp;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &id;
    resbind[0].is_unsigned = 1;
    
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[1].is_unsigned = 1;
    
    resbind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[2].buffer = &rid;
    resbind[2].is_unsigned = 1;
    
    resbind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[3].buffer = &added_by;
    resbind[3].is_unsigned = 1;
    
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = reason;
    resbind[4].buffer_length = sizeof(reason);
    resbind[4].length = &reason_len;
    resbind[4].is_null = &reason_null;
    
    resbind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[5].buffer = &timestamp;
    resbind[5].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        XPBlacklistRole entry;
        entry.id = id;
        entry.guild_id = gid;
        entry.role_id = rid;
        entry.added_by = added_by;
        entry.reason = reason_null ? "" : std::string(reason, reason_len);
        entry.created_at = std::chrono::system_clock::from_time_t(timestamp);
        result.push_back(entry);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

// ============================================================================
// USER BLACKLIST
// ============================================================================

bool add_user_blacklist(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t added_by, const std::string& reason) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO xp_blacklist_users (guild_id, user_id, added_by, reason) VALUES (?, ?, ?, ?) "
                       "ON DUPLICATE KEY UPDATE reason=VALUES(reason)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&added_by;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)reason.c_str();
    bind[3].buffer_length = reason.size();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool remove_user_blacklist(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM xp_blacklist_users WHERE guild_id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool is_user_blacklisted(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "SELECT COUNT(*) FROM xp_blacklist_users WHERE guild_id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        db->get_pool()->release(conn);
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND resbind[1];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t count = 0;
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &count;
    resbind[0].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return count > 0;
}

std::vector<XPBlacklistUser> get_blacklisted_users(Database* db, uint64_t guild_id) {
    std::vector<XPBlacklistUser> result;
    auto conn = db->get_pool()->acquire();
    if (!conn) return result;
    
    const char* query = "SELECT id, guild_id, user_id, added_by, reason, UNIX_TIMESTAMP(created_at) "
                       "FROM xp_blacklist_users WHERE guild_id = ? ORDER BY created_at DESC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }
    
    MYSQL_BIND resbind[6];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t id, gid, uid, added_by;
    char reason[256];
    unsigned long reason_len;
    my_bool reason_null;
    uint64_t timestamp;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &id;
    resbind[0].is_unsigned = 1;
    
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[1].is_unsigned = 1;
    
    resbind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[2].buffer = &uid;
    resbind[2].is_unsigned = 1;
    
    resbind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[3].buffer = &added_by;
    resbind[3].is_unsigned = 1;
    
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = reason;
    resbind[4].buffer_length = sizeof(reason);
    resbind[4].length = &reason_len;
    resbind[4].is_null = &reason_null;
    
    resbind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[5].buffer = &timestamp;
    resbind[5].is_unsigned = 1;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        XPBlacklistUser entry;
        entry.id = id;
        entry.guild_id = gid;
        entry.user_id = uid;
        entry.added_by = added_by;
        entry.reason = reason_null ? "" : std::string(reason, reason_len);
        entry.created_at = std::chrono::system_clock::from_time_t(timestamp);
        result.push_back(entry);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

// ============================================================================
// COMBINED CHECK
// ============================================================================

bool should_block_xp(Database* db, uint64_t guild_id, uint64_t channel_id, uint64_t user_id, const std::vector<uint64_t>& user_role_ids) {
    // Check channel blacklist
    if (is_channel_blacklisted(db, guild_id, channel_id)) {
        return true;
    }
    
    // Check user blacklist
    if (is_user_blacklisted(db, guild_id, user_id)) {
        return true;
    }
    
    // Check role blacklist
    for (uint64_t role_id : user_role_ids) {
        if (is_role_blacklisted(db, guild_id, role_id)) {
            return true;
        }
    }
    
    return false;
}

} // namespace xp_blacklist_operations
} // namespace db
} // namespace bronx
