#include "inventory_operations.h"
#include "../../core/database.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <set>
#include <execinfo.h>
#include "../../../log.h"
#include <unistd.h>

namespace bronx {
namespace db {

bool Database::add_item(uint64_t user_id, const std::string& item_id, const std::string& item_type, int quantity, const std::string& metadata, int level, uint64_t guild_id) {
    // ensure item_type matches allowed enum; default to 'other' for unknown values
    static const std::set<std::string> allowed = {"potion","upgrade","rod","bait","collectible","other","automation","boosts","title","tools","pickaxe","minecart","bag","crafted"};
    std::string db_type = item_type;
    if (allowed.find(db_type) == allowed.end()) {
        db_type = "other";
    }
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    // ---- FIX: MySQL NULL != NULL breaks ON DUPLICATE KEY with nullable guild_id.
    // For guild_id=0 (global economy, stored as NULL), use a two-step
    // UPDATE-then-INSERT pattern so we never create duplicate rows.
    // For guild_id>0, ON DUPLICATE KEY works correctly.
    
    bool success = false;
    
    if (guild_id == 0) {
        // Step 1: Try to UPDATE an existing row (LIMIT 1 in case duplicates already exist)
        const char* update_sql =
            "UPDATE user_inventory SET quantity = quantity + ?, level = ? "
            "WHERE user_id = ? AND item_id = ? AND guild_id IS NULL LIMIT 1";
        MYSQL_STMT* upd = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(upd, update_sql, strlen(update_sql)) != 0) {
            last_error_ = mysql_stmt_error(upd);
            log_error("add_item update prepare");
            mysql_stmt_close(upd);
            pool_->release(conn);
            return false;
        }
        MYSQL_BIND ubind[4];
        memset(ubind, 0, sizeof(ubind));
        ubind[0].buffer_type = MYSQL_TYPE_LONG;
        ubind[0].buffer = (char*)&quantity;
        ubind[1].buffer_type = MYSQL_TYPE_LONG;
        ubind[1].buffer = (char*)&level;
        ubind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        ubind[2].buffer = (char*)&user_id;
        ubind[2].is_unsigned = 1;
        ubind[3].buffer_type = MYSQL_TYPE_STRING;
        ubind[3].buffer = (char*)item_id.c_str();
        ubind[3].buffer_length = item_id.length();
        mysql_stmt_bind_param(upd, ubind);
        
        if (mysql_stmt_execute(upd) == 0 && mysql_stmt_affected_rows(upd) > 0) {
            // Existing row updated successfully
            success = true;
        }
        mysql_stmt_close(upd);
        
        if (!success) {
            // Step 2: No existing row — INSERT a new one
            const char* ins_sql =
                "INSERT INTO user_inventory (user_id, item_id, item_type, quantity, level, metadata, guild_id) "
                "VALUES (?, ?, ?, ?, ?, ?, NULL)";
            MYSQL_STMT* ins = mysql_stmt_init(conn->get());
            if (mysql_stmt_prepare(ins, ins_sql, strlen(ins_sql)) != 0) {
                last_error_ = mysql_stmt_error(ins);
                log_error("add_item insert prepare");
                mysql_stmt_close(ins);
                pool_->release(conn);
                return false;
            }
            MYSQL_BIND ibind[6];
            memset(ibind, 0, sizeof(ibind));
            ibind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            ibind[0].buffer = (char*)&user_id;
            ibind[0].is_unsigned = 1;
            ibind[1].buffer_type = MYSQL_TYPE_STRING;
            ibind[1].buffer = (char*)item_id.c_str();
            ibind[1].buffer_length = item_id.length();
            ibind[2].buffer_type = MYSQL_TYPE_STRING;
            ibind[2].buffer = (char*)db_type.c_str();
            ibind[2].buffer_length = db_type.length();
            ibind[3].buffer_type = MYSQL_TYPE_LONG;
            ibind[3].buffer = (char*)&quantity;
            ibind[4].buffer_type = MYSQL_TYPE_LONG;
            ibind[4].buffer = (char*)&level;
            my_bool metadata_is_null = metadata.empty();
            ibind[5].buffer_type = MYSQL_TYPE_STRING;
            ibind[5].buffer = (char*)metadata.c_str();
            ibind[5].buffer_length = metadata.length();
            ibind[5].is_null = &metadata_is_null;
            mysql_stmt_bind_param(ins, ibind);
            
            success = (mysql_stmt_execute(ins) == 0);
            if (!success) {
                last_error_ = mysql_stmt_error(ins);
                log_error("add_item insert execute");
            }
            mysql_stmt_close(ins);
        }
    } else {
        // guild_id > 0: ON DUPLICATE KEY works correctly (no NULLs)
        std::string upsert_query =
            "INSERT INTO user_inventory (user_id, item_id, item_type, quantity, level, metadata, guild_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON DUPLICATE KEY UPDATE quantity = quantity + VALUES(quantity), level = VALUES(level)";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, upsert_query.c_str(), upsert_query.length()) != 0) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("add_item upsert prepare");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return false;
        }
        MYSQL_BIND insert_bind[7];
        memset(insert_bind, 0, sizeof(insert_bind));
        insert_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        insert_bind[0].buffer = (char*)&user_id;
        insert_bind[0].is_unsigned = 1;
        insert_bind[1].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[1].buffer = (char*)item_id.c_str();
        insert_bind[1].buffer_length = item_id.length();
        insert_bind[2].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[2].buffer = (char*)db_type.c_str();
        insert_bind[2].buffer_length = db_type.length();
        insert_bind[3].buffer_type = MYSQL_TYPE_LONG;
        insert_bind[3].buffer = (char*)&quantity;
        insert_bind[4].buffer_type = MYSQL_TYPE_LONG;
        insert_bind[4].buffer = (char*)&level;
        my_bool metadata_is_null = metadata.empty();
        insert_bind[5].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[5].buffer = (char*)metadata.c_str();
        insert_bind[5].buffer_length = metadata.length();
        insert_bind[5].is_null = &metadata_is_null;
        insert_bind[6].buffer_type = MYSQL_TYPE_LONGLONG;
        insert_bind[6].buffer = (char*)&guild_id;
        insert_bind[6].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, insert_bind);
        
        success = (mysql_stmt_execute(stmt) == 0);
        if (!success) {
            last_error_ = mysql_stmt_error(stmt);
            log_error("add_item execute");
        }
        mysql_stmt_close(stmt);
    }
    
    if (!success) {
        // print backtrace to help diagnose what path caused the error
        void* bt[20];
        int cnt = backtrace(bt, 20);
        std::cerr << DBG_INV "add_item execute failed, backtrace:" << std::endl;
        backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    } else {
        // show updated quantity for debugging (only when inventory_debug_ enabled)
        if (inventory_debug_) {
            std::cerr << DBG_INV "add_item: about to query qty for user=" << user_id << " item=" << item_id << " guild=" << guild_id << "\n";
            int qty = get_item_quantity(user_id, item_id, guild_id);
            std::cerr << DBG_INV "add_item: query returned qty=" << qty << " for user=" << user_id << " item=" << item_id << "\n";
        }
    }
    
    pool_->release(conn);
    
    return success;
}

