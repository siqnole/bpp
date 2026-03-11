#include "permission_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

bool Database::set_guild_command_enabled(uint64_t guild_id, const std::string& command, bool enabled,
                                              const std::string& scope_type, uint64_t scope_id, bool exclusive) {
    auto conn = pool_->acquire();
    const char* query;
    if (scope_type == "guild") {
        query = "INSERT INTO guild_command_settings (guild_id, command, enabled) VALUES (?, ?, ?) "
                "ON DUPLICATE KEY UPDATE enabled = ?";
    } else {
        // scoped override
        query = "INSERT INTO guild_command_scope_settings (guild_id, command, scope_type, scope_id, enabled, exclusive) "
                "VALUES (?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE enabled = ?, exclusive = ?";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_guild_command_enabled prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    // binding depends on whether scoped
    if (scope_type == "guild") {
        MYSQL_BIND bind[4];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)command.c_str();
        bind[1].buffer_length = command.length();
        bind[2].buffer_type = MYSQL_TYPE_TINY;
        bind[2].buffer = (char*)&enabled;
        bind[3].buffer_type = MYSQL_TYPE_TINY;
        bind[3].buffer = (char*)&enabled;
        mysql_stmt_bind_param(stmt, bind);
    } else {
        MYSQL_BIND bind[8];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)command.c_str();
        bind[1].buffer_length = command.length();
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)scope_type.c_str();
        bind[2].buffer_length = scope_type.length();
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&scope_id;
        bind[3].is_unsigned = 1;
        bind[4].buffer_type = MYSQL_TYPE_TINY;
        bind[4].buffer = (char*)&enabled;
        bind[5].buffer_type = MYSQL_TYPE_TINY;
        bind[5].buffer = (char*)&exclusive;
        // ON DUPLICATE KEY UPDATE values
        bind[6].buffer_type = MYSQL_TYPE_TINY;
        bind[6].buffer = (char*)&enabled;
        bind[7].buffer_type = MYSQL_TYPE_TINY;
        bind[7].buffer = (char*)&exclusive;
        mysql_stmt_bind_param(stmt, bind);
    }
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_guild_command_enabled execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_guild_command_enabled(uint64_t guild_id, const std::string& command,
                                           uint64_t user_id, uint64_t channel_id,
                                           const std::vector<uint64_t>& roles) {
    auto conn = pool_->acquire();
    
    // FIRST: Check if there's an exclusive scope setting
    // Exclusive means ONLY that scope can use it, all others are blocked
    {
        const char* exclusive_query = "SELECT scope_type, scope_id FROM guild_command_scope_settings "
                                     "WHERE guild_id = ? AND command = ? AND exclusive = TRUE AND enabled = TRUE LIMIT 1";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, exclusive_query, strlen(exclusive_query)) == 0) {
            MYSQL_BIND bind[2];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = (char*)&guild_id;
            bind[0].is_unsigned = 1;
            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (char*)command.c_str();
            bind[1].buffer_length = command.length();
            mysql_stmt_bind_param(stmt, bind);
            
            if (mysql_stmt_execute(stmt) == 0) {
                char scope_type_buf[32];
                uint64_t exclusive_scope_id;
                unsigned long scope_type_length;
                
                MYSQL_BIND result[2];
                memset(result, 0, sizeof(result));
                result[0].buffer_type = MYSQL_TYPE_STRING;
                result[0].buffer = scope_type_buf;
                result[0].buffer_length = sizeof(scope_type_buf);
                result[0].length = &scope_type_length;
                result[1].buffer_type = MYSQL_TYPE_LONGLONG;
                result[1].buffer = (char*)&exclusive_scope_id;
                result[1].is_unsigned = 1;
                mysql_stmt_bind_result(stmt, result);
                
                if (mysql_stmt_fetch(stmt) == 0) {
                    // Found an exclusive setting - ONLY allow if user matches the exclusive scope
                    std::string exclusive_scope(scope_type_buf, scope_type_length);
                    mysql_stmt_close(stmt);
                    pool_->release(conn);
                    
                    if (exclusive_scope == "user" && user_id == exclusive_scope_id) return true;
                    if (exclusive_scope == "channel" && channel_id == exclusive_scope_id) return true;
                    if (exclusive_scope == "role") {
                        for (uint64_t r : roles) {
                            if (r == exclusive_scope_id) return true;
                        }
                    }
                    // Not in the exclusive scope - deny
                    return false;
                }
            }
        }
        mysql_stmt_close(stmt);
    }
    
    // No exclusive setting - check scoped overrides in priority order: user, channel, roles, then guild default
    MYSQL_STMT* stmt;
    MYSQL_BIND bind[4];
    bool enabled;

    // helper lambda to query one scope
    auto query_scope = [&](const std::string& scope_type, uint64_t scope_id_val, bool& found)->bool {
        const char* q = " SELECT enabled FROM guild_command_scope_settings WHERE guild_id = ? AND command = ? AND scope_type = ? AND scope_id = ? LIMIT 1";
        stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("is_guild_command_enabled scope prepare");
            mysql_stmt_close(stmt);
            return true; // treat as enabled on error
        }
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)command.c_str();
        bind[1].buffer_length = command.length();
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)scope_type.c_str();
        bind[2].buffer_length = scope_type.length();
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&scope_id_val;
        bind[3].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        if (mysql_stmt_execute(stmt) != 0) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("is_guild_command_enabled scope execute");
            mysql_stmt_close(stmt);
            return true;
        }
        MYSQL_BIND rb;
        memset(&rb, 0, sizeof(rb));
        rb.buffer_type = MYSQL_TYPE_TINY;
        rb.buffer = (char*)&enabled;
        rb.is_unsigned = 0;
        mysql_stmt_bind_result(stmt, &rb);
        if (mysql_stmt_fetch(stmt) == 0) {
            found = true;
        } else {
            found = false;
        }
        mysql_stmt_close(stmt);
        return true;
    };

    bool found = false;
    if (user_id != 0) {
        if (!query_scope("user", user_id, found)) { pool_->release(conn); return true; }
        if (found) { pool_->release(conn); return enabled; }
    }
    if (channel_id != 0) {
        if (!query_scope("channel", channel_id, found)) { pool_->release(conn); return true; }
        if (found) { pool_->release(conn); return enabled; }
    }
    for (uint64_t r : roles) {
        if (!query_scope("role", r, found)) { pool_->release(conn); return true; }
        if (found) {
            // if any role rule exists, disable takes precedence
            bool this_enabled = enabled;
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return this_enabled;
        }
    }
    // fallback to guild default
    const char* q2 = "SELECT enabled FROM guild_command_settings WHERE guild_id = ? AND command = ? LIMIT 1";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q2, strlen(q2)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_guild_command_enabled default prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return true;
    }
    MYSQL_BIND bind2[2];
    memset(bind2, 0, sizeof(bind2));
    bind2[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind2[0].buffer = (char*)&guild_id;
    bind2[0].is_unsigned = 1;
    bind2[1].buffer_type = MYSQL_TYPE_STRING;
    bind2[1].buffer = (char*)command.c_str();
    bind2[1].buffer_length = command.length();
    mysql_stmt_bind_param(stmt, bind2);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_guild_command_enabled default execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return true;
    }
    MYSQL_BIND rb2;
    memset(&rb2, 0, sizeof(rb2));
    rb2.buffer_type = MYSQL_TYPE_TINY;
    rb2.buffer = (char*)&enabled;
    rb2.is_unsigned = 0;
    mysql_stmt_bind_result(stmt, &rb2);
    if (mysql_stmt_fetch(stmt) != 0) {
        enabled = true;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return enabled;
}

