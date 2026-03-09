#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/economy/server_economy_operations.h"
#include "../database/operations/moderation/permission_operations.h"
#include <dpp/dpp.h>

using namespace bronx::db;
using namespace bronx::db::server_economy_operations;

namespace commands {

/**
 * Example: Balance Command with Server Economy Support
 * 
 * This demonstrates how to update an existing economy command
 * to support both global and server economies.
 */
class BalanceCommand : public Command {
public:
    dpp::slashcommand get_command_info() const override {
        return dpp::slashcommand("balance", "Check your balance", bot->me.id)
            .add_option(
                dpp::command_option(dpp::co_user, "user", "Check another user's balance", false)
            );
    }

    void execute(const dpp::slashcommand_t& event) override {
        uint64_t user_id = event.command.usr.id;
        std::optional<uint64_t> guild_id = event.command.guild_id;
        
        // Check if a user was mentioned
        auto options = event.command.get_command_interaction().options;
        if (!options.empty() && options[0].name == "user") {
            user_id = std::get<dpp::snowflake>(options[0].value);
        }
        
        // Get balances using unified operations (auto-routes to correct economy)
        int64_t wallet = get_wallet_unified(db, user_id, guild_id);
        int64_t bank = get_bank_unified(db, user_id, guild_id);
        int64_t bank_limit = 0;
        int64_t networth = wallet + bank;
        
        // Get bank limit based on economy mode
        if (guild_id && is_server_economy(db, *guild_id)) {
            auto server_user = server_economy_operations::get_server_user(db, *guild_id, user_id);
            if (server_user) {
                bank_limit = server_user->bank_limit;
            }
        } else {
            bank_limit = db->get_bank_limit(user_id);
        }
        
        // Check economy mode for the embed
        std::string economy_type = "Global";
        if (guild_id && is_server_economy(db, *guild_id)) {
            economy_type = "Server";
        }
        
        // Format numbers with commas
        auto format_num = [](int64_t num) -> std::string {
            std::string str = std::to_string(num);
            int insert_pos = str.length() - 3;
            while (insert_pos > 0) {
                str.insert(insert_pos, ",");
                insert_pos -= 3;
            }
            return str;
        };
        
        // Create embed
        dpp::embed embed = dpp::embed()
            .set_color(EMBED_COLOR_INFO)
            .set_title("💰 Balance")
            .add_field(
                "Wallet",
                "💵 **" + format_num(wallet) + "**",
                true
            )
            .add_field(
                "Bank",
                "🏦 **" + format_num(bank) + "** / " + format_num(bank_limit),
                true
            )
            .add_field(
                "Net Worth",
                "💎 **" + format_num(networth) + "**",
                true
            )
            .set_footer(dpp::embed_footer()
                .set_text(economy_type + " Economy")
            )
            .set_timestamp(time(0));
        
        event.reply(dpp::message().add_embed(embed));
    }
};

/**
 * Example: Daily Command with Server Economy Support
 * 
 * Shows how to handle cooldowns and multipliers in server economy.
 */
class DailyCommand : public Command {
public:
    dpp::slashcommand get_command_info() const override {
        return dpp::slashcommand("daily", "Claim your daily reward", bot->me.id);
    }

    void execute(const dpp::slashcommand_t& event) override {
        uint64_t user_id = event.command.usr.id;
        std::optional<uint64_t> guild_id = event.command.guild_id;
        
        // Base reward
        int64_t base_reward = 1000;
        int cooldown_seconds = 86400; // 24 hours
        
        // Get settings if server economy
        if (guild_id) {
            auto settings = get_guild_economy_settings(db, *guild_id);
            if (settings && settings->economy_mode == "server") {
                // Use custom cooldown if set
                if (settings->daily_cooldown > 0) {
                    cooldown_seconds = settings->daily_cooldown;
                }
                // Note: You could add a daily_multiplier to settings if desired
            }
        }
        
        // Check cooldown (you'd implement this with server_cooldowns table for server economy)
        // For this example, we'll skip cooldown checking
        
        // Apply reward
        auto new_balance = update_wallet_unified(db, user_id, guild_id, base_reward);
        
        if (new_balance) {
            dpp::embed embed = dpp::embed()
                .set_color(EMBED_COLOR_SUCCESS)
                .set_title(bronx::EMOJI_CHECK + " Daily Reward Claimed!")
                .set_description("You received **" + std::to_string(base_reward) + "** coins!")
                .add_field(
                    "New Balance",
                    "💵 **" + std::to_string(*new_balance) + "**",
                    false
                )
                .set_footer(dpp::embed_footer()
                    .set_text("Come back in " + std::to_string(cooldown_seconds / 3600) + " hours")
                )
                .set_timestamp(time(0));
            
            event.reply(dpp::message().add_embed(embed));
        } else {
            event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to claim daily reward.").set_flags(dpp::m_ephemeral));
        }
    }
};

/**
 * Example: Work Command with Feature Checks and Multipliers
 */
class WorkCommand : public Command {
public:
    dpp::slashcommand get_command_info() const override {
        return dpp::slashcommand("work", "Work for money", bot->me.id);
    }

