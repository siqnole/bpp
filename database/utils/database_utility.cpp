#include "database_utility.h"
#include "../core/database.h"
#include <iostream>
#include <mariadb/mysql.h>

namespace bronx {
namespace db {

bool Database::execute(const std::string& query) {
    auto conn = pool_->acquire();
    // clear any previous error so a successful query doesn't leave stale text
    last_error_.clear();
    bool success = mysql_query(conn->get(), query.c_str()) == 0;
    if (!success) {
        last_error_ = mysql_error(conn->get());
    }
    pool_->release(conn);
    return success;
}

int Database::execute_batch(const std::vector<std::string>& queries) {
    if (queries.empty()) return 0;
    auto conn = pool_->acquire();
    if (!conn) return 0;
    int ok = 0;
    for (const auto& q : queries) {
        if (mysql_query(conn->get(), q.c_str()) == 0) {
            // consume any result set (needed for multi-statement mode)
            MYSQL_RES* res = mysql_store_result(conn->get());
            if (res) mysql_free_result(res);
            // drain any remaining result sets from CLIENT_MULTI_STATEMENTS
            while (mysql_next_result(conn->get()) == 0) {
                res = mysql_store_result(conn->get());
                if (res) mysql_free_result(res);
            }
            ++ok;
        } else {
            std::cerr << "execute_batch error: " << mysql_error(conn->get())
                      << " [query: " << q.substr(0, 80) << "...]\n";
        }
    }
    pool_->release(conn);
    return ok;
}

void Database::log_error(const std::string& context) {
    std::cerr << "Database error [" << context << "]: " << last_error_ << std::endl;
    // avoid stale messages lingering on the connection
    last_error_.clear();
}

std::string Database::get_last_error() const {
    return last_error_;
}

} // namespace db
} // namespace bronx