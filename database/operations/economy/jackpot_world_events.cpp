#include "jackpot_world_events.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ============================================================
// JACKPOT OPERATIONS
// ============================================================
namespace jackpot_operations {

int64_t get_jackpot_pool(Database* db) {
    auto conn = db->get_pool()->acquire();

    const char* query = "SELECT pool FROM progressive_jackpot WHERE id = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_jackpot_pool prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_jackpot_pool execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    int64_t pool = 0;
    MYSQL_BIND result[1];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &pool;

    if (mysql_stmt_bind_result(stmt, result) != 0 ||
        mysql_stmt_store_result(stmt) != 0 ||
        mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return pool;
}

std::optional<JackpotData> get_jackpot(Database* db) {
    auto conn = db->get_pool()->acquire();

    const char* query = "SELECT pool, COALESCE(last_winner_id, 0), last_won_amount, "
                        "total_won_all_time, times_won "
                        "FROM progressive_jackpot WHERE id = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_jackpot prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_jackpot execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    JackpotData data;
    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &data.pool;

    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &data.last_winner_id;
    result[1].is_unsigned = 1;

    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = &data.last_won_amount;

    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &data.total_won_all_time;

    result[4].buffer_type = MYSQL_TYPE_LONG;
    result[4].buffer = &data.times_won;

    if (mysql_stmt_bind_result(stmt, result) != 0 ||
        mysql_stmt_store_result(stmt) != 0 ||
        mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return data;
}

bool contribute_to_jackpot(Database* db, int64_t amount) {
    if (amount <= 0) return false;

    auto conn = db->get_pool()->acquire();

    const char* query = "UPDATE progressive_jackpot SET pool = pool + ? WHERE id = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("contribute_to_jackpot prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("contribute_to_jackpot bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

int64_t try_win_jackpot(Database* db, uint64_t user_id) {
    // Get current pool
    int64_t pool = get_jackpot_pool(db);
    if (pool < 10000) return 0; // minimum $10K in pool to win

    auto conn = db->get_pool()->acquire();

    // Atomically claim the pool: set pool=0 and record the winner
    const char* query = "UPDATE progressive_jackpot SET pool = 0, last_winner_id = ?, "
                        "last_won_amount = pool, last_won_at = NOW(), "
                        "total_won_all_time = total_won_all_time + pool, "
                        "times_won = times_won + 1 "
                        "WHERE id = 1 AND pool > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("try_win_jackpot prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("try_win_jackpot bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("try_win_jackpot execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);

    if (affected == 0) return 0; // race: someone else won first

    // Record in history
    {
        auto conn2 = db->get_pool()->acquire();
        const char* hist_query = "INSERT INTO jackpot_history (user_id, amount, pool_before) VALUES (?, ?, ?)";
        MYSQL_STMT* hist_stmt = mysql_stmt_init(conn2->get());

        if (mysql_stmt_prepare(hist_stmt, hist_query, strlen(hist_query)) == 0) {
            MYSQL_BIND hbind[3];
            memset(hbind, 0, sizeof(hbind));

            hbind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            hbind[0].buffer = (char*)&user_id;
            hbind[0].is_unsigned = 1;

            hbind[1].buffer_type = MYSQL_TYPE_LONGLONG;
            hbind[1].buffer = (char*)&pool;

            hbind[2].buffer_type = MYSQL_TYPE_LONGLONG;
            hbind[2].buffer = (char*)&pool;

            if (mysql_stmt_bind_param(hist_stmt, hbind) == 0) {
                mysql_stmt_execute(hist_stmt);
            }
        }
        mysql_stmt_close(hist_stmt);
        db->get_pool()->release(conn2);
    }

    return pool;
}

std::vector<JackpotHistoryEntry> get_jackpot_history(Database* db, int limit) {
    std::vector<JackpotHistoryEntry> entries;
    auto conn = db->get_pool()->acquire();

    const char* query = "SELECT user_id, amount, pool_before, UNIX_TIMESTAMP(won_at) "
                        "FROM jackpot_history ORDER BY won_at DESC LIMIT ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_jackpot_history prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return entries;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&limit;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_jackpot_history exec");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return entries;
    }

    JackpotHistoryEntry entry;
    MYSQL_BIND result[4];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &entry.user_id;
    result[0].is_unsigned = 1;

    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &entry.amount;

    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = &entry.pool_before;

    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &entry.won_at_timestamp;

    if (mysql_stmt_bind_result(stmt, result) != 0 ||
        mysql_stmt_store_result(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return entries;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        entries.push_back(entry);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return entries;
}

} // namespace jackpot_operations

// ============================================================
// WORLD EVENT OPERATIONS
// ============================================================
namespace world_event_operations {

std::optional<WorldEventData> get_active_event(Database* db) {
    // First expire any events whose time has passed
    expire_events(db);

    auto conn = db->get_pool()->acquire();

    const char* query = "SELECT id, event_type, event_name, description, emoji, "
                        "bonus_type, bonus_value, UNIX_TIMESTAMP(started_at), "
                        "UNIX_TIMESTAMP(ends_at), active "
                        "FROM world_events WHERE active = 1 AND ends_at > NOW() "
                        "ORDER BY started_at DESC LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_active_event prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_active_event execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    WorldEventData ev;
    ev.id = 0;
    char event_type_buf[65], event_name_buf[129], desc_buf[2048], emoji_buf[33], bonus_type_buf[65];
    unsigned long event_type_len, event_name_len, desc_len, emoji_len, bonus_type_len;

    MYSQL_BIND result[10];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &ev.id;
    result[0].is_unsigned = 1;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = event_type_buf;
    result[1].buffer_length = sizeof(event_type_buf);
    result[1].length = &event_type_len;

    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = event_name_buf;
    result[2].buffer_length = sizeof(event_name_buf);
    result[2].length = &event_name_len;

    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = desc_buf;
    result[3].buffer_length = sizeof(desc_buf);
    result[3].length = &desc_len;

    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = emoji_buf;
    result[4].buffer_length = sizeof(emoji_buf);
    result[4].length = &emoji_len;

    result[5].buffer_type = MYSQL_TYPE_STRING;
    result[5].buffer = bonus_type_buf;
    result[5].buffer_length = sizeof(bonus_type_buf);
    result[5].length = &bonus_type_len;

    result[6].buffer_type = MYSQL_TYPE_DOUBLE;
    result[6].buffer = &ev.bonus_value;

    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &ev.started_at_timestamp;

    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &ev.ends_at_timestamp;

    my_bool active_val = 0;
    result[9].buffer_type = MYSQL_TYPE_TINY;
    result[9].buffer = &active_val;

    if (mysql_stmt_bind_result(stmt, result) != 0 ||
        mysql_stmt_store_result(stmt) != 0) {
        db->log_error("get_active_event bind/store");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt; // no active event
    }

    ev.event_type = std::string(event_type_buf, event_type_len);
    ev.event_name = std::string(event_name_buf, event_name_len);
    ev.description = std::string(desc_buf, desc_len);
    ev.emoji = std::string(emoji_buf, emoji_len);
    ev.bonus_type = std::string(bonus_type_buf, bonus_type_len);
    ev.active = active_val;

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ev;
}

bool start_event(Database* db, const std::string& event_type, const std::string& event_name,
                 const std::string& description, const std::string& emoji,
                 const std::string& bonus_type, double bonus_value, int duration_minutes) {
    // End any currently active event first
    end_active_event(db);

    auto conn = db->get_pool()->acquire();

    const char* query = "INSERT INTO world_events (event_type, event_name, description, emoji, "
                        "bonus_type, bonus_value, started_at, ends_at, active) "
                        "VALUES (?, ?, ?, ?, ?, ?, NOW(), DATE_ADD(NOW(), INTERVAL ? MINUTE), 1)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("start_event prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));

    unsigned long et_len = event_type.size();
    unsigned long en_len = event_name.size();
    unsigned long d_len = description.size();
    unsigned long em_len = emoji.size();
    unsigned long bt_len = bonus_type.size();

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)event_type.c_str();
    bind[0].buffer_length = et_len;
    bind[0].length = &et_len;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)event_name.c_str();
    bind[1].buffer_length = en_len;
    bind[1].length = &en_len;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)description.c_str();
    bind[2].buffer_length = d_len;
    bind[2].length = &d_len;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)emoji.c_str();
    bind[3].buffer_length = em_len;
    bind[3].length = &em_len;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)bonus_type.c_str();
    bind[4].buffer_length = bt_len;
    bind[4].length = &bt_len;

    bind[5].buffer_type = MYSQL_TYPE_DOUBLE;
    bind[5].buffer = (char*)&bonus_value;

    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (char*)&duration_minutes;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("start_event bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool end_active_event(Database* db) {
    auto conn = db->get_pool()->acquire();

    const char* query = "UPDATE world_events SET active = 0 WHERE active = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("end_active_event prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

int expire_events(Database* db) {
    auto conn = db->get_pool()->acquire();

    const char* query = "UPDATE world_events SET active = 0 WHERE active = 1 AND ends_at <= NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("expire_events prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("expire_events execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    int affected = (int)mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return affected;
}

double get_active_bonus(Database* db, const std::string& bonus_type) {
    auto ev = get_active_event(db);
    if (!ev) return 0.0;
    if (ev->bonus_type == bonus_type) return ev->bonus_value;
    return 0.0;
}

std::vector<WorldEventData> get_event_history(Database* db, int limit) {
    std::vector<WorldEventData> events;
    auto conn = db->get_pool()->acquire();

    const char* query = "SELECT id, event_type, event_name, description, emoji, "
                        "bonus_type, bonus_value, UNIX_TIMESTAMP(started_at), "
                        "UNIX_TIMESTAMP(ends_at), active "
                        "FROM world_events ORDER BY started_at DESC LIMIT ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());

    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_event_history prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return events;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&limit;

    if (mysql_stmt_bind_param(stmt, bind) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_event_history exec");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return events;
    }

    WorldEventData ev;
    char event_type_buf[65], event_name_buf[129], desc_buf[2048], emoji_buf[33], bonus_type_buf[65];
    unsigned long event_type_len, event_name_len, desc_len, emoji_len, bonus_type_len;
    my_bool active_val = 0;

    MYSQL_BIND result[10];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &ev.id;
    result[0].is_unsigned = 1;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = event_type_buf;
    result[1].buffer_length = sizeof(event_type_buf);
    result[1].length = &event_type_len;

    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = event_name_buf;
    result[2].buffer_length = sizeof(event_name_buf);
    result[2].length = &event_name_len;

    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = desc_buf;
    result[3].buffer_length = sizeof(desc_buf);
    result[3].length = &desc_len;

    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = emoji_buf;
    result[4].buffer_length = sizeof(emoji_buf);
    result[4].length = &emoji_len;

    result[5].buffer_type = MYSQL_TYPE_STRING;
    result[5].buffer = bonus_type_buf;
    result[5].buffer_length = sizeof(bonus_type_buf);
    result[5].length = &bonus_type_len;

    result[6].buffer_type = MYSQL_TYPE_DOUBLE;
    result[6].buffer = &ev.bonus_value;

    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &ev.started_at_timestamp;

    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &ev.ends_at_timestamp;

    result[9].buffer_type = MYSQL_TYPE_TINY;
    result[9].buffer = &active_val;

    if (mysql_stmt_bind_result(stmt, result) != 0 ||
        mysql_stmt_store_result(stmt) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return events;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        ev.event_type = std::string(event_type_buf, event_type_len);
        ev.event_name = std::string(event_name_buf, event_name_len);
        ev.description = std::string(desc_buf, desc_len);
        ev.emoji = std::string(emoji_buf, emoji_len);
        ev.bonus_type = std::string(bonus_type_buf, bonus_type_len);
        ev.active = active_val;
        events.push_back(ev);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return events;
}

} // namespace world_event_operations

// ============================================================
// Database class wrapper methods
// ============================================================

// --- Jackpot wrappers ---
int64_t Database::get_jackpot_pool() {
    return jackpot_operations::get_jackpot_pool(this);
}

std::optional<JackpotData> Database::get_jackpot() {
    return jackpot_operations::get_jackpot(this);
}

bool Database::contribute_to_jackpot(int64_t amount) {
    return jackpot_operations::contribute_to_jackpot(this, amount);
}

int64_t Database::try_win_jackpot(uint64_t user_id) {
    return jackpot_operations::try_win_jackpot(this, user_id);
}

std::vector<JackpotHistoryEntry> Database::get_jackpot_history(int limit) {
    return jackpot_operations::get_jackpot_history(this, limit);
}

// --- World Event wrappers ---
std::optional<WorldEventData> Database::get_active_world_event() {
    return world_event_operations::get_active_event(this);
}

bool Database::start_world_event(const std::string& event_type, const std::string& event_name,
                                  const std::string& description, const std::string& emoji,
                                  const std::string& bonus_type, double bonus_value, int duration_minutes) {
    return world_event_operations::start_event(this, event_type, event_name, description,
                                                emoji, bonus_type, bonus_value, duration_minutes);
}

bool Database::end_active_world_event() {
    return world_event_operations::end_active_event(this);
}

int Database::expire_world_events() {
    return world_event_operations::expire_events(this);
}

double Database::get_world_event_bonus(const std::string& bonus_type) {
    return world_event_operations::get_active_bonus(this, bonus_type);
}

std::vector<WorldEventData> Database::get_world_event_history(int limit) {
    return world_event_operations::get_event_history(this, limit);
}

} // namespace db
} // namespace bronx
