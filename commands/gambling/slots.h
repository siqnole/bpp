#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../../database/operations/economy/gambling_verification.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

namespace commands {
namespace gambling {

inline Command* get_slots_command(Database* db) {
    static Command* slots = new Command("slots", "spin the slot machine", "gambling", {"slot"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "slots", 3)) {
                ::bronx::send_message(bot, event, ::bronx::error("slow down! wait a few seconds between spins"));
                return;
            }
            
            // Check if gambling is allowed in this server
            std::optional<uint64_t> guild_id;
            if (event.msg.guild_id) guild_id = event.msg.guild_id;
            if (!::commands::gambling::is_gambling_allowed(db, guild_id)) {
                ::bronx::send_message(bot, event, ::bronx::error("gambling is disabled in this server's economy"));
                return;
            }
            
            if (args.empty()) {
                ::bronx::send_message(bot, event, ::bronx::error("specify an amount to bet"));
                return;
            }
            
            int64_t wallet = ::commands::gambling::get_gambling_wallet(db, event.msg.author.id, guild_id);
            
            int64_t bet;
            try {
                bet = parse_amount(args[0], wallet);
            } catch (const std::exception& e) {
                ::bronx::send_message(bot, event, ::bronx::error("invalid bet amount"));
                return;
            }
            
            if (bet < 100) {
                ::bronx::send_message(bot, event, ::bronx::error("minimum bet is $100"));
                return;
            }
            
            if (bet > ::commands::gambling::MAX_BET) {
                ::bronx::send_message(bot, event, ::bronx::error("maximum bet is $2,000,000,000"));
                return;
            }
            
            if (bet > wallet) {
                ::bronx::send_message(bot, event, ::bronx::error("you don't have that much"));
                return;
            }
            
            // Slot symbols with weights
            ::std::vector<::std::string> symbols = {"🍒", "🍋", "🍊", "🍇", "💎", "7️⃣"};
            ::std::vector<int> weights = {35, 30, 20, 10, 4, 1};
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::discrete_distribution<> dis(weights.begin(), weights.end());
            
            ::std::string slot1 = symbols[dis(gen)];
            ::std::string slot2 = symbols[dis(gen)];
            ::std::string slot3 = symbols[dis(gen)];
            
            // Calculate winnings
            int64_t winnings = 0;
            ::std::string result_text;
            
            if (slot1 == slot2 && slot2 == slot3) {
                // All three match
                if (slot1 == "7️⃣") {
                    winnings = bet * 50;
                    result_text = "**JACKPOT!** 🎰✨";
                } else if (slot1 == "💎") {
                    winnings = bet * 20;
                    result_text = "**TRIPLE DIAMONDS!** 💎💎💎";
                } else if (slot1 == "🍇") {
                    winnings = bet * 10;
                    result_text = "**TRIPLE GRAPES!** 🍇🍇🍇";
                } else if (slot1 == "🍊") {
                    winnings = bet * 5;
                    result_text = "**TRIPLE ORANGES!** 🍊🍊🍊";
                } else if (slot1 == "🍋") {
                    winnings = bet * 2;
                    result_text = "**TRIPLE LEMONS!** 🍋🍋🍋";
                } else {
                    winnings = static_cast<int64_t>(bet * 1.5);
                    result_text = "**TRIPLE CHERRIES!** 🍒🍒🍒";
                }
            } else if (slot1 == slot2 || slot2 == slot3 || slot1 == slot3) {
                // Two match - break even (return bet)
                winnings = 0;
                result_text = "two match! you get your bet back";
            } else {
                // No match
                winnings = -bet;
                result_text = "no matches...";
            }
            
            // Show spinning animation first
            ::std::string spin_desc = "🎰 **SPINNING...**\n\n";
            spin_desc += "┌─────────────┐\n";
            spin_desc += "│ ❓ │ ❓ │ ❓ │\n";
            spin_desc += "└─────────────┘\n\n";
            spin_desc += "bet: $" + format_number(bet);
            
            auto spin_embed = ::bronx::create_embed(spin_desc);
            ::bronx::add_invoker_footer(spin_embed, event.msg.author);
            
            bot.message_create(dpp::message(event.msg.channel_id, spin_embed), [&bot, event, slot1, slot2, slot3, winnings, result_text, bet, db](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) return;
                
                // Extract sent message before spawning thread (callback ref won't survive)
                auto sent_msg = ::std::get<dpp::message>(callback.value);
                
                // PERFORMANCE FIX: Run animation delay in a separate thread to avoid
                // blocking DPP's thread pool for 1.5s with sleep_for.
                std::thread([&bot, event, slot1, slot2, slot3, winnings, result_text, bet, db, sent_msg]() {
                // Wait a moment, then edit message with result
                ::std::this_thread::sleep_for(::std::chrono::milliseconds(1500));
                
                // Update balance using unified operations
                std::optional<uint64_t> guild_id_inner;
                if (event.msg.guild_id) guild_id_inner = event.msg.guild_id;
                
                // Apply gambling multiplier to winnings (not losses)
                int64_t actual_winnings = winnings;
                if (winnings > 0) {
                    double mult = ::commands::gambling::get_gambling_multiplier(db, guild_id_inner);
                    actual_winnings = static_cast<int64_t>(winnings * mult);
                }
                
                // VERIFIED GAMBLING TRANSACTION
                int64_t balance_before = ::commands::gambling::get_gambling_wallet(db, event.msg.author.id, guild_id_inner);
                std::string transaction_id = bronx::db::gambling_verification::create_gambling_transaction(
                    db, event.msg.author.id, "slots", bet, actual_winnings,
                    balance_before, "", "slot1:" + slot1 + ",slot2:" + slot2 + ",slot3:" + slot3
                );
                
                if (!bronx::db::gambling_verification::verify_gambling_transaction(db, event.msg.author.id, transaction_id, balance_before, balance_before + actual_winnings)) {
                    std::cerr << "[SLOTS] Verification failed for transaction " << transaction_id << std::endl;
                    return;
                }
                
                if (!::commands::gambling::update_gambling_wallet(db, event.msg.author.id, guild_id_inner, actual_winnings)) {
                    std::cerr << "[SLOTS] Failed to update wallet for transaction " << transaction_id << std::endl;
                    return;
                }
                
                bronx::db::gambling_verification::apply_verified_gambling_winnings(db, event.msg.author.id, transaction_id, actual_winnings, "slots");
                
                // Track gambling stats
                if (winnings > 0) {
                    db->increment_stat(event.msg.author.id, "gambling_profit", winnings);
                    // Check gambling profit achievements
                    ::commands::gambling::track_gambling_profit(bot, db, event.msg.channel_id, event.msg.author.id);
                } else if (winnings < 0) {
                    db->increment_stat(event.msg.author.id, "gambling_losses", -winnings);
                }
                
                // Track milestone
                ::commands::gambling::track_gambling_result(bot, db, event.msg.channel_id, event.msg.author.id, winnings > 0, winnings);
                
                // Log gambling result to history
                int64_t new_balance = ::commands::gambling::get_gambling_wallet(db, event.msg.author.id, guild_id_inner);
                std::string log_desc = actual_winnings > 0 ? "won slots for $" + format_number(actual_winnings) : "lost slots for $" + format_number(-actual_winnings);
                ::bronx::db::history_operations::log_gambling(db, event.msg.author.id, log_desc, actual_winnings, new_balance);
                
                ::std::string final_desc = "🎰 **SLOT MACHINE**\n\n";
                final_desc += "┌─────────────┐\n";
                final_desc += "│ " + slot1 + " │ " + slot2 + " │ " + slot3 + " │\n";
                final_desc += "└─────────────┘\n\n";
                final_desc += result_text;
                if (winnings > 0) {
                    final_desc += "\n\n**won:** $" + format_number(winnings);
                } else if (winnings < 0) {
                    final_desc += " lost $" + format_number(-winnings);
                }
                
                auto final_embed = (winnings > 0) ? ::bronx::success(final_desc) : ::bronx::error(final_desc);
                ::bronx::add_invoker_footer(final_embed, event.msg.author);
                
                dpp::message edit_msg(event.msg.channel_id, final_embed);
                edit_msg.id = sent_msg.id;
                ::bronx::safe_message_edit(bot, edit_msg);
                }).detach(); // end of animation thread
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "slots", 3)) {
                event.reply(dpp::message().add_embed(::bronx::error("slow down! wait a few seconds between spins")));
                return;
            }
            
