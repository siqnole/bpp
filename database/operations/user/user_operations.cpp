#include "user_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace bronx {
namespace db {

bool Database::ensure_user_exists(uint64_t user_id) {
    auto conn = pool_->acquire();
    
    const char* query = "INSERT IGNORE INTO users (user_id) VALUES (?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("ensure_user_exists prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("ensure_user_exists bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}

std::vector<uint64_t> Database::get_all_user_ids() {
    std::vector<uint64_t> user_ids;
    auto conn = pool_->acquire();
    
    const char* query = "SELECT user_id FROM users";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_all_user_ids prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return user_ids;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_user_ids execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return user_ids;
    }
    
    // Bind result
    uint64_t user_id;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &user_id;
    bind_result[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_all_user_ids bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return user_ids;
    }
    
    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("get_all_user_ids store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return user_ids;
    }
    
    while (mysql_stmt_fetch(stmt) == 0) {
        user_ids.push_back(user_id);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return user_ids;
}

std::optional<UserData> Database::get_user(uint64_t user_id) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    const char* query = "SELECT user_id, wallet, bank, bank_limit, interest_rate, interest_level, "
                       "UNIX_TIMESTAMP(last_interest_claim), UNIX_TIMESTAMP(last_daily), "
                       "UNIX_TIMESTAMP(last_work), UNIX_TIMESTAMP(last_beg), UNIX_TIMESTAMP(last_rob), "
                       "total_gambled, total_won, total_lost, dev, admin, is_mod, vip, passive, prestige "
                       "FROM users WHERE user_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_user prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    // Bind input parameter
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind_param) != 0) {
        log_error("get_user bind param");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_user execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    // Bind result
    UserData data;
    data.prestige = 0;
    MYSQL_BIND bind_result[20];
    memset(bind_result, 0, sizeof(bind_result));
    
    long long interest_claim_ts = 0, daily_ts = 0, work_ts = 0, beg_ts = 0, rob_ts = 0;
    my_bool interest_null = 1, daily_null = 1, work_null = 1, beg_null = 1, rob_null = 1;
    my_bool dev_val = 0, admin_val = 0, mod_val = 0, vip_val = 0, passive_val = 0;
    
    // Bind all 20 columns
    int col = 0;
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
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col].buffer = &rob_ts; bind_result[col++].is_null = &rob_null;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_gambled;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_won;
    bind_result[col].buffer_type = MYSQL_TYPE_LONGLONG; bind_result[col++].buffer = &data.total_lost;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &dev_val;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &admin_val;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &mod_val;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &vip_val;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &passive_val;
    bind_result[col].buffer_type = MYSQL_TYPE_LONG; bind_result[col++].buffer = &data.prestige;
    
    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_user bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    // Convert timestamps to time_points
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
    if (!rob_null) {
        data.last_rob = std::chrono::system_clock::from_time_t(rob_ts);
    }
    
    data.dev = dev_val != 0;
    data.is_mod = mod_val != 0;
    data.admin = admin_val != 0;
    data.vip = vip_val != 0;
    data.passive = passive_val != 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return data;
}

int64_t Database::get_wallet(uint64_t user_id) {
    auto user_data = get_user(user_id);
    return user_data ? user_data->wallet : 0;
}

int64_t Database::get_bank(uint64_t user_id) {
    auto user_data = get_user(user_id);
    return user_data ? user_data->bank : 0;
}

int64_t Database::get_bank_limit(uint64_t user_id) {
    auto user_data = get_user(user_id);
    return user_data ? user_data->bank_limit : 10000;
}

int64_t Database::get_networth(uint64_t user_id) {
    auto user_data = get_user(user_id);
    return user_data ? (user_data->wallet + user_data->bank) : 0;
}

int64_t Database::get_fish_inventory_value(uint64_t user_id) {
    auto inventory = get_inventory(user_id);
    int64_t total_value = 0;
    
    for (const auto& item : inventory) {
        // Fish are stored with item_type = "collectible" and metadata containing "value"
        if (item.item_type != "collectible") continue;
        
        const std::string& metadata = item.metadata;
        size_t value_pos = metadata.find("\"value\":");
        if (value_pos != std::string::npos) {
            size_t start = value_pos + 8;
            size_t end = metadata.find(",", start);
            if (end == std::string::npos) end = metadata.find("}", start);
            if (end != std::string::npos) {
                try {
                    int64_t fish_value = std::stoll(metadata.substr(start, end - start));
                    // Only count unlocked fish
                    size_t locked_pos = metadata.find("\"locked\":");
                    bool is_locked = false;
                    if (locked_pos != std::string::npos) {
                        is_locked = (metadata.find("true", locked_pos) != std::string::npos);
                    }
                    if (!is_locked) {
                        total_value += fish_value;
                    }
                } catch (...) {
                    // Skip invalid values
                }
            }
        }
    }
    
    return total_value;
}

