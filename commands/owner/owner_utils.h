#pragma once
#include <string>
#include <vector>
#include "../../database/core/database.h"

namespace commands {
namespace owner {

// Helper to run a single SQL query that returns one numeric column, one row
int64_t sql_count(bronx::db::Database* db, const std::string& sql);

// Helper to run a SQL query returning multiple string columns, multiple rows
struct SqlRow { std::vector<std::string> cols; };
std::vector<SqlRow> sql_query(bronx::db::Database* db, const std::string& sql);

} // namespace owner
} // namespace commands
