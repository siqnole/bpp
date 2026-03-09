#include "reaction_role_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

bool Database::add_reaction_role(uint64_t message_id, uint64_t channel_id, const std::string& emoji_raw, uint64_t emoji_id, uint64_t role_id) {
    // normalize the emoji string here as a safety net so callers don't need to
    // remember. strips surrounding <> and a leading colon if present.
    std::string norm = emoji_raw;
    if (norm.size() >= 2 && norm.front() == '<' && norm.back() == '>') {
        norm = norm.substr(1, norm.size() - 2);
    }
    if (!norm.empty() && norm.front() == ':') {
        norm.erase(0, 1);
    }

    auto conn = pool_->acquire();

    const char* query = "INSERT INTO reaction_roles (message_id, channel_id, emoji_raw, emoji_id, role_id) VALUES (?, ?, ?, ?, ?)"
                        " ON DUPLICATE KEY UPDATE channel_id = VALUES(channel_id), emoji_id = VALUES(emoji_id), role_id = VALUES(role_id)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_reaction_role prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&message_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&channel_id;
    bind[1].is_unsigned = 1;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)norm.c_str();
    bind[2].buffer_length = norm.length();

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&emoji_id;
    bind[3].is_unsigned = 1;

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&role_id;
    bind[4].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_reaction_role execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_reaction_role(uint64_t message_id, const std::string& emoji_raw) {
    auto conn = pool_->acquire();

    const char* query = "DELETE FROM reaction_roles WHERE message_id = ? AND emoji_raw = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_reaction_role prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&message_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)emoji_raw.c_str();
    bind[1].buffer_length = emoji_raw.length();

    mysql_stmt_bind_param(stmt, bind);

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_reaction_role execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<ReactionRoleRow> Database::get_all_reaction_roles() {
    std::vector<ReactionRoleRow> out;
    auto conn = pool_->acquire();

    const char* query = "SELECT message_id, channel_id, emoji_raw, emoji_id, role_id FROM reaction_roles";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_reaction_roles prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_reaction_roles execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    // result buffers
    uint64_t message_id;
    uint64_t channel_id;
    char emoji_buf[256]; unsigned long emoji_len = 0;
    uint64_t emoji_id;
    uint64_t role_id;

    MYSQL_BIND result_bind[5];
    memset(result_bind, 0, sizeof(result_bind));

    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&message_id;
    result_bind[0].is_unsigned = 1;

    result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[1].buffer = (char*)&channel_id;
    result_bind[1].is_unsigned = 1;

    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = emoji_buf;
    result_bind[2].buffer_length = sizeof(emoji_buf);
    result_bind[2].length = &emoji_len;

    result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[3].buffer = (char*)&emoji_id;
    result_bind[3].is_unsigned = 1;

    result_bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[4].buffer = (char*)&role_id;
    result_bind[4].is_unsigned = 1;

    mysql_stmt_bind_result(stmt, result_bind);

    while (mysql_stmt_fetch(stmt) == 0) {
        ReactionRoleRow r;
        r.message_id = message_id;
        r.channel_id = channel_id;
        r.emoji_raw = std::string(emoji_buf, emoji_len);
        r.emoji_id = emoji_id;
        r.role_id = role_id;
        out.push_back(r);
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
namespace reaction_role_operations {
    bool add_reaction_role(Database* db, uint64_t message_id, uint64_t channel_id, const std::string& emoji_raw, uint64_t emoji_id, uint64_t role_id) {
        return db->add_reaction_role(message_id, channel_id, emoji_raw, emoji_id, role_id);
    }
    bool remove_reaction_role(Database* db, uint64_t message_id, const std::string& emoji_raw) {
        return db->remove_reaction_role(message_id, emoji_raw);
    }
    std::vector<ReactionRoleRow> get_all_reaction_roles(Database* db) {
        return db->get_all_reaction_roles();
    }
}
} // namespace db
} // namespace bronx