int64_t Database::get_total_networth(uint64_t user_id) {
    return get_networth(user_id) + get_fish_inventory_value(user_id);
}

std::optional<int64_t> Database::update_wallet(uint64_t user_id, int64_t amount) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    // Start transaction
    mysql_query(conn->get(), "START TRANSACTION");
    
    // Get current balance with lock
    const char* select_query = "SELECT wallet FROM users WHERE user_id = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, select_query, strlen(select_query));
    
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind_param);
    mysql_stmt_execute(stmt);
    
    int64_t current_wallet = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &current_wallet;
    
    mysql_stmt_bind_result(stmt, bind_result);
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    mysql_stmt_close(stmt);
    
    // Calculate new balance
    int64_t new_balance = current_wallet + amount;
    
    // Check for overflow/underflow
    if (new_balance < 0) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    
    // Update wallet
    const char* update_query = "UPDATE users SET wallet = ? WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, update_query, strlen(update_query));
    
    MYSQL_BIND bind_update[2];
    memset(bind_update, 0, sizeof(bind_update));
    bind_update[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[0].buffer = &new_balance;
    bind_update[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[1].buffer = (char*)&user_id;
    bind_update[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind_update);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    
    mysql_stmt_close(stmt);
    mysql_query(conn->get(), "COMMIT");
    pool_->release(conn);
    
    return new_balance;
}

std::optional<int64_t> Database::update_bank(uint64_t user_id, int64_t amount) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    mysql_query(conn->get(), "START TRANSACTION");
    
    // Get current balance and limit with lock
    const char* select_query = "SELECT bank, bank_limit FROM users WHERE user_id = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, select_query, strlen(select_query));
    
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind_param);
    mysql_stmt_execute(stmt);
    
    int64_t current_bank = 0, bank_limit = 0;
    MYSQL_BIND bind_result[2];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &current_bank;
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &bank_limit;
    
    mysql_stmt_bind_result(stmt, bind_result);
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    mysql_stmt_close(stmt);
    
    // Calculate new balance
    int64_t new_balance = current_bank + amount;
    
    // Validate
    if (new_balance < 0 || new_balance > bank_limit) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    
    // Update bank
    const char* update_query = "UPDATE users SET bank = ? WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, update_query, strlen(update_query));
    
    MYSQL_BIND bind_update[2];
    memset(bind_update, 0, sizeof(bind_update));
    bind_update[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[0].buffer = &new_balance;
    bind_update[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[1].buffer = (char*)&user_id;
    bind_update[1].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind_update);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return std::nullopt;
    }
    
    mysql_stmt_close(stmt);
    mysql_query(conn->get(), "COMMIT");
    pool_->release(conn);
    
    return new_balance;
}

// update bank limit for a user, ensuring the limit is not below current balance
bool Database::update_bank_limit(uint64_t user_id, int64_t new_limit) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    mysql_query(conn->get(), "START TRANSACTION");

    // lock current bank and limit
    const char* select_query = "SELECT bank FROM users WHERE user_id = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, select_query, strlen(select_query));

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_param);
    mysql_stmt_execute(stmt);

    int64_t current_bank = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &current_bank;
    mysql_stmt_bind_result(stmt, bind_result);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);

    if (new_limit < current_bank) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }

    const char* update_query = "UPDATE users SET bank_limit = ? WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    mysql_stmt_prepare(stmt, update_query, strlen(update_query));

    MYSQL_BIND bind_update[2];
    memset(bind_update, 0, sizeof(bind_update));
    bind_update[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[0].buffer = &new_limit;
    bind_update[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[1].buffer = (char*)&user_id;
    bind_update[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_update);

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);

    if (success) {
        mysql_query(conn->get(), "COMMIT");
    } else {
        mysql_query(conn->get(), "ROLLBACK");
    }
    pool_->release(conn);
    return success;
}

