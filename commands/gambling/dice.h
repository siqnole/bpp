#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../../database/operations/economy/gambling_verification.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>

namespace commands {
namespace gambling {

using namespace bronx::db::gambling_verification;

inline Command* get_dice_command(Database* db) {
    static Command* dice = new Command("dice", "roll two dice and bet on the outcome", "gambling", {"roll"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "dice", 3)) {
                ::bronx::send_message(bot, event, ::bronx::error("slow down! wait a few seconds between rolls"));
                return;
            }
            
            if (args.empty()) {
                ::bronx::send_message(bot, event, ::bronx::error("usage: dice <amount>"));
                return;
            }
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            int64_t bet;
            try {
                bet = parse_amount(args[0], user->wallet);
            } catch (const std::exception& e) {
                ::bronx::send_message(bot, event, ::bronx::error("invalid bet amount"));
                return;
            }
            
            if (bet < 50) {
                ::bronx::send_message(bot, event, ::bronx::error("minimum bet is $50"));
                return;
            }
            
            if (bet > ::commands::gambling::MAX_BET) {
                ::bronx::send_message(bot, event, ::bronx::error("maximum bet is $2,000,000,000"));
                return;
            }
            
            if (bet > user->wallet) {
                ::bronx::send_message(bot, event, ::bronx::error("you don't have that much"));
                return;
            }
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> dis(1, 6);
            
            int die1 = dis(gen);
            int die2 = dis(gen);
            int total = die1 + die2;
            
            // Payouts: snake eyes/boxcars = 4x, 7 or 11 = 1.5x, other doubles = 2x
            // EV ≈ -5.6% house edge
            int64_t winnings = 0;
            ::std::string result_text;
            
            if (total == 2 || total == 12) {
                // Snake eyes / boxcars (must check before doubles since these are also doubles)
                winnings = bet * 4;
                result_text = "**SNAKE EYES/BOXCARS!** 🐍";
            } else if (total == 7 || total == 11) {
                winnings = static_cast<int64_t>(bet * 1.5);
                result_text = "**LUCKY " + ::std::to_string(total) + "!** 🍀";
            } else if (die1 == die2) {
                winnings = bet * 2;
                result_text = "**DOUBLES!** 🎲🎲";
            } else {
                winnings = -bet;
                result_text = "no special combination...";
            }
            
            // VERIFIED GAMBLING TRANSACTION
            int64_t balance_before = db->get_wallet(event.msg.author.id);
            std::string transaction_id = bronx::db::gambling_verification::create_gambling_transaction(
                db, event.msg.author.id, "dice", bet, winnings,
                balance_before, "", "die1:" + std::to_string(die1) + ",die2:" + std::to_string(die2) + ",total:" + std::to_string(total)
            );
            
            if (!bronx::db::gambling_verification::verify_gambling_transaction(db, event.msg.author.id, transaction_id, balance_before, balance_before + winnings)) {
                ::bronx::send_message(bot, event, ::bronx::error("gambling verification failed - please try again"));
                return;
            }
            
            if (!bronx::db::gambling_verification::apply_verified_gambling_winnings(db, event.msg.author.id, transaction_id, winnings, "dice")) {
                ::bronx::send_message(bot, event, ::bronx::error("failed to apply gambling winnings - contact support"));
                return;
            }
            
            // Track gambling stats
            if (winnings > 0) {
                db->increment_stat(event.msg.author.id, "gambling_profit", winnings);
                // Check gambling profit achievements
                ::commands::gambling::track_gambling_profit(bot, db, event.msg.channel_id, event.msg.author.id);
            } else {
                db->increment_stat(event.msg.author.id, "gambling_losses", -winnings);
            }
            
            // Track milestone
            ::commands::gambling::track_gambling_result(bot, db, event.msg.channel_id, event.msg.author.id, winnings > 0, winnings);
            
            // Log gambling result to history
            int64_t new_balance = db->get_wallet(event.msg.author.id);
            std::string log_desc = winnings > 0 ? "won dice for $" + format_number(winnings) : "lost dice for $" + format_number(-winnings);
            ::bronx::db::history_operations::log_gambling(db, event.msg.author.id, log_desc, winnings, new_balance);
            
            ::std::string description = "🎲 **DICE ROLL**\n\n";
            description += "you rolled: **" + ::std::to_string(die1) + "** + **" + ::std::to_string(die2) + "** = **" + ::std::to_string(total) + "**\n\n";
            description += result_text;
            
            if (winnings > 0) {
                description += "\n\n**won:** $" + format_number(winnings);
            } else {
                description += " lost $" + format_number(-winnings);
            }
            
            auto embed = (winnings > 0) ? ::bronx::success(description) : ::bronx::error(description);
            ::bronx::add_invoker_footer(embed, event.msg.author);
            ::bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "dice", 3)) {
                event.reply(dpp::message().add_embed(::bronx::error("slow down! wait a few seconds between rolls")));
                return;
            }
            
