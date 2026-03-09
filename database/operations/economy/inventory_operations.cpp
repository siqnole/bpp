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

bool Database::add_item(uint64_t user_id, const std::string& item_id, const std::string& item_type, int quantity, const std::string& metadata, int level) {
    // ensure item_type matches allowed enum; default to 'other' for unknown values
    static const std::set<std::string> allowed = {"potion","upgrade","rod","bait","collectible","other","automation","boosts","title","tools","pickaxe","minecart","bag","crafted"};
    std::string db_type = item_type;
    if (allowed.find(db_type) == allowed.end()) {
        db_type = "other";
    }
    ensure_user_exists(user_id);
    
    auto conn = pool_->acquire();
    
    // Check if item already exists
    const char* check_query = "SELECT quantity FROM inventory WHERE user_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, check_query, strlen(check_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_item check prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_item check execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    int existing_quantity = 0;
    
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = (char*)&existing_quantity;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    bool exists = (mysql_stmt_fetch(stmt) == 0);
    mysql_stmt_close(stmt);
    
    // Insert or update including level column
    const char* upsert_query;
    if (exists) {
        upsert_query = "UPDATE inventory SET quantity = quantity + ?, level = ? WHERE user_id = ? AND item_id = ?";
    } else {
        upsert_query = "INSERT INTO inventory (user_id, item_id, item_type, quantity, level, metadata) VALUES (?, ?, ?, ?, ?, ?)";
    }
    
    stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, upsert_query, strlen(upsert_query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_item upsert prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    if (exists) {
        MYSQL_BIND update_bind[4];
        memset(update_bind, 0, sizeof(update_bind));
        
        update_bind[0].buffer_type = MYSQL_TYPE_LONG;
        update_bind[0].buffer = (char*)&quantity;
        
        update_bind[1].buffer_type = MYSQL_TYPE_LONG;
        update_bind[1].buffer = (char*)&level;
        
        update_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        update_bind[2].buffer = (char*)&user_id;
        update_bind[2].is_unsigned = 1;
        
        update_bind[3].buffer_type = MYSQL_TYPE_STRING;
        update_bind[3].buffer = (char*)item_id.c_str();
        update_bind[3].buffer_length = item_id.length();
        
        mysql_stmt_bind_param(stmt, update_bind);
    } else {
        MYSQL_BIND insert_bind[6];
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
        
        // Handle metadata - can be NULL or JSON string
        my_bool metadata_is_null = metadata.empty();
        insert_bind[5].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[5].buffer = (char*)metadata.c_str();
        insert_bind[5].buffer_length = metadata.length();
        insert_bind[5].is_null = &metadata_is_null;
        
        mysql_stmt_bind_param(stmt, insert_bind);
    }
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_item execute");
        // print backtrace to help diagnose what path caused the error
        void* bt[20];
        int cnt = backtrace(bt, 20);
        std::cerr << DBG_INV "add_item execute failed, backtrace:" << std::endl;
        backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    } else {
        // show updated quantity for debugging (only when inventory_debug_ enabled)
        if (inventory_debug_) {
            std::cerr << DBG_INV "add_item: about to query qty for user=" << user_id << " item=" << item_id << "\n";
            int qty = get_item_quantity(user_id, item_id);
            std::cerr << DBG_INV "add_item: query returned qty=" << qty << " for user=" << user_id << " item=" << item_id << "\n";
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}

bool Database::has_item(uint64_t user_id, const std::string& item_id, int quantity) {
    return get_item_quantity(user_id, item_id) >= quantity;
}

int Database::get_item_quantity(uint64_t user_id, const std::string& item_id) {
    if (inventory_debug_) std::cerr << DBG_INV "get_item_quantity entry user=" << user_id << " item=" << item_id << "\n";
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_item_quantity acquire failed");
        return 0;
    }
    
    const char* query = "SELECT quantity FROM inventory WHERE user_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_item_quantity prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    
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
    const char* query = "SELECT COUNT(DISTINCT user_id) FROM inventory WHERE item_id = ? AND quantity > 0";
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
    const char* query = "DELETE FROM inventory WHERE item_id = ?";
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
    const char* query = "SELECT DISTINCT user_id FROM inventory WHERE item_id = ? AND quantity > 0";
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

std::vector<InventoryItem> Database::get_inventory(uint64_t user_id) {
    std::vector<InventoryItem> items;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_inventory acquire failed");
        return items;
    }
    
    const char* query = "SELECT item_id, item_type, quantity, level, metadata FROM inventory WHERE user_id = ? AND quantity > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_inventory prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return items;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_inventory execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return items;
    }
    
    // Bind results
    char item_id_buf[101];
    char item_type_buf[51];
    int quantity;
    int lvl;
    char metadata_buf[1001];
    unsigned long item_id_length, item_type_length, metadata_length;
    my_bool metadata_is_null;
    
    MYSQL_BIND result_bind[5];
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
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        InventoryItem item;
        item.item_id = std::string(item_id_buf, item_id_length);
        item.item_type = std::string(item_type_buf, item_type_length);
        item.quantity = quantity;
        item.level = lvl;
        item.metadata = metadata_is_null ? "" : std::string(metadata_buf, metadata_length);
        items.push_back(item);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return items;
}

// Update the metadata for an existing inventory item
bool Database::update_item_metadata(uint64_t user_id, const std::string& item_id, const std::string& new_metadata) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE inventory SET metadata = ? WHERE user_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("update_item_metadata prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
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

bool Database::remove_item(uint64_t user_id, const std::string& item_id, int quantity) {
    auto conn = pool_->acquire();
    
    const char* query = "UPDATE inventory SET quantity = quantity - ? WHERE user_id = ? AND item_id = ? AND quantity >= ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
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
    
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("remove_item execute");
    } else {
        if (inventory_debug_) {
            int qty = get_item_quantity(user_id, item_id);
            std::cerr << DBG_INV "remove_item: user=" << user_id << " item=" << item_id << " qty=" << qty << "\n";
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    return success;
}


} // namespace db
} // namespace bronx