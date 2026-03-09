#include "shop_operations.h"
#include "../../core/database.h"
#include "../../../commands/titles.h"  // for commands::TitleDef
#include <cstring>

namespace bronx {
namespace db {

std::vector<ShopItem> Database::get_shop_items() {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_shop_items acquire failed");
        return {};
    }
    const char* query = "SELECT item_id, name, description, category, price, \
                        max_quantity, required_level, level, usable, metadata\
                        FROM shop_items ORDER BY category ASC, price ASC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    std::vector<ShopItem> results;
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_shop_items prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_shop_items execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    MYSQL_BIND bind[10];
    memset(bind, 0, sizeof(bind));

    // buffers for each column
    char item_id_buf[128]; unsigned long item_id_len;
    char name_buf[256]; unsigned long name_len;
    char desc_buf[1024]; unsigned long desc_len;
    char cat_buf[64]; unsigned long cat_len;
    int64_t price;
    int max_q;
    int req_lvl;
    int lvl;
    my_bool usable;
    char meta_buf[1024]; unsigned long meta_len;
    my_bool meta_is_null;

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = item_id_buf;
    bind[0].buffer_length = sizeof(item_id_buf);
    bind[0].length = &item_id_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = name_buf;
    bind[1].buffer_length = sizeof(name_buf);
    bind[1].length = &name_len;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = desc_buf;
    bind[2].buffer_length = sizeof(desc_buf);
    bind[2].length = &desc_len;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = cat_buf;
    bind[3].buffer_length = sizeof(cat_buf);
    bind[3].length = &cat_len;

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = &price;

    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = &max_q;

    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = &req_lvl;

    bind[7].buffer_type = MYSQL_TYPE_LONG;
    bind[7].buffer = &lvl;

    bind[8].buffer_type = MYSQL_TYPE_TINY;
    bind[8].buffer = &usable;

    bind[9].buffer_type = MYSQL_TYPE_STRING;
    bind[9].buffer = meta_buf;
    bind[9].buffer_length = sizeof(meta_buf);
    bind[9].is_null = &meta_is_null;
    bind[9].length = &meta_len;

    mysql_stmt_bind_result(stmt, bind);