// deposit money from wallet into bank (checks limits)
bool Database::deposit(uint64_t user_id, int64_t amount) {
    if (amount <= 0) return false;
    ensure_user_exists(user_id);

    auto conn = pool_->acquire();
    mysql_query(conn->get(), "START TRANSACTION");

    const char* select_query = "SELECT wallet, bank, bank_limit FROM users WHERE user_id = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, select_query, strlen(select_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("deposit select prepare");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_param);
    mysql_stmt_execute(stmt);

    int64_t current_wallet = 0, current_bank = 0, bank_limit = 0;
    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &current_wallet;
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &current_bank;
    bind_result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[2].buffer = &bank_limit;
    mysql_stmt_bind_result(stmt, bind_result);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);

    int64_t new_wallet = current_wallet - amount;
    int64_t new_bank = current_bank + amount;
    if (new_wallet < 0 || new_bank > bank_limit) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }

    const char* update_query = "UPDATE users SET wallet = ?, bank = ? WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, update_query, strlen(update_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("deposit update prepare");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind_update[3];
    memset(bind_update, 0, sizeof(bind_update));
    bind_update[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[0].buffer = &new_wallet;
    bind_update[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[1].buffer = &new_bank;
    bind_update[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[2].buffer = (char*)&user_id;
    bind_update[2].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_update);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("deposit update execute");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);
    mysql_query(conn->get(), "COMMIT");
    pool_->release(conn);
    return true;
}

bool Database::withdraw(uint64_t user_id, int64_t amount) {
    if (amount <= 0) return false;
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    mysql_query(conn->get(), "START TRANSACTION");
    const char* select_query = "SELECT wallet, bank FROM users WHERE user_id = ? FOR UPDATE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, select_query, strlen(select_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("withdraw select prepare");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (char*)&user_id;
    bind_param[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_param);
    mysql_stmt_execute(stmt);
    int64_t current_wallet = 0, current_bank = 0;
    MYSQL_BIND bind_result[2];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &current_wallet;
    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &current_bank;
    mysql_stmt_bind_result(stmt, bind_result);
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);
    if (current_bank < amount) {
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    const char* update_query = "UPDATE users SET wallet = ?, bank = ? WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, update_query, strlen(update_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("withdraw update prepare");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    int64_t new_wallet = current_wallet + amount;
    int64_t new_bank = current_bank - amount;
    MYSQL_BIND bind_update[3];
    memset(bind_update, 0, sizeof(bind_update));
    bind_update[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[0].buffer = &new_wallet;
    bind_update[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[1].buffer = &new_bank;
    bind_update[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_update[2].buffer = (char*)&user_id;
    bind_update[2].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind_update);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("withdraw update execute");
        mysql_stmt_close(stmt);
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);
    mysql_query(conn->get(), "COMMIT");
    pool_->release(conn);
    return true;
}


TransactionResult Database::transfer_money(uint64_t from_user, uint64_t to_user, int64_t amount) {
    if (amount <= 0) return TransactionResult::InvalidAmount;
    
    ensure_user_exists(from_user);
    ensure_user_exists(to_user);
    
    auto conn = pool_->acquire();
    
    // Call stored procedure for atomic transfer
    const char* query = "CALL sp_transfer_money(?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("transfer_money prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return TransactionResult::DatabaseError;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&from_user;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&to_user;
    bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&amount;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        std::string error = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        
        if (error.find("Insufficient funds") != std::string::npos) {
            return TransactionResult::InsufficientFunds;
        }
        return TransactionResult::DatabaseError;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return TransactionResult::Success;
}

bool Database::is_passive(uint64_t user_id) {
    auto user = get_user(user_id);
    return user ? user->passive : false;
}

bool Database::set_passive(uint64_t user_id, bool passive) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    const char* query = "UPDATE users SET passive = ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("set_passive prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    my_bool passive_val = passive ? 1 : 0;
    bind[0].buffer_type = MYSQL_TYPE_TINY;
    bind[0].buffer = &passive_val;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("set_passive bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}

bool Database::increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount) {
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    // Use INSERT ... ON DUPLICATE KEY UPDATE to atomically increment the stat
    const char* query = 
        "INSERT INTO user_stats (user_id, stat_name, stat_value) VALUES (?, ?, ?) "
        "ON DUPLICATE KEY UPDATE stat_value = stat_value + VALUES(stat_value)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("increment_stat prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)stat_name.c_str();
    bind[1].buffer_length = stat_name.length();
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&amount;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("increment_stat bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    if (!success) {
        log_error("increment_stat execute");
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}

int64_t Database::get_stat(uint64_t user_id, const std::string& stat_name) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT stat_value FROM user_stats WHERE user_id = ? AND stat_name = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)stat_name.c_str();
    bind[1].buffer_length = stat_name.length();
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int64_t value = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &value;
    
    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);
    
    if (mysql_stmt_fetch(stmt) != 0) {
        value = 0; // stat doesn't exist
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return value;
}

