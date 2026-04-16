#include "logging_operations.h"
#include "../../core/database.h"
#include "../../../log.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ────────────────────────────────────────────────────────────────────────
// Database Member Functions (C API)
// ────────────────────────────────────────────────────────────────────────

bool Database::set_log_config(const LogConfig& config) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO guild_log_configs (guild_id, log_type, channel_id, webhook_url, webhook_id, webhook_token, enabled) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?) "
                        "ON DUPLICATE KEY UPDATE channel_id = VALUES(channel_id), webhook_url = VALUES(webhook_url), "
                        "webhook_id = VALUES(webhook_id), webhook_token = VALUES(webhook_token), enabled = VALUES(enabled)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_log_config prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));

    uint64_t guild_id = config.guild_id;
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)config.log_type.c_str();
    bind[1].buffer_length = config.log_type.size();

    uint64_t channel_id = config.channel_id;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&channel_id;
    bind[2].is_unsigned = 1;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)config.webhook_url.c_str();
    bind[3].buffer_length = config.webhook_url.size();

    uint64_t webhook_id = config.webhook_id;
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&webhook_id;
    bind[4].is_unsigned = 1;

    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = (char*)config.webhook_token.c_str();
    bind[5].buffer_length = config.webhook_token.size();

    char enabled = config.enabled ? 1 : 0;
    bind[6].buffer_type = MYSQL_TYPE_TINY;
    bind[6].buffer = &enabled;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_log_config bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = true;
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_log_config execute");
        success = false;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<LogConfig> Database::get_log_config(uint64_t guild_id, const std::string& log_type) {
    auto conn = pool_->acquire();
    const char* query = "SELECT channel_id, webhook_url, webhook_id, webhook_token, enabled FROM guild_log_configs WHERE guild_id = ? AND log_type = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_log_config prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND param_bind[2];
    memset(param_bind, 0, sizeof(param_bind));

    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = (char*)&guild_id;
    param_bind[0].is_unsigned = 1;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = (char*)log_type.c_str();
    param_bind[1].buffer_length = log_type.size();

    if (mysql_stmt_bind_param(stmt, param_bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    uint64_t channel_id;
    char url_buf[1024]; unsigned long url_len = 0;
    uint64_t webhook_id;
    char token_buf[512]; unsigned long token_len = 0;
    char enabled;

    MYSQL_BIND result_bind[5];
    memset(result_bind, 0, sizeof(result_bind));

    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&channel_id;
    result_bind[0].is_unsigned = 1;

    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = url_buf;
    result_bind[1].buffer_length = sizeof(url_buf);
    result_bind[1].length = &url_len;

    result_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[2].buffer = (char*)&webhook_id;
    result_bind[2].is_unsigned = 1;

    result_bind[3].buffer_type = MYSQL_TYPE_STRING;
    result_bind[3].buffer = token_buf;
    result_bind[3].buffer_length = sizeof(token_buf);
    result_bind[3].length = &token_len;

    result_bind[4].buffer_type = MYSQL_TYPE_TINY;
    result_bind[4].buffer = &enabled;

    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);

    std::optional<LogConfig> result = std::nullopt;
    if (mysql_stmt_fetch(stmt) == 0) {
        LogConfig cfg;
        cfg.guild_id = guild_id;
        cfg.log_type = log_type;
        cfg.channel_id = channel_id;
        cfg.webhook_url = std::string(url_buf, url_len);
        cfg.webhook_id = webhook_id;
        cfg.webhook_token = std::string(token_buf, token_len);
        cfg.enabled = enabled != 0;
        result = cfg;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

std::vector<LogConfig> Database::get_all_log_configs(uint64_t guild_id) {
    auto conn = pool_->acquire();
    std::vector<LogConfig> configs;
    const char* query = "SELECT log_type, channel_id, webhook_url, webhook_id, webhook_token, enabled FROM guild_log_configs WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return configs;
    }

    MYSQL_BIND param_bind[1];
    memset(param_bind, 0, sizeof(param_bind));
    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = (char*)&guild_id;
    param_bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, param_bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return configs;
    }

    char type_buf[128]; unsigned long type_len = 0;
    uint64_t channel_id;
    char url_buf[1024]; unsigned long url_len = 0;
    uint64_t webhook_id;
    char token_buf[512]; unsigned long token_len = 0;
    char enabled;

    MYSQL_BIND result_bind[6];
    memset(result_bind, 0, sizeof(result_bind));

    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = type_buf;
    result_bind[0].buffer_length = sizeof(type_buf);
    result_bind[0].length = &type_len;

    result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[1].buffer = (char*)&channel_id;
    result_bind[1].is_unsigned = 1;

    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = url_buf;
    result_bind[2].buffer_length = sizeof(url_buf);
    result_bind[2].length = &url_len;

    result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[3].buffer = (char*)&webhook_id;
    result_bind[3].is_unsigned = 1;

    result_bind[4].buffer_type = MYSQL_TYPE_STRING;
    result_bind[4].buffer = token_buf;
    result_bind[4].buffer_length = sizeof(token_buf);
    result_bind[4].length = &token_len;

    result_bind[5].buffer_type = MYSQL_TYPE_TINY;
    result_bind[5].buffer = &enabled;

    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        LogConfig cfg;
        cfg.guild_id = guild_id;
        cfg.log_type = std::string(type_buf, type_len);
        cfg.channel_id = channel_id;
        cfg.webhook_url = std::string(url_buf, url_len);
        cfg.webhook_id = webhook_id;
        cfg.webhook_token = std::string(token_buf, token_len);
        cfg.enabled = enabled != 0;
        configs.push_back(cfg);
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return configs;
}

bool Database::delete_log_config(uint64_t guild_id, const std::string& log_type) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM guild_log_configs WHERE guild_id = ? AND log_type = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)log_type.c_str();
    bind[1].buffer_length = log_type.size();

    bool success = true;
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        success = false;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::clear_all_log_configs(uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM guild_log_configs WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bool success = true;
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        success = false;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_guild_beta_tester(uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT beta_tester FROM guild_settings WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND param_bind[1];
    memset(param_bind, 0, sizeof(param_bind));
    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = (char*)&guild_id;
    param_bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, param_bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    char beta_tester = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &beta_tester;

    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);

    bool is_tester = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        is_tester = beta_tester != 0;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return is_tester;
}

