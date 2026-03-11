#pragma once
#include "helpers.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../daily_challenges/daily_stat_tracker.h"

using namespace bronx::db::server_economy_operations;

namespace commands {
namespace economy {

inline Command* create_pay_command(Database* db) {
    static Command* pay = new Command("pay", "transfer money to another user", "economy", {"give"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("please mention a user to pay"));
                return;
            }
            
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("please specify an amount"));
                return;
            }
            
            uint64_t recipient_id = event.msg.mentions.begin()->first.id;
            
            if (recipient_id == event.msg.author.id) {
                bronx::send_message(bot, event, bronx::error("you can't pay yourself"));
                return;
            }
            
            if (event.msg.mentions.begin()->first.is_bot()) {
                bronx::send_message(bot, event, bronx::error("you can't pay bots"));
                return;
            }
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            // Determine economy mode
            std::optional<uint64_t> guild_id;
            if (event.msg.guild_id) guild_id = event.msg.guild_id;
            bool server_mode = guild_id && is_server_economy(db, *guild_id);
            
            int64_t sender_wallet = server_mode ? get_server_wallet(db, *guild_id, event.msg.author.id) : user->wallet;
            
            int64_t amount;
            try {
                amount = parse_amount(args[1], sender_wallet);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("invalid amount"));
                return;
            }
            
            if (amount <= 0) {
                bronx::send_message(bot, event, bronx::error("amount must be positive"));
                return;
            }
            
            if (amount > sender_wallet) {
                bronx::send_message(bot, event, bronx::error("you don't have that much"));
                return;
            }
            
            // Block transfers between prestiged and non-prestiged users (global only)
            if (!server_mode) {
                int sender_prestige = db->get_prestige(event.msg.author.id);
                int recipient_prestige = db->get_prestige(recipient_id);
                if ((sender_prestige == 0) != (recipient_prestige == 0)) {
                    bronx::send_message(bot, event, bronx::error("you can't transfer money between prestiged and non-prestiged players"));
                    return;
                }
            }
            
            // Check if payment would 100x recipient's balance
            int64_t recipient_current = get_wallet_unified(db, recipient_id, guild_id);
            if (recipient_current > 0 && amount > recipient_current * 99) {
                bronx::send_message(bot, event, bronx::error("that payment would 100x their balance. try a smaller amount"));
                return;
            }
            
            // Tax: use server tax settings if in server economy, else 5% default
            double tax_rate = 5.0;
            if (server_mode) {
                auto settings = get_guild_economy_settings(db, *guild_id);
                if (settings && settings->enable_tax) {
                    tax_rate = settings->transaction_tax_percent;
                } else {
                    tax_rate = 0.0;
                }
            }
            int64_t tax = static_cast<int64_t>(amount * (tax_rate / 100.0));
            if (tax_rate > 0 && tax < 1) tax = 1;
            int64_t received = amount - tax;
            
            // Split tax: half destroyed, half goes to guild giveaway balance
            int64_t tax_to_guild = tax / 2;
            int64_t tax_destroyed = tax - tax_to_guild;
            
            // Perform transfer using unified operations
            auto sender_result = update_wallet_unified(db, event.msg.author.id, guild_id, -amount);
            if (!sender_result) {
                bronx::send_message(bot, event, bronx::error("failed to transfer money"));
                return;
            }
            auto recipient_result = update_wallet_unified(db, recipient_id, guild_id, received);
            if (recipient_result) {
                // Add half of tax to guild giveaway balance
                if (tax_to_guild > 0 && guild_id) {
                    add_to_guild_balance(db, *guild_id, tax_to_guild);
                }
                
                // Log payment to history
                int64_t sender_balance = get_wallet_unified(db, event.msg.author.id, guild_id);
                int64_t recipient_balance = get_wallet_unified(db, recipient_id, guild_id);
                std::string tax_str = tax > 0 ? (" (tax: $" + format_number(tax) + ")") : "";
                std::string sender_log = "paid <@" + std::to_string(recipient_id) + "> $" + format_number(amount) + tax_str;
                std::string recipient_log = "received $" + format_number(received) + " from <@" + std::to_string(event.msg.author.id) + ">" + (tax > 0 ? " (after " + std::to_string((int)tax_rate) + "% tax)" : "");
                bronx::db::history_operations::log_payment(db, event.msg.author.id, sender_log, -amount, sender_balance);
                bronx::db::history_operations::log_payment(db, recipient_id, recipient_log, received, recipient_balance);
                
                // Track daily challenge stat
                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "coins_paid_today", amount);
                
                std::string tax_display = "";
                if (tax > 0) {
                    tax_display = "\n💸 " + std::to_string((int)tax_rate) + "% tax: $" + format_number(tax);
                    tax_display += "\n🗑️ $" + format_number(tax_destroyed) + " destroyed";
                    if (tax_to_guild > 0) {
                        tax_display += "\n🏦 $" + format_number(tax_to_guild) + " added to server giveaway balance";
                    }
                }
                auto embed = bronx::success("transferred $" + format_number(received) + 
                    " to " + event.msg.mentions.begin()->first.format_username() + tax_display);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to transfer money"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto user_param = event.get_parameter("user");
            if (!std::holds_alternative<dpp::snowflake>(user_param)) {
                event.reply(dpp::message().add_embed(bronx::error("please mention a user")));
                return;
            }
            uint64_t recipient_id = std::get<dpp::snowflake>(user_param);
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
            
            if (recipient_id == event.command.get_issuing_user().id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't pay yourself")));
                return;
            }
            
            auto recipient = event.command.get_resolved_user(recipient_id);
            if (recipient.is_bot()) {
                event.reply(dpp::message().add_embed(bronx::error("you can't pay bots")));
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
            
            int64_t sender_wallet = server_mode ? get_server_wallet(db, *guild_id, event.command.get_issuing_user().id) : user->wallet;
            
            int64_t amount;
            try {
                amount = parse_amount(amount_str, sender_wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(bronx::error("invalid amount")));
                return;
            }
            
            if (amount <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be positive")));
                return;
            }
            
            if (amount > sender_wallet) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that much")));
                return;
            }
            
            // Block transfers between prestiged and non-prestiged users (global only)
            if (!server_mode) {
                int sender_prestige = db->get_prestige(event.command.get_issuing_user().id);
                int recipient_prestige = db->get_prestige(recipient_id);
                if ((sender_prestige == 0) != (recipient_prestige == 0)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't transfer money between prestiged and non-prestiged players")));
                    return;
                }
            }
            
            // Check if payment would 100x recipient's balance
            int64_t recipient_current = get_wallet_unified(db, recipient_id, guild_id);
            if (recipient_current > 0 && amount > recipient_current * 99) {
                event.reply(dpp::message().add_embed(bronx::error("that payment would 100x their balance. try a smaller amount")));
                return;
            }
            
            // Tax: use server tax settings if in server economy, else 5% default
            double tax_rate = 5.0;
            if (server_mode) {
                auto settings = get_guild_economy_settings(db, *guild_id);
                if (settings && settings->enable_tax) {
                    tax_rate = settings->transaction_tax_percent;
                } else {
                    tax_rate = 0.0;
                }
            }
            int64_t tax = static_cast<int64_t>(amount * (tax_rate / 100.0));
            if (tax_rate > 0 && tax < 1) tax = 1;
            int64_t received = amount - tax;
            
            // Split tax: half destroyed, half goes to guild giveaway balance
            int64_t tax_to_guild = tax / 2;
            int64_t tax_destroyed = tax - tax_to_guild;
            
            // Perform transfer using unified operations
            uint64_t sender_id = event.command.get_issuing_user().id;
            auto sender_result = update_wallet_unified(db, sender_id, guild_id, -amount);
            if (!sender_result) {
                event.reply(dpp::message().add_embed(bronx::error("failed to transfer money")));
                return;
            }
            auto recipient_result = update_wallet_unified(db, recipient_id, guild_id, received);
            if (recipient_result) {
                // Add half of tax to guild giveaway balance
                if (tax_to_guild > 0 && guild_id) {
                    add_to_guild_balance(db, *guild_id, tax_to_guild);
                }
                
                int64_t sender_balance = get_wallet_unified(db, sender_id, guild_id);
                int64_t recipient_balance = get_wallet_unified(db, recipient_id, guild_id);
                std::string tax_str = tax > 0 ? (" (tax: $" + format_number(tax) + ")") : "";
                std::string sender_log = "paid <@" + std::to_string(recipient_id) + "> $" + format_number(amount) + tax_str;
                std::string recipient_log = "received $" + format_number(received) + " from <@" + std::to_string(sender_id) + ">" + (tax > 0 ? " (after " + std::to_string((int)tax_rate) + "% tax)" : "");
                bronx::db::history_operations::log_payment(db, sender_id, sender_log, -amount, sender_balance);
                bronx::db::history_operations::log_payment(db, recipient_id, recipient_log, received, recipient_balance);
                
                // Track daily challenge stat
                ::commands::daily_challenges::track_daily_stat(db, sender_id, "coins_paid_today", amount);
                
                std::string tax_display = "";
                if (tax > 0) {
                    tax_display = "\n💸 " + std::to_string((int)tax_rate) + "% tax: $" + format_number(tax);
                    tax_display += "\n🗑️ $" + format_number(tax_destroyed) + " destroyed";
                    if (tax_to_guild > 0) {
                        tax_display += "\n🏦 $" + format_number(tax_to_guild) + " added to server giveaway balance";
                    }
                }
                auto embed = bronx::success("transferred $" + format_number(received) + 
                    " to " + recipient.format_username() + tax_display);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to transfer money")));
            }
        },
        {
            dpp::command_option(dpp::co_user, "user", "user to pay", true),
            dpp::command_option(dpp::co_string, "amount", "amount to pay (supports all, half, 50%, 1k, etc)", true)
        });
    return pay;
}

} // namespace economy
} // namespace commands
