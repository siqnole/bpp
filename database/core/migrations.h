#pragma once
#include <vector>
#include <string>

namespace bronx {
namespace db {

/**
 * @brief Returns the list of SQL migration queries to initialize and update the database schema.
 */
std::vector<std::string> get_schema_migrations();

} // namespace db
} // namespace bronx