            // Check if gambling is allowed in this server
            std::optional<uint64_t> guild_id;
            if (event.command.guild_id) guild_id = static_cast<uint64_t>(event.command.guild_id);
            if (!::commands::gambling::is_gambling_allowed(db, guild_id)) {
                event.reply(dpp::message().add_embed(::bronx::error("gambling is disabled in this server's economy")));
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
            
            int64_t wallet = ::commands::gambling::get_gambling_wallet(db, event.command.get_issuing_user().id, guild_id);
            
            int64_t bet;
            try {
                bet = parse_amount(amount_str, wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(::bronx::error("invalid bet amount")));
                return;
            }
            
            if (bet < 100) {
                event.reply(dpp::message().add_embed(::bronx::error("minimum bet is $100")));
                return;
            }
            
            if (bet > ::commands::gambling::MAX_BET) {
                event.reply(dpp::message().add_embed(::bronx::error("maximum bet is $2,000,000,000")));
                return;
            }
            
            if (bet > wallet) {
                event.reply(dpp::message().add_embed(::bronx::error("you don't have that much")));
                return;
            }
            
            // Slot symbols with weights
            ::std::vector<::std::string> symbols = {"🍒", "🍋", "🍊", "🍇", "💎", "7️⃣"};
            ::std::vector<int> weights = {35, 30, 20, 10, 4, 1};
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::discrete_distribution<> dis(weights.begin(), weights.end());
            
            ::std::string slot1 = symbols[dis(gen)];
            ::std::string slot2 = symbols[dis(gen)];
            ::std::string slot3 = symbols[dis(gen)];
            
            // Calculate winnings
            int64_t winnings = 0;
            ::std::string result_text;
            
            if (slot1 == slot2 && slot2 == slot3) {
                if (slot1 == "7️⃣") {
                    winnings = bet * 50;
                    result_text = "**JACKPOT!** 🎰✨";
                } else if (slot1 == "💎") {
                    winnings = bet * 20;
                    result_text = "**TRIPLE DIAMONDS!** 💎💎💎";
                } else if (slot1 == "🍇") {
                    winnings = bet * 10;
                    result_text = "**TRIPLE GRAPES!** 🍇🍇🍇";
                } else if (slot1 == "🍊") {
                    winnings = bet * 5;
                    result_text = "**TRIPLE ORANGES!** 🍊🍊🍊";
                } else if (slot1 == "🍋") {
                    winnings = bet * 2;
                    result_text = "**TRIPLE LEMONS!** 🍋🍋🍋";
                } else {
                    winnings = static_cast<int64_t>(bet * 1.5);
                    result_text = "**TRIPLE CHERRIES!** 🍒🍒🍒";
                }
            } else if (slot1 == slot2 || slot2 == slot3 || slot1 == slot3) {
                winnings = 0;
                result_text = "two match! you get your bet back";
            } else {
                winnings = -bet;
                result_text = "no matches...";
            }
            
            // Show spinning animation
            ::std::string spin_desc = "🎰 **SPINNING...**\n\n";
            spin_desc += "┌─────────────┐\n";
            spin_desc += "│ ❓ │ ❓ │ ❓ │\n";
            spin_desc += "└─────────────┘\n\n";
            spin_desc += "bet: $" + format_number(bet);
            
            auto spin_embed = ::bronx::create_embed(spin_desc);
            ::bronx::add_invoker_footer(spin_embed, event.command.get_issuing_user());
            
            event.thinking(false, [&bot, event, slot1, slot2, slot3, winnings, result_text, bet, db](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) return;
                
                // PERFORMANCE FIX: Run animation delay in a separate thread to avoid
                // blocking DPP's thread pool for 1.5s with sleep_for.
                std::thread([&bot, event, slot1, slot2, slot3, winnings, result_text, bet, db]() {
                ::std::this_thread::sleep_for(::std::chrono::milliseconds(1500));
                
                // Track gambling stats
                uint64_t uid = event.command.get_issuing_user().id;
                
                // Update balance using unified operations
                std::optional<uint64_t> guild_id_inner;
                if (event.command.guild_id) guild_id_inner = static_cast<uint64_t>(event.command.guild_id);
                
                int64_t actual_winnings = winnings;
                if (winnings > 0) {
                    double mult = ::commands::gambling::get_gambling_multiplier(db, guild_id_inner);
                    actual_winnings = static_cast<int64_t>(winnings * mult);
                }
                
                // VERIFIED GAMBLING TRANSACTION
                int64_t balance_before = ::commands::gambling::get_gambling_wallet(db, uid, guild_id_inner);
                std::string transaction_id = bronx::db::gambling_verification::create_gambling_transaction(
                    db, uid, "slots", bet, actual_winnings,
                    balance_before, "", "slot1:" + slot1 + ",slot2:" + slot2 + ",slot3:" + slot3
                );
                
                if (!bronx::db::gambling_verification::verify_gambling_transaction(db, uid, transaction_id, balance_before, balance_before + actual_winnings)) {
                    std::cerr << "[SLOTS SLASH] Verification failed for transaction " << transaction_id << std::endl;
                    return;
                }
                
                if (!::commands::gambling::update_gambling_wallet(db, uid, guild_id_inner, actual_winnings)) {
                    std::cerr << "[SLOTS SLASH] Failed to update wallet for transaction " << transaction_id << std::endl;
                    return;
                }
                
                bronx::db::gambling_verification::apply_verified_gambling_winnings(db, uid, transaction_id, actual_winnings, "slots");
                
                if (actual_winnings > 0) {
                    db->increment_stat(uid, "gambling_profit", actual_winnings);
                    // Check gambling profit achievements
                    ::commands::gambling::track_gambling_profit(const_cast<dpp::cluster&>(bot), db, event.command.channel_id, uid);
                } else if (actual_winnings < 0) {
                    db->increment_stat(uid, "gambling_losses", -actual_winnings);
                }
                
                // Track milestone
                ::commands::gambling::track_gambling_result(const_cast<dpp::cluster&>(bot), db, event.command.channel_id, uid, actual_winnings > 0, actual_winnings);
                
                // Log gambling result to history
                int64_t new_balance = ::commands::gambling::get_gambling_wallet(db, uid, guild_id_inner);
                std::string log_desc = actual_winnings > 0 ? "won slots for $" + format_number(actual_winnings) : "lost slots for $" + format_number(-actual_winnings);
                ::bronx::db::history_operations::log_gambling(db, uid, log_desc, actual_winnings, new_balance);
                
                ::std::string final_desc = "🎰 **SLOT MACHINE**\n\n";
                final_desc += "┌─────────────┐\n";
                final_desc += "│ " + slot1 + " │ " + slot2 + " │ " + slot3 + " │\n";
                final_desc += "└─────────────┘\n\n";
                final_desc += result_text;
                if (actual_winnings > 0) {
                    final_desc += "\n\n**won:** $" + format_number(actual_winnings);
                } else if (actual_winnings < 0) {
                    final_desc += " lost $" + format_number(-actual_winnings);
                }
                
                auto final_embed = (actual_winnings > 0) ? ::bronx::success(final_desc) : ::bronx::error(final_desc);
                ::bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                
                event.edit_response(dpp::message().add_embed(final_embed));
                }).detach(); // end of animation thread
            });
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to bet (supports all, half, 50%, 1k, etc)", true)
        });
    
    return slots;
}

} // namespace gambling
} // namespace commands
