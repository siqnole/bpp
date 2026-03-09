#include "bugreport_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

bool Database::add_bug_report(uint64_t user_id, const std::string& command_or_feature,
                              const std::string& reproduction_steps, const std::string& expected_behavior,
                              const std::string& actual_behavior, int64_t networth) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO bug_reports (user_id, command_or_feature, reproduction_steps, expected_behavior, actual_behavior, networth) VALUES (?,?,?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_bug_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)command_or_feature.c_str();
    bind[1].buffer_length = command_or_feature.size();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)reproduction_steps.c_str();
    bind[2].buffer_length = reproduction_steps.size();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)expected_behavior.c_str();
    bind[3].buffer_length = expected_behavior.size();

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)actual_behavior.c_str();
    bind[4].buffer_length = actual_behavior.size();

    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&networth;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("add_bug_report bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<BugReport> Database::fetch_bug_reports(const std::string& order_clause) {
    std::vector<BugReport> results;
    auto conn = pool_->acquire();
    std::string query = "SELECT id, user_id, command_or_feature, reproduction_steps, expected_behavior, actual_behavior, networth, UNIX_TIMESTAMP(submitted_at), `read`, resolved FROM bug_reports";
    if (!order_clause.empty()) {
        query += " ORDER BY " + order_clause;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
        log_error("fetch_bug_reports prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("fetch_bug_reports execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    // Bind result
    BugReport r;
    long long ts = 0;
    my_bool read_val = 0;
    my_bool resolved_val = 0;

    static char cmd_buf[256];
    static char repro_buf[4096];
    static char expected_buf[4096];
    static char actual_buf[4096];
    
    unsigned long cmd_len = 0;
    unsigned long repro_len = 0;
    unsigned long expected_len = 0;
    unsigned long actual_len = 0;

    MYSQL_BIND bind_result[10];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &r.id;
    bind_result[0].is_unsigned = 1;

    bind_result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[1].buffer = &r.user_id;
    bind_result[1].is_unsigned = 1;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = cmd_buf;
    bind_result[2].buffer_length = sizeof(cmd_buf);
    bind_result[2].length = &cmd_len;

    bind_result[3].buffer_type = MYSQL_TYPE_STRING;
    bind_result[3].buffer = repro_buf;
    bind_result[3].buffer_length = sizeof(repro_buf);
    bind_result[3].length = &repro_len;

    bind_result[4].buffer_type = MYSQL_TYPE_STRING;
    bind_result[4].buffer = expected_buf;
    bind_result[4].buffer_length = sizeof(expected_buf);
    bind_result[4].length = &expected_len;

    bind_result[5].buffer_type = MYSQL_TYPE_STRING;
    bind_result[5].buffer = actual_buf;
    bind_result[5].buffer_length = sizeof(actual_buf);
    bind_result[5].length = &actual_len;

    bind_result[6].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[6].buffer = &r.networth;

    bind_result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[7].buffer = &ts;

    bind_result[8].buffer_type = MYSQL_TYPE_TINY;
    bind_result[8].buffer = &read_val;

    bind_result[9].buffer_type = MYSQL_TYPE_TINY;
    bind_result[9].buffer = &resolved_val;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("fetch_bug_reports bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        log_error("fetch_bug_reports store result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        r.command_or_feature = std::string(cmd_buf, cmd_len);
        r.reproduction_steps = std::string(repro_buf, repro_len);
        r.expected_behavior = std::string(expected_buf, expected_len);
        r.actual_behavior = std::string(actual_buf, actual_len);
        r.submitted_at = std::chrono::system_clock::from_time_t(ts);
        r.read = read_val != 0;
        r.resolved = resolved_val != 0;
        results.push_back(r);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

bool Database::mark_bug_report_read(uint64_t report_id) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE bug_reports SET `read` = TRUE WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("mark_bug_report_read prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&report_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("mark_bug_report_read bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::mark_bug_report_resolved(uint64_t report_id) {
    auto conn = pool_->acquire();
    const char* query = "UPDATE bug_reports SET resolved = TRUE WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("mark_bug_report_resolved prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&report_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("mark_bug_report_resolved bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_bug_report(uint64_t report_id) {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM bug_reports WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("delete_bug_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&report_id;
    bind[0].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("delete_bug_report bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int Database::get_bug_report_count() {
    auto conn = pool_->acquire();
    const char* query = "SELECT COUNT(*) FROM bug_reports";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_bug_report_count prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_bug_report_count execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    int count = 0;
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));
    bind_result[0].buffer_type = MYSQL_TYPE_LONG;
    bind_result[0].buffer = &count;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0) {
        log_error("get_bug_report_count bind result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

} // namespace db
} // namespace bronx
