#include "raid_operations.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <cstring>

namespace bronx {
namespace db {
namespace raid_operations {

bool initialize_table(Database* db) {
    const char* query = 
        "CREATE TABLE IF NOT EXISTS raid_settings ("
        "guild_id BIGINT UNSIGNED PRIMARY KEY,"
        "join_gate_level INT DEFAULT 0,"
        "min_account_age_days INT DEFAULT 1,"
        "join_velocity_threshold INT DEFAULT 10,"
        "notify_channel_id BIGINT UNSIGNED,"
        "alert_on_raid BOOLEAN DEFAULT TRUE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    
    return db->execute(query);
}

RaidSettings get_settings(Database* db, uint64_t guild_id) {
    RaidSettings settings;
    settings.guild_id = guild_id;

    auto conn = db->get_pool()->acquire();
    if (!conn) return settings;

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) return settings;

    const char* query = "SELECT join_gate_level, min_account_age_days, join_velocity_threshold, notify_channel_id, alert_on_raid FROM raid_settings WHERE guild_id = ?";
    
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        mysql_stmt_close(stmt);
        return settings;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;

    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt)) {
        mysql_stmt_close(stmt);
        return settings;
    }

    int gate_level, age_days, velocity;
    unsigned long long notify_ch;
    signed char alert;
    my_bool is_null[5];

    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &gate_level;
    result[0].is_null = &is_null[0];

    result[1].buffer_type = MYSQL_TYPE_LONG;
    result[1].buffer = &age_days;
    result[1].is_null = &is_null[1];

    result[2].buffer_type = MYSQL_TYPE_LONG;
    result[2].buffer = &velocity;
    result[2].is_null = &is_null[2];

    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &notify_ch;
    result[3].is_null = &is_null[3];
    result[3].is_unsigned = true;

    result[4].buffer_type = MYSQL_TYPE_TINY;
    result[4].buffer = &alert;
    result[4].is_null = &is_null[4];

    mysql_stmt_bind_result(stmt, result);

    if (!mysql_stmt_fetch(stmt)) {
        settings.join_gate_level = static_cast<JoinGateLevel>(gate_level);
        settings.min_account_age_days = age_days;
        settings.join_velocity_threshold = velocity;
        if (!is_null[3]) settings.notify_channel_id = notify_ch;
        settings.alert_on_raid = (alert != 0);
    } else {
        // Not found, insert default
        mysql_stmt_close(stmt);
        update_settings(db, settings);
        return settings;
    }

    mysql_stmt_close(stmt);
    return settings;
}

bool update_settings(Database* db, const RaidSettings& settings) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) return false;

    const char* query = 
        "INSERT INTO raid_settings (guild_id, join_gate_level, min_account_age_days, join_velocity_threshold, notify_channel_id, alert_on_raid) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE "
        "join_gate_level = VALUES(join_gate_level), "
        "min_account_age_days = VALUES(min_account_age_days), "
        "join_velocity_threshold = VALUES(join_velocity_threshold), "
        "notify_channel_id = VALUES(notify_channel_id), "
        "alert_on_raid = VALUES(alert_on_raid)";

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        mysql_stmt_close(stmt);
        return false;
    }

    int gate_level = static_cast<int>(settings.join_gate_level);
    unsigned long long guild_id = settings.guild_id;
    int age_days = settings.min_account_age_days;
    int velocity = settings.join_velocity_threshold;
    unsigned long long notify_ch = settings.notify_channel_id.value_or(0);
    bool has_notify = settings.notify_channel_id.has_value();
    signed char alert = settings.alert_on_raid ? 1 : 0;
    my_bool is_null_notify = !has_notify;

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &gate_level;

    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &age_days;

    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = &velocity;

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = &notify_ch;
    bind[4].is_null = &is_null_notify;
    bind[4].is_unsigned = true;

    bind[5].buffer_type = MYSQL_TYPE_TINY;
    bind[5].buffer = &alert;

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return success;
}

bool set_gate_level(Database* db, uint64_t guild_id, JoinGateLevel level) {
    auto settings = get_settings(db, guild_id);
    settings.join_gate_level = level;
    return update_settings(db, settings);
}

bool set_notify_channel(Database* db, uint64_t guild_id, std::optional<uint64_t> channel_id) {
    auto settings = get_settings(db, guild_id);
    settings.notify_channel_id = channel_id;
    return update_settings(db, settings);
}

} // namespace raid_operations
} // namespace db
} // namespace bronx