std::vector<std::string> Database::get_disabled_commands(uint64_t guild_id) {
    std::vector<std::string> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT command FROM guild_command_settings WHERE guild_id = ? AND enabled = FALSE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_disabled_commands prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_disabled_commands execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char buf[128]; unsigned long len = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = buf;
    result_bind[0].buffer_length = sizeof(buf);
    result_bind[0].length = &len;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(buf, len);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

bool Database::set_guild_module_enabled(uint64_t guild_id, const std::string& module, bool enabled,
                                             const std::string& scope_type, uint64_t scope_id, bool exclusive) {
    auto conn = pool_->acquire();
    const char* query;
    if (scope_type == "guild") {
        query = "INSERT INTO guild_module_settings (guild_id, module, enabled) VALUES (?, ?, ?) "
                "ON DUPLICATE KEY UPDATE enabled = ?";
    } else {
        query = "INSERT INTO guild_module_scope_settings (guild_id, module, scope_type, scope_id, enabled, exclusive) "
                "VALUES (?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE enabled = ?, exclusive = ?";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_guild_module_enabled prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    if (scope_type == "guild") {
        MYSQL_BIND bind[4];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)module.c_str();
        bind[1].buffer_length = module.length();
        bind[2].buffer_type = MYSQL_TYPE_TINY;
        bind[2].buffer = (char*)&enabled;
        bind[3].buffer_type = MYSQL_TYPE_TINY;
        bind[3].buffer = (char*)&enabled;
        mysql_stmt_bind_param(stmt, bind);
    } else {
        MYSQL_BIND bind[8];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)module.c_str();
        bind[1].buffer_length = module.length();
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)scope_type.c_str();
        bind[2].buffer_length = scope_type.length();
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&scope_id;
        bind[3].is_unsigned = 1;
        bind[4].buffer_type = MYSQL_TYPE_TINY;
        bind[4].buffer = (char*)&enabled;
        bind[5].buffer_type = MYSQL_TYPE_TINY;
        bind[5].buffer = (char*)&exclusive;
        bind[6].buffer_type = MYSQL_TYPE_TINY;
        bind[6].buffer = (char*)&enabled;
        bind[7].buffer_type = MYSQL_TYPE_TINY;
        bind[7].buffer = (char*)&exclusive;
        mysql_stmt_bind_param(stmt, bind);
    }
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_guild_module_enabled execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_guild_module_enabled(uint64_t guild_id, const std::string& module,
                                          uint64_t user_id, uint64_t channel_id,
                                          const std::vector<uint64_t>& roles) {
    auto conn = pool_->acquire();
    
    // FIRST: Check if there's an exclusive scope setting
    // Exclusive means ONLY that scope can use it, all others are blocked
    {
        const char* exclusive_query = "SELECT scope_type, scope_id FROM guild_module_scope_settings "
                                     "WHERE guild_id = ? AND module = ? AND exclusive = TRUE AND enabled = TRUE LIMIT 1";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, exclusive_query, strlen(exclusive_query)) == 0) {
            MYSQL_BIND bind[2];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = (char*)&guild_id;
            bind[0].is_unsigned = 1;
            bind[1].buffer_type = MYSQL_TYPE_STRING;
            bind[1].buffer = (char*)module.c_str();
            bind[1].buffer_length = module.length();
            mysql_stmt_bind_param(stmt, bind);
            
            if (mysql_stmt_execute(stmt) == 0) {
                char scope_type_buf[32];
                uint64_t exclusive_scope_id;
                unsigned long scope_type_length;
                
                MYSQL_BIND result[2];
                memset(result, 0, sizeof(result));
                result[0].buffer_type = MYSQL_TYPE_STRING;
                result[0].buffer = scope_type_buf;
                result[0].buffer_length = sizeof(scope_type_buf);
                result[0].length = &scope_type_length;
                result[1].buffer_type = MYSQL_TYPE_LONGLONG;
                result[1].buffer = (char*)&exclusive_scope_id;
                result[1].is_unsigned = 1;
                mysql_stmt_bind_result(stmt, result);
                
                if (mysql_stmt_fetch(stmt) == 0) {
                    // Found an exclusive setting - ONLY allow if user matches the exclusive scope
                    std::string exclusive_scope(scope_type_buf, scope_type_length);
                    mysql_stmt_close(stmt);
                    pool_->release(conn);
                    
                    if (exclusive_scope == "user" && user_id == exclusive_scope_id) return true;
                    if (exclusive_scope == "channel" && channel_id == exclusive_scope_id) return true;
                    if (exclusive_scope == "role") {
                        for (uint64_t r : roles) {
                            if (r == exclusive_scope_id) return true;
                        }
                    }
                    // Not in the exclusive scope - deny
                    return false;
                }
            }
        }
        mysql_stmt_close(stmt);
    }
    
    // No exclusive setting - check scoped overrides in priority order: user, channel, roles, then guild default
    MYSQL_STMT* stmt;
    MYSQL_BIND bind[4];
    bool enabled;
    auto query_scope = [&](const std::string& scope_type, uint64_t scope_id_val, bool& found)->bool {
        const char* q = "SELECT enabled FROM guild_module_scope_settings WHERE guild_id = ? AND module = ? AND scope_type = ? AND scope_id = ? LIMIT 1";
        stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("is_guild_module_enabled scope prepare");
            mysql_stmt_close(stmt);
            return true;
        }
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&guild_id;
        bind[0].is_unsigned = 1;
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)module.c_str();
        bind[1].buffer_length = module.length();
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)scope_type.c_str();
        bind[2].buffer_length = scope_type.length();
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&scope_id_val;
        bind[3].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        if (mysql_stmt_execute(stmt) != 0) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("is_guild_module_enabled scope execute");
            mysql_stmt_close(stmt);
            return true;
        }
        MYSQL_BIND rb;
        memset(&rb, 0, sizeof(rb));
        rb.buffer_type = MYSQL_TYPE_TINY;
        rb.buffer = (char*)&enabled;
        rb.is_unsigned = 0;
        mysql_stmt_bind_result(stmt, &rb);
        if (mysql_stmt_fetch(stmt) == 0) {
            found = true;
        } else {
            found = false;
        }
        mysql_stmt_close(stmt);
        return true;
    };

    bool found = false;
    if (user_id != 0) {
        if (!query_scope("user", user_id, found)) { pool_->release(conn); return true; }
        if (found) { pool_->release(conn); return enabled; }
    }
    if (channel_id != 0) {
        if (!query_scope("channel", channel_id, found)) { pool_->release(conn); return true; }
        if (found) { pool_->release(conn); return enabled; }
    }
    for (uint64_t r : roles) {
        if (!query_scope("role", r, found)) { pool_->release(conn); return true; }
        if (found) {
            bool this_enabled = enabled;
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return this_enabled;
        }
    }
    const char* q2 = "SELECT enabled FROM guild_module_settings WHERE guild_id = ? AND module = ? LIMIT 1";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q2, strlen(q2)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_guild_module_enabled default prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return true;
    }
    MYSQL_BIND bind2[2];
    memset(bind2, 0, sizeof(bind2));
    bind2[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind2[0].buffer = (char*)&guild_id;
    bind2[0].is_unsigned = 1;
    bind2[1].buffer_type = MYSQL_TYPE_STRING;
    bind2[1].buffer = (char*)module.c_str();
    bind2[1].buffer_length = module.length();
    mysql_stmt_bind_param(stmt, bind2);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_guild_module_enabled default execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return true;
    }
    MYSQL_BIND rb2;
    memset(&rb2, 0, sizeof(rb2));
    rb2.buffer_type = MYSQL_TYPE_TINY;
    rb2.buffer = (char*)&enabled;
    rb2.is_unsigned = 0;
    mysql_stmt_bind_result(stmt, &rb2);
    if (mysql_stmt_fetch(stmt) != 0) {
        enabled = true;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return enabled;
}