bool Database::set_guild_beta_tester(uint64_t guild_id, bool is_beta) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO guild_settings (guild_id, beta_tester) VALUES (?, ?) "
                        "ON DUPLICATE KEY UPDATE beta_tester = VALUES(beta_tester)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    char beta = is_beta ? 1 : 0;
    bind[1].buffer_type = MYSQL_TYPE_TINY;
    bind[1].buffer = &beta;

    bool success = true;
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        success = false;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

// ────────────────────────────────────────────────────────────────────────
// Operations Wrappers
// ────────────────────────────────────────────────────────────────────────

namespace logging_operations {

    bool set_log_config(Database* db, const LogConfig& config) {
        if (!db) return false;
        return db->set_log_config(config);
    }
    
    std::optional<LogConfig> get_log_config(Database* db, uint64_t guild_id, const std::string& log_type) {
        if (!db) return std::nullopt;
        return db->get_log_config(guild_id, log_type);
    }
    
    std::vector<LogConfig> get_all_log_configs(Database* db, uint64_t guild_id) {
        if (!db) return {};
        return db->get_all_log_configs(guild_id);
    }
    
    bool delete_log_config(Database* db, uint64_t guild_id, const std::string& log_type) {
        if (!db) return false;
        return db->delete_log_config(guild_id, log_type);
    }
    
    bool clear_all_log_configs(Database* db, uint64_t guild_id) {
        if (!db) return false;
        return db->clear_all_log_configs(guild_id);
    }
    
    bool is_guild_beta_tester(Database* db, uint64_t guild_id) {
        if (!db) return false;
        return db->is_guild_beta_tester(guild_id);
    }
    
    bool set_guild_beta_tester(Database* db, uint64_t guild_id, bool is_beta) {
        if (!db) return false;
        return db->set_guild_beta_tester(guild_id, is_beta);
    }

} // namespace logging_operations
} // namespace db
} // namespace bronx
