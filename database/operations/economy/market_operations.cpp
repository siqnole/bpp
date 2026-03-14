#include "market_operations.h"
#include "../../core/database.h"
#include <cstring>

namespace bronx {
namespace db {

using namespace market_operations;

std::vector<MarketItem> Database::get_market_items(uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_market_items acquire failed");
        return {};
    }
    const char* query = "SELECT guild_id, item_id, name, description, category, price, max_quantity, metadata, expires_at "
                        "FROM guild_market_items WHERE guild_id = ? AND (expires_at IS NULL OR expires_at > NOW()) "
                        "ORDER BY category ASC, price ASC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    std::vector<MarketItem> results;
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_market_items prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_market_items execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    MYSQL_BIND resbind[9];
    memset(resbind, 0, sizeof(resbind));
    uint64_t gid;
    char item_buf[128]; unsigned long item_len;
    char name_buf[256]; unsigned long name_len;
    char desc_buf[1024]; unsigned long desc_len;
    char cat_buf[64]; unsigned long cat_len;
    int64_t price;
    int max_q;
    char meta_buf[1024]; unsigned long meta_len;
    my_bool meta_is_null;
    MYSQL_TIME expires;
    my_bool expires_is_null;

    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &gid;
    resbind[1].buffer_type = MYSQL_TYPE_STRING;
    resbind[1].buffer = item_buf;
    resbind[1].buffer_length = sizeof(item_buf);
    resbind[1].length = &item_len;
    resbind[2].buffer_type = MYSQL_TYPE_STRING;
    resbind[2].buffer = name_buf;
    resbind[2].buffer_length = sizeof(name_buf);
    resbind[2].length = &name_len;
    resbind[3].buffer_type = MYSQL_TYPE_STRING;
    resbind[3].buffer = desc_buf;
    resbind[3].buffer_length = sizeof(desc_buf);
    resbind[3].length = &desc_len;
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = cat_buf;
    resbind[4].buffer_length = sizeof(cat_buf);
    resbind[4].length = &cat_len;
    resbind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[5].buffer = &price;
    resbind[6].buffer_type = MYSQL_TYPE_LONG;
    resbind[6].buffer = &max_q;
    resbind[7].buffer_type = MYSQL_TYPE_STRING;
    resbind[7].buffer = meta_buf;
    resbind[7].buffer_length = sizeof(meta_buf);
    resbind[7].is_null = &meta_is_null;
    resbind[7].length = &meta_len;
    resbind[8].buffer_type = MYSQL_TYPE_DATETIME;
    resbind[8].buffer = &expires;
    resbind[8].is_null = &expires_is_null;

