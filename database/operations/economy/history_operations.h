#pragma once

#include "../../core/types.h"
#include "../../core/database.h"
#include <vector>
#include <cstdint>
#include <string>

namespace bronx {
namespace db {

namespace history_operations {
    // Log a history entry for a user
    // entry_type: CMD (command), BAL (balance change), FSH (fishing), PAY (payment), GAM (gambling), SHP (shop)
    inline bool log_entry(Database* db, uint64_t user_id, const std::string& entry_type,
                          const std::string& description, int64_t amount = 0, int64_t balance_after = 0) {
        return db->log_history(user_id, entry_type, description, amount, balance_after);
    }
    
    // Convenience helpers for common entry types
    inline bool log_command(Database* db, uint64_t user_id, const std::string& command) {
        return db->log_history(user_id, "CMD", "ran ." + command);
    }
    
    // Simple balance change log (no amount/balance tracking)
    inline bool log_balance_change(Database* db, uint64_t user_id, const std::string& description) {
        return db->log_history(user_id, "BAL", description, 0, 0);
    }
    
    inline bool log_balance_change(Database* db, uint64_t user_id, const std::string& description, 
                                   int64_t amount, int64_t balance_after) {
        return db->log_history(user_id, "BAL", description, amount, balance_after);
    }
    
    // Simple gambling log (no amount/balance tracking)
    inline bool log_gambling(Database* db, uint64_t user_id, const std::string& description) {
        return db->log_history(user_id, "GAM", description, 0, 0);
    }
    
    inline bool log_fishing(Database* db, uint64_t user_id, const std::string& description,
                            int64_t value, int64_t balance_after) {
        return db->log_history(user_id, "FSH", description, value, balance_after);
    }
    
    // Simple fishing log (no amount/balance tracking)
    inline bool log_fishing(Database* db, uint64_t user_id, const std::string& description) {
        return db->log_history(user_id, "FSH", description, 0, 0);
    }
    
    inline bool log_payment(Database* db, uint64_t user_id, const std::string& description,
                            int64_t amount, int64_t balance_after) {
        return db->log_history(user_id, "PAY", description, amount, balance_after);
    }
    
    inline bool log_gambling(Database* db, uint64_t user_id, const std::string& description,
                             int64_t amount, int64_t balance_after) {
        return db->log_history(user_id, "GAM", description, amount, balance_after);
    }
    
    inline bool log_shop(Database* db, uint64_t user_id, const std::string& description,
                         int64_t amount, int64_t balance_after) {
        return db->log_history(user_id, "SHP", description, amount, balance_after);
    }
    
    // Fetch history for a user
    inline std::vector<HistoryEntry> fetch_history(Database* db, uint64_t user_id, int limit = 50, int offset = 0) {
        return db->fetch_history(user_id, limit, offset);
    }
    
    // Get total count of history entries for a user
    inline int get_count(Database* db, uint64_t user_id) {
        return db->get_history_count(user_id);
    }
    
    // Clear all history for a user
    inline bool clear_history(Database* db, uint64_t user_id) {
        return db->clear_history(user_id);
    }
}

} // namespace db
} // namespace bronx
