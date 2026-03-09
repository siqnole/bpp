#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <vector>
#include <chrono>
#include <random>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {

::std::vector<Command*> get_rob_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Rob command
    static Command* rob = new Command("rob", "attempt to rob another user", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Check if user is on cooldown
            if (db->is_on_cooldown(event.msg.author.id, "rob")) {
                auto expiry = db->get_cooldown_expiry(event.msg.author.id, "rob");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you're laying low! try robbing again <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Check if user mentioned someone
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to rob!\nusage: `.rob @user`"));
                return;
            }
            
            uint64_t victim_id = event.msg.mentions.begin()->first.id;
            uint64_t robber_id = event.msg.author.id;
            ::std::string victim_username = event.msg.mentions.begin()->first.format_username();
            
            // Can't rob yourself
            if (victim_id == robber_id) {
                bronx::send_message(bot, event, bronx::error("you can't rob yourself!"));
                return;
            }
            
            // Check if robber is in passive mode
            if (db->is_passive(robber_id)) {
                bronx::send_message(bot, event, bronx::error("you can't rob while in passive mode! use `.passive` to toggle it off"));
                return;
            }
            
            // Check if robber recently exited passive mode
            if (db->is_on_cooldown(robber_id, "passive")) {
                auto expiry = db->get_cooldown_expiry(robber_id, "passive");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you can't rob yet! you recently changed passive mode. try again <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Check if victim is in passive mode
            if (db->is_passive(victim_id)) {
                bronx::send_message(bot, event, bronx::error(victim_username + " is in passive mode and can't be robbed!"));
                return;
            }
            
            // Get user data
            auto robber = db->get_user(robber_id);
            auto victim = db->get_user(victim_id);
            
            if (!robber || !victim) {
                bronx::send_message(bot, event, bronx::error("failed to retrieve user data"));
                return;
            }
            
            // Check if victim has money in wallet
            if (victim->wallet < 100) {
                bronx::send_message(bot, event, bronx::error(victim_username + " has less than $100 in their wallet! not worth the risk"));
                return;
            }
            
            // Check if robber has money to risk
            if (robber->wallet < 100) {
                bronx::send_message(bot, event, bronx::error("you need at least $100 in your wallet to attempt a robbery!"));
                return;
            }
            
            // Calculate success rates based on wallet comparison
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> dis(1, 100);
            int roll = dis(gen);
            
            bool success = false;
            int success_chance = 50; // Default 50% if equal
            
            if (robber->wallet < (victim->wallet / 2)) {
                // Robber wallet is less than half of victim's - 30% success rate (70% fail)
                success_chance = 30;
            } else if (robber->wallet > victim->wallet) {
                // Robber wallet is larger - 60% success rate
                success_chance = 60;
            }
            
            success = roll <= success_chance;
            
            // 10% chance to get caught by police and lose money
            ::std::uniform_int_distribution<> police_dis(1, 100);
            int police_roll = police_dis(gen);
            bool caught_by_police = police_roll <= 10;
            
            if (caught_by_police) {
                // Lose up to 75% of wallet
                ::std::uniform_int_distribution<> loss_dis(50, 75);
                int loss_percent = loss_dis(gen);
                int64_t loss_amount = (robber->wallet * loss_percent) / 100;
                
                db->update_wallet(robber_id, -loss_amount);
                log_balance_change(db, robber_id, "caught by police while robbing, lost $" + format_number(loss_amount));
                db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                
                ::std::string description = "\xf0\x9f\x9a\xa8 **you got caught by the police!**\n";
                description += "you lost $" + format_number(loss_amount) + " (" + ::std::to_string(loss_percent) + "% of your wallet) while running away!";
                
                auto embed = bronx::error(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (success) {
                // Calculate stolen amount (15-50% of victim's wallet)
                ::std::uniform_int_distribution<> steal_dis(15, 50);
                int steal_percent = steal_dis(gen);
                int64_t stolen_amount = (victim->wallet * steal_percent) / 100;
                
                // Transfer money
                auto result = db->transfer_money(victim_id, robber_id, stolen_amount);
                
                if (result == TransactionResult::Success) {
                    db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                    log_balance_change(db, robber_id, "robbed " + victim_username + " for $" + format_number(stolen_amount));
                    log_balance_change(db, victim_id, "was robbed by <@" + ::std::to_string(robber_id) + "> for $" + format_number(stolen_amount));
                    
                    ::std::string description = "💰 **robbery successful!**\n";
                    description += "you stole $" + format_number(stolen_amount) + " from " + victim_username + "!";
                    
                    auto embed = bronx::success(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("robbery failed - transaction error"));
                }
            } else {
                // Failed robbery - lose 15-30% of wallet
                ::std::uniform_int_distribution<> fail_dis(15, 30);
                int fail_percent = fail_dis(gen);
                int64_t fine_amount = (robber->wallet * fail_percent) / 100;
                
                db->update_wallet(robber_id, -fine_amount);
                log_balance_change(db, robber_id, "failed robbery, lost $" + format_number(fine_amount));
                db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                
                ::std::string description = bronx::EMOJI_DENY + " **robbery failed!**\n";
                description += "you got caught and paid $" + format_number(fine_amount) + " to avoid jail time!";
                
                auto embed = bronx::create_embed(description, bronx::COLOR_ERROR);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Check if user is on cooldown
            if (db->is_on_cooldown(event.command.get_issuing_user().id, "rob")) {
                auto expiry = db->get_cooldown_expiry(event.command.get_issuing_user().id, "rob");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you're laying low! try robbing again <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Get victim from command parameter
            auto victim_param = event.get_parameter("user");
            if (!::std::holds_alternative<dpp::snowflake>(victim_param)) {
                event.reply(dpp::message().add_embed(bronx::error("you need to specify a user to rob!")));
                return;
            }
            
            uint64_t victim_id = ::std::get<dpp::snowflake>(victim_param);
            uint64_t robber_id = event.command.get_issuing_user().id;
            auto resolved_victim = event.command.get_resolved_user(victim_id);
            ::std::string victim_username = resolved_victim.format_username();
            
            // Can't rob yourself
            if (victim_id == robber_id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't rob yourself!")));
                return;
            }
            
            // Check if robber is in passive mode
            if (db->is_passive(robber_id)) {
                event.reply(dpp::message().add_embed(bronx::error("you can't rob while in passive mode! use `/passive` to toggle it off")));
                return;
            }
            
            // Check if robber recently exited passive mode
            if (db->is_on_cooldown(robber_id, "passive")) {
                auto expiry = db->get_cooldown_expiry(robber_id, "passive");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you can't rob yet! you recently changed passive mode. try again <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Check if victim is in passive mode
            if (db->is_passive(victim_id)) {
                event.reply(dpp::message().add_embed(bronx::error(victim_username + " is in passive mode and can't be robbed!")));
                return;
            }
            
            // Get user data
            auto robber = db->get_user(robber_id);
            auto victim = db->get_user(victim_id);
            
            if (!robber || !victim) {
                event.reply(dpp::message().add_embed(bronx::error("failed to retrieve user data")));
                return;
            }
            
            // Check if victim has money in wallet
            if (victim->wallet < 100) {
                event.reply(dpp::message().add_embed(bronx::error(victim_username + " has less than $100 in their wallet! not worth the risk")));
                return;
            }
            
            // Check if robber has money to risk
            if (robber->wallet < 100) {
                event.reply(dpp::message().add_embed(bronx::error("you need at least $100 in your wallet to attempt a robbery!")));
                return;
            }
            
            // Calculate success rates based on wallet comparison
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> dis(1, 100);
            int roll = dis(gen);
            
            bool success = false;
            int success_chance = 50; // Default 50% if equal
            
            if (robber->wallet < (victim->wallet / 2)) {
                // Robber wallet is less than half of victim's - 30% success rate (70% fail)
                success_chance = 30;
            } else if (robber->wallet > victim->wallet) {
                // Robber wallet is larger - 60% success rate
                success_chance = 60;
            }
            
            success = roll <= success_chance;
            
            // 10% chance to get caught by police and lose money
            ::std::uniform_int_distribution<> police_dis(1, 100);
            int police_roll = police_dis(gen);
            bool caught_by_police = police_roll <= 10;
            
            if (caught_by_police) {
                // Lose up to 75% of wallet
                ::std::uniform_int_distribution<> loss_dis(50, 75);
                int loss_percent = loss_dis(gen);
                int64_t loss_amount = (robber->wallet * loss_percent) / 100;
                
                db->update_wallet(robber_id, -loss_amount);
                log_balance_change(db, robber_id, "caught by police while robbing, lost $" + format_number(loss_amount));
                db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                
                ::std::string description = "🚨 **you got caught by the police!**\n";
                description += "you lost $" + format_number(loss_amount) + " (" + ::std::to_string(loss_percent) + "% of your wallet) while running away!";
                
                auto embed = bronx::error(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            if (success) {
                // Calculate stolen amount (15-50% of victim's wallet)
                ::std::uniform_int_distribution<> steal_dis(15, 50);
                int steal_percent = steal_dis(gen);
                int64_t stolen_amount = (victim->wallet * steal_percent) / 100;
                
                // Transfer money
                auto result = db->transfer_money(victim_id, robber_id, stolen_amount);
                
                if (result == TransactionResult::Success) {
                    db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                    log_balance_change(db, robber_id, "robbed " + victim_username + " for $" + format_number(stolen_amount));
                    log_balance_change(db, victim_id, "was robbed by <@" + ::std::to_string(robber_id) + "> for $" + format_number(stolen_amount));
                    
                    ::std::string description = "💰 **robbery successful!**\n";
                    description += "you stole $" + format_number(stolen_amount) + " from " + victim_username + "!";
                    
                    auto embed = bronx::success(description);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("robbery failed - transaction error")));
                }
            } else {
                // Failed robbery - lose 15-30% of wallet
                ::std::uniform_int_distribution<> fail_dis(15, 30);
                int fail_percent = fail_dis(gen);
                int64_t fine_amount = (robber->wallet * fail_percent) / 100;
                
                db->update_wallet(robber_id, -fine_amount);
                log_balance_change(db, robber_id, "failed robbery, lost $" + format_number(fine_amount));
                db->set_cooldown(robber_id, "rob", 7200); // 2 hour cooldown
                
                ::std::string description = bronx::EMOJI_DENY + " **robbery failed!**\n";
                description += "you got caught and paid $" + format_number(fine_amount) + " to avoid jail time!";
                
                auto embed = bronx::create_embed(description, bronx::COLOR_ERROR);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            }
        });
    
    // Add slash command option for user parameter
    rob->options.push_back(dpp::command_option(dpp::co_user, "user", "The user to rob", true));
    
    cmds.push_back(rob);
    
    // Passive command
    static Command* passive = new Command("passive", "toggle passive mode (can't rob or be robbed)", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            bool current_passive = db->is_passive(user_id);
            bool new_passive = !current_passive;
            
            // Check if user is on passive cooldown
            if (db->is_on_cooldown(user_id, "passive")) {
                auto expiry = db->get_cooldown_expiry(user_id, "passive");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you need to wait before changing passive mode again! try again <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // If trying to go passive, check if user robbed recently
            if (new_passive && db->is_on_cooldown(user_id, "rob")) {
                auto expiry = db->get_cooldown_expiry(user_id, "rob");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you can't go passive while on rob cooldown! you must wait <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // If trying to go passive, check if net worth (including fish) exceeds 1 billion
            if (new_passive) {
                int64_t total_networth = db->get_total_networth(user_id);
                constexpr int64_t ONE_BILLION = 1'000'000'000LL;
                if (total_networth > ONE_BILLION) {
                    bronx::send_message(bot, event,
                        bronx::error("you can't go passive with a net worth over $1 billion (including fish value)! your current net worth: $" + format_number(total_networth)));
                    return;
                }
            }
            
            if (db->set_passive(user_id, new_passive)) {
                // Set 30-minute cooldown for passive mode changes
                db->set_cooldown(user_id, "passive", 1800); // 30 minutes
                ::std::string description;
                if (new_passive) {
                    description = "🛡️ **passive mode enabled**\n";
                    description += "you can no longer rob or be robbed by other users";
                } else {
                    description = "⚔️ **passive mode disabled**\n";
                    description += "you can now rob and be robbed by other users";
                }
                
                auto embed = bronx::info(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to toggle passive mode"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            bool current_passive = db->is_passive(user_id);
            bool new_passive = !current_passive;
            
            // Check if user is on passive cooldown
            if (db->is_on_cooldown(user_id, "passive")) {
                auto expiry = db->get_cooldown_expiry(user_id, "passive");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you need to wait before changing passive mode again! try again <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // If trying to go passive, check if user robbed recently
            if (new_passive && db->is_on_cooldown(user_id, "rob")) {
                auto expiry = db->get_cooldown_expiry(user_id, "rob");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you can't go passive while on rob cooldown! you must wait <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // If trying to go passive, check if net worth (including fish) exceeds 1 billion
            if (new_passive) {
                int64_t total_networth = db->get_total_networth(user_id);
                constexpr int64_t ONE_BILLION = 1'000'000'000LL;
                if (total_networth > ONE_BILLION) {
                    event.reply(dpp::message().add_embed(
                        bronx::error("you can't go passive with a net worth over $1 billion (including fish value)! your current net worth: $" + format_number(total_networth))));
                    return;
                }
            }
            
            if (db->set_passive(user_id, new_passive)) {
                // Set 30-minute cooldown for passive mode changes
                db->set_cooldown(user_id, "passive", 1800); // 30 minutes
                ::std::string description;
                if (new_passive) {
                    description = "🛡️ **passive mode enabled**\n";
                    description += "you can no longer rob or be robbed by other users";
                } else {
                    description = "⚔️ **passive mode disabled**\n";
                    description += "you can now rob and be robbed by other users";
                }
                
                auto embed = bronx::info(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to toggle passive mode")));
            }
        });
    
    cmds.push_back(passive);
    
    return cmds;
}

} // namespace commands
