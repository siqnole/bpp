#pragma once

#include "../../core/types.h"
#include <optional>
#include <chrono>
#include <cstdint>

namespace bronx {
namespace db {

// Forward declaration
class Database;

// Loan operations for bank upgrade financing (guild_id=0 → global)
namespace loan_operations {
    // Create a new loan with interest (interest_rate is a percentage, e.g., 5.0 for 5%)
    bool create_loan(Database* db, uint64_t user_id, int64_t principal, double interest_rate, uint64_t guild_id = 0);
    
    // Get user's current loan (returns empty if no active loan)
    std::optional<LoanData> get_loan(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    
    // Make a payment towards the loan (returns new remaining balance)
    std::optional<int64_t> make_payment(Database* db, uint64_t user_id, int64_t amount, uint64_t guild_id = 0);
    
    // Check if user has an active loan
    bool has_active_loan(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    
    // Pay off loan completely (returns true if successful)
    bool payoff_loan(Database* db, uint64_t user_id, uint64_t guild_id = 0);
    
    // Get total amount owed (for convenience)
    int64_t get_loan_balance(Database* db, uint64_t user_id, uint64_t guild_id = 0);
}

} // namespace db
} // namespace bronx