std::vector<std::string> Database::get_disabled_modules(uint64_t guild_id) {
    std::vector<std::string> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT module FROM guild_module_settings WHERE guild_id = ? AND enabled = FALSE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_disabled_modules prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_disabled_modules execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char buf[128]; unsigned long len = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = buf;
    result_bind[0].buffer_length = sizeof(buf);
    result_bind[0].length = &len;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(buf, len);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::ModuleSettingRow> Database::get_all_module_settings(uint64_t guild_id) {
    std::vector<ModuleSettingRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT module, enabled FROM guild_module_settings WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char name_buf[128]; unsigned long name_len = 0;
    bool enabled_val = false;
    MYSQL_BIND result[2];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = name_buf;
    result[0].buffer_length = sizeof(name_buf);
    result[0].length = &name_len;
    result[1].buffer_type = MYSQL_TYPE_TINY;
    result[1].buffer = (char*)&enabled_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({std::string(name_buf, name_len), enabled_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::CommandSettingRow> Database::get_all_command_settings(uint64_t guild_id) {
    std::vector<CommandSettingRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT command, enabled FROM guild_command_settings WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char name_buf[128]; unsigned long name_len = 0;
    bool enabled_val = false;
    MYSQL_BIND result[2];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = name_buf;
    result[0].buffer_length = sizeof(name_buf);
    result[0].length = &name_len;
    result[1].buffer_type = MYSQL_TYPE_TINY;
    result[1].buffer = (char*)&enabled_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({std::string(name_buf, name_len), enabled_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::ScopedSettingRow> Database::get_all_module_scope_settings(uint64_t guild_id) {
    std::vector<ScopedSettingRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT module, scope_type, scope_id, enabled, exclusive FROM guild_module_scope_settings WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_scope_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_scope_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char name_buf[128]; unsigned long name_len = 0;
    char scope_buf[32]; unsigned long scope_len = 0;
    uint64_t scope_id_val = 0;
    bool enabled_val = false;
    bool exclusive_val = false;
    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = name_buf;
    result[0].buffer_length = sizeof(name_buf);
    result[0].length = &name_len;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = scope_buf;
    result[1].buffer_length = sizeof(scope_buf);
    result[1].length = &scope_len;
    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = (char*)&scope_id_val;
    result[2].is_unsigned = 1;
    result[3].buffer_type = MYSQL_TYPE_TINY;
    result[3].buffer = (char*)&enabled_val;
    result[4].buffer_type = MYSQL_TYPE_TINY;
    result[4].buffer = (char*)&exclusive_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({std::string(name_buf, name_len), std::string(scope_buf, scope_len), scope_id_val, enabled_val, exclusive_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::ScopedSettingRow> Database::get_all_command_scope_settings(uint64_t guild_id) {
    std::vector<ScopedSettingRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT command, scope_type, scope_id, enabled, exclusive FROM guild_command_scope_settings WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_scope_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_scope_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    char name_buf[128]; unsigned long name_len = 0;
    char scope_buf[32]; unsigned long scope_len = 0;
    uint64_t scope_id_val = 0;
    bool enabled_val = false;
    bool exclusive_val = false;
    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = name_buf;
    result[0].buffer_length = sizeof(name_buf);
    result[0].length = &name_len;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = scope_buf;
    result[1].buffer_length = sizeof(scope_buf);
    result[1].length = &scope_len;
    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = (char*)&scope_id_val;
    result[2].is_unsigned = 1;
    result[3].buffer_type = MYSQL_TYPE_TINY;
    result[3].buffer = (char*)&enabled_val;
    result[4].buffer_type = MYSQL_TYPE_TINY;
    result[4].buffer = (char*)&exclusive_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({std::string(name_buf, name_len), std::string(scope_buf, scope_len), scope_id_val, enabled_val, exclusive_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// ========================================================================
// BULK SETTINGS FETCH — one query per table, all guilds at once
// ========================================================================

std::vector<Database::GuildPrefixRow> Database::get_all_guild_prefixes_bulk() {
    std::vector<GuildPrefixRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT guild_id, prefix FROM guild_prefixes";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_guild_prefixes_bulk prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_guild_prefixes_bulk execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    uint64_t gid = 0;
    char prefix_buf[64]; unsigned long prefix_len = 0;
    MYSQL_BIND result[2];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = (char*)&gid;
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = prefix_buf;
    result[1].buffer_length = sizeof(prefix_buf);
    result[1].length = &prefix_len;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({gid, std::string(prefix_buf, prefix_len)});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::GuildModuleRow> Database::get_all_module_settings_bulk() {
    std::vector<GuildModuleRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT guild_id, module, enabled FROM guild_module_settings";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_settings_bulk prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_module_settings_bulk execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    uint64_t gid = 0;
    char name_buf[128]; unsigned long name_len = 0;
    bool enabled_val = false;
    MYSQL_BIND result[3];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = (char*)&gid;
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name_buf;
    result[1].buffer_length = sizeof(name_buf);
    result[1].length = &name_len;
    result[2].buffer_type = MYSQL_TYPE_TINY;
    result[2].buffer = (char*)&enabled_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({gid, std::string(name_buf, name_len), enabled_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<Database::GuildCommandRow> Database::get_all_command_settings_bulk() {
    std::vector<GuildCommandRow> out;
    auto conn = pool_->acquire();
    const char* query = "SELECT guild_id, command, enabled FROM guild_command_settings";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_settings_bulk prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_all_command_settings_bulk execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    uint64_t gid = 0;
    char name_buf[128]; unsigned long name_len = 0;
    bool enabled_val = false;
    MYSQL_BIND result[3];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = (char*)&gid;
    result[0].is_unsigned = 1;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name_buf;
    result[1].buffer_length = sizeof(name_buf);
    result[1].length = &name_len;
    result[2].buffer_type = MYSQL_TYPE_TINY;
    result[2].buffer = (char*)&enabled_val;
    mysql_stmt_bind_result(stmt, result);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({gid, std::string(name_buf, name_len), enabled_val});
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

} // namespace db
} // namespace bronx
namespace bronx {
namespace db {
namespace permission_operations {
    bool set_guild_command_enabled(Database* db, uint64_t guild_id, const std::string& command, bool enabled,
                                   const std::string& scope_type, uint64_t scope_id) {
        return db->set_guild_command_enabled(guild_id, command, enabled, scope_type, scope_id);
    }
    bool is_guild_command_enabled(Database* db, uint64_t guild_id, const std::string& command,
                                   uint64_t user_id, uint64_t channel_id,
                                   const std::vector<uint64_t>& roles) {
        return db->is_guild_command_enabled(guild_id, command, user_id, channel_id, roles);
    }
    std::vector<std::string> get_disabled_commands(Database* db, uint64_t guild_id) {
        return db->get_disabled_commands(guild_id);
    }
    bool set_guild_module_enabled(Database* db, uint64_t guild_id, const std::string& module, bool enabled,
                                  const std::string& scope_type, uint64_t scope_id) {
        return db->set_guild_module_enabled(guild_id, module, enabled, scope_type, scope_id);
    }
    bool is_guild_module_enabled(Database* db, uint64_t guild_id, const std::string& module,
                                  uint64_t user_id, uint64_t channel_id,
                                  const std::vector<uint64_t>& roles) {
        return db->is_guild_module_enabled(guild_id, module, user_id, channel_id, roles);
    }
    std::vector<std::string> get_disabled_modules(Database* db, uint64_t guild_id) {
        return db->get_disabled_modules(guild_id);
    }
}

// User permission operations
namespace permission_operations {
    
    // Check if user is admin for this server
    // ALWAYS checks server-specific permissions first, not global database
    bool is_admin(Database* db, uint64_t user_id, uint64_t guild_id) {
        // Check server-specific admin
        if (is_server_admin(db, guild_id, user_id)) {
            return true;
        }
        
        // Only check global admin for bot-wide developers
        auto user = db->get_user(user_id);
        if (user && (user->dev || user->admin)) {
            return true;
        }
        
        return false;
    }
    
    // Check if user is mod for this server
    // ALWAYS checks server-specific permissions first
    bool is_mod(Database* db, uint64_t user_id, uint64_t guild_id) {
        // Admins are also mods
        if (is_admin(db, user_id, guild_id)) {
            return true;
        }
        
        // Check server-specific mod
        if (is_server_mod(db, guild_id, user_id)) {
            return true;
        }
        
        return false;
    }
    
    // Check if user is bot developer (global only)
    bool is_dev(Database* db, uint64_t user_id) {
        auto user = db->get_user(user_id);
        return user && user->dev;
    }
    
    // Server-specific admin management
    bool add_server_admin(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t granted_by) {
        db->ensure_user_exists(user_id);
        auto conn = db->get_pool()->acquire();
        
        const char* query = "INSERT INTO server_bot_admins (guild_id, user_id, granted_by) "
                           "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE granted_by = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("add_server_admin prepare");
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
        bind[2].buffer = (char*)&granted_by;
        bind[2].is_unsigned = 1;
        
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&granted_by;
        bind[3].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("add_server_admin bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool remove_server_admin(Database* db, uint64_t guild_id, uint64_t user_id) {
        auto conn = db->get_pool()->acquire();
        
        const char* query = "DELETE FROM server_bot_admins WHERE guild_id = ? AND user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("remove_server_admin prepare");
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
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("remove_server_admin bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool is_server_admin(Database* db, uint64_t guild_id, uint64_t user_id) {
        auto conn = db->get_pool()->acquire();
        
        const char* query = "SELECT user_id FROM server_bot_admins WHERE guild_id = ? AND user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("is_server_admin prepare");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
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
            db->log_error("is_server_admin bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            db->log_error("is_server_admin execute");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        uint64_t result_id;
        MYSQL_BIND bind_result[1];
        memset(bind_result, 0, sizeof(bind_result));
        bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind_result[0].buffer = &result_id;
        bind_result[0].is_unsigned = 1;
        
        if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
            db->log_error("is_server_admin bind result");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool exists = mysql_stmt_fetch(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return exists;
    }
    
    bool add_server_mod(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t granted_by) {
        db->ensure_user_exists(user_id);
        auto conn = db->get_pool()->acquire();
        
        const char* query = "INSERT INTO server_bot_mods (guild_id, user_id, granted_by) "
                           "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE granted_by = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("add_server_mod prepare");
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
        bind[2].buffer = (char*)&granted_by;
        bind[2].is_unsigned = 1;
        
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&granted_by;
        bind[3].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("add_server_mod bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool remove_server_mod(Database* db, uint64_t guild_id, uint64_t user_id) {
        auto conn = db->get_pool()->acquire();
        
        const char* query = "DELETE FROM server_bot_mods WHERE guild_id = ? AND user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("remove_server_mod prepare");
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
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("remove_server_mod bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool is_server_mod(Database* db, uint64_t guild_id, uint64_t user_id) {
        auto conn = db->get_pool()->acquire();
        
        const char* query = "SELECT user_id FROM server_bot_mods WHERE guild_id = ? AND user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("is_server_mod prepare");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
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
            db->log_error("is_server_mod bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        if (mysql_stmt_execute(stmt) != 0) {
            db->log_error("is_server_mod execute");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        uint64_t result_id;
        MYSQL_BIND bind_result[1];
        memset(bind_result, 0, sizeof(bind_result));
        bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind_result[0].buffer = &result_id;
        bind_result[0].is_unsigned = 1;
        
        if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
            db->log_error("is_server_mod bind result");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool exists = mysql_stmt_fetch(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return exists;
    }
    
    // Global permission management (for bot-wide permissions only)
    bool set_global_admin(Database* db, uint64_t user_id, bool is_admin) {
        db->ensure_user_exists(user_id);
        
        auto conn = db->get_pool()->acquire();
        const char* query = "UPDATE users SET admin = ? WHERE user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("set_global_admin prepare");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));
        
        my_bool admin_val = is_admin ? 1 : 0;
        bind[0].buffer_type = MYSQL_TYPE_TINY;
        bind[0].buffer = &admin_val;
        
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&user_id;
        bind[1].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("set_global_admin bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool set_global_mod(Database* db, uint64_t user_id, bool is_mod) {
        db->ensure_user_exists(user_id);
        
        auto conn = db->get_pool()->acquire();
        const char* query = "UPDATE users SET is_mod = ? WHERE user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("set_global_mod prepare");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));
        
        my_bool mod_val = is_mod ? 1 : 0;
        bind[0].buffer_type = MYSQL_TYPE_TINY;
        bind[0].buffer = &mod_val;
        
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&user_id;
        bind[1].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("set_global_mod bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    bool set_dev(Database* db, uint64_t user_id, bool is_dev) {
        db->ensure_user_exists(user_id);
        
        auto conn = db->get_pool()->acquire();
        const char* query = "UPDATE users SET dev = ? WHERE user_id = ?";
        
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            db->log_error("set_dev prepare");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));
        
        my_bool dev_val = is_dev ? 1 : 0;
        bind[0].buffer_type = MYSQL_TYPE_TINY;
        bind[0].buffer = &dev_val;
        
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&user_id;
        bind[1].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("set_dev bind");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return false;
        }
        
        bool success = mysql_stmt_execute(stmt) == 0;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        
        return success;
    }
    
    std::optional<UserPermissions> get_user_permissions(Database* db, uint64_t user_id) {
        auto user = db->get_user(user_id);
        if (!user) {
            return std::nullopt;
        }
        
        UserPermissions perms;
        perms.admin = user->admin;
        perms.mod = user->is_mod;
        perms.dev = user->dev;
        perms.vip = user->vip;
        
        return perms;
    }
    
}
}
}
