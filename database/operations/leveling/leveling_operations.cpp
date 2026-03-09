#include "leveling_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <cmath>

namespace bronx {
namespace db {

// Level calculation utilities (in namespace for standalone use)
uint32_t leveling_operations::calculate_level_from_xp(uint64_t xp) {
    if (xp == 0) return 1;
    uint32_t level = 1;
    while (calculate_xp_for_level(level + 1) <= xp) {
        level++;
        if (level > 10000) break; // Safety cap
    }
    return level;
}

uint64_t leveling_operations::calculate_xp_for_level(uint32_t level) {
    if (level <= 1) return 0;
    return static_cast<uint64_t>(100.0 * std::pow(level - 1, 1.5));
}

uint64_t leveling_operations::calculate_xp_for_next_level(uint32_t current_level) {
    return calculate_xp_for_level(current_level + 1);
}

// Database wrapper methods for calculations
uint32_t Database::calculate_level_from_xp(uint64_t xp) {
    return leveling_operations::calculate_level_from_xp(xp);
}

uint64_t Database::calculate_xp_for_level(uint32_t level) {
    return leveling_operations::calculate_xp_for_level(level);
}

uint64_t Database::calculate_xp_for_next_level(uint32_t current_level) {
    return leveling_operations::calculate_xp_for_next_level(current_level);
}

// User XP operations
std::optional<UserXP> Database::get_user_xp(uint64_t user_id) {
    return leveling_operations::get_user_xp(this, user_id);
}

bool Database::create_user_xp(uint64_t user_id) {
    return leveling_operations::create_user_xp(this, user_id);
}

bool Database::add_xp(uint64_t user_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up) {
    return leveling_operations::add_xp(this, user_id, xp_amount, new_level, leveled_up);
}

bool Database::set_xp(uint64_t user_id, uint64_t xp_amount) {
    return leveling_operations::set_xp(this, user_id, xp_amount);
}

// Server XP operations  
std::optional<ServerXP> Database::get_server_xp(uint64_t user_id, uint64_t guild_id) {
    return leveling_operations::get_server_xp(this, user_id, guild_id);
}

bool Database::create_server_xp(uint64_t user_id, uint64_t guild_id) {
    return leveling_operations::create_server_xp(this, user_id, guild_id);
}

bool Database::add_server_xp(uint64_t user_id, uint64_t guild_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up) {
    return leveling_operations::add_server_xp(this, user_id, guild_id, xp_amount, new_level, leveled_up);
}

bool Database::reset_server_xp(uint64_t guild_id) {
    return leveling_operations::reset_server_xp(this, guild_id);
}

bool Database::reset_user_server_xp(uint64_t user_id, uint64_t guild_id) {
    return leveling_operations::reset_user_server_xp(this, user_id, guild_id);
}

// Server leveling configuration
std::optional<ServerLevelingConfig> Database::get_server_leveling_config(uint64_t guild_id) {
    return leveling_operations::get_server_config(this, guild_id);
}

bool Database::create_server_leveling_config(uint64_t guild_id) {
    return leveling_operations::create_server_config(this, guild_id);
}

bool Database::update_server_leveling_config(const ServerLevelingConfig& config) {
    return leveling_operations::update_server_config(this, config);
}

// Level roles
std::vector<LevelRole> Database::get_level_roles(uint64_t guild_id) {
    return leveling_operations::get_level_roles(this, guild_id);
}

std::optional<LevelRole> Database::get_level_role_at_level(uint64_t guild_id, uint32_t level) {
    return leveling_operations::get_level_role_at_level(this, guild_id, level);
}

bool Database::create_level_role(const LevelRole& role) {
    return leveling_operations::create_level_role(this, role);
}

bool Database::delete_level_role(uint64_t guild_id, uint32_t level) {
    return leveling_operations::delete_level_role(this, guild_id, level);
}

bool Database::delete_level_role_by_id(uint64_t id) {
    return leveling_operations::delete_level_role_by_id(this, id);
}

// Leveling leaderboards
std::vector<LeaderboardEntry> Database::get_global_xp_leaderboard(int limit) {
    return leveling_operations::get_global_xp_leaderboard(this, limit);
}

std::vector<LeaderboardEntry> Database::get_server_xp_leaderboard(uint64_t guild_id, int limit) {
    return leveling_operations::get_server_xp_leaderboard(this, guild_id, limit);
}

int Database::get_user_global_xp_rank(uint64_t user_id) {
    return leveling_operations::get_user_global_xp_rank(this, user_id);
}

int Database::get_user_server_xp_rank(uint64_t user_id, uint64_t guild_id) {
    return leveling_operations::get_user_server_xp_rank(this, user_id, guild_id);
}

// Namespace function implementations
namespace leveling_operations {

std::optional<UserXP> get_user_xp(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) {
        db->log_error("get_user_xp acquire failed");
        return {};
    }
    