// ========================================
// PRESTIGE OPERATIONS
// ========================================

int Database::get_prestige(uint64_t user_id) {
    auto user = get_user(user_id);
    return user ? user->prestige : 0;
}

bool Database::set_prestige(uint64_t user_id, int prestige) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    
    const char* query = "UPDATE users SET prestige = ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("set_prestige prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &prestige;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("set_prestige bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::increment_prestige(uint64_t user_id) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    
    const char* query = "UPDATE users SET prestige = prestige + 1 WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("increment_prestige prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("increment_prestige bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::perform_prestige(uint64_t user_id) {
    ensure_user_exists(user_id);
    auto conn = pool_->acquire();
    
    // Start transaction
    if (mysql_query(conn->get(), "START TRANSACTION") != 0) {
        log_error("perform_prestige start transaction");
        pool_->release(conn);
        return false;
    }
    
    // 1. Set wallet and bank to 0, increment prestige
    const char* reset_query = "UPDATE users SET wallet = 0, bank = 0, prestige = prestige + 1 WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, reset_query, strlen(reset_query)) != 0) {
        log_error("perform_prestige reset prepare");
        mysql_query(conn->get(), "ROLLBACK");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        log_error("perform_prestige reset execute");
        mysql_query(conn->get(), "ROLLBACK");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    mysql_stmt_close(stmt);
    
    // 2. Delete fish inventory (unsold fish catches)
    const char* fish_query = "DELETE FROM fish_catches WHERE user_id = ? AND sold = FALSE";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, fish_query, strlen(fish_query)) == 0) {
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
    }
    mysql_stmt_close(stmt);
    
    // 3. Delete from inventory: fish, bait, rods, upgrades, potions (keep titles and other items)
    const char* inv_query = "DELETE FROM inventory WHERE user_id = ? AND item_type IN ('collectible', 'bait', 'rod', 'upgrade', 'potion')";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, inv_query, strlen(inv_query)) == 0) {
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
    }
    mysql_stmt_close(stmt);
    
    // 4. Reset active fishing gear
    const char* gear_query = "DELETE FROM active_fishing_gear WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, gear_query, strlen(gear_query)) == 0) {
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
    }
    mysql_stmt_close(stmt);
    
    // 5. Delete autofisher storage
    const char* afs_query = "DELETE FROM autofish_storage WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, afs_query, strlen(afs_query)) == 0) {
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
    }
    mysql_stmt_close(stmt);
    
    // 6. Delete autofisher entry
    const char* af_query = "DELETE FROM autofishers WHERE user_id = ?";
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, af_query, strlen(af_query)) == 0) {
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
    }
    mysql_stmt_close(stmt);
    
    // Commit transaction
    if (mysql_query(conn->get(), "COMMIT") != 0) {
        log_error("perform_prestige commit");
        mysql_query(conn->get(), "ROLLBACK");
        pool_->release(conn);
        return false;
    }
    
    pool_->release(conn);
    return true;
}

// ========================================
// FISH COUNTING OPERATIONS
// ========================================

uint64_t Database::add_fish_catch(uint64_t user_id, const std::string& rarity, const std::string& fish_name, 
                                   double weight, int64_t value, const std::string& rod_id, const std::string& bait_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "INSERT INTO fish_catches (user_id, rarity, fish_name, weight, value, rod_id, bait_id, caught_at) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, NOW())";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));
    
    unsigned long rarity_len = rarity.length();
    unsigned long name_len = fish_name.length();
    unsigned long rod_len = rod_id.length();
    unsigned long bait_len = bait_id.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)rarity.c_str();
    bind[1].buffer_length = rarity.length();
    bind[1].length = &rarity_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)fish_name.c_str();
    bind[2].buffer_length = fish_name.length();
    bind[2].length = &name_len;
    
    bind[3].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[3].buffer = &weight;
    
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&value;
    
    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = (char*)rod_id.c_str();
    bind[5].buffer_length = rod_id.length();
    bind[5].length = &rod_len;
    
    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer = (char*)bait_id.c_str();
    bind[6].buffer_length = bait_id.length();
    bind[6].length = &bait_len;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    uint64_t insert_id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return insert_id;
}

