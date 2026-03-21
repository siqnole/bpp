#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/gambling_verification.h"
#include "../economy_core.h"
#include <dpp/dpp.h>

using namespace bronx::db;
using namespace bronx::db::gambling_verification;

namespace commands {
namespace gambling {

// Gambling audit/verification command for users
inline Command* get_gambling_audit_command(Database* db) {
    static Command* audit = new Command("gamblingaudit", "verify your gambling transaction history", 
        "gambling", {"gambaudit", "gbet_audit", "verify_gambling"}, true,
        
        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Get gambling transactions for verification
            auto history = get_gambling_history(db, event.msg.author.id, 50, 0);
            
            if (history.empty()) {
                bronx::send_message(bot, event, bronx::error("no gambling transactions found"));
                return;
            }
            
            // Get gambling statistics
            auto stats = get_gambling_stats(db, event.msg.author.id);
            
            // Build verification report
            std::string report = "**Gambling Verification Report**\n";
            report += "━━━━━━━━━━━━━━━━━━━━━\n\n";
            report += "**Statistics:**\n";
            report += "• Total Transactions: " + std::to_string(stats.total_transactions) + "\n";
            report += "• Verified Transactions: " + std::to_string(stats.verified_transactions) + "\n";
            report += "• Unverified Transactions: " + std::to_string(stats.unverified_transactions) + "\n";
            report += "• Win Rate: " + std::to_string(static_cast<int>(stats.win_rate * 100)) + "%\n";
            report += "• Total Wagered: " + format_number(stats.total_wagered) + "\n";
            report += "• Total Winnings: " + format_number(stats.total_winnings) + "\n\n";
            
            // Run audit
            bool audit_passed = audit_gambling_transactions(db, event.msg.author.id);
            
            report += "**Audit Result:** " + std::string(audit_passed ? "✅ PASSED" : "❌ FAILED") + "\n";
            if (!audit_passed) {
                report += "⚠️ Inconsistencies detected in your gambling history. Contact support.\n";
            }
            
            // Show recent transactions
            report += "\n**Recent Transactions (max 10):**\n";
            int count = 0;
            for (const auto& entry : history) {
                if (entry.entry_type == "GAMB" && count < 10) {
                    report += "• " + entry.description + " | Balance: " + format_number(entry.balance_after) + "\n";
                    count++;
                }
            }
            
            auto embed = bronx::success(report)
                .set_color(audit_passed ? 0x00FF00 : 0xFF6B00);
            
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        
        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto user_id = event.command.get_issuing_user().id;
            
            // Get gambling transactions for verification
            auto history = get_gambling_history(db, user_id, 50, 0);
            
            if (history.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no gambling transactions found")));
                return;
            }
            
            // Get gambling statistics
            auto stats = get_gambling_stats(db, user_id);
            
            // Build verification report
            std::string report = "**Gambling Verification Report**\n";
            report += "━━━━━━━━━━━━━━━━━━━━━\n\n";
            report += "**Statistics:**\n";
            report += "• Total Transactions: " + std::to_string(stats.total_transactions) + "\n";
            report += "• Verified Transactions: " + std::to_string(stats.verified_transactions) + "\n";
            report += "• Unverified Transactions: " + std::to_string(stats.unverified_transactions) + "\n";
            report += "• Win Rate: " + std::to_string(static_cast<int>(stats.win_rate * 100)) + "%\n";
            report += "• Total Wagered: " + format_number(stats.total_wagered) + "\n";
            report += "• Total Winnings: " + format_number(stats.total_winnings) + "\n\n";
            
            // Run audit
            bool audit_passed = audit_gambling_transactions(db, user_id);
            
            report += "**Audit Result:** " + std::string(audit_passed ? "✅ PASSED" : "❌ FAILED") + "\n";
            if (!audit_passed) {
                report += "⚠️ Inconsistencies detected in your gambling history. Contact support.\n";
            }
            
            // Show recent transactions
            report += "\n**Recent Transactions (max 10):**\n";
            int count = 0;
            for (const auto& entry : history) {
                if (entry.entry_type == "GAMB" && count < 10) {
                    report += "• " + entry.description + " | Balance: " + format_number(entry.balance_after) + "\n";
                    count++;
                }
            }
            
            auto embed = bronx::success(report)
                .set_color(audit_passed ? 0x00FF00 : 0xFF6B00);
            
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        
        // Slash command options
        {}
    );
    
    return audit;
}

} // namespace gambling
} // namespace commands
