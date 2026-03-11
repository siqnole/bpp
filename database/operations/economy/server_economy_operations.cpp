#include "server_economy_operations.h"
#include "../user/user_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

namespace server_economy_operations {

bool create_guild_economy(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "INSERT IGNORE INTO guild_economy_settings (guild_id) VALUES (?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("create_guild_economy prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("create_guild_economy bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

std::optional<GuildEconomySettings> get_guild_economy_settings(Database* db, uint64_t guild_id) {
    create_guild_economy(db, guild_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT guild_id, economy_mode, starting_wallet, starting_bank_limit, "
                       "default_interest_rate, daily_cooldown, work_cooldown, beg_cooldown, "
                       "rob_cooldown, fish_cooldown, work_multiplier, gambling_multiplier, "
                       "fishing_multiplier, allow_gambling, allow_fishing, allow_trading, "
                       "allow_robbery, max_wallet, max_bank, max_networth, enable_tax, "
                       "transaction_tax_percent FROM guild_economy_settings WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_guild_economy_settings prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&guild_id;
    bind_param[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        db->log_error("get_guild_economy_settings bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_guild_economy_settings execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    GuildEconomySettings settings;
    char mode_buffer[16];
    long long max_wallet_val, max_bank_val, max_networth_val;
    my_bool max_wallet_null, max_bank_null, max_networth_null;
    my_bool gambling, fishing, trading, robbery, tax_enabled;
    unsigned long mode_length;
    
    MYSQL_BIND bind_result[22];
    memset(bind_result, 0, sizeof(bind_result));
    
    int col = 0;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &settings.guild_id; bind_result[col++].is_unsigned = 1;
    bind_result[col].buffer_type = MYSQL_TYPE_STRING; bind_result[col].buffer = mode_buffer; bind_result[col].buffer_length = sizeof(mode_buffer); bind_result[col++].length = &mode_length;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &settings.starting_wallet;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &settings.starting_bank_limit;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &settings.default_interest_rate;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &settings.daily_cooldown;
    bind_result[col].buffer_type   = MYSQL_TYPE_LONG; bind_result[col++].buffer = &settings.work_cooldown;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &settings.beg_cooldown;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &settings.rob_cooldown;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &settings.fish_cooldown;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &settings.work_multiplier;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &settings.gambling_multiplier;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &settings.fishing_multiplier;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &gambling;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &fishing;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &trading;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &robbery;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &max_wallet_val; bind_result[col++].is_null = &max_wallet_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &max_bank_val; bind_result[col++].is_null = &max_bank_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &max_networth_val; bind_result[col++].is_null = &max_networth_null;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &tax_enabled;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &settings.transaction_tax_percent;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        db->log_error("get_guild_economy_settings bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    settings.economy_mode = std::string(mode_buffer, mode_length);
    settings.allow_gambling = gambling != 0;
    settings.allow_fishing = fishing != 0;
    settings.allow_trading = trading != 0;
    settings.allow_robbery = robbery != 0;
    settings.enable_tax = tax_enabled != 0;
    
    settings.max_wallet = max_wallet_null ? std::nullopt : std::optional<int64_t>(max_wallet_val);
    settings.max_bank = max_bank_null ? std::nullopt : std::optional<int64_t>(max_bank_val);
    settings.max_networth = max_networth_null ? std::nullopt : std::optional<int64_t>(max_networth_val);
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return settings;
}

bool set_economy_mode(Database* db, uint64_t guild_id, const std::string& mode) {
    create_guild_economy(db, guild_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "UPDATE guild_economy_settings SET economy_mode = ? WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("set_economy_mode prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    unsigned long mode_length = mode.length();
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)mode.c_str();
    bind[0].buffer_length = mode.length();
    bind[0].length = &mode_length;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("set_economy_mode bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

bool is_server_economy(Database* db, uint64_t guild_id) {
    auto settings = get_guild_economy_settings(db, guild_id);
    if (!settings) return false;
    return settings->economy_mode == "server";
}

bool ensure_server_user_exists(Database* db, uint64_t guild_id, uint64_t user_id) {
    // Ensure global user exists first
    db->ensure_user_exists(user_id);
    
    auto conn = db->get_pool()->acquire();
    
    // Get guild's starting values
    auto settings = get_guild_economy_settings(db, guild_id);
    if (!settings) return false;
    
    const char* query = "INSERT IGNORE INTO server_users (guild_id, user_id, wallet, bank_limit, interest_rate) "
                       "VALUES (?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("ensure_server_user_exists prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&settings->starting_wallet;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&settings->starting_bank_limit;
    
    bind[4].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[4].buffer = (char*)&settings->default_interest_rate;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("ensure_server_user_exists bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

std::optional<ServerUserData> get_server_user(Database* db, uint64_t guild_id, uint64_t user_id) {
    ensure_server_user_exists(db, guild_id, user_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT guild_id, user_id, wallet, bank, bank_limit, interest_rate, "
                       "interest_level, UNIX_TIMESTAMP(last_interest_claim), UNIX_TIMESTAMP(last_daily), "
                       "UNIX_TIMESTAMP(last_work), UNIX_TIMESTAMP(last_beg), total_gambled, "
                       "total_won, total_lost, commands_used FROM server_users "
                       "WHERE guild_id = ? AND user_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_server_user prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
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
        db->log_error("get_server_user bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_server_user execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    ServerUserData data;
    MYSQL_BIND bind_result[15];
    memset(bind_result, 0, sizeof(bind_result));
    
    long long interest_claim_ts = 0, daily_ts = 0, work_ts = 0, beg_ts = 0;
    my_bool interest_null = 1, daily_null = 1, work_null = 1, beg_null = 1;
    
    int col = 0;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &data.guild_id; bind_result[col++].is_unsigned = 1;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &data.user_id; bind_result[col++].is_unsigned = 1;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.wallet;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.bank;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.bank_limit;
    bind_result[col].buffer_type = MYSQL_TYPE_DOUBLE; bind_result[col++].buffer = &data.interest_rate;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &data.interest_level;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &interest_claim_ts; bind_result[col++].is_null = &interest_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &daily_ts; bind_result[col++].is_null = &daily_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &work_ts; bind_result[col++].is_null = &work_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &beg_ts; bind_result[col++].is_null = &beg_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_gambled;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_won;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_lost;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &data.commands_used;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        db->log_error("get_server_user bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (!interest_null) {
        data.last_interest_claim = std::chrono::system_clock::from_time_t(interest_claim_ts);
    }
    if (!daily_null) {
        data.last_daily = std::chrono::system_clock::from_time_t(daily_ts);
    }
    if (!work_null) {
        data.last_work = std::chrono::system_clock::from_time_t(work_ts);
    }
    if (!beg_null) {
        data.last_beg = std::chrono::system_clock::from_time_t(beg_ts);
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return data;
}

int64_t get_server_wallet(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto user = get_server_user(db, guild_id, user_id);
    return user ? user->wallet : 0;
}

int64_t get_server_bank(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto user = get_server_user(db, guild_id, user_id);
    return user ? user->bank : 0;
}

int64_t get_server_bank_limit(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto user = get_server_user(db, guild_id, user_id);
    return user ? user->bank_limit : 0;
}

int64_t get_server_networth(Database* db, uint64_t guild_id, uint64_t user_id) {
    auto user = get_server_user(db, guild_id, user_id);
    return user ? (user->wallet + user->bank) : 0;
}

std::optional<int64_t> update_server_wallet(Database* db, uint64_t guild_id, uint64_t user_id, int64_t amount) {
    ensure_server_user_exists(db, guild_id, user_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "UPDATE server_users SET wallet = wallet + ? "
                       "WHERE guild_id = ? AND user_id = ? AND wallet + ? >= 0";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("update_server_wallet prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&amount;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("update_server_wallet bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    if (affected == 0) {
        return std::nullopt;
    }
    
    return get_server_wallet(db, guild_id, user_id);
}

std::optional<int64_t> update_server_bank(Database* db, uint64_t guild_id, uint64_t user_id, int64_t amount) {
    ensure_server_user_exists(db, guild_id, user_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "UPDATE server_users SET bank = bank + ? "
                       "WHERE guild_id = ? AND user_id = ? AND bank + ? >= 0 AND bank + ? <= bank_limit";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("update_server_bank prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&amount;
    
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&amount;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("update_server_bank bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    if (affected == 0) {
        return std::nullopt;
    }
    
    return get_server_bank(db, guild_id, user_id);
}

TransactionResult transfer_server_money(Database* db, uint64_t guild_id, uint64_t from_user, uint64_t to_user, int64_t amount) {
    if (amount <= 0) {
        return TransactionResult::InvalidAmount;
    }
    
    ensure_server_user_exists(db, guild_id, from_user);
    ensure_server_user_exists(db, guild_id, to_user);
    
    auto from_wallet = get_server_wallet(db, guild_id, from_user);
    if (from_wallet < amount) {
        return TransactionResult::InsufficientFunds;
    }
    
    auto conn = db->get_pool()->acquire();
    
    // Use stored procedure
    const char* query = "CALL sp_server_transfer_money(?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("transfer_server_money prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return TransactionResult::DatabaseError;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&from_user;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&to_user;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&amount;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("transfer_server_money bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return TransactionResult::DatabaseError;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return TransactionResult::DatabaseError;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return TransactionResult::Success;
}

// Guild giveaway balance operations (funded by tax)

bool ensure_guild_balance_exists(Database* db, uint64_t guild_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "INSERT IGNORE INTO guild_balances (guild_id, balance, total_donated, total_given) VALUES (?, 0, 0, 0)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("ensure_guild_balance_exists prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("ensure_guild_balance_exists bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

int64_t get_guild_giveaway_balance(Database* db, uint64_t guild_id) {
    ensure_guild_balance_exists(db, guild_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT balance FROM guild_balances WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_guild_giveaway_balance prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&guild_id;
    bind_param[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        db->log_error("get_guild_giveaway_balance bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_guild_giveaway_balance execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    int64_t balance = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &balance;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        db->log_error("get_guild_giveaway_balance bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }
    
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return balance;
}

bool add_to_guild_balance(Database* db, uint64_t guild_id, int64_t amount) {
    if (amount <= 0) return false;
    ensure_guild_balance_exists(db, guild_id);
    
    auto conn = db->get_pool()->acquire();
    
    const char* query = "UPDATE guild_balances SET balance = balance + ?, total_donated = total_donated + ? WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("add_to_guild_balance prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&amount;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&guild_id;
    bind[2].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("add_to_guild_balance bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

// Unified operations that check economy mode and route accordingly

bool ensure_user_exists_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return ensure_server_user_exists(db, *guild_id, user_id);
    }
    return db->ensure_user_exists(user_id);
}

int64_t get_wallet_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return get_server_wallet(db, *guild_id, user_id);
    }
    return db->get_wallet(user_id);
}

int64_t get_bank_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return get_server_bank(db, *guild_id, user_id);
    }
    return db->get_bank(user_id);
}

int64_t get_networth_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return get_server_networth(db, *guild_id, user_id);
    }
    return db->get_networth(user_id);
}

std::optional<int64_t> update_wallet_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id, int64_t amount) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return update_server_wallet(db, *guild_id, user_id, amount);
    }
    return db->update_wallet(user_id, amount);
}

std::optional<int64_t> update_bank_unified(Database* db, uint64_t user_id, std::optional<uint64_t> guild_id, int64_t amount) {
    if (guild_id && is_server_economy(db, *guild_id)) {
        return update_server_bank(db, *guild_id, user_id, amount);
    }
    return db->update_bank(user_id, amount);
}

bool log_server_command(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& command_name) {
    // Ensure guild_economy_settings row exists (FK requirement for server_command_stats)
    create_guild_economy(db, guild_id);

    auto conn = db->get_pool()->acquire();

    const char* query = "INSERT INTO server_command_stats (guild_id, user_id, command_name) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("log_server_command prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    unsigned long name_len = command_name.size();

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)command_name.c_str();
    bind[2].buffer_length = name_len;
    bind[2].length = &name_len;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("log_server_command bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    if (!success) {
        // Silently ignore — table may not exist yet for this guild
    }
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

} // namespace server_economy_operations

} // namespace db
} // namespace bronx