int64_t Database::count_fish_caught_by_rarity(uint64_t user_id, const std::string& rarity) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT COUNT(*) FROM fish_catches WHERE user_id = ? AND rarity = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    unsigned long rarity_len = rarity.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)rarity.c_str();
    bind[1].buffer_length = rarity.length();
    bind[1].length = &rarity_len;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int64_t count = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &count;
    
    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

int64_t Database::count_total_fish_caught(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT COUNT(*) FROM fish_catches WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int64_t count = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &count;
    
    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

int64_t Database::count_prestige_fish_caught(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    // Prestige fish names from fishing_helpers.h
    static const std::vector<std::string> prestige_fish = {
        // P1-P5
        "phoenix fish", "firebird", "ember carp",
        "void fish", "abyssal void", "shadow bass",
        "nebula fish", "stardust sprite", "aurora trout",
        "cosmic fish", "stellar phantom", "moonbeam ray",
        "eternal fish", "immortal koi", "ascendant angel",
        // P6-P10
        "primordial eel", "genesis whale", "origin serpent",
        "mana fish", "arcane leviathan", "spell weaver",
        "schrodinger fish", "entangled pair", "quantum ghost",
        "time fish", "paradox salmon", "temporal anomaly",
        "multiverse carp", "rift swimmer", "dimensional echo",
        // P11-P15
        "neutron fish", "supernova ray", "pulsar pike",
        "black hole fish", "quasar bass", "gravity well",
        "entropy fish", "big bang bass", "chaos breeder",
        "infinity fish", "omega whale", "limit breaker",
        "world serpent", "elder god fish", "ancient one",
        // P16-P20
        "all-seeing fish", "fate weaver", "oracle oracle", "prophet pike",
        "reality fish", "creation koi", "world builder", "genesis leviathan",
        "void emperor", "concept fish", "void sovereign", "abstract entity",
        "axiom fish", "singularity", "absolute truth", "perfect form",
        "the one fish", "end of all", "alpha omega", "eternal paradox"
    };
    
    // Build IN clause with placeholders
    std::string query = "SELECT COUNT(*) FROM fish_catches WHERE user_id = ? AND fish_name IN (";
    for (size_t i = 0; i < prestige_fish.size(); i++) {
        query += (i == 0) ? "?" : ", ?";
    }
    query += ")";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    std::vector<MYSQL_BIND> bind(1 + prestige_fish.size());
    std::vector<unsigned long> lengths(prestige_fish.size());
    memset(bind.data(), 0, sizeof(MYSQL_BIND) * bind.size());
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    for (size_t i = 0; i < prestige_fish.size(); i++) {
        lengths[i] = prestige_fish[i].length();
        bind[i + 1].buffer_type = MYSQL_TYPE_STRING;
        bind[i + 1].buffer = (char*)prestige_fish[i].c_str();
        bind[i + 1].buffer_length = prestige_fish[i].length();
        bind[i + 1].length = &lengths[i];
    }
    
    if (mysql_stmt_bind_param(stmt, bind.data()) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int64_t count = 0;
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &count;
    
    mysql_stmt_bind_result(stmt, result_bind);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

std::map<std::string, int64_t> Database::get_fish_catch_counts_by_species(uint64_t user_id) {
    std::map<std::string, int64_t> result;
    auto conn = pool_->acquire();
    if (!conn) return result;

    const char* query = "SELECT fish_name, COUNT(*) FROM fish_catches WHERE user_id = ? GROUP BY fish_name";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }

    char name_buf[128];
    unsigned long name_len = 0;
    int64_t cnt = 0;
    MYSQL_BIND res[2];
    memset(res, 0, sizeof(res));
    res[0].buffer_type = MYSQL_TYPE_STRING;
    res[0].buffer = name_buf;
    res[0].buffer_length = sizeof(name_buf);
    res[0].length = &name_len;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG;
    res[1].buffer = &cnt;

    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        result[std::string(name_buf, name_len)] = cnt;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

} // namespace db
} // namespace bronx