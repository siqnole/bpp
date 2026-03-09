#pragma once

#include "../../core/types.h"
#include <optional>
#include <chrono>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// User operations extension for Database class
// These methods handle user account management, balances, and money operations
namespace user_operations {
    bool ensure_user_exists(Database* db, uint64_t user_id);
    std::optional<UserData> get_user(Database* db, uint64_t user_id);
    int64_t get_wallet(Database* db, uint64_t user_id);
    int64_t get_bank(Database* db, uint64_t user_id);
    int64_t get_bank_limit(Database* db, uint64_t user_id);
    int64_t get_networth(Database* db, uint64_t user_id);
    std::optional<int64_t> update_wallet(Database* db, uint64_t user_id, int64_t amount);
    std::optional<int64_t> update_bank(Database* db, uint64_t user_id, int64_t amount);
    TransactionResult transfer_money(Database* db, uint64_t from_user, uint64_t to_user, int64_t amount);
}

} // namespace db
} // namespace bronx