#include "xp_blacklist_operations.h"
#include "../../core/database.h"
#include <algorithm>
#include <cstring>

namespace bronx {
namespace db {
namespace xp_blacklist_operations {

// Helper: convert string to XPBlacklistTargetType enum
static XPBlacklistTargetType parse_target_type(const std::string& s) {
    if (s == "role")    return XPBlacklistTargetType::Role;
    if (s == "user")    return XPBlacklistTargetType::User;
    return XPBlacklistTargetType::Channel; // default
}

// ============================================================================
// ADD BLACKLIST
// ============================================================================

bool add_blacklist(Database* db, uint64_t guild_id, const std::string& target_type,
                   uint64_t target_id, uint64_t added_by, const std::string& reason) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "INSERT INTO guild_xp_blacklist (guild_id, target_type, target_id, added_by, reason) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE reason=VALUES(reason), added_by=VALUES(added_by)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)target_type.c_str();
    bind[1].buffer_length = target_type.size();

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&target_id;
    bind[2].is_unsigned = 1;

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&added_by;
    bind[3].is_unsigned = 1;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)reason.c_str();
    bind[4].buffer_length = reason.size();

    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

// ============================================================================
// REMOVE BLACKLIST
// ============================================================================

bool remove_blacklist(Database* db, uint64_t guild_id, const std::string& target_type, uint64_t target_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "DELETE FROM guild_xp_blacklist WHERE guild_id = ? AND target_type = ? AND target_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)target_type.c_str();
    bind[1].buffer_length = target_type.size();

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&target_id;
    bind[2].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

// ============================================================================
// IS BLACKLISTED
// ============================================================================

bool is_blacklisted(Database* db, uint64_t guild_id, const std::string& target_type, uint64_t target_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "SELECT 1 FROM guild_xp_blacklist WHERE guild_id = ? AND target_type = ? AND target_id = ? LIMIT 1";
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

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)target_type.c_str();
    bind[1].buffer_length = target_type.size();

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&target_id;
    bind[2].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND resbind[1];
    memset(resbind, 0, sizeof(resbind));

    uint64_t one = 0;
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &one;
    resbind[0].is_unsigned = 1;

    mysql_stmt_bind_result(stmt, resbind);
    bool found = (mysql_stmt_fetch(stmt) == 0);

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return found;
}

// ============================================================================
// GET BLACKLIST
// ============================================================================

std::vector<XPBlacklistEntry> get_blacklist(Database* db, uint64_t guild_id, const std::string& target_type) {
    std::vector<XPBlacklistEntry> result;
    auto conn = db->get_pool()->acquire();
    if (!conn) return result;

    // Build query — optionally filter by target_type
    std::string query_str;
    if (target_type.empty()) {
        query_str = "SELECT id, guild_id, target_type, target_id, added_by, reason, UNIX_TIMESTAMP(created_at) "
                    "FROM guild_xp_blacklist WHERE guild_id = ? ORDER BY created_at DESC";
    } else {
        query_str = "SELECT id, guild_id, target_type, target_id, added_by, reason, UNIX_TIMESTAMP(created_at) "
                    "FROM guild_xp_blacklist WHERE guild_id = ? AND target_type = ? ORDER BY created_at DESC";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query_str.c_str(), query_str.size()) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }

    // Bind params
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)target_type.c_str();
    bind[1].buffer_length = target_type.size();

    int param_count = target_type.empty() ? 1 : 2;
    // mysql_stmt_bind_param needs the full array up to param_count
    // but we always pass the array; MySQL will only use param_count params
    // Actually we must only bind exactly the number of params the query has:
    if (param_count == 1) {
        mysql_stmt_bind_param(stmt, bind);  // only bind[0] used
    } else {
        mysql_stmt_bind_param(stmt, bind);
    }

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return result;
    }

    // Bind result columns: id, guild_id, target_type, target_id, added_by, reason, timestamp
    MYSQL_BIND resbind[7];
    memset(resbind, 0, sizeof(resbind));

    uint64_t id, gid, tid, added_by;
    char tt_buf[16];
    unsigned long tt_len;
    char reason_buf[256];
    unsigned long reason_len;
    my_bool reason_null;
    uint64_t timestamp;

    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &id;
    resbind[0].is_unsigned = 1;

    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[1].is_unsigned = 1;

    resbind[2].buffer_type = MYSQL_TYPE_STRING;
    resbind[2].buffer = tt_buf;
    resbind[2].buffer_length = sizeof(tt_buf);
    resbind[2].length = &tt_len;

    resbind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[3].buffer = &tid;
    resbind[3].is_unsigned = 1;

    resbind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[4].buffer = &added_by;
    resbind[4].is_unsigned = 1;

    resbind[5].buffer_type = MYSQL_TYPE_STRING;
    resbind[5].buffer = reason_buf;
    resbind[5].buffer_length = sizeof(reason_buf);
    resbind[5].length = &reason_len;
    resbind[5].is_null = &reason_null;

    resbind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[6].buffer = &timestamp;
    resbind[6].is_unsigned = 1;

    mysql_stmt_bind_result(stmt, resbind);

    while (mysql_stmt_fetch(stmt) == 0) {
        XPBlacklistEntry entry;
        entry.id = id;
        entry.guild_id = gid;
        entry.target_type = parse_target_type(std::string(tt_buf, tt_len));
        entry.target_id = tid;
        entry.added_by = added_by;
        entry.reason = reason_null ? "" : std::string(reason_buf, reason_len);
        entry.created_at = std::chrono::system_clock::from_time_t(timestamp);
        result.push_back(entry);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

// ============================================================================
// SHOULD BLOCK XP  (single query, check in C++)
// ============================================================================

bool should_block_xp(Database* db, uint64_t guild_id, uint64_t channel_id,
                     uint64_t user_id, const std::vector<uint64_t>& user_role_ids) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* query = "SELECT target_type, target_id FROM guild_xp_blacklist WHERE guild_id = ?";
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

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND resbind[2];
    memset(resbind, 0, sizeof(resbind));

    char tt_buf[16];
    unsigned long tt_len;
    uint64_t tid;

    resbind[0].buffer_type = MYSQL_TYPE_STRING;
    resbind[0].buffer = tt_buf;
    resbind[0].buffer_length = sizeof(tt_buf);
    resbind[0].length = &tt_len;

    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &tid;
    resbind[1].is_unsigned = 1;

    mysql_stmt_bind_result(stmt, resbind);

    bool blocked = false;
    while (mysql_stmt_fetch(stmt) == 0) {
        std::string tt(tt_buf, tt_len);

        if (tt == "channel" && tid == channel_id) {
            blocked = true;
            break;
        }
        if (tt == "user" && tid == user_id) {
            blocked = true;
            break;
        }
        if (tt == "role") {
            if (std::find(user_role_ids.begin(), user_role_ids.end(), tid) != user_role_ids.end()) {
                blocked = true;
                break;
            }
        }
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return blocked;
}

} // namespace xp_blacklist_operations
} // namespace db
} // namespace bronx
