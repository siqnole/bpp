#include "user_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace bronx {
namespace db {

bool Database::ensure_user_exists(uint64_t user_id) {
    // Fast path: if we already know this user exists, skip the DB round-trip
    if (is_user_known(user_id)) {
        return true;
    }
    
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
    
    if (success) {
        mark_user_known(user_id);
    }
    
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
                       "total_gambled, total_won, total_lost, dev, admin, is_mod, maintainer, contributor, vip, passive, prestige "
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
    MYSQL_BIND bind_result[22];
    memset(bind_result, 0, sizeof(bind_result));
    
    long long interest_claim_ts = 0, daily_ts = 0, work_ts = 0, beg_ts = 0, rob_ts = 0;
    my_bool interest_null = 1, daily_null = 1, work_null = 1, beg_null = 1, rob_null = 1;
    my_bool dev_val = 0, admin_val = 0, mod_val = 0, maintainer_val = 0, contributor_val = 0, vip_val = 0, passive_val = 0;
    
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
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &maintainer_val;
    bind_result[col].buffer_type = MYSQL_TYPE_TINY; bind_result[col++].buffer = &contributor_val;
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
    data.maintainer = maintainer_val != 0;
    data.contributor = contributor_val != 0;
    data.vip = vip_val != 0;
    data.passive = passive_val != 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    // Load extended stats from user_stats_ext
    {
        auto stats_conn = pool_->acquire();
        const char* stats_query = "SELECT stat_name, stat_value FROM user_stats_ext WHERE user_id = ?";
        MYSQL_STMT* stats_stmt = mysql_stmt_init(stats_conn->get());
        if (stats_stmt && mysql_stmt_prepare(stats_stmt, stats_query, strlen(stats_query)) == 0) {
            MYSQL_BIND stats_bind[1];
            memset(stats_bind, 0, sizeof(stats_bind));
            stats_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            stats_bind[0].buffer = (char*)&user_id;
            stats_bind[0].is_unsigned = 1;
            
            if (mysql_stmt_bind_param(stats_stmt, stats_bind) == 0 &&
                mysql_stmt_execute(stats_stmt) == 0) {
                char stat_name_buf[64];
                unsigned long stat_name_len = 0;
                int64_t stat_val = 0;
                
                MYSQL_BIND stats_result[2];
                memset(stats_result, 0, sizeof(stats_result));
                stats_result[0].buffer_type = MYSQL_TYPE_STRING;
                stats_result[0].buffer = stat_name_buf;
                stats_result[0].buffer_length = sizeof(stat_name_buf);
                stats_result[0].length = &stat_name_len;
                stats_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
                stats_result[1].buffer = &stat_val;
                
                if (mysql_stmt_bind_result(stats_stmt, stats_result) == 0) {
                    mysql_stmt_store_result(stats_stmt);
                    while (mysql_stmt_fetch(stats_stmt) == 0) {
                        std::string name(stat_name_buf, stat_name_len);
                        if (name == "fish_caught") data.fish_caught = stat_val;
                        else if (name == "fish_sold") data.fish_sold = stat_val;
                        else if (name == "gambling_wins") data.gambling_wins = stat_val;
                        else if (name == "gambling_losses") data.gambling_losses = stat_val;
                        else if (name == "commands_used") data.commands_used = stat_val;
                        else if (name == "daily_streak") data.daily_streak = static_cast<int>(stat_val);
                        else if (name == "work_count") data.work_count = stat_val;
                        else if (name == "ores_mined") data.ores_mined = stat_val;
                        else if (name == "items_crafted") data.items_crafted = stat_val;
                        else if (name == "trades_completed") data.trades_completed = stat_val;
                    }
                }
            }
            mysql_stmt_close(stats_stmt);
        }
        pool_->release(stats_conn);
    }
    
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

int64_t Database::get_fish_inventory_value(uint64_t user_id, uint64_t guild_id) {
    auto inventory = get_inventory(user_id, guild_id);
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

int64_t Database::get_total_networth(uint64_t user_id, uint64_t guild_id) {
    return get_networth(user_id) + get_fish_inventory_value(user_id, guild_id);
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
        "INSERT INTO user_stats_ext (user_id, stat_name, stat_value) VALUES (?, ?, ?) "
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
    
    const char* query = "SELECT stat_value FROM user_stats_ext WHERE user_id = ? AND stat_name = ?";
    
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
    int64_t val = 0;
    if (mysql_stmt_fetch(stmt) == 0) {
        val = value;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return val;
}

bool Database::increment_daily_stat(uint64_t user_id, const std::string& stat_name, int64_t amount, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = 
        "INSERT INTO daily_stats (user_id, stat_name, stat_value, stat_date) VALUES (?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE stat_value = stat_value + VALUES(stat_value)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)stat_name.c_str();
    bind[1].buffer_length = stat_name.length();
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&amount;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)date.c_str();
    bind[3].buffer_length = date.length();
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return true;
}

int64_t Database::get_daily_stat(uint64_t user_id, const std::string& stat_name, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT stat_value FROM daily_stats WHERE user_id = ? AND stat_name = ? AND stat_date = ? LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)stat_name.c_str();
    bind[1].buffer_length = stat_name.length();
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)date.c_str();
    bind[2].buffer_length = date.length();
    
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
    
    int64_t val = 0;
    if (mysql_stmt_fetch(stmt) == 0) {
        val = value;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return val;
}

bool Database::set_daily_stat(uint64_t user_id, const std::string& stat_name, int64_t value, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = 
        "INSERT INTO daily_stats (user_id, stat_name, stat_value, stat_date) VALUES (?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE stat_value = VALUES(stat_value)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)stat_name.c_str();
    bind[1].buffer_length = stat_name.length();
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&value;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)date.c_str();
    bind[3].buffer_length = date.length();
    
    if (mysql_stmt_bind_param(stmt, bind) != 0 || mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return true;
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
    const char* fish_query = "DELETE FROM user_fish_catches WHERE user_id = ? AND sold = FALSE";
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
    const char* inv_query = "DELETE FROM user_inventory WHERE user_id = ? AND item_type IN ('collectible', 'bait', 'rod', 'upgrade', 'potion')";
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
    const char* gear_query = "DELETE FROM user_fishing_gear WHERE user_id = ?";
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
    const char* afs_query = "DELETE FROM user_autofish_storage WHERE user_id = ?";
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
    const char* af_query = "DELETE FROM user_autofishers WHERE user_id = ?";
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
                                   double weight, int64_t value, const std::string& rod_id, const std::string& bait_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "INSERT INTO user_fish_catches (user_id, rarity, fish_name, weight, value, rod_id, bait_id, caught_at) "
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

// ---------------------------------------------------------------------------
// Batch insert multiple fish catches in a single multi-row INSERT statement.
// This replaces N individual add_fish_catch() calls with 1 round-trip.
// ---------------------------------------------------------------------------
bool Database::add_fish_catches_batch(uint64_t user_id, const std::vector<FishCatchRow>& rows, uint64_t guild_id) {
    if (rows.empty()) return true;
    auto conn = pool_->acquire();
    if (!conn) return false;

    // Build multi-row INSERT using string escaping (prepared stmts can't do dynamic row counts)
    std::string sql = "INSERT INTO user_fish_catches (user_id, rarity, fish_name, weight, value, rod_id, bait_id, caught_at) VALUES ";
    
    std::string uid_str = std::to_string(user_id);
    
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) sql += ',';
        
        // Escape strings to prevent SQL injection
        char esc_rarity[201], esc_name[201], esc_rod[201], esc_bait[201];
        mysql_real_escape_string(conn->get(), esc_rarity, rows[i].rarity.c_str(), rows[i].rarity.size());
        mysql_real_escape_string(conn->get(), esc_name, rows[i].fish_name.c_str(), rows[i].fish_name.size());
        mysql_real_escape_string(conn->get(), esc_rod, rows[i].rod_id.c_str(), rows[i].rod_id.size());
        mysql_real_escape_string(conn->get(), esc_bait, rows[i].bait_id.c_str(), rows[i].bait_id.size());
        
        sql += '(';
        sql += uid_str;
        sql += ",'";
        sql += esc_rarity;
        sql += "','";
        sql += esc_name;
        sql += "',";
        sql += std::to_string(rows[i].weight);
        sql += ',';
        sql += std::to_string(rows[i].value);
        sql += ",'";
        sql += esc_rod;
        sql += "','";
        sql += esc_bait;
        sql += "',NOW())";
    }

    bool ok = (mysql_real_query(conn->get(), sql.c_str(), sql.size()) == 0);
    if (!ok) {
        last_error_ = mysql_error(conn->get());
        log_error("add_fish_catches_batch");
    }
    pool_->release(conn);
    return ok;
}

int64_t Database::count_fish_caught_by_rarity(uint64_t user_id, const std::string& rarity, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT COUNT(*) FROM user_fish_catches WHERE user_id = ? AND rarity = ?";
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

int64_t Database::count_total_fish_caught(uint64_t user_id, uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT COUNT(*) FROM user_fish_catches WHERE user_id = ?";
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

int64_t Database::count_prestige_fish_caught(uint64_t user_id, uint64_t guild_id) {
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
    std::string query = "SELECT COUNT(*) FROM user_fish_catches WHERE user_id = ? AND fish_name IN (";
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

std::map<std::string, int64_t> Database::get_fish_catch_counts_by_species(uint64_t user_id, uint64_t guild_id) {
    std::map<std::string, int64_t> result;
    auto conn = pool_->acquire();
    if (!conn) return result;

    const char* query = "SELECT fish_name, COUNT(*) FROM user_fish_catches WHERE user_id = ? GROUP BY fish_name";
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
// ========================================
// CHALLENGE TRACKING OPERATIONS
// ========================================

bool Database::update_challenge_streak(uint64_t user_id, const std::string& challenge_id, bool is_success, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) { pool_->release(conn); return false; }
    
    if (is_success) {
        const char* query = "INSERT INTO challenge_streaks (user_id, challenge_id, current_streak, streak_date) "
                           "VALUES (?, ?, 1, ?) "
                           "ON DUPLICATE KEY UPDATE current_streak = current_streak + 1";
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            mysql_stmt_close(stmt); pool_->release(conn); return false;
        }
        
        MYSQL_BIND bind[3];
        memset(bind, 0, sizeof(bind));
        
        unsigned long cid_len = challenge_id.length();
        unsigned long date_len = date.length();
        
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)challenge_id.c_str();
        bind[1].buffer_length = challenge_id.length();
        bind[1].length = &cid_len;
        
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)date.c_str();
        bind[2].buffer_length = date.length();
        bind[2].length = &date_len;
        
        mysql_stmt_bind_param(stmt, bind);
    } else {
        const char* query = "UPDATE challenge_streaks SET current_streak = 0 "
                           "WHERE user_id = ? AND challenge_id = ? AND streak_date = ?";
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            mysql_stmt_close(stmt); pool_->release(conn); return false;
        }
        
        MYSQL_BIND bind[3];
        memset(bind, 0, sizeof(bind));
        
        unsigned long cid_len = challenge_id.length();
        unsigned long date_len = date.length();
        
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        
        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)challenge_id.c_str();
        bind[1].buffer_length = challenge_id.length();
        bind[1].length = &cid_len;
        