    const char* query = "SELECT user_id, total_xp, level, last_xp_gain FROM user_xp WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_user_xp prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_user_xp execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND resbind[4];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t uid, total_xp;
    uint32_t level;
    MYSQL_TIME last_msg;
    my_bool last_msg_null;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &uid;
    resbind[0].is_unsigned = 1;
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &total_xp;
    resbind[1].is_unsigned = 1;
    resbind[2].buffer_type = MYSQL_TYPE_LONG;
    resbind[2].buffer = &level;
    resbind[2].is_unsigned = 1;
    resbind[3].buffer_type = MYSQL_TYPE_DATETIME;
    resbind[3].buffer = &last_msg;
    resbind[3].is_null = &last_msg_null;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    std::optional<UserXP> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        UserXP uxp;
        uxp.user_id = uid;
        uxp.total_xp = total_xp;
        uxp.level = level;
        
        if (!last_msg_null) {
            std::tm tm = {0};
            tm.tm_year = last_msg.year - 1900;
            tm.tm_mon = last_msg.month - 1;
            tm.tm_mday = last_msg.day;
            tm.tm_hour = last_msg.hour;
            tm.tm_min = last_msg.minute;
            tm.tm_sec = last_msg.second;
            uxp.last_message_xp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        
        result = uxp;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

bool create_user_xp(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) {
        db->log_error("create_user_xp acquire failed");
        return false;
    }
    
    const char* query = "INSERT INTO user_xp (user_id, total_xp, level) VALUES (?, 0, 1) ON DUPLICATE KEY UPDATE user_id=user_id";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("create_user_xp prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool add_xp(Database* db, uint64_t user_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up) {
    auto current = get_user_xp(db, user_id);
    if (!current) {
        create_user_xp(db, user_id);
        current = get_user_xp(db, user_id);
        if (!current) return false;
    }
    
    auto conn = db->get_pool()->acquire();
    if (!conn) {
        db->log_error("add_xp acquire failed");
        return false;
    }
    
    uint64_t new_xp = current->total_xp + xp_amount;
    uint32_t old_level = current->level;
    new_level = calculate_level_from_xp(new_xp);
    leveled_up = new_level > old_level;
    
    const char* query = "UPDATE user_xp SET total_xp = ?, level = ?, last_xp_gain = NOW() WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("add_xp prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&new_xp;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&new_level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool set_xp(Database* db, uint64_t user_id, uint64_t xp_amount) {
    auto conn = db->get_pool()->acquire();
    if (!conn) {
        db->log_error("set_xp acquire failed");
        return false;
    }
    
    uint32_t level = calculate_level_from_xp(xp_amount);
    
    const char* query ="UPDATE user_xp SET total_xp = ?, level = ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("set_xp prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&xp_amount;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

// Implement remaining operations
std::optional<ServerXP> get_server_xp(Database* db, uint64_t user_id, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {};
    
    const char* query = "SELECT user_id, guild_id, server_xp, server_level, last_server_xp_gain FROM server_xp WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND resbind[5];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t uid, gid, sxp;
    uint32_t slvl;
    MYSQL_TIME last_msg;
    my_bool last_msg_null;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &uid;
    resbind[0].is_unsigned = 1;
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[1].is_unsigned = 1;
    resbind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[2].buffer = &sxp;
    resbind[2].is_unsigned = 1;
    resbind[3].buffer_type = MYSQL_TYPE_LONG;
    resbind[3].buffer = &slvl;
    resbind[3].is_unsigned = 1;
    resbind[4].buffer_type = MYSQL_TYPE_DATETIME;
    resbind[4].buffer = &last_msg;
    resbind[4].is_null = &last_msg_null;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    std::optional<ServerXP> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ServerXP sx;
        sx.user_id = uid;
        sx.guild_id = gid;
        sx.server_xp = sxp;
        sx.server_level = slvl;
        
        if (!last_msg_null) {
            std::tm tm = {0};
            tm.tm_year = last_msg.year - 1900;
            tm.tm_mon = last_msg.month - 1;
            tm.tm_mday = last_msg.day;
            tm.tm_hour = last_msg.hour;
            tm.tm_min = last_msg.minute;
            tm.tm_sec = last_msg.second;
            sx.last_message_xp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        result = sx;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

bool create_server_xp(Database* db, uint64_t user_id, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO server_xp (user_id, guild_id, server_xp, server_level) VALUES (?, ?, 0, 1) ON DUPLICATE KEY UPDATE user_id=user_id";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool add_server_xp(Database* db, uint64_t user_id, uint64_t guild_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up) {
    auto current = get_server_xp(db, user_id, guild_id);
    if (!current) {
        create_server_xp(db, user_id, guild_id);
        current = get_server_xp(db, user_id, guild_id);
        if (!current) return false;
    }
    
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    uint64_t new_xp = current->server_xp + xp_amount;
    uint32_t old_level = current->server_level;
    new_level = calculate_level_from_xp(new_xp);
    leveled_up = new_level > old_level;
    
    const char* query = "UPDATE server_xp SET server_xp = ?, server_level = ?, last_server_xp_gain = NOW() WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&new_xp;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&new_level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool reset_server_xp(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "UPDATE server_xp SET server_xp = 0, server_level = 1 WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool reset_user_server_xp(Database* db, uint64_t user_id, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "UPDATE server_xp SET server_xp = 0, server_level = 1 WHERE user_id = ? AND guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

std::optional<ServerLevelingConfig> get_server_config(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {};
    
    const char* query = "SELECT guild_id, enabled, coin_rewards, coins_per_message, min_xp, "
                        "max_xp, min_message_length, xp_cooldown, level_up_channel "
                        "FROM server_leveling_config WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND resbind[9];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t gid;
    my_bool enabled, reward_coins;
    int coins_per_msg, min_xp, max_xp, min_chars, cooldown;
    uint64_t announce_ch;
    my_bool announce_ch_null;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &gid;
    resbind[1].buffer_type = MYSQL_TYPE_TINY;
    resbind[1].buffer = &enabled;
    resbind[2].buffer_type = MYSQL_TYPE_TINY;
    resbind[2].buffer = &reward_coins;
    resbind[3].buffer_type = MYSQL_TYPE_LONG;
    resbind[3].buffer = &coins_per_msg;
    resbind[4].buffer_type = MYSQL_TYPE_LONG;
    resbind[4].buffer = &min_xp;
    resbind[5].buffer_type = MYSQL_TYPE_LONG;
    resbind[5].buffer = &max_xp;
    resbind[6].buffer_type = MYSQL_TYPE_LONG;
    resbind[6].buffer = &min_chars;
    resbind[7].buffer_type = MYSQL_TYPE_LONG;
    resbind[7].buffer = &cooldown;
    resbind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[8].buffer = &announce_ch;
    resbind[8].is_null = &announce_ch_null;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    std::optional<ServerLevelingConfig> result;
    if (mysql_stmt_fetch(stmt) == 0) {
        ServerLevelingConfig cfg;
        cfg.guild_id = gid;
        cfg.enabled = enabled;
        cfg.reward_coins = reward_coins;
        cfg.coins_per_message = coins_per_msg;
        cfg.min_xp_per_message = min_xp;
        cfg.max_xp_per_message = max_xp;
        cfg.min_message_chars = min_chars;
        cfg.xp_cooldown_seconds = cooldown;
        if (!announce_ch_null) cfg.announcement_channel = announce_ch;
        cfg.announce_levelup = true;  // Default to true since DB doesn't have this column
        result = cfg;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return result;
}

bool create_server_config(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO server_leveling_config (guild_id) VALUES (?) ON DUPLICATE KEY UPDATE guild_id=guild_id";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool update_server_config(Database* db, const ServerLevelingConfig& config) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "UPDATE server_leveling_config SET enabled=?, coin_rewards=?, coins_per_message=?, "
                        "min_xp=?, max_xp=?, min_message_length=?, xp_cooldown=?, "
                        "level_up_channel=? WHERE guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[9];
    memset(bind, 0, sizeof(bind));
    
    my_bool enabled = config.enabled;
    my_bool reward_coins = config.reward_coins;
    uint64_t announce_ch = config.announcement_channel.value_or(0);
    my_bool announce_ch_null = !config.announcement_channel.has_value();
    
    bind[0].buffer_type = MYSQL_TYPE_TINY;
    bind[0].buffer = &enabled;
    bind[1].buffer_type = MYSQL_TYPE_TINY;
    bind[1].buffer = &reward_coins;
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = (char*)&config.coins_per_message;
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&config.min_xp_per_message;
    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer = (char*)&config.max_xp_per_message;
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = (char*)&config.min_message_chars;
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (char*)&config.xp_cooldown_seconds;
    bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[7].buffer = &announce_ch;
    bind[7].is_null = &announce_ch_null;
    bind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[8].buffer = (char*)&config.guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

std::vector<LevelRole> get_level_roles(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {};
    
    const char* query = "SELECT id, guild_id, level, role_id, role_name, description, remove_previous "
                        "FROM level_roles WHERE guild_id = ? ORDER BY level ASC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_BIND resbind[7];
    memset(resbind, 0, sizeof(resbind));
    
    uint64_t id, gid, role_id;
    uint32_t level;
    char role_name_buf[256], desc_buf[1024];
    unsigned long role_name_len, desc_len;
    my_bool desc_null, remove_prev;
    
    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &id;
    resbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[1].buffer = &gid;
    resbind[2].buffer_type = MYSQL_TYPE_LONG;
    resbind[2].buffer = &level;
    resbind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[3].buffer = &role_id;
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = role_name_buf;
    resbind[4].buffer_length = sizeof(role_name_buf);
    resbind[4].length = &role_name_len;
    resbind[5].buffer_type = MYSQL_TYPE_STRING;
    resbind[5].buffer = desc_buf;
    resbind[5].buffer_length = sizeof(desc_buf);
    resbind[5].length = &desc_len;
    resbind[5].is_null = &desc_null;
    resbind[6].buffer_type = MYSQL_TYPE_TINY;
    resbind[6].buffer = &remove_prev;
    
    mysql_stmt_bind_result(stmt, resbind);
    
    std::vector<LevelRole> results;
    while (mysql_stmt_fetch(stmt) == 0) {
        LevelRole lr;
        lr.id = id;
        lr.guild_id = gid;
        lr.level = level;
        lr.role_id = role_id;
        lr.role_name = std::string(role_name_buf, role_name_len);
        if (!desc_null) lr.description = std::string(desc_buf, desc_len);
        lr.remove_previous = remove_prev;
        results.push_back(lr);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return results;
}

std::optional<LevelRole> get_level_role_at_level(Database* db, uint64_t guild_id, uint32_t level) {
    auto roles = get_level_roles(db, guild_id);
    for (const auto& r : roles) {
        if (r.level == level) return r;
    }
    return {};
}

bool create_level_role(Database* db, const LevelRole& role) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO level_roles (guild_id, level, role_id, role_name, description, remove_previous) "
                        "VALUES (?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));
    
    unsigned long role_name_len = role.role_name.length();
    unsigned long desc_len = role.description.length();
    my_bool desc_null = role.description.empty();
    my_bool remove_prev = role.remove_previous;
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&role.guild_id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&role.level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&role.role_id;
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)role.role_name.c_str();
    bind[3].buffer_length = role_name_len;
    bind[3].length = &role_name_len;
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)role.description.c_str();
    bind[4].buffer_length = desc_len;
    bind[4].length = &desc_len;
    bind[4].is_null = &desc_null;
    bind[5].buffer_type = MYSQL_TYPE_TINY;
    bind[5].buffer = &remove_prev;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool delete_level_role(Database* db, uint64_t guild_id, uint32_t level) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM level_roles WHERE guild_id = ? AND level = ?";
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
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&level;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool delete_level_role_by_id(Database* db, uint64_t id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM level_roles WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&id;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

std::vector<LeaderboardEntry> get_global_xp_leaderboard(Database* db, int limit) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {};
    
    std::string query = "SELECT u.user_id, ux.total_xp, ux.level FROM user_xp ux "
                        "JOIN users u ON u.user_id = ux.user_id "
                        "ORDER BY ux.total_xp DESC LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn->get(), query.c_str()) != 0) {
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn->get());
    if (!result) {
        db->get_pool()->release(conn);
        return {};
    }
    
    std::vector<LeaderboardEntry> entries;
    MYSQL_ROW row;
    int rank = 1;
    while ((row = mysql_fetch_row(result))) {
        LeaderboardEntry entry;
        entry.user_id = std::stoull(row[0]);
        entry.value = std::stoll(row[1]);
        entry.rank = rank++;
        entry.extra_info = "Level " + std::string(row[2]);
        entries.push_back(entry);
    }
    
    mysql_free_result(result);
    db->get_pool()->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> get_server_xp_leaderboard(Database* db, uint64_t guild_id, int limit) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return {};
    
    std::string query = "SELECT sx.user_id, sx.server_xp, sx.server_level FROM server_xp sx "
                        "WHERE sx.guild_id = " + std::to_string(guild_id) + " "
                        "ORDER BY sx.server_xp DESC LIMIT " + std::to_string(limit);
    
    if (mysql_query(conn->get(), query.c_str()) != 0) {
        db->get_pool()->release(conn);
        return {};
    }
    
    MYSQL_RES* result = mysql_store_result(conn->get());
    if (!result) {
        db->get_pool()->release(conn);
        return {};
    }
    
    std::vector<LeaderboardEntry> entries;
    MYSQL_ROW row;
    int rank = 1;
    while ((row = mysql_fetch_row(result))) {
        LeaderboardEntry entry;
        entry.user_id = std::stoull(row[0]);
        entry.value = std::stoll(row[1]);
        entry.rank = rank++;
        entry.extra_info = "Level " + std::string(row[2]);
        entries.push_back(entry);
    }
    
    mysql_free_result(result);
    db->get_pool()->release(conn);
    return entries;
}

int get_user_global_xp_rank(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return -1;
    
    std::string query = "SELECT COUNT(*) + 1 FROM user_xp WHERE total_xp > "
                        "(SELECT total_xp FROM user_xp WHERE user_id = " + std::to_string(user_id) + ")";
    
    if (mysql_query(conn->get(), query.c_str()) != 0) {
        db->get_pool()->release(conn);
        return -1;
    }
    
    MYSQL_RES* result = mysql_store_result(conn->get());
    if (!result) {
        db->get_pool()->release(conn);
        return -1;
    }
    
    int rank = -1;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) rank = std::stoi(row[0]);
    
    mysql_free_result(result);
    db->get_pool()->release(conn);
    return rank;
}

int get_user_server_xp_rank(Database* db, uint64_t user_id, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return -1;
    
    std::string query = "SELECT COUNT(*) + 1 FROM server_xp WHERE guild_id = " + std::to_string(guild_id) + 
                        " AND server_xp > (SELECT server_xp FROM server_xp WHERE user_id = " + std::to_string(user_id) + 
                        " AND guild_id = " + std::to_string(guild_id) + ")";
    
    if (mysql_query(conn->get(), query.c_str()) != 0) {
        db->get_pool()->release(conn);
        return -1;
    }
    
    MYSQL_RES* result = mysql_store_result(conn->get());
    if (!result) {
        db->get_pool()->release(conn);
        return -1;
    }
    
    int rank = -1;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) rank = std::stoi(row[0]);
    
    mysql_free_result(result);
    db->get_pool()->release(conn);
    return rank;
}

} // namespace leveling_operations

} // namespace db
} // namespace bronx
