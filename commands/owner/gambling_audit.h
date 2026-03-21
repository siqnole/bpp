#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/gambling_verification.h"
#include <dpp/dpp.h>

using namespace bronx::db;
using namespace bronx::db::gambling_verification;

namespace commands {
namespace owner {

// Owner command to audit specific users' gambling transactions
inline Command* get_gambling_audit_owner_command(Database* db) {
    static Command* audit = new Command(
        "gambaudit", 
        "audit a user's gambling transactions (owner only)",
        "owner",
        {"audit_gambling", "check_gambling"},
        true,

        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Owner check
            auto user = db->get_user(event.msg.author.id);
            if (!user || !user->dev) {
                bronx::send_message(bot, event, bronx::error("owner only"));
                return;
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: gambaudit <user_id> [limit]"));
                return;
            }

            uint64_t target_id;
            try {
                target_id = std::stoull(args[0]);
            } catch (...) {
                bronx::send_message(bot, event, bronx::error("invalid user id"));
                return;
            }

            int limit = 50;
            if (args.size() >= 2) {
                try {
                    limit = std::stoi(args[1]);
                    if (limit > 1000) limit = 1000;
                    if (limit < 1) limit = 1;
                } catch (...) {}
            }

            auto target_user = db->get_user(target_id);
            if (!target_user) {
                bronx::send_message(bot, event, bronx::error("user not found"));
                return;
            }

            // Get gambling statistics
            auto stats = get_gambling_stats(db, target_id);
            bool audit_passed = audit_gambling_transactions(db, target_id);

            // Get recent transactions
            auto history = get_gambling_history(db, target_id, limit, 0);

            std::string report = "**Gambling Audit Report**\n";
            report += "━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
            report += "**User:** <@" + std::to_string(target_id) + ">\n";
            report += "**Status:** " + std::string(audit_passed ? "✅ PASSED" : "❌ FAILED") + "\n\n";

            report += "**Statistics:**\n";
            report += "├ Total Transactions: " + std::to_string(stats.total_transactions) + "\n";
            report += "├ Verified: " + std::to_string(stats.verified_transactions) + "\n";
            report += "├ Unverified: " + std::to_string(stats.unverified_transactions) + "\n";
            report += "├ Win Rate: " + std::to_string(static_cast<int>(stats.win_rate * 100)) + "%\n";
            report += "├ Total Wagered: $" + format_number(stats.total_wagered) + "\n";
            report += "└ Total Winnings: $" + format_number(stats.total_winnings) + "\n\n";

            if (!audit_passed) {
                report += "⚠️ **AUDIT FAILED** - inconsistencies detected\n\n";
            }

            report += "**Recent Transactions:**\n";
            int count = 0;
            for (const auto& entry : history) {
                if (count >= 15) break;
                report += "• " + entry.description + "\n";
                report += "  → Balance: " + format_number(entry.balance_after) + "\n";
                count++;
            }

            auto embed = bronx::create_embed(report);
            embed.set_title("Gambling Audit Report");
            embed.set_color(audit_passed ? 0x00FF00 : 0xFF6B00);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },

        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Owner check
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user || !user->dev) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }

            auto user_id_param = event.get_parameter("user_id");
            uint64_t target_id = 0;

            if (std::holds_alternative<std::string>(user_id_param)) {
                try {
                    target_id = std::stoull(std::get<std::string>(user_id_param));
                } catch (...) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid user id")));
                    return;
                }
            } else if (std::holds_alternative<int64_t>(user_id_param)) {
                target_id = static_cast<uint64_t>(std::get<int64_t>(user_id_param));
            }

            if (target_id == 0) {
                event.reply(dpp::message().add_embed(bronx::error("user id required")));
                return;
            }

            int limit = 50;
            auto limit_param = event.get_parameter("limit");
            if (std::holds_alternative<int64_t>(limit_param)) {
                limit = std::get<int64_t>(limit_param);
                if (limit > 1000) limit = 1000;
                if (limit < 1) limit = 1;
            }

            auto target_user = db->get_user(target_id);
            if (!target_user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }

            // Get gambling statistics
            auto stats = get_gambling_stats(db, target_id);
            bool audit_passed = audit_gambling_transactions(db, target_id);

            // Get recent transactions
            auto history = get_gambling_history(db, target_id, limit, 0);

            std::string report = "**Gambling Audit Report**\n";
            report += "━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
            report += "**User:** <@" + std::to_string(target_id) + ">\n";
            report += "**Status:** " + std::string(audit_passed ? "✅ PASSED" : "❌ FAILED") + "\n\n";

            report += "**Statistics:**\n";
            report += "├ Total Transactions: " + std::to_string(stats.total_transactions) + "\n";
            report += "├ Verified: " + std::to_string(stats.verified_transactions) + "\n";
            report += "├ Unverified: " + std::to_string(stats.unverified_transactions) + "\n";
            report += "├ Win Rate: " + std::to_string(static_cast<int>(stats.win_rate * 100)) + "%\n";
            report += "├ Total Wagered: $" + format_number(stats.total_wagered) + "\n";
            report += "└ Total Winnings: $" + format_number(stats.total_winnings) + "\n\n";

            if (!audit_passed) {
                report += "⚠️ **AUDIT FAILED** - inconsistencies detected\n\n";
            }

            report += "**Recent Transactions:**\n";
            int count = 0;
            for (const auto& entry : history) {
                if (count >= 15) break;
                report += "• " + entry.description + "\n";
                report += "  → Balance: " + format_number(entry.balance_after) + "\n";
                count++;
            }

            auto embed = bronx::create_embed(report);
            embed.set_title("Gambling Audit Report");
            embed.set_color(audit_passed ? 0x00FF00 : 0xFF6B00);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },

        // Slash command options
        {
            dpp::command_option(dpp::co_string, "user_id", "user id to audit", true),
            dpp::command_option(dpp::co_integer, "limit", "max transactions to show (default: 50, max: 1000)", false),
        }
    );

    return audit;
}

} // namespace owner
} // namespace commands