        bind[2].buffer_type = MYSQL_TYPE_STRING;
        bind[2].buffer = (char*)date.c_str();
        bind[2].buffer_length = date.length();
        bind[2].length = &date_len;
        
        mysql_stmt_bind_param(stmt, bind);
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::track_challenge_variety(uint64_t user_id, const std::string& challenge_id, const std::string& item_id, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT IGNORE INTO challenge_variety (user_id, challenge_id, item_id, variety_date) VALUES (?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    unsigned long cid_len = challenge_id.length();
    unsigned long iid_len = item_id.length();
    unsigned long date_len = date.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)challenge_id.c_str();
    bind[1].buffer_length = challenge_id.length();
    bind[1].length = &cid_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item_id.c_str();
    bind[2].buffer_length = item_id.length();
    bind[2].length = &iid_len;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)date.c_str();
    bind[3].buffer_length = date.length();
    bind[3].length = &date_len;
    
    bool success = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int64_t Database::get_challenge_variety_count(uint64_t user_id, const std::string& challenge_id, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* query = "SELECT COUNT(DISTINCT item_id) FROM challenge_variety WHERE user_id = ? AND challenge_id = ? AND variety_date = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    unsigned long cid_len = challenge_id.length();
    unsigned long date_len = date.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)challenge_id.c_str();
    bind[1].buffer_length = challenge_id.length();
    bind[1].length = &cid_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)date.c_str();
    bind[2].buffer_length = date.length();
    bind[2].length = &date_len;
    
    int64_t count = 0;
    if (mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND res[1];
        memset(res, 0, sizeof(res));
        res[0].buffer_type = MYSQL_TYPE_LONGLONG;
        res[0].buffer = &count;
        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

bool Database::track_challenge_attempt(uint64_t user_id, const std::string& challenge_id, bool is_success, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO challenge_attempts (user_id, challenge_id, attempt_type, is_success, attempt_date) VALUES (?, ?, 'attempt', ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    unsigned long cid_len = challenge_id.length();
    unsigned long date_len = date.length();
    int8_t success_bit = is_success ? 1 : 0;
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)challenge_id.c_str();
    bind[1].buffer_length = challenge_id.length();
    bind[1].length = &cid_len;
    
    bind[2].buffer_type = MYSQL_TYPE_TINY;
    bind[2].buffer = &success_bit;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)date.c_str();
    bind[3].buffer_length = date.length();
    bind[3].length = &date_len;
    
    bool ok = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

Database::AttemptStats Database::get_challenge_attempt_stats(uint64_t user_id, const std::string& challenge_id, const std::string& date) {
    Database::AttemptStats stats = {0, 0};
    auto conn = pool_->acquire();
    if (!conn) return stats;
    
    const char* query = "SELECT COUNT(*), SUM(IF(is_success, 1, 0)) FROM challenge_attempts WHERE user_id = ? AND challenge_id = ? AND attempt_date = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return stats;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    unsigned long cid_len = challenge_id.length();
    unsigned long date_len = date.length();
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)challenge_id.c_str();
    bind[1].buffer_length = challenge_id.length();
    bind[1].length = &cid_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)date.c_str();
    bind[2].buffer_length = date.length();
    bind[2].length = &date_len;
    
    if (mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND res[2];
        memset(res, 0, sizeof(res));
        res[0].buffer_type = MYSQL_TYPE_LONGLONG;
        res[0].buffer = &stats.total;
        res[1].buffer_type = MYSQL_TYPE_LONGLONG;
        res[1].buffer = &stats.wins;
        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return stats;
}

