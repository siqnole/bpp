#include "autopurge_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

uint64_t Database::add_autopurge(uint64_t user_id, uint64_t guild_id, uint64_t channel_id,
                                 int interval_seconds, int message_limit,
                                 uint64_t target_user_id, uint64_t target_role_id) {
    auto conn = pool_->acquire();

    const char* query = "INSERT INTO autopurges (user_id, guild_id, channel_id, interval_seconds, message_limit, target_user_id, target_role_id)"
                        " VALUES (?, ?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_autopurge prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&channel_id;
    bind[2].is_unsigned = 1;

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&interval_seconds;
    bind[3].is_unsigned = 0;

    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer = (char*)&message_limit;
    bind[4].is_unsigned = 0;

    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&target_user_id;
    bind[5].is_unsigned = 1;

    bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[6].buffer = (char*)&target_role_id;
    bind[6].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);

    bool success = (mysql_stmt_execute(stmt) == 0);
    uint64_t rowid = 0;
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_autopurge execute");
    } else {
        rowid = mysql_stmt_insert_id(stmt);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return rowid;
}

bool Database::remove_autopurge(uint64_t autopurge_id, uint64_t user_id) {
    auto conn = pool_->acquire();

    const char* query = "DELETE FROM autopurges WHERE id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_autopurge prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&autopurge_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_autopurge execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<AutopurgeRow> Database::get_all_autopurges() {
    std::vector<AutopurgeRow> out;
    auto conn = pool_->acquire();

    // attempt to select new columns; if that fails, retry without them
    const char* query = "SELECT id, user_id, guild_id, channel_id, interval_seconds, message_limit, target_user_id, target_role_id FROM autopurges";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        std::string err = mysql_stmt_error(stmt);
        // if missing column error, retry simpler query
        if (err.find("Unknown column") != std::string::npos) {
            mysql_stmt_close(stmt);
            query = "SELECT id, user_id, guild_id, channel_id, interval_seconds, message_limit FROM autopurges";
            stmt = mysql_stmt_init(conn->get());
            if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
                last_error_ = mysql_stmt_error(stmt);
                log_error("get_all_autopurges prepare");
                mysql_stmt_close(stmt);
                pool_->release(conn);
                return out;
            }
        } else {
            last_error_ = err;
            log_error("get_all_autopurges prepare");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return out;
        }
    }

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_autopurges execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    uint64_t id;
    uint64_t user_id;
    uint64_t guild_id;
    uint64_t channel_id;
    int interval_seconds;
    int message_limit;
    uint64_t target_user_id = 0;
    uint64_t target_role_id = 0;

    bool include_targets = (strstr(query, "target_user_id") != nullptr);
    if (include_targets) {
        MYSQL_BIND result_bind[8];
        memset(result_bind, 0, sizeof(result_bind));

        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = (char*)&id;
        result_bind[0].is_unsigned = 1;

        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = (char*)&user_id;
        result_bind[1].is_unsigned = 1;

        result_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[2].buffer = (char*)&guild_id;
        result_bind[2].is_unsigned = 1;

        result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[3].buffer = (char*)&channel_id;
        result_bind[3].is_unsigned = 1;

        result_bind[4].buffer_type = MYSQL_TYPE_LONG;
        result_bind[4].buffer = (char*)&interval_seconds;
        result_bind[4].is_unsigned = 0;

        result_bind[5].buffer_type = MYSQL_TYPE_LONG;
        result_bind[5].buffer = (char*)&message_limit;
        result_bind[5].is_unsigned = 0;

        result_bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[6].buffer = (char*)&target_user_id;
        result_bind[6].is_unsigned = 1;

        result_bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[7].buffer = (char*)&target_role_id;
        result_bind[7].is_unsigned = 1;

        mysql_stmt_bind_result(stmt, result_bind);

        while (mysql_stmt_fetch(stmt) == 0) {
            AutopurgeRow r;
            r.id = id;
            r.user_id = user_id;
            r.guild_id = guild_id;
            r.channel_id = channel_id;
            r.interval_seconds = interval_seconds;
            r.message_limit = message_limit;
            r.target_user_id = target_user_id;
            r.target_role_id = target_role_id;
            out.push_back(r);
        }
    } else {
        MYSQL_BIND result_bind[6];
        memset(result_bind, 0, sizeof(result_bind));

        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = (char*)&id;
        result_bind[0].is_unsigned = 1;

        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = (char*)&user_id;
        result_bind[1].is_unsigned = 1;

        result_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[2].buffer = (char*)&guild_id;
        result_bind[2].is_unsigned = 1;

        result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[3].buffer = (char*)&channel_id;
        result_bind[3].is_unsigned = 1;

        result_bind[4].buffer_type = MYSQL_TYPE_LONG;
        result_bind[4].buffer = (char*)&interval_seconds;
        result_bind[4].is_unsigned = 0;

        result_bind[5].buffer_type = MYSQL_TYPE_LONG;
        result_bind[5].buffer = (char*)&message_limit;
        result_bind[5].is_unsigned = 0;

        mysql_stmt_bind_result(stmt, result_bind);

        while (mysql_stmt_fetch(stmt) == 0) {
            AutopurgeRow r;
            r.id = id;
            r.user_id = user_id;
            r.guild_id = guild_id;
            r.channel_id = channel_id;
            r.interval_seconds = interval_seconds;
            r.message_limit = message_limit;
            out.push_back(r);
        }
    }


    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<AutopurgeRow> Database::get_autopurges_for_user(uint64_t user_id_arg) {
    std::vector<AutopurgeRow> out;
    auto conn = pool_->acquire();

    const char* query = "SELECT id, user_id, guild_id, channel_id, interval_seconds, message_limit, target_user_id, target_role_id "
                        "FROM autopurges WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        std::string err = mysql_stmt_error(stmt);
        if (err.find("Unknown column") != std::string::npos) {
            // retry without the new columns
            mysql_stmt_close(stmt);
            query = "SELECT id, user_id, guild_id, channel_id, interval_seconds, message_limit "
                    "FROM autopurges WHERE user_id = ?";
            stmt = mysql_stmt_init(conn->get());
            if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
                last_error_ = mysql_stmt_error(stmt);
                log_error("get_autopurges_for_user prepare");
                mysql_stmt_close(stmt);
                pool_->release(conn);
                return out;
            }
        } else {
            last_error_ = err;
            log_error("get_autopurges_for_user prepare");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return out;
        }
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id_arg;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_autopurges_for_user execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }

    uint64_t id;
    uint64_t user_id;
    uint64_t guild_id;
    uint64_t channel_id;
    int interval_seconds;
    int message_limit;
    uint64_t target_user_id;
    uint64_t target_role_id;

    MYSQL_BIND result_bind[8];
    memset(result_bind, 0, sizeof(result_bind));

    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&id;
    result_bind[0].is_unsigned = 1;

    result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[1].buffer = (char*)&user_id;
    result_bind[1].is_unsigned = 1;

    result_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[2].buffer = (char*)&guild_id;
    result_bind[2].is_unsigned = 1;

    result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[3].buffer = (char*)&channel_id;
    result_bind[3].is_unsigned = 1;

    result_bind[4].buffer_type = MYSQL_TYPE_LONG;
    result_bind[4].buffer = (char*)&interval_seconds;
    result_bind[4].is_unsigned = 0;

    result_bind[5].buffer_type = MYSQL_TYPE_LONG;
    result_bind[5].buffer = (char*)&message_limit;
    result_bind[5].is_unsigned = 0;

    mysql_stmt_bind_result(stmt, result_bind);

    // include target columns in binding
    result_bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[6].buffer = (char*)&target_user_id;
    result_bind[6].is_unsigned = 1;

    result_bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[7].buffer = (char*)&target_role_id;
    result_bind[7].is_unsigned = 1;
    while (mysql_stmt_fetch(stmt) == 0) {
        AutopurgeRow r;
        r.target_user_id = target_user_id;
        r.target_role_id = target_role_id;
        r.id = id;
        r.user_id = user_id;
        r.guild_id = guild_id;
        r.channel_id = channel_id;
        r.interval_seconds = interval_seconds;
        r.message_limit = message_limit;
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
namespace autopurge_operations {
    uint64_t add_autopurge(Database* db, uint64_t user_id, uint64_t guild_id, uint64_t channel_id,
                           int interval_seconds, int message_limit) {
        return db->add_autopurge(user_id, guild_id, channel_id, interval_seconds, message_limit);
    }
    bool remove_autopurge(Database* db, uint64_t autopurge_id, uint64_t user_id) {
        return db->remove_autopurge(autopurge_id, user_id);
    }
    std::vector<AutopurgeRow> get_all_autopurges(Database* db) {
        return db->get_all_autopurges();
    }
    std::vector<AutopurgeRow> get_autopurges_for_user(Database* db, uint64_t user_id) {
        return db->get_autopurges_for_user(user_id);
    }
}
} // namespace db
} // namespace bronx
