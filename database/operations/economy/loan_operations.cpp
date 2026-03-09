#include "loan_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <cmath>

namespace bronx {
namespace db {

namespace loan_operations {

bool create_loan(Database* db, uint64_t user_id, int64_t principal, double interest_rate) {
    if (has_active_loan(db, user_id)) {
        return false; // User already has a loan
    }
    
    auto conn = db->get_pool()->acquire();
    
    // Calculate interest (applied immediately)
    int64_t interest = static_cast<int64_t>(std::round(principal * (interest_rate / 100.0)));
    int64_t total_owed = principal + interest;
    
    const char* query = "INSERT INTO loans (user_id, principal, interest, remaining, created_at) "
                       "VALUES (?, ?, ?, ?, NOW())";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("create_loan prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&principal;
    
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&interest;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&total_owed;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("create_loan bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    return success;
}

std::optional<LoanData> get_loan(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    
    const char* query = "SELECT user_id, principal, interest, remaining, "
                       "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(last_payment_at) "
                       "FROM loans WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("get_loan prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        db->log_error("get_loan bind param");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        db->log_error("get_loan execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    // Bind result
    LoanData loan;
    int64_t created_timestamp, last_payment_timestamp;
    my_bool is_null_last_payment;
    
    MYSQL_BIND result[6];
    memset(result, 0, sizeof(result));
    
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &loan.user_id;
    result[0].is_unsigned = 1;
    
    result[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result[1].buffer = &loan.principal;
    
    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = &loan.interest;
    
    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &loan.remaining;
    
    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = &created_timestamp;
    
    result[5].buffer_type = MYSQL_TYPE_LONGLONG;
    result[5].buffer = &last_payment_timestamp;
    result[5].is_null = &is_null_last_payment;
    
    if (mysql_stmt_bind_result(stmt, result) != 0) {
        db->log_error("get_loan bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_store_result(stmt) != 0) {
        db->log_error("get_loan store result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    if (mysql_stmt_fetch(stmt) == 0) {
        loan.created_at = std::chrono::system_clock::from_time_t(created_timestamp);
        
        if (!is_null_last_payment) {
            loan.last_payment_at = std::chrono::system_clock::from_time_t(last_payment_timestamp);
        }
        
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return loan;
    }
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return std::nullopt;
}

std::optional<int64_t> make_payment(Database* db, uint64_t user_id, int64_t amount) {
    auto loan = get_loan(db, user_id);
    if (!loan) return std::nullopt;
    
    if (amount <= 0 || amount > loan->remaining) {
        return std::nullopt;
    }
    
    int64_t new_remaining = loan->remaining - amount;
    
    auto conn = db->get_pool()->acquire();
    
    const char* query;
    if (new_remaining == 0) {
        // Loan fully paid off - delete the record
        query = "DELETE FROM loans WHERE user_id = ?";
    } else {
        // Update remaining balance
        query = "UPDATE loans SET remaining = ?, last_payment_at = NOW() WHERE user_id = ?";
    }
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        db->log_error("make_payment prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    if (new_remaining == 0) {
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("make_payment bind (delete)");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return std::nullopt;
        }
    } else {
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&new_remaining;
        
        bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[1].buffer = (char*)&user_id;
        bind[1].is_unsigned = 1;
        
        if (mysql_stmt_bind_param(stmt, bind) != 0) {
            db->log_error("make_payment bind (update)");
            mysql_stmt_close(stmt);
            db->get_pool()->release(conn);
            return std::nullopt;
        }
    }
    
    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    
    if (!success) return std::nullopt;
    
    return new_remaining;
}

bool has_active_loan(Database* db, uint64_t user_id) {
    return get_loan(db, user_id).has_value();
}

bool payoff_loan(Database* db, uint64_t user_id) {
    auto loan = get_loan(db, user_id);
    if (!loan) return false;
    
    auto result = make_payment(db, user_id, loan->remaining);
    return result.has_value() && *result == 0;
}

int64_t get_loan_balance(Database* db, uint64_t user_id) {
    auto loan = get_loan(db, user_id);
    return loan ? loan->remaining : 0;
}

} // namespace loan_operations

// Add these methods to the Database class
bool Database::create_loan(uint64_t user_id, int64_t principal, double interest_rate) {
    return loan_operations::create_loan(this, user_id, principal, interest_rate);
}

std::optional<LoanData> Database::get_loan(uint64_t user_id) {
    return loan_operations::get_loan(this, user_id);
}

std::optional<int64_t> Database::make_loan_payment(uint64_t user_id, int64_t amount) {
    return loan_operations::make_payment(this, user_id, amount);
}

bool Database::has_active_loan(uint64_t user_id) {
    return loan_operations::has_active_loan(this, user_id);
}

bool Database::payoff_loan(uint64_t user_id) {
    return loan_operations::payoff_loan(this, user_id);
}

int64_t Database::get_loan_balance(uint64_t user_id) {
    return loan_operations::get_loan_balance(this, user_id);
}

} // namespace db
} // namespace bronx