Database::UserStreak Database::get_daily_streak_stats(uint64_t user_id) {
    Database::UserStreak streak = {0, 0, "", 0, 0};
    auto conn = pool_->acquire();
    if (!conn) return streak;
    
    const char* query = "SELECT current_streak, longest_streak, last_claim_date, total_claims, total_streak_bonus "
                       "FROM daily_streaks WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return streak;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND res[5];
        memset(res, 0, sizeof(res));
        
        char date_buf[11];
        unsigned long date_len = 0;
        
        res[0].buffer_type = MYSQL_TYPE_LONG;
        res[0].buffer = &streak.current_streak;
        
        res[1].buffer_type = MYSQL_TYPE_LONG;
        res[1].buffer = &streak.longest_streak;
        
        res[2].buffer_type = MYSQL_TYPE_STRING;
        res[2].buffer = date_buf;
        res[2].buffer_length = sizeof(date_buf);
        res[2].length = &date_len;
        
        res[3].buffer_type = MYSQL_TYPE_LONG;
        res[3].buffer = &streak.total_claims;
        
        res[4].buffer_type = MYSQL_TYPE_LONGLONG;
        res[4].buffer = &streak.total_bonus;
        
        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);
        if (mysql_stmt_fetch(stmt) == 0) {
            streak.last_claim_date = std::string(date_buf, date_len);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return streak;
}