    while (mysql_stmt_fetch(stmt) == 0) {
        ShopItem item;
        item.item_id = std::string(item_id_buf, item_id_len);
        item.name = std::string(name_buf, name_len);
        item.description = std::string(desc_buf, desc_len);
        item.category = std::string(cat_buf, cat_len);
        item.price = price;
        item.max_quantity = max_q;
        item.required_level = req_lvl;
        item.level = lvl;
        item.usable = usable;
        if (!meta_is_null) {
            item.metadata = std::string(meta_buf, meta_len);
        }
        results.push_back(item);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

std::optional<ShopItem> Database::get_shop_item(const std::string& item_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_shop_item acquire failed");
        return std::nullopt;
    }
    const char* query = "SELECT item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata FROM shop_items WHERE item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_shop_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item_id.c_str();
    bind[0].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_shop_item execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    // bind results same as get_shop_items
    MYSQL_BIND resbind[10];
    memset(resbind, 0, sizeof(resbind));
    char item_buf[128]; unsigned long item_len;
    char name_buf[256]; unsigned long name_len;
    char desc_buf[1024]; unsigned long desc_len;
    char cat_buf[64]; unsigned long cat_len;
    int64_t price;
    int max_q;
    int req_lvl;
    int lvl;
    my_bool usable;
    char meta_buf[1024]; unsigned long meta_len;
    my_bool meta_is_null;
    
    resbind[0].buffer_type = MYSQL_TYPE_STRING;
    resbind[0].buffer = item_buf;
    resbind[0].buffer_length = sizeof(item_buf);
    resbind[0].length = &item_len;
    
    resbind[1].buffer_type = MYSQL_TYPE_STRING;
    resbind[1].buffer = name_buf;
    resbind[1].buffer_length = sizeof(name_buf);
    resbind[1].length = &name_len;
    
    resbind[2].buffer_type = MYSQL_TYPE_STRING;
    resbind[2].buffer = desc_buf;
    resbind[2].buffer_length = sizeof(desc_buf);
    resbind[2].length = &desc_len;
    
    resbind[3].buffer_type = MYSQL_TYPE_STRING;
    resbind[3].buffer = cat_buf;
    resbind[3].buffer_length = sizeof(cat_buf);
    resbind[3].length = &cat_len;
    
    resbind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[4].buffer = &price;
    
    resbind[5].buffer_type = MYSQL_TYPE_LONG;
    resbind[5].buffer = &max_q;
    
    resbind[6].buffer_type = MYSQL_TYPE_LONG;
    resbind[6].buffer = &req_lvl;
    
    resbind[7].buffer_type = MYSQL_TYPE_LONG;
    resbind[7].buffer = &lvl;
    
    resbind[8].buffer_type = MYSQL_TYPE_TINY;
    resbind[8].buffer = &usable;
    
    resbind[9].buffer_type = MYSQL_TYPE_STRING;
    resbind[9].buffer = meta_buf;
    resbind[9].buffer_length = sizeof(meta_buf);
    resbind[9].is_null = &meta_is_null;
    resbind[9].length = &meta_len;

    mysql_stmt_bind_result(stmt, resbind);
    ShopItem item;
    if (mysql_stmt_fetch(stmt) == 0) {
        item.item_id = std::string(item_buf, item_len);
        item.name = std::string(name_buf, name_len);
        item.description = std::string(desc_buf, desc_len);
        item.category = std::string(cat_buf, cat_len);
        item.price = price;
        item.max_quantity = max_q;
        item.required_level = req_lvl;
        item.level = lvl;
        item.usable = usable;
        if (!meta_is_null) item.metadata = std::string(meta_buf, meta_len);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return item;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return std::nullopt;
}

bool Database::create_shop_item(const ShopItem& item) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("create_shop_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[10];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item.item_id.c_str();
    bind[0].buffer_length = item.item_id.length();
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item.name.c_str();
    bind[1].buffer_length = item.name.length();
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item.description.c_str();
    bind[2].buffer_length = item.description.length();
    
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)item.category.c_str();
    bind[3].buffer_length = item.category.length();
    
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&item.price;
    
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = (char*)&item.max_quantity;
    
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (char*)&item.required_level;
    
    bind[7].buffer_type = MYSQL_TYPE_LONG;
    bind[7].buffer = (char*)&item.level;
    
    bind[8].buffer_type = MYSQL_TYPE_TINY;
    bind[8].buffer = (char*)&item.usable;
    
    my_bool meta_is_null = item.metadata.empty();
    bind[9].buffer_type = MYSQL_TYPE_STRING;
    bind[9].buffer = (char*)item.metadata.c_str();
    bind[9].buffer_length = item.metadata.length();
    bind[9].is_null = &meta_is_null;

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("create_shop_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::update_shop_item_price(const std::string& item_id, int64_t new_price) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE shop_items SET price = ? WHERE item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("update_shop_item_price prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&new_price;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("update_shop_item_price execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::update_shop_item(const ShopItem& item) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE shop_items SET name = ?, description = ?, category = ?, price = ?, max_quantity = ?, required_level = ?, level = ?, usable = ?, metadata = ? WHERE item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("update_shop_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[10];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item.name.c_str();
    bind[0].buffer_length = item.name.length();
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item.description.c_str();
    bind[1].buffer_length = item.description.length();
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item.category.c_str();
    bind[2].buffer_length = item.category.length();
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&item.price;
    
    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer = (char*)&item.max_quantity;
    
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = (char*)&item.required_level;
    
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (char*)&item.level;
    
    bind[7].buffer_type = MYSQL_TYPE_TINY;
    bind[7].buffer = (char*)&item.usable;
    
    my_bool meta_is_null = item.metadata.empty();
    bind[8].buffer_type = MYSQL_TYPE_STRING;
    bind[8].buffer = (char*)item.metadata.c_str();
    bind[8].buffer_length = item.metadata.length();
    bind[8].is_null = &meta_is_null;

    bind[9].buffer_type = MYSQL_TYPE_STRING;
    bind[9].buffer = (char*)item.item_id.c_str();
    bind[9].buffer_length = item.item_id.length();

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("update_shop_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_shop_item(const std::string& item_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM shop_items WHERE item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_shop_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)item_id.c_str();
    bind[0].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("delete_shop_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

// Helper to parse an integer field from a JSON-like metadata string.  This
// is intentionally simple since we only need a couple of numeric keys and the
// JSON payload we store is under our control.
static int parse_meta_int(const std::string& meta, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = meta.find(pattern);
    if (pos == std::string::npos) return 0;
    pos = meta.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos++;
    // skip whitespace
    while (pos < meta.size() && isspace((unsigned char)meta[pos])) pos++;
    int sign = 1;
    if (pos < meta.size() && meta[pos] == '-') { sign = -1; pos++; }
    long val = 0;
    while (pos < meta.size() && isdigit((unsigned char)meta[pos])) {
        val = val * 10 + (meta[pos] - '0');
        pos++;
    }
    return static_cast<int>(val * sign);
}

// Convert metadata values back into JSON string for storage
static std::string make_meta_json(int rotation_slot, int purchase_limit) {
    std::string out = "{";
    out += "\"rotation_slot\":" + std::to_string(rotation_slot);
    out += ",\"purchase_limit\":" + std::to_string(purchase_limit);
    out += "}";
    return out;
}

// Dynamic titles implementation
std::vector<commands::TitleDef> Database::get_dynamic_titles() {
    std::vector<commands::TitleDef> out;
    auto items = get_shop_items();
    for (const auto& s : items) {
        if (s.category != "title") continue;
        commands::TitleDef t;
        t.item_id = s.item_id;
        t.display = s.name;
        t.shop_desc = s.description;
        t.price = s.price;
        t.rotation_slot = parse_meta_int(s.metadata, "rotation_slot");
        t.purchase_limit = parse_meta_int(s.metadata, "purchase_limit");
        out.push_back(t);
    }
    return out;
}

std::optional<commands::TitleDef> Database::get_dynamic_title(const std::string& item_id) {
    auto opt = get_shop_item(item_id);
    if (!opt) return std::nullopt;
    const ShopItem& s = *opt;
    if (s.category != "title") return std::nullopt;
    commands::TitleDef t;
    t.item_id = s.item_id;
    t.display = s.name;
    t.shop_desc = s.description;
    t.price = s.price;
    t.rotation_slot = parse_meta_int(s.metadata, "rotation_slot");
    t.purchase_limit = parse_meta_int(s.metadata, "purchase_limit");
    return t;
}

bool Database::create_dynamic_title(const commands::TitleDef& title) {
    // reuse shop_item insertion with category 'title'
    ShopItem item;
    item.item_id = title.item_id;
    item.name = title.display;
    item.description = title.shop_desc;
    item.category = "title";
    item.price = title.price;
    item.max_quantity = 0;
    item.required_level = 0;
    item.level = 1;
    item.usable = false;
    item.metadata = make_meta_json(title.rotation_slot, title.purchase_limit);
    return create_shop_item(item);
}

bool Database::update_dynamic_title(const commands::TitleDef& title) {
    ShopItem item;
    item.item_id = title.item_id;
    item.name = title.display;
    item.description = title.shop_desc;
    item.category = "title";
    item.price = title.price;
    item.max_quantity = 0;
    item.required_level = 0;
    item.level = 1;
    item.usable = false;
    item.metadata = make_meta_json(title.rotation_slot, title.purchase_limit);
    return update_shop_item(item);
}

bool Database::delete_dynamic_title(const std::string& item_id) {
    // just delete from shop_items
    return delete_shop_item(item_id);
}

} // namespace db
} // namespace bronx
