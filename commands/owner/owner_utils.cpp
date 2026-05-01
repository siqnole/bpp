#include "owner_utils.h"
#include <mariadb/mysql.h>

namespace commands {
namespace owner {

int64_t sql_count(bronx::db::Database* db, const std::string& sql) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return -1;
    if (mysql_query(conn->get(), sql.c_str()) != 0) { db->get_pool()->release(conn); return -1; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return -1; }
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t val = (row && row[0]) ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return val;
}

std::vector<SqlRow> sql_query(bronx::db::Database* db, const std::string& sql) {
    std::vector<SqlRow> rows;
    auto conn = db->get_pool()->acquire();
    if (!conn) return rows;
    if (mysql_query(conn->get(), sql.c_str()) != 0) { db->get_pool()->release(conn); return rows; }
    MYSQL_RES* res = mysql_store_result(conn->get());
    if (!res) { db->get_pool()->release(conn); return rows; }
    int ncols = mysql_num_fields(res);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        SqlRow r;
        for (int i = 0; i < ncols; i++) r.cols.push_back(row[i] ? row[i] : "0");
        rows.push_back(std::move(r));
    }
    mysql_free_result(res);
    db->get_pool()->release(conn);
    return rows;
}

} // namespace owner
} // namespace commands