bool Database::update_daily_streak(uint64_t user_id, int current, int longest, const std::string& date, int64_t bonus) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT INTO daily_streaks (user_id, current_streak, longest_streak, last_claim_date, total_claims, total_streak_bonus) "
                       "VALUES (?, ?, ?, ?, 1, ?) "
                       "ON DUPLICATE KEY UPDATE "
                       "current_streak = ?, "
                       "longest_streak = ?, "
                       "last_claim_date = ?, "
                       "total_claims = total_claims + 1, "
                       "total_streak_bonus = total_streak_bonus + ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[10];
    memset(bind, 0, sizeof(bind));
    
    unsigned long date_len = date.length();
    
    // INSERT values
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &current;
    
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &longest;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)date.c_str();
    bind[3].buffer_length = date.length();
    bind[3].length = &date_len;
    
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&bonus;
    
    // UPDATE values
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = &current;
    
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = &longest;
    
    bind[7].buffer_type = MYSQL_TYPE_STRING;
    bind[7].buffer = (char*)date.c_str();
    bind[7].buffer_length = date.length();
    bind[7].length = &date_len;
    
    bind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[8].buffer = (char*)&bonus;
    
    bool ok = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

std::vector<Database::ActiveChallenge> Database::get_user_challenges(uint64_t user_id, const std::string& date) {
    std::vector<Database::ActiveChallenge> challenges;
    auto conn = pool_->acquire();
    if (!conn) return challenges;
    
    const char* query = "SELECT challenge_id, challenge_name, challenge_desc, stat_name, category, "
                       "target, start_value, completed, claimed, reward_coins, reward_xp, emoji, challenge_type "
                       "FROM daily_challenges WHERE user_id = ? AND assigned_date = ? ORDER BY id";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return challenges;
    }
    
    unsigned long date_len = date.length();
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)date.c_str();
    bind[1].buffer_length = date.length();
    bind[1].length = &date_len;
    
    if (mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND res[13];
        memset(res, 0, sizeof(res));
        
        char id_buf[64], name_buf[128], desc_buf[256], stat_buf[64], cat_buf[64], emoji_buf[32];
        unsigned long id_len, name_len, desc_len, stat_len, cat_len, emoji_len;
        int type_int;
        char completed_char, claimed_char;
        
        res[0].buffer_type = MYSQL_TYPE_STRING;
        res[0].buffer = id_buf;
        res[0].buffer_length = sizeof(id_buf);
        res[0].length = &id_len;
        
        res[1].buffer_type = MYSQL_TYPE_STRING;
        res[1].buffer = name_buf;
        res[1].buffer_length = sizeof(name_buf);
        res[1].length = &name_len;
        
        res[2].buffer_type = MYSQL_TYPE_STRING;
        res[2].buffer = desc_buf;
        res[2].buffer_length = sizeof(desc_buf);
        res[2].length = &desc_len;
        
        res[3].buffer_type = MYSQL_TYPE_STRING;
        res[3].buffer = stat_buf;
        res[3].buffer_length = sizeof(stat_buf);
        res[3].length = &stat_len;
        
        res[4].buffer_type = MYSQL_TYPE_STRING;
        res[4].buffer = cat_buf;
        res[4].buffer_length = sizeof(cat_buf);
        res[4].length = &cat_len;
        
        res[5].buffer_type = MYSQL_TYPE_LONGLONG;
        
        res[6].buffer_type = MYSQL_TYPE_LONGLONG;
        
        res[7].buffer_type = MYSQL_TYPE_TINY;
        res[7].buffer = &completed_char;
        
        res[8].buffer_type = MYSQL_TYPE_TINY;
        res[8].buffer = &claimed_char;
        
        res[9].buffer_type = MYSQL_TYPE_LONGLONG;
        
        res[10].buffer_type = MYSQL_TYPE_LONG;
        
        res[11].buffer_type = MYSQL_TYPE_STRING;
        res[11].buffer = emoji_buf;
        res[11].buffer_length = sizeof(emoji_buf);
        res[11].length = &emoji_len;
        
        res[12].buffer_type = MYSQL_TYPE_LONG;
        res[12].buffer = &type_int;
        
        Database::ActiveChallenge c;
        res[5].buffer = &c.target;
        res[6].buffer = &c.start_value;
        res[9].buffer = &c.reward.coins;
        res[10].buffer = &c.reward.xp;

        mysql_stmt_bind_result(stmt, res);
        mysql_stmt_store_result(stmt);
        
        while (mysql_stmt_fetch(stmt) == 0) {
            c.challenge_id = std::string(id_buf, id_len);
            c.name = std::string(name_buf, name_len);
            c.description = std::string(desc_buf, desc_len);
            c.stat_name = std::string(stat_buf, stat_len);
            c.category = std::string(cat_buf, cat_len);
            c.emoji = std::string(emoji_buf, emoji_len);
            c.challenge_type = static_cast<Database::ChallengeType>(type_int);
            c.completed = completed_char != 0;
            c.claimed = claimed_char != 0;
            
            challenges.push_back(c);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    for (auto& c : challenges) {
        c.current_progress = get_daily_stat(user_id, c.stat_name, date) - c.start_value;
        if (c.current_progress < 0) c.current_progress = 0;
        
        switch (c.challenge_type) {
            case Database::ChallengeType::STREAK: {
                auto stats = get_challenge_attempt_stats(user_id, c.challenge_id, date);
                c.progress_data.current_streak = stats.wins;
                break;
            }
            case Database::ChallengeType::VARIETY: {
                c.current_progress = get_challenge_variety_count(user_id, c.challenge_id, date);
                break;
            }
            case Database::ChallengeType::RATIO: {
                auto stats = get_challenge_attempt_stats(user_id, c.challenge_id, date);
                c.progress_data.total_wins = stats.wins;
                c.progress_data.total_attempts = stats.total;
                break;
            }
            default:
                break;
        }
    }
    
    return challenges;
}

bool Database::update_challenge_status(uint64_t user_id, const std::string& challenge_id, const std::string& date, bool completed, bool claimed) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = "UPDATE daily_challenges SET completed = ?, claimed = ? "
                       "WHERE user_id = ? AND challenge_id = ? AND assigned_date = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    char completed_val = completed ? 1 : 0;
    char claimed_val = claimed ? 1 : 0;
    unsigned long id_len = challenge_id.length();
    unsigned long date_len = date.length();
    
    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_TINY;
    bind[0].buffer = &completed_val;
    
    bind[1].buffer_type = MYSQL_TYPE_TINY;
    bind[1].buffer = &claimed_val;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[2].is_unsigned = 1;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)challenge_id.c_str();
    bind[3].buffer_length = challenge_id.length();
    bind[3].length = &id_len;
    
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)date.c_str();
    bind[4].buffer_length = date.length();
    bind[4].length = &date_len;
    
    bool ok = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

