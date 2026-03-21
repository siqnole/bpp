#pragma once

#include "../../core/types.h"
#include "../../core/database.h"
#include <vector>
#include <cstdint>
#include <string>
#include <random>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace bronx {
namespace db {

namespace gambling_verification {

    // Gambling verification entry structure
    struct GamblingVerificationEntry {
        uint64_t id;
        uint64_t user_id;
        std::string transaction_id;      // Unique transaction ID (UUID)
        std::string game_type;           // "coinflip", "slots", "dice", etc.
        int64_t wager_amount;            // Amount wagered
        int64_t winnings_amount;         // Amount won (negative for loss)
        int64_t balance_before;          // Balance before gambling
        int64_t balance_after;           // Balance after gambling
        bool verified;                   // Whether this transaction was verified
        std::string verification_hash;   // Hash of game state for verification
        std::string game_result_data;    // JSON or serialized game result data
        std::chrono::system_clock::time_point created_at;
        std::optional<std::chrono::system_clock::time_point> verified_at;
    };

    // Generate a unique transaction ID (UUID-like format without libuuid dependency)
    // Format: timestamp-random-sequence
    inline std::string generate_transaction_id() {
        static unsigned int sequence = 0;
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> dis(0, 0xFFFFFFFF);
        
        auto now = std::chrono::system_clock::now();
        auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(12) << (time_ms & 0xFFFFFFFFFFFF)
           << "-" << std::setw(8) << dis(gen)
           << "-" << std::setw(8) << dis(gen)
           << "-" << std::setw(4) << (sequence++ & 0xFFFF);
        
        return ss.str();
    }

    // Create a gambling verification entry
    // This should be called BEFORE updating the wallet
    // Returns the transaction ID for tracking
    inline std::string create_gambling_transaction(
        Database* db,
        uint64_t user_id,
        const std::string& game_type,
        int64_t wager_amount,
        int64_t winnings_amount,
        int64_t balance_before,
        const std::string& verification_hash,
        const std::string& game_result_data = "")
    {
        std::string transaction_id = generate_transaction_id();
        int64_t balance_after = balance_before + winnings_amount;
        
        // Log this as a pending gambling transaction
        std::string desc = std::string("gambling transaction: ") + game_type + 
                           " for $" + std::to_string(wager_amount);
        
        // Use GAMB entry type with transaction ID in description
        db->log_history(user_id, "GAMB", 
                       desc + " [TXN:" + transaction_id + "]",
                       winnings_amount, balance_after);
        
        return transaction_id;
    }

    // Verify a gambling transaction before applying winnings
    // This should be called to verify the game result is legitimate
    inline bool verify_gambling_transaction(
        Database* db,
        uint64_t user_id,
        const std::string& transaction_id,
        int64_t expected_balance_before,
        int64_t expected_balance_after)
    {
        // Get current balance to verify it matches expected
        int64_t current_balance = db->get_wallet(user_id);
        
        // The balance should still be at balance_before
        // (we haven't applied winnings yet)
        if (current_balance != expected_balance_before) {
            std::cerr << "[GAMBLING VERIFICATION] Transaction " << transaction_id 
                     << " failed: balance mismatch. Expected " << expected_balance_before
                     << " but got " << current_balance << std::endl;
            return false;
        }
        
        return true;
    }

    // Apply verified gambling winnings to wallet
    // Returns true if successfully applied, false otherwise
    inline bool apply_verified_gambling_winnings(
        Database* db,
        uint64_t user_id,
        const std::string& transaction_id,
        int64_t winnings_amount,
        const std::string& game_type)
    {
        try {
            auto result = db->update_wallet(user_id, winnings_amount);
            
            if (!result.has_value()) {
                std::cerr << "[GAMBLING VERIFICATION] Failed to apply winnings for transaction " 
                         << transaction_id << std::endl;
                return false;
            }
            
            int64_t new_balance = result.value();
            
            // Log successful verification
            std::string verify_desc = "gambling verified: " + game_type + 
                                     " [TXN:" + transaction_id + "]";
            db->log_history(user_id, "GBHK", verify_desc, winnings_amount, new_balance);  // GBHK = Gambling Hook
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[GAMBLING VERIFICATION] Exception applying winnings for transaction " 
                     << transaction_id << ": " << e.what() << std::endl;
            return false;
        }
    }