            auto amount_param = event.get_parameter("amount");
            ::std::string amount_str;
            if (std::holds_alternative<std::string>(amount_param)) {
                amount_str = std::get<std::string>(amount_param);
            } else if (std::holds_alternative<int64_t>(amount_param)) {
                amount_str = std::to_string(std::get<int64_t>(amount_param));
            } else {
                event.reply(dpp::message().add_embed(::bronx::error("please provide a bet amount")));
                return;
            }
            
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(::bronx::error("user not found")));
                return;
            }
            
            int64_t bet;
            try {
                bet = parse_amount(amount_str, user->wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(::bronx::error("invalid bet amount")));
                return;
            }
            
            if (bet < 50) {
                event.reply(dpp::message().add_embed(::bronx::error("minimum bet is $50")));
                return;
            }
            
            if (bet > ::commands::gambling::MAX_BET) {
                event.reply(dpp::message().add_embed(::bronx::error("maximum bet is $2,000,000,000")));
                return;
            }
            
            if (bet > user->wallet) {
                event.reply(dpp::message().add_embed(::bronx::error("you don't have that much")));
                return;
            }
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> dis(1, 6);
            
            int die1 = dis(gen);
            int die2 = dis(gen);
            int total = die1 + die2;
            
            // Payouts: snake eyes/boxcars = 4x, 7 or 11 = 1.5x, other doubles = 2x
            int64_t winnings = 0;
            ::std::string result_text;
            
            if (total == 2 || total == 12) {
                winnings = bet * 4;
                result_text = "**SNAKE EYES/BOXCARS!** 🐍";
            } else if (total == 7 || total == 11) {
                winnings = static_cast<int64_t>(bet * 1.5);
                result_text = "**LUCKY " + ::std::to_string(total) + "!** 🍀";
            } else if (die1 == die2) {
                winnings = bet * 2;
                result_text = "**DOUBLES** 🎲🎲";
            } else {
                winnings = -bet;
                result_text = "no special combination...";
            }
            
            // VERIFIED GAMBLING TRANSACTION
            uint64_t uid = event.command.get_issuing_user().id;
            int64_t balance_before = db->get_wallet(uid);
            std::string transaction_id = bronx::db::gambling_verification::create_gambling_transaction(
                db, uid, "dice", bet, winnings,
                balance_before, "", "die1:" + std::to_string(die1) + ",die2:" + std::to_string(die2) + ",total:" + std::to_string(total)
            );
            
            if (!bronx::db::gambling_verification::verify_gambling_transaction(db, uid, transaction_id, balance_before, balance_before + winnings)) {
                event.reply(dpp::message().add_embed(::bronx::error("gambling verification failed - please try again")));
                return;
            }
            
            if (!bronx::db::gambling_verification::apply_verified_gambling_winnings(db, uid, transaction_id, winnings, "dice")) {
                event.reply(dpp::message().add_embed(::bronx::error("failed to apply gambling winnings - contact support")));
                return;
            }
            
            // Track gambling stats
            if (winnings > 0) {
                db->increment_stat(uid, "gambling_profit", winnings);
                // Check gambling profit achievements
                ::commands::gambling::track_gambling_profit(bot, db, event.command.channel_id, uid);
            } else {
                db->increment_stat(uid, "gambling_losses", -winnings);
            }
            
            // Track milestone
            ::commands::gambling::track_gambling_result(bot, db, event.command.channel_id, uid, winnings > 0, winnings);
            
            // Log gambling result to history
            int64_t new_balance = db->get_wallet(uid);
            std::string log_desc = winnings > 0 ? "won dice for $" + format_number(winnings) : "lost dice for $" + format_number(-winnings);
            ::bronx::db::history_operations::log_gambling(db, uid, log_desc, winnings, new_balance);
            
            ::std::string description = "🎲 **DICE ROLL**\n\n";
            description += "you rolled: **" + ::std::to_string(die1) + "** + **" + ::std::to_string(die2) + "** = **" + ::std::to_string(total) + "**\n\n";
            description += result_text;
            
            if (winnings > 0) {
                description += "\n\n**won:** $" + format_number(winnings);
            } else {
                description += " lost $" + format_number(-winnings);
            }
            
            auto embed = (winnings > 0) ? ::bronx::success(description) : ::bronx::error(description);
            ::bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to bet", true)
        });
    
    return dice;
}

} // namespace gambling
} // namespace commands