bool Database::assign_daily_challenge(uint64_t user_id, const Database::ActiveChallenge& challenge, const std::string& date) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* query = "INSERT IGNORE INTO daily_challenges "
                       "(user_id, challenge_id, challenge_name, challenge_desc, stat_name, category, "
                       " challenge_type, target, start_value, reward_coins, reward_xp, emoji, assigned_date) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    int type_int = static_cast<int>(challenge.challenge_type);
    unsigned long id_len = challenge.challenge_id.length();
    unsigned long name_len = challenge.name.length();
    unsigned long desc_len = challenge.description.length();
    unsigned long stat_len = challenge.stat_name.length();
    unsigned long cat_len = challenge.category.length();
    unsigned long emoji_len = challenge.emoji.length();
    unsigned long date_len = date.length();
    
    MYSQL_BIND bind[13];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)challenge.challenge_id.c_str();
    bind[1].buffer_length = challenge.challenge_id.length();
    bind[1].length = &id_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)challenge.name.c_str();
    bind[2].buffer_length = challenge.name.length();
    bind[2].length = &name_len;
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)challenge.description.c_str();
    bind[3].buffer_length = challenge.description.length();
    bind[3].length = &desc_len;
    
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)challenge.stat_name.c_str();
    bind[4].buffer_length = challenge.stat_name.length();
    bind[4].length = &stat_len;
    
    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = (char*)challenge.category.c_str();
    bind[5].buffer_length = challenge.category.length();
    bind[5].length = &cat_len;
    
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = &type_int;
    
    bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[7].buffer = (char*)&challenge.target;
    
    bind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[8].buffer = (char*)&challenge.start_value;
    
    bind[9].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[9].buffer = (char*)&challenge.reward.coins;
    
    bind[10].buffer_type = MYSQL_TYPE_LONG;
    bind[10].buffer = (char*)&challenge.reward.xp;
    
    bind[11].buffer_type = MYSQL_TYPE_STRING;
    bind[11].buffer = (char*)challenge.emoji.c_str();
    bind[11].buffer_length = challenge.emoji.length();
    bind[11].length = &emoji_len;
    
    bind[12].buffer_type = MYSQL_TYPE_STRING;
    bind[12].buffer = (char*)date.c_str();
    bind[12].buffer_length = date.length();
    bind[12].length = &date_len;
    
    bool ok = mysql_stmt_bind_param(stmt, bind) == 0 && mysql_stmt_execute(stmt) == 0;
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

} // namespace db
} // namespace bronx