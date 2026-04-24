#include "../core/database.h"
#include <mariadb/mysql.h>
#include <cstring>

namespace bronx {
namespace db {

bool Database::update_heartbeat(int shard_id, uint64_t uptime_seconds, uint64_t memory_usage_mb, int guild_count, const std::string& status) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO bot_heartbeats (shard_id, uptime_seconds, memory_usage_mb, guild_count, status) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE "
                        "last_heartbeat = CURRENT_TIMESTAMP, "
                        "uptime_seconds = VALUES(uptime_seconds), "
                        "memory_usage_mb = VALUES(memory_usage_mb), "
                        "guild_count = VALUES(guild_count), "
                        "status = VALUES(status)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    // shard_id
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&shard_id;

    // uptime_seconds
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&uptime_seconds;
    bind[1].is_unsigned = 1;

    // memory_usage_mb
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&memory_usage_mb;
    bind[2].is_unsigned = 1;

    // guild_count
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&guild_count;
    bind[3].is_unsigned = 1;

    // status
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)status.c_str();
    bind[4].buffer_length = status.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return true;
}

} // namespace db
} // namespace bronx