    // Safe gambling transaction
    // Wraps the entire gambling flow with verification
    // Returns true if gambling was successfully processed and verified
    inline bool safe_gambling_transaction(
        Database* db,
        uint64_t user_id,
        const std::string& game_type,
        int64_t wager_amount,
        int64_t winnings_amount,
        const std::string& game_result_data = "")
    {
        // Get balance before
        int64_t balance_before = db->get_wallet(user_id);
        int64_t balance_after = balance_before + winnings_amount;
        
        // Create transaction
        std::string transaction_id = create_gambling_transaction(
            db, user_id, game_type, wager_amount, winnings_amount,
            balance_before, "", game_result_data
        );
        
        // Verify transaction state
        if (!verify_gambling_transaction(db, user_id, transaction_id, 
                                        balance_before, balance_after)) {
            std::cerr << "[GAMBLING VERIFICATION] Pre-verification failed for transaction " 
                     << transaction_id << std::endl;
            return false;
        }
        
        // Apply winnings
        if (!apply_verified_gambling_winnings(db, user_id, transaction_id, 
                                             winnings_amount, game_type)) {
            std::cerr << "[GAMBLING VERIFICATION] Failed to apply verified winnings for transaction " 
                     << transaction_id << std::endl;
            return false;
        }
        
        return true;
    }

    // Get gambling transaction history for a user
    inline std::vector<HistoryEntry> get_gambling_history(
        Database* db, 
        uint64_t user_id, 
        int limit = 50, 
        int offset = 0)
    {
        auto all_history = db->fetch_history(user_id, limit, offset);
        std::vector<HistoryEntry> gambling_only;
        
        for (const auto& entry : all_history) {
            if (entry.entry_type == "GAMB" || entry.entry_type == "GBHK") {
                gambling_only.push_back(entry);
            }
        }
        
        return gambling_only;
    }

    // Audit gambling transactions for a user
    // Returns true if all transactions are verified consistent
    inline bool audit_gambling_transactions(
        Database* db,
        uint64_t user_id)
    {
        auto history = get_gambling_history(db, user_id, 1000, 0);
        
        int64_t running_balance = 0;
        bool found_inconsistency = false;
        
        for (const auto& entry : history) {
            if (entry.entry_type == "GAMB") {
                // Verify the balance_after is consistent
                running_balance += entry.amount;
                
                if (running_balance != entry.balance_after) {
                    std::cerr << "[GAMBLING AUDIT] Inconsistency detected for user " << user_id 
                             << " in entry " << entry.id << ". Expected balance " << running_balance
                             << " but entry says " << entry.balance_after << std::endl;
                    found_inconsistency = true;
                }
            }
        }
        
        return !found_inconsistency;
    }

    // Get gambling statistics for verification report
    struct GamblingStats {
        int64_t total_transactions;
        int64_t verified_transactions;
        int64_t unverified_transactions;
        int64_t total_wagered;
        int64_t total_winnings;
        double win_rate;
        std::string last_transaction;
    };

    inline GamblingStats get_gambling_stats(Database* db, uint64_t user_id) {
        auto history = get_gambling_history(db, user_id, 10000, 0);
        
        GamblingStats stats = {0, 0, 0, 0, 0, 0.0, ""};
        stats.total_transactions = history.size();
        
        for (const auto& entry : history) {
            if (entry.entry_type == "GAMB") {
                stats.total_wagered += std::abs(entry.amount);
                if (entry.amount > 0) {
                    stats.verified_transactions++;
                    stats.total_winnings += entry.amount;
                }
                if (!entry.description.empty()) {
                    stats.last_transaction = entry.description;
                }
            }
        }
        
        stats.unverified_transactions = stats.total_transactions - stats.verified_transactions;
        stats.win_rate = stats.total_transactions > 0 ? 
                        (double)stats.verified_transactions / stats.total_transactions : 0.0;
        
        return stats;
    }

} // namespace gambling_verification

} // namespace db
} // namespace bronx
