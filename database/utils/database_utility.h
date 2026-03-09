#pragma once

#include <string>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// Database utility operations extension for Database class
// These methods provide error handling and general database utilities
namespace database_utility {
    bool execute(Database* db, const std::string& query);
    void log_error(Database* db, const std::string& context);
    std::string get_last_error(Database* db);
}

} // namespace db
} // namespace bronx