    mysql_stmt_bind_result(stmt, resbind);
    while (mysql_stmt_fetch(stmt) == 0) {
        MarketItem item;
        item.guild_id = gid;
        item.item_id = std::string(item_buf, item_len);
        item.name = std::string(name_buf, name_len);
        item.description = std::string(desc_buf, desc_len);
        item.category = std::string(cat_buf, cat_len);
        item.price = price;
        item.max_quantity = max_q;
        if (!meta_is_null) item.metadata = std::string(meta_buf, meta_len);
        if (!expires_is_null) {
            std::tm tm = {0};
            tm.tm_year = expires.year - 1900;
            tm.tm_mon = expires.month - 1;
            tm.tm_mday = expires.day;
            tm.tm_hour = expires.hour;
            tm.tm_min = expires.minute;
            tm.tm_sec = expires.second;
            item.expires_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        results.push_back(item);
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

std::optional<MarketItem> Database::get_market_item(uint64_t guild_id, const std::string& item_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_market_item acquire failed");
        return std::nullopt;
    }
    const char* query = "SELECT guild_id, item_id, name, description, category, price, max_quantity, metadata, expires_at "
                        "FROM guild_market_items WHERE guild_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_market_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_market_item execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND resbind[9];
    memset(resbind, 0, sizeof(resbind));
    uint64_t gid;
    char item_buf[128]; unsigned long item_len;
    char name_buf[256]; unsigned long name_len;
    char desc_buf[1024]; unsigned long desc_len;
    char cat_buf[64]; unsigned long cat_len;
    int64_t price;
    int max_q;
    char meta_buf[1024]; unsigned long meta_len;
    my_bool meta_is_null;
    MYSQL_TIME expires;
    my_bool expires_is_null;

    resbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[0].buffer = &gid;
    resbind[1].buffer_type = MYSQL_TYPE_STRING;
    resbind[1].buffer = item_buf;
    resbind[1].buffer_length = sizeof(item_buf);
    resbind[1].length = &item_len;
    resbind[2].buffer_type = MYSQL_TYPE_STRING;
    resbind[2].buffer = name_buf;
    resbind[2].buffer_length = sizeof(name_buf);
    resbind[2].length = &name_len;
    resbind[3].buffer_type = MYSQL_TYPE_STRING;
    resbind[3].buffer = desc_buf;
    resbind[3].buffer_length = sizeof(desc_buf);
    resbind[3].length = &desc_len;
    resbind[4].buffer_type = MYSQL_TYPE_STRING;
    resbind[4].buffer = cat_buf;
    resbind[4].buffer_length = sizeof(cat_buf);
    resbind[4].length = &cat_len;
    resbind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    resbind[5].buffer = &price;
    resbind[6].buffer_type = MYSQL_TYPE_LONG;
    resbind[6].buffer = &max_q;
    resbind[7].buffer_type = MYSQL_TYPE_STRING;
    resbind[7].buffer = meta_buf;
    resbind[7].buffer_length = sizeof(meta_buf);
    resbind[7].is_null = &meta_is_null;
    resbind[7].length = &meta_len;
    resbind[8].buffer_type = MYSQL_TYPE_DATETIME;
    resbind[8].buffer = &expires;
    resbind[8].is_null = &expires_is_null;

    mysql_stmt_bind_result(stmt, resbind);
    if (mysql_stmt_fetch(stmt) == 0) {
        MarketItem item;
        item.guild_id = gid;
        item.item_id = std::string(item_buf, item_len);
        item.name = std::string(name_buf, name_len);
        item.description = std::string(desc_buf, desc_len);
        item.category = std::string(cat_buf, cat_len);
        item.price = price;
        item.max_quantity = max_q;
        if (!meta_is_null) item.metadata = std::string(meta_buf, meta_len);
        if (!expires_is_null) {
            std::tm tm = {0};
            tm.tm_year = expires.year - 1900;
            tm.tm_mon = expires.month - 1;
            tm.tm_mday = expires.day;
            tm.tm_hour = expires.hour;
            tm.tm_min = expires.minute;
            tm.tm_sec = expires.second;
            item.expires_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return item;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return std::nullopt;
}

bool Database::create_market_item(const MarketItem& item) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("create_market_item acquire failed");
        return false;
    }
    const char* query = "INSERT INTO guild_market_items (guild_id, item_id, name, description, category, price, max_quantity, metadata, expires_at) "
                        "VALUES (?,?,?,?,?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("create_market_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[9];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&item.guild_id;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item.item_id.c_str();
    bind[1].buffer_length = item.item_id.length();
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item.name.c_str();
    bind[2].buffer_length = item.name.length();
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)item.description.c_str();
    bind[3].buffer_length = item.description.length();
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)item.category.c_str();
    bind[4].buffer_length = item.category.length();
    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&item.price;
    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (char*)&item.max_quantity;
    my_bool meta_is_null = item.metadata.empty();
    bind[7].buffer_type = MYSQL_TYPE_STRING;
    bind[7].buffer = (char*)item.metadata.c_str();
    bind[7].buffer_length = item.metadata.length();
    bind[7].is_null = &meta_is_null;
    my_bool expires_is_null = !item.expires_at.has_value();
    MYSQL_TIME expires_tm;
    if (!expires_is_null) {
        std::time_t t = std::chrono::system_clock::to_time_t(*item.expires_at);
        std::tm* tm = std::gmtime(&t);
        expires_tm.year = tm->tm_year + 1900;
        expires_tm.month = tm->tm_mon + 1;
        expires_tm.day = tm->tm_mday;
        expires_tm.hour = tm->tm_hour;
        expires_tm.minute = tm->tm_min;
        expires_tm.second = tm->tm_sec;
    }
    bind[8].buffer_type = MYSQL_TYPE_DATETIME;
    bind[8].buffer = &expires_tm;
    bind[8].is_null = &expires_is_null;

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("create_market_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::update_market_item(const MarketItem& item) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("update_market_item acquire failed");
        return false;
    }
    const char* query = "UPDATE guild_market_items SET name = ?, description = ?, category = ?, price = ?, max_quantity = ?, metadata = ?, expires_at = ? "
                        "WHERE guild_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("update_market_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[9];
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
    my_bool meta_is_null = item.metadata.empty();
    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = (char*)item.metadata.c_str();
    bind[5].buffer_length = item.metadata.length();
    bind[5].is_null = &meta_is_null;
    my_bool expires_is_null = !item.expires_at.has_value();
    MYSQL_TIME expires_tm;
    if (!expires_is_null) {
        std::time_t t = std::chrono::system_clock::to_time_t(*item.expires_at);
        std::tm* tm = std::gmtime(&t);
        expires_tm.year = tm->tm_year + 1900;
        expires_tm.month = tm->tm_mon + 1;
        expires_tm.day = tm->tm_mday;
        expires_tm.hour = tm->tm_hour;
        expires_tm.minute = tm->tm_min;
        expires_tm.second = tm->tm_sec;
    }
    bind[6].buffer_type = MYSQL_TYPE_DATETIME;
    bind[6].buffer = &expires_tm;
    bind[6].is_null = &expires_is_null;
    bind[7].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[7].buffer = (char*)&item.guild_id;
    bind[8].buffer_type = MYSQL_TYPE_STRING;
    bind[8].buffer = (char*)item.item_id.c_str();
    bind[8].buffer_length = item.item_id.length();

    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("update_market_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_market_item(uint64_t guild_id, const std::string& item_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("delete_market_item acquire failed");
        return false;
    }
    const char* query = "DELETE FROM guild_market_items WHERE guild_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_market_item prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)item_id.c_str();
    bind[1].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("delete_market_item execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::adjust_market_item_quantity(uint64_t guild_id, const std::string& item_id, int delta) {
    if (delta == 0) return true;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("adjust_market_item_quantity acquire failed");
        return false;
    }
    const char* query = "UPDATE guild_market_items SET max_quantity = max_quantity + ? WHERE guild_id = ? AND item_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("adjust_market_item_quantity prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&delta;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&guild_id;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)item_id.c_str();
    bind[2].buffer_length = item_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("adjust_market_item_quantity execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

} // namespace db
} // namespace bronx
