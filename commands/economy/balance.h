#pragma once
#include "helpers.h"
#include "../../database/operations/economy/server_economy_operations.h"

using namespace bronx::db::server_economy_operations;

namespace commands {
namespace economy {

inline Command* create_balance_command(Database* db) {
    static Command* balance = new Command("balance", "check your wallet, bank & net worth", "economy", {"bal", "money"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            ::std::string username = event.msg.author.format_username();
            
            // Check if user mentioned someone
            if (!event.msg.mentions.empty()) {
                user_id = event.msg.mentions.begin()->first.id;
                username = event.msg.mentions.begin()->first.format_username();
            } else if (!args.empty()) {
                // Try to parse user ID from args
                try {
                    uint64_t target_id = std::stoull(args[0]);
                    user_id = target_id;
                    // Fetch the user from Discord to get their username
                    bot.user_get(user_id, [&bot, db, event, user_id](const dpp::confirmation_callback_t& callback) {
                        if (callback.is_error()) {
                            bronx::send_message(bot, event, bronx::error("could not find that user"));
                            return;
                        }
                        dpp::user_identified fetched_user = std::get<dpp::user_identified>(callback.value);
                        ::std::string username = fetched_user.format_username();
                        
                        // Use unified operations for server economy support
                        std::optional<uint64_t> guild_id;
                        if (event.msg.guild_id) guild_id = event.msg.guild_id;
                        
                        int64_t wallet = get_wallet_unified(db, user_id, guild_id);
                        int64_t bank = get_bank_unified(db, user_id, guild_id);
                        int64_t net_worth = wallet + bank;
                        
                        ::std::string description = "**wallet:** $" + format_number(wallet) + "\n";
                        description += "**bank:** $" + format_number(bank) + "\n";
                        description += "**net worth:** $" + format_number(net_worth);
                        
                        // Show economy mode indicator
                        std::string mode_label = "global";
                        if (guild_id && is_server_economy(db, *guild_id)) mode_label = "server";
                        
                        auto embed = bronx::create_embed(description)
                            .set_author(username + "'s balance", "", "")
                            .set_footer(dpp::embed_footer().set_text(mode_label + " economy"));
                        bronx::send_message(bot, event, embed);
                    });
                    return;
                } catch (...) {
                    // Not a valid user ID, continue with author's balance
                }
            }
            
            auto user = db->get_user(user_id);
            if (!user) {
                bronx::send_message(bot, event, bronx::error("user not found in database"));
                return;
            }
            
            // Use unified operations for server economy support
            std::optional<uint64_t> guild_id;
            if (event.msg.guild_id) guild_id = event.msg.guild_id;
            
            int64_t wallet = get_wallet_unified(db, user_id, guild_id);
            int64_t bank = get_bank_unified(db, user_id, guild_id);
            int64_t net_worth = wallet + bank;
            ::std::string description = "**wallet:** $" + format_number(wallet) + "\n";
            description += "**bank:** $" + format_number(bank) + "\n";
            description += "**net worth:** $" + format_number(net_worth);
            
            // Show economy mode indicator
            std::string mode_label = "global";
            if (guild_id && is_server_economy(db, *guild_id)) mode_label = "server";
            
            auto embed = bronx::create_embed(description)
                .set_author(username + "'s balance", "", "")
                .set_footer(dpp::embed_footer().set_text(mode_label + " economy"));
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            ::std::string username = event.command.get_issuing_user().format_username();
            
            // Check if user option is provided
            auto user_param = event.get_parameter("user");
            if (::std::holds_alternative<dpp::snowflake>(user_param)) {
                user_id = ::std::get<dpp::snowflake>(user_param);
                // Get username from the resolved user
                auto resolved = event.command.get_resolved_user(user_id);
                username = resolved.format_username();
            }
            
            auto user = db->get_user(user_id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found in database")));
                return;
            }
            
            // Use unified operations for server economy support
            std::optional<uint64_t> guild_id;
            if (event.command.guild_id) guild_id = static_cast<uint64_t>(event.command.guild_id);
            
            int64_t wallet = get_wallet_unified(db, user_id, guild_id);
            int64_t bank = get_bank_unified(db, user_id, guild_id);
            int64_t net_worth = wallet + bank;
            ::std::string description = "**wallet:** $" + format_number(wallet) + "\n";
            description += "**bank:** $" + format_number(bank) + "\n";
            description += "**net worth:** $" + format_number(net_worth);
            
            // Show economy mode indicator
            std::string mode_label = "global";
            if (guild_id && is_server_economy(db, *guild_id)) mode_label = "server";
            
            auto embed = bronx::create_embed(description)
                .set_author(username + "'s balance", "", "")
                .set_footer(dpp::embed_footer().set_text(mode_label + " economy"));
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_user, "user", "user to check balance of", false)
        });
    return balance;
}

} // namespace economy
} // namespace commands