bool Database::has_item(uint64_t user_id, const std::string& item_id, int quantity, uint64_t guild_id) {
    return get_item_quantity(user_id, item_id, guild_id) >= quantity;
}

int Database::get_item_quantity(uint64_t user_id, const std::string& item_id, uint64_t guild_id) {
    if (inventory_debug_) std::cerr << DBG_INV "get_item_quantity entry user=" << user_id << " item=" << item_id << " guild=" << guild_id << "\n";
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_item_quantity acquire failed");
        return 0;
    }
    
    // guild_id=0 means global (NULL in DB), guild_id>0 means server-specific
    // FIX: Use SUM(quantity) to correctly total across any duplicate rows
    // (duplicates can exist because NULL != NULL in MySQL UNIQUE keys)
    std::string query = "SELECT COALESCE(SUM(quantity), 0) FROM user_inventory WHERE user_id = ? AND item_id = ?";
    if (guild_id == 0) {
        query += " AND guild_id IS NULL";
    } else {
        query += " AND guild_id = ?";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_item_quantity prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    // 2 base binds + 1 optional guild_id bind
    const int bind_count = guild_id > 0 ? 3 : 2;
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    
    if (guild_id > 0) {
        bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[2].buffer = (char*)&guild_id;
        bind[2].is_unsigned = 1;
    }
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_item_quantity execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    int qty = 0;
    
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = (char*)&qty;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    if (mysql_stmt_fetch(stmt) != 0) {
        qty = 0; // Item not found
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    if (inventory_debug_) std::cerr << DBG_INV "get_item_quantity exit qty=" << qty << " for user=" << user_id << " item=" << item_id << "\n";
    return qty;
}

int Database::count_item_owners(const std::string& item_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("count_item_owners acquire failed");
        return 0;
    }
    const char* query = "SELECT COUNT(DISTINCT user_id) FROM user_inventory WHERE item_id = ? AND quantity > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("count_item_owners prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item_id.c_str();
    bind[0].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("count_item_owners execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND res[1];
    memset(res, 0, sizeof(res));
    int count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) != 0) count = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

int Database::delete_inventory_item_for_all_users(const std::string& item_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("delete_inventory_item_for_all_users acquire failed");
        return 0;
    }
    const char* query = "DELETE FROM user_inventory WHERE item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_inventory_item_for_all_users prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item_id.c_str();
    bind[0].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("delete_inventory_item_for_all_users execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    int affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return affected;
}

std::vector<uint64_t> Database::get_users_with_item(const std::string& item_id) {
    std::vector<uint64_t> out;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_users_with_item acquire failed");
        return out;
    }
    const char* query = "SELECT DISTINCT user_id FROM user_inventory WHERE item_id = ? AND quantity > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_users_with_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item_id.c_str();
    bind[0].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_users_with_item execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    uint64_t uid = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&uid;
    result_bind[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, result_bind);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back(uid);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