    void execute(const dpp::slashcommand_t& event) override {
        uint64_t user_id = event.command.usr.id;
        std::optional<uint64_t> guild_id = event.command.guild_id;
        
        // Base earnings
        int64_t base_earning = 500 + (rand() % 501); // 500-1000
        int64_t actual_earning = base_earning;
        
        // Get settings and apply multiplier
        if (guild_id) {
            auto settings = get_guild_economy_settings(db, *guild_id);
            if (settings && settings->economy_mode == "server") {
                // Apply work multiplier
                actual_earning = static_cast<int64_t>(base_earning * settings->work_multiplier);
            }
        }
        
        // Update wallet
        auto new_balance = update_wallet_unified(db, user_id, guild_id, actual_earning);
        
        if (new_balance) {
            // Random work descriptions
            std::vector<std::string> jobs = {
                "worked as a cashier",
                "delivered packages",
                "coded a website",
                "mowed lawns",
                "walked dogs"
            };
            
            std::string job = jobs[rand() % jobs.size()];
            
            dpp::embed embed = dpp::embed()
                .set_color(EMBED_COLOR_SUCCESS)
                .set_title("💼 Work Complete")
                .set_description("You " + job + " and earned **" + 
                               std::to_string(actual_earning) + "** coins!")
                .set_footer(dpp::embed_footer()
                    .set_text("New balance: " + std::to_string(*new_balance))
                )
                .set_timestamp(time(0));
            
            event.reply(dpp::message().add_embed(embed));
        } else {
            event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to complete work.").set_flags(dpp::m_ephemeral));
        }
    }
};

/**
 * Example: Pay Command with Tax System
 */
class PayCommand : public Command {
public:
    dpp::slashcommand get_command_info() const override {
        return dpp::slashcommand("pay", "Send money to another user", bot->me.id)
            .add_option(
                dpp::command_option(dpp::co_user, "user", "User to pay", true)
            )
            .add_option(
                dpp::command_option(dpp::co_integer, "amount", "Amount to send", true)
                    .set_min_value(1)
            );
    }

    void execute(const dpp::slashcommand_t& event) override {
        uint64_t from_user = event.command.usr.id;
        std::optional<uint64_t> guild_id = event.command.guild_id;
        
        auto options = event.command.get_command_interaction().options;
        uint64_t to_user = std::get<dpp::snowflake>(options[0].value);
        int64_t amount = std::get<int64_t>(options[1].value);
        
        // Check self-payment
        if (from_user == to_user) {
            event.reply(dpp::message(bronx::EMOJI_DENY + " You can't pay yourself!").set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Get current wallet
        int64_t from_wallet = get_wallet_unified(db, from_user, guild_id);
        
        if (from_wallet < amount) {
            event.reply(dpp::message(bronx::EMOJI_DENY + " You don't have enough money!").set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Check for tax in server economy
        double tax_rate = 0.0;
        int64_t tax_amount = 0;
        int64_t amount_received = amount;
        
        if (guild_id) {
            auto settings = get_guild_economy_settings(db, *guild_id);
            if (settings && settings->economy_mode == "server" && settings->enable_tax) {
                tax_rate = settings->transaction_tax_percent;
                tax_amount = static_cast<int64_t>(amount * (tax_rate / 100.0));
                amount_received = amount - tax_amount;
            }
        }
        
        // Perform transfer
        TransactionResult result;
        if (guild_id && is_server_economy(db, *guild_id)) {
            result = transfer_server_money(db, *guild_id, from_user, to_user, amount);
        } else {
            result = db->transfer_money(from_user, to_user, amount);
        }
        
        if (result == TransactionResult::Success) {
            std::string description = "Successfully sent **" + std::to_string(amount) + "** coins";
            if (tax_amount > 0) {
                description += "\n💸 Tax: **" + std::to_string(tax_amount) + "** (" + 
                             std::to_string(tax_rate) + "%)";
                description += "\n📥 They received: **" + std::to_string(amount_received) + "**";
            }
            
            dpp::embed embed = dpp::embed()
                .set_color(EMBED_COLOR_SUCCESS)
                .set_title("💸 Payment Sent")
                .set_description(description)
                .set_footer(dpp::embed_footer()
                    .set_text("Transaction completed")
                )
                .set_timestamp(time(0));
            
            event.reply(dpp::message().add_embed(embed));
        } else {
            event.reply(dpp::message(bronx::EMOJI_DENY + " Transaction failed!").set_flags(dpp::m_ephemeral));
        }
    }
};

} // namespace commands
