#include "skill_operations.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <cstring>

namespace bronx {
namespace db {
namespace skill_operations {

bool ensure_tables(Database* db) {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS user_skill_points ("
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  skill_id VARCHAR(64) NOT NULL,"
        "  rank INT NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (user_id, skill_id),"
        "  INDEX idx_user (user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    return db->execute(sql);
}

std::map<std::string, int> get_user_skills(Database* db, uint64_t user_id) {
    std::map<std::string, int> skills;
    auto conn = db->get_pool()->acquire();
    if (!conn) return skills;

    const char* sql = "SELECT skill_id, rank FROM user_skill_points WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return skills;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    MYSQL_BIND res[2];
    memset(res, 0, sizeof(res));
    char skill_id[65];
    int rank;
    unsigned long id_len;

    res[0].buffer_type = MYSQL_TYPE_STRING; res[0].buffer = skill_id; res[0].buffer_length = sizeof(skill_id); res[0].length = &id_len;
    res[1].buffer_type = MYSQL_TYPE_LONG; res[1].buffer = (char*)&rank;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        skills[std::string(skill_id, id_len)] = rank;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return skills;
}

bool invest_skill_point(Database* db, uint64_t user_id, const std::string& skill_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "INSERT INTO user_skill_points (user_id, skill_id, rank) "
                      "VALUES (?, ?, 1) "
                      "ON DUPLICATE KEY UPDATE rank = rank + 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (char*)skill_id.c_str(); bind[1].buffer_length = skill_id.length();
    
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

bool reset_all_skills(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "DELETE FROM user_skill_points WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

} // namespace skill_operations
} // namespace db
} // namespace bronx
