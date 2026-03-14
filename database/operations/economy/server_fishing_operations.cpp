#include "server_fishing_operations.h"
#include "server_economy_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

namespace server_fishing_operations {

bool create_fish_catch(Database* db, uint64_t guild_id, uint64_t user_id,
                       const std::string& rarity, const std::string& fish_name,
                       double weight, int64_t value,
                       const std::string& rod_id, const std::string& bait_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "INSERT INTO user_fish_catches (guild_id, user_id, rarity, fish_name, weight, value, rod_id, bait_id) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("create_fish_catch prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[8];
    memset(bind, 0, sizeof(bind));
    
    unsigned long rarity_len = rarity.length();
    unsigned long fish_name_len = fish_name.length();
    unsigned long rod_len = rod_id.length();
    unsigned long bait_len = bait_id.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)rarity.c_str();
    bind[2].buffer_length = rarity.length();
    bind[2].length = &rarity_len;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)fish_name.c_str();
    bind[3].buffer_length = fish_name.length();
    bind[3].length = &fish_name_len;
    
    bind[4].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[4].buffer = (char*)&weight;
    
    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&value;
    
    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer = (char*)rod_id.c_str();
    bind[6].buffer_length = rod_id.length();
    bind[6].length = &rod_len;
    
    bind[7].buffer_type = MYSQL_TYPE_STRING;
    bind[7].buffer = (char*)bait_id.c_str();
    bind[7].buffer_length = bait_id.length();
    bind[7].length = &bait_len;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("create_fish_catch bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

std::vector<FishCatch> get_unsold_fish(Database* db, uint64_t guild_id, uint64_t user_id) {
    std::vector<FishCatch> catches;
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT id, guild_id, rarity, fish_name, weight, value, "
                       "UNIX_TIMESTAMP(caught_at), sold, rod_id, bait_id "
                       "FROM user_fish_catches WHERE guild_id = ? AND user_id = ? AND sold = FALSE "
                       "ORDER BY caught_at DESC";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_unsold_fish prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return catches;
    }
    
    MYSQL_BIND bind_param[2];
    memset(bind_param, 0, sizeof(bind_param));
    
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&guild_id;
    bind_param[0].is_unsigned = 1;
    
    bind_param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[1].buffer = (char*)&user_id;
    bind_param[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        db->log_error("get_unsold_fish bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return catches;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_unsold_fish execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return catches;
    }
    
    MYSQL_BIND bind_result[10];
    memset(bind_result, 0, sizeof(bind_result));
    
    FishCatch catch_data;
    char rarity_buf[32], fish_name_buf[128], rod_buf[128], bait_buf[128];
    unsigned long rarity_len, fish_len, rod_len, bait_len;
    long long caught_ts;
    my_bool sold_val;
    
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &catch_data.id;
    bind_result[0].is_unsigned = 1;
    
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &catch_data.guild_id;
    bind_result[1].is_unsigned = 1;
    
    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = rarity_buf;
    bind_result[2].buffer_length = sizeof(rarity_buf);
    bind_result[2].length = &rarity_len;
    
    bind_result[3].buffer_type = MYSQL_TYPE_STRING;
    bind_result[3].buffer = fish_name_buf;
    bind_result[3].buffer_length = sizeof(fish_name_buf);
    bind_result[3].length = &fish_len;
    
    bind_result[4].buffer_type = MYSQL_TYPE_DOUBLE;
    bind_result[4].buffer = &catch_data.weight;
    
    bind_result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[5].buffer = &catch_data.value;
    
    bind_result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[6].buffer = &caught_ts;
    
    bind_result[7].buffer_type = MYSQL_TYPE_TINY;
    bind_result[7].buffer = &sold_val;
    
    bind_result[8].buffer_type = MYSQL_TYPE_STRING;
    bind_result[8].buffer = rod_buf;
    bind_result[8].buffer_length = sizeof(rod_buf);
    bind_result[8].length = &rod_len;
    
    bind_result[9].buffer_type = MYSQL_TYPE_STRING;
    bind_result[9].buffer = bait_buf;
    bind_result[9].buffer_length = sizeof(bait_buf);
    bind_result[9].length = &bait_len;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        db->log_error("get_unsold_fish bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return catches;
    }
    
    if (mysql_stmt_store_result(stmt) != 0) {
        db->log_error("get_unsold_fish store result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return catches;
    }
    
    while (mysql_stmt_fetch(stmt) == 0) {
        catch_data.rarity = std::string(rarity_buf, rarity_len);
        catch_data.fish_name = std::string(fish_name_buf, fish_len);
        catch_data.rod_id = std::string(rod_buf, rod_len);
        catch_data.bait_id = std::string(bait_buf, bait_len);
        catch_data.caught_at = std::chrono::system_clock::from_time_t(caught_ts);
        catch_data.sold = sold_val != 0;
        
        catches.push_back(catch_data);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return catches;
}

int64_t sell_all_fish(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto unsold = get_unsold_fish(db, guild_id, user_id);
    
    if (unsold.empty()) {
        return 0;
    }
    
    int64_t total_value = 0;
    for (const auto& fish : unsold) {
        total_value += fish.value;
    }
    
    auto conn = db->get_pool()->acquire();
    
    // Mark all as sold
    const char* query = "UPDATE user_fish_catches SET sold = TRUE, sold_at = CURRENT_TIMESTAMP "
                       "WHERE guild_id = ? AND user_id = ? AND sold = FALSE";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("sell_all_fish prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("sell_all_fish bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    // Add money to wallet
    server_economy_operations::update_server_wallet(db, guild_id, user_id, total_value);
    
    return total_value;
}

std::pair<std::string, std::string> get_active_gear(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT active_rod_id, active_bait_id FROM user_fishing_gear "
                       "WHERE guild_id = ? AND user_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_active_gear prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {"", ""};
    }
    
    MYSQL_BIND bind_param[2];
    memset(bind_param, 0, sizeof(bind_param));
    
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&guild_id;
    bind_param[0].is_unsigned = 1;
    
    bind_param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[1].buffer = (char*)&user_id;
    bind_param[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        db->log_error("get_active_gear bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {"", ""};
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_active_gear execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {"", ""};
    }
    
    char rod_buf[128], bait_buf[128];
    unsigned long rod_len = 0, bait_len = 0;
    my_bool rod_null = 1, bait_null = 1;
    
    MYSQL_BIND bind_result[2];
    memset(bind_result, 0, sizeof(bind_result));
    
    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = rod_buf;
    bind_result[0].buffer_length = sizeof(rod_buf);
    bind_result[0].length = &rod_len;
    bind_result[0].is_null = &rod_null;
    
    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = bait_buf;
    bind_result[1].buffer_length = sizeof(bait_buf);
    bind_result[1].length = &bait_len;
    bind_result[1].is_null = &bait_null;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        db->log_error("get_active_gear bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {"", ""};
    }
    
    std::string rod_id, bait_id;
    
    if (mysql_stmt_fetch(stmt) == 0) {
        if (!rod_null) rod_id = std::string(rod_buf, rod_len);
        if (!bait_null) bait_id = std::string(bait_buf, bait_len);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return {rod_id, bait_id};
}

bool set_active_rod(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& rod_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "INSERT INTO user_fishing_gear (guild_id, user_id, active_rod_id) "
                       "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE active_rod_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("set_active_rod prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    unsigned long rod_len = rod_id.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)rod_id.c_str();
    bind[2].buffer_length = rod_id.length();
    bind[2].length = &rod_len;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)rod_id.c_str();
    bind[3].buffer_length = rod_id.length();
    bind[3].length = &rod_len;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("set_active_rod bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

bool set_active_bait(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& bait_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "INSERT INTO user_fishing_gear (guild_id, user_id, active_bait_id) "
                       "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE active_bait_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("set_active_bait prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    unsigned long bait_len = bait_id.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)bait_id.c_str();
    bind[2].buffer_length = bait_id.length();
    bind[2].length = &bait_len;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)bait_id.c_str();
    bind[3].buffer_length = bait_id.length();
    bind[3].length = &bait_len;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("set_active_bait bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

} // namespace server_fishing_operations

} // namespace db
} // namespace bronx