std::vector<InventoryItem> Database::get_inventory(uint64_t user_id, uint64_t guild_id) {
    std::vector<InventoryItem> items;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_inventory acquire failed");
        return items;
    }
    
    // guild_id=0 means global (NULL in DB), guild_id>0 means server-specific
    std::string query = "SELECT item_id, item_type, quantity, level, metadata, guild_id FROM user_inventory WHERE user_id = ? AND quantity > 0";
    if (guild_id == 0) {
        query += " AND guild_id IS NULL";
    } else {
        query += " AND guild_id = ?";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_inventory prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return items;
    }
    
    // 1 base bind + 1 optional guild_id bind
    const int bind_count = guild_id > 0 ? 2 : 1;
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (guild_id > 0) {
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&guild_id;
        bind[1].is_unsigned = 1;
    }
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_inventory execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return items;
    }
    
    // Bind results (6 columns now including guild_id)
    char item_id_buf[101];
    char item_type_buf[51];
    int quantity;
    int lvl;
    char metadata_buf[1001];
    uint64_t result_guild_id = 0;
    unsigned long item_id_length, item_type_length, metadata_length;
    my_bool metadata_is_null;
    my_bool guild_id_is_null;
    
    MYSQL_BIND result_bind[6];
    memset(result_bind, 0, sizeof(result_bind));
    
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = item_id_buf;
    result_bind[0].buffer_length = sizeof(item_id_buf);
    result_bind[0].length = &item_id_length;
    
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = item_type_buf;
    result_bind[1].buffer_length = sizeof(item_type_buf);
    result_bind[1].length = &item_type_length;
    
    result_bind[2].buffer_type = MYSQL_TYPE_LONG;
    result_bind[2].buffer = (char*)&quantity;
    
    result_bind[3].buffer_type = MYSQL_TYPE_LONG;
    result_bind[3].buffer = (char*)&lvl;
    
    result_bind[4].buffer_type = MYSQL_TYPE_STRING;
    result_bind[4].buffer = metadata_buf;
    result_bind[4].buffer_length = sizeof(metadata_buf);
    result_bind[4].length = &metadata_length;
    result_bind[4].is_null = &metadata_is_null;
    
    result_bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[5].buffer = (char*)&result_guild_id;
    result_bind[5].is_unsigned = 1;
    result_bind[5].is_null = &guild_id_is_null;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        InventoryItem item;
        item.item_id = std::string(item_id_buf, item_id_length);
        item.item_type = std::string(item_type_buf, item_type_length);
        item.quantity = quantity;
        item.level = lvl;
        item.metadata = metadata_is_null ? "" : std::string(metadata_buf, metadata_length);
        item.guild_id = guild_id_is_null ? 0 : result_guild_id;
        items.push_back(item);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return items;
}

// Update the metadata for an existing inventory item
bool Database::update_item_metadata(uint64_t user_id, const std::string& item_id, const std::string& new_metadata, uint64_t guild_id) {
    auto conn = pool_->acquire();
    // guild_id=0 means global (NULL in DB), guild_id>0 means server-specific
    std::string query = "UPDATE user_inventory SET metadata = ? WHERE user_id = ? AND item_id = ?";
    if (guild_id == 0) {
        query += " AND guild_id IS NULL";
    } else {
        query += " AND guild_id = ?";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("update_item_metadata prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    // 3 base binds + 1 optional guild_id bind
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    // An empty metadata string is not valid JSON; store NULL instead so the
    // database JSON column constraint is never violated by callers that pass "".
    my_bool meta_is_null = new_metadata.empty() ? 1 : 0;
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)new_metadata.c_str();
    bind[0].buffer_length = new_metadata.length();
    bind[0].is_null = &meta_is_null;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item_id.c_str();
    bind[2].buffer_length = item_id.length();

    if (guild_id > 0) {
        bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[3].buffer = (char*)&guild_id;
        bind[3].is_unsigned = 1;
    }

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("update_item_metadata execute");
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::remove_item(uint64_t user_id, const std::string& item_id, int quantity, uint64_t guild_id) {
    auto conn = pool_->acquire();
    
    // guild_id=0 means global (NULL in DB), guild_id>0 means server-specific
    // FIX: Add LIMIT 1 so only one row is decremented when duplicates exist
    // (duplicates can exist because NULL != NULL in MySQL UNIQUE keys)
    std::string query = "UPDATE user_inventory SET quantity = quantity - ? WHERE user_id = ? AND item_id = ? AND quantity >= ?";
    if (guild_id == 0) {
        query += " AND guild_id IS NULL LIMIT 1";
    } else {
        query += " AND guild_id = ? LIMIT 1";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    // 4 base binds + 1 optional guild_id bind
    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&quantity;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item_id.c_str();
    bind[2].buffer_length = item_id.length();
    
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = (char*)&quantity;
    
    if (guild_id > 0) {
        bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[4].buffer = (char*)&guild_id;
        bind[4].is_unsigned = 1;
    }
    
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_item execute");
    } else {
        if (inventory_debug_) {
            int qty = get_item_quantity(user_id, item_id, guild_id);
            std::cerr << DBG_INV "remove_item: user=" << user_id << " item=" << item_id << " guild=" << guild_id << " qty=" << qty << "\n";
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}


} // namespace db
} // namespace bronx