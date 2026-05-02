#include "pet_operations.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <cstring>

namespace bronx {
namespace db {
namespace pet_operations {

bool ensure_tables(Database* db) {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS user_pets ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  species_id VARCHAR(32) NOT NULL,"
        "  nickname VARCHAR(32) NOT NULL,"
        "  level INT NOT NULL DEFAULT 1,"
        "  xp INT NOT NULL DEFAULT 0,"
        "  hunger INT NOT NULL DEFAULT 100,"
        "  equipped BOOLEAN NOT NULL DEFAULT FALSE,"
        "  adopted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  last_fed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_user (user_id),"
        "  INDEX idx_user_equipped (user_id, equipped)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    return db->execute(sql);
}

std::vector<UserPetRow> get_user_pets(Database* db, uint64_t user_id) {
    std::vector<UserPetRow> pets;
    auto conn = db->get_pool()->acquire();
    if (!conn) return pets;

    const char* sql = "SELECT id, species_id, nickname, level, xp, hunger, equipped, adopted_at, last_fed "
                      "FROM user_pets WHERE user_id = ? ORDER BY equipped DESC, level DESC";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) { db->get_pool()->release(conn); return pets; }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return pets;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return pets;
    }

    MYSQL_BIND res[9];
    memset(res, 0, sizeof(res));

    int64_t id;
    char species[33], nick[33], adopted[25], fed[25];
    int level, xp, hunger;
    my_bool equipped;
    unsigned long species_len, nick_len, adopted_len, fed_len;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = (char*)&id;
    res[1].buffer_type = MYSQL_TYPE_STRING; res[1].buffer = species; res[1].buffer_length = sizeof(species); res[1].length = &species_len;
    res[2].buffer_type = MYSQL_TYPE_STRING; res[2].buffer = nick; res[2].buffer_length = sizeof(nick); res[2].length = &nick_len;
    res[3].buffer_type = MYSQL_TYPE_LONG; res[3].buffer = (char*)&level;
    res[4].buffer_type = MYSQL_TYPE_LONG; res[4].buffer = (char*)&xp;
    res[5].buffer_type = MYSQL_TYPE_LONG; res[5].buffer = (char*)&hunger;
    res[6].buffer_type = MYSQL_TYPE_TINY; res[6].buffer = (char*)&equipped;
    res[7].buffer_type = MYSQL_TYPE_STRING; res[7].buffer = adopted; res[7].buffer_length = sizeof(adopted); res[7].length = &adopted_len;
    res[8].buffer_type = MYSQL_TYPE_STRING; res[8].buffer = fed; res[8].buffer_length = sizeof(fed); res[8].length = &fed_len;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        UserPetRow p;
        p.id = id;
        p.user_id = user_id;
        p.species_id = std::string(species, species_len);
        p.nickname = std::string(nick, nick_len);
        p.level = level;
        p.xp = xp;
        p.hunger = hunger;
        p.equipped = (equipped != 0);
        p.adopted_at = std::string(adopted, adopted_len);
        p.last_fed = std::string(fed, fed_len);
        pets.push_back(p);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return pets;
}

bool adopt_pet(Database* db, uint64_t user_id, const std::string& species_id, const std::string& nickname, bool equipped) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "INSERT INTO user_pets (user_id, species_id, nickname, level, xp, hunger, equipped) "
                      "VALUES (?, ?, ?, 1, 0, 100, ?)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    my_bool eq = equipped;

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (char*)species_id.c_str(); bind[1].buffer_length = species_id.length();
    bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = (char*)nickname.c_str(); bind[2].buffer_length = nickname.length();
    bind[3].buffer_type = MYSQL_TYPE_TINY; bind[3].buffer = (char*)&eq;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

bool feed_pet(Database* db, int64_t pet_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "UPDATE user_pets SET hunger = 100, last_fed = NOW() WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&pet_id;

    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

bool equip_pet(Database* db, uint64_t user_id, int64_t pet_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    // First unequip all
    const char* sql1 = "UPDATE user_pets SET equipped = FALSE WHERE user_id = ?";
    MYSQL_STMT* stmt1 = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt1, sql1, strlen(sql1));
    MYSQL_BIND bind1[1];
    memset(bind1, 0, sizeof(bind1));
    bind1[0].buffer_type = MYSQL_TYPE_LONGLONG; bind1[0].buffer = (char*)&user_id; bind1[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt1, bind1);
    mysql_stmt_execute(stmt1);
    mysql_stmt_close(stmt1);

    // Then equip target
    const char* sql2 = "UPDATE user_pets SET equipped = TRUE WHERE id = ? AND user_id = ?";
    MYSQL_STMT* stmt2 = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt2, sql2, strlen(sql2));
    MYSQL_BIND bind2[2];
    memset(bind2, 0, sizeof(bind2));
    bind2[0].buffer_type = MYSQL_TYPE_LONGLONG; bind2[0].buffer = (char*)&pet_id;
    bind2[1].buffer_type = MYSQL_TYPE_LONGLONG; bind2[1].buffer = (char*)&user_id; bind2[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt2, bind2);
    bool ok = (mysql_stmt_execute(stmt2) == 0);
    mysql_stmt_close(stmt2);

    db->get_pool()->release(conn);
    return ok;
}

bool release_pet(Database* db, uint64_t user_id, int64_t pet_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "DELETE FROM user_pets WHERE id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&pet_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = (char*)&user_id; bind[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

bool rename_pet(Database* db, int64_t pet_id, const std::string& new_name) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "UPDATE user_pets SET nickname = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)new_name.c_str(); bind[0].buffer_length = new_name.length();
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = (char*)&pet_id;
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

bool update_pet_stats(Database* db, int64_t pet_id, int new_level, int new_xp) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;

    const char* sql = "UPDATE user_pets SET level = ?, xp = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG; bind[0].buffer = (char*)&new_level;
    bind[1].buffer_type = MYSQL_TYPE_LONG; bind[1].buffer = (char*)&new_xp;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG; bind[2].buffer = (char*)&pet_id;
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

int count_user_pets(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;

    const char* sql = "SELECT COUNT(*) FROM user_pets WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, sql, strlen(sql));
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = (char*)&user_id; bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    mysql_stmt_execute(stmt);

    MYSQL_BIND res[1];
    memset(res, 0, sizeof(res));
    int64_t count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_fetch(stmt);

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return (int)count;
}

} // namespace pet_operations
} // namespace db
} // namespace bronx
