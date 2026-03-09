#pragma once
#include "helpers.h"
#include "../../database/operations/economy/server_economy_operations.h"

using namespace bronx::db::server_economy_operations;

namespace commands {
namespace economy {

inline Command* create_withdraw_command(Database* db) {
    static Command* withdraw = new Command("withdraw", "withdraw money from your bank", "economy", {"w", "with"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("please specify an amount to withdraw"));
                return;
            }
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            // Determine economy mode
            std::optional<uint64_t> guild_id;
            if (event.msg.guild_id) guild_id = event.msg.guild_id;
            bool server_mode = guild_id && is_server_economy(db, *guild_id);
            
            int64_t bank_balance = server_mode ? get_server_bank(db, *guild_id, event.msg.author.id) : user->bank;
            
            int64_t amount;
            try {
                amount = parse_amount(args[0], bank_balance);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("invalid amount"));
                return;
            }
            
            if (amount <= 0) {
                bronx::send_message(bot, event, bronx::error("amount must be positive"));
                return;
            }

            if (amount > bank_balance) {
                bronx::send_message(bot, event, bronx::error("you don't have that much in your bank"));
                return;
            }
            
            // Withdraw: subtract from bank, add to wallet
            bool success = false;
            if (server_mode) {
                auto bank_result = update_server_bank(db, *guild_id, event.msg.author.id, -amount);
                if (bank_result) {
                    auto wallet_result = update_server_wallet(db, *guild_id, event.msg.author.id, amount);
                    success = wallet_result.has_value();
                }
            } else {
                success = db->withdraw(event.msg.author.id, amount);
            }
            
            if (success) {
                log_balance_change(db, event.msg.author.id, "withdrew $" + format_number(amount) + " from bank");
                auto embed = bronx::success("withdrew $" + format_number(amount) + " from your bank");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to withdraw"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto amount_param = event.get_parameter("amount");
            ::std::string amount_str;
            if (std::holds_alternative<std::string>(amount_param)) {
                amount_str = std::get<std::string>(amount_param);
            } else if (std::holds_alternative<int64_t>(amount_param)) {
                amount_str = std::to_string(std::get<int64_t>(amount_param));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide an amount")));
                return;
            }
            
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }
            
            // Determine economy mode
            std::optional<uint64_t> guild_id;
            if (event.command.guild_id) guild_id = static_cast<uint64_t>(event.command.guild_id);
            bool server_mode = guild_id && is_server_economy(db, *guild_id);
            
            int64_t bank_balance = server_mode ? get_server_bank(db, *guild_id, event.command.get_issuing_user().id) : user->bank;
            
            int64_t amount;
            try {
                amount = parse_amount(amount_str, bank_balance);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(bronx::error("invalid amount")));
                return;
            }
            
            if (amount <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be positive")));
                return;
            }
            
            if (amount > bank_balance) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that much in your bank")));
                return;
            }
            
            // Withdraw: subtract from bank, add to wallet
            bool success = false;
            if (server_mode) {
                auto bank_result = update_server_bank(db, *guild_id, event.command.get_issuing_user().id, -amount);
                if (bank_result) {
                    auto wallet_result = update_server_wallet(db, *guild_id, event.command.get_issuing_user().id, amount);
                    success = wallet_result.has_value();
                }
            } else {
                success = db->withdraw(event.command.get_issuing_user().id, amount);
            }
            
            if (success) {
                log_balance_change(db, event.command.get_issuing_user().id, "withdrew $" + format_number(amount) + " from bank");
                auto embed = bronx::success("withdrew $" + format_number(amount) + " from your bank");
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to withdraw")));
            }
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to withdraw (supports all, half, 50%, 1k, etc)", true)
        });
    return withdraw;
}

} // namespace economy
} // namespace commands
