#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "../pets/pets.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include <dpp/dpp.h>
#include <vector>
#include <chrono>
#include <random>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {

::std::vector<Command*> get_money_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Daily command
    static Command* daily = new Command("daily", "claim your daily reward", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.msg.author.id);
            int64_t amount = static_cast<int64_t>(networth * 0.08);
            if (amount < 500) amount = 500;
            if (amount > 250000000) amount = 250000000; // $250M cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.msg.author.id, "daily", 86400)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.msg.author.id, "daily");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you already claimed your daily! try again <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.msg.author.id, amount)) {
                log_balance_change(db, event.msg.author.id, "claimed daily reward +$" + format_number(amount));
                ::commands::pets::pet_hooks::on_daily(db, event.msg.author.id);
                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "coins_earned_today", amount);
                
                auto embed = bronx::success("claimed your daily reward of $" + format_number(amount) + "!\n(8% of your networth: $" + format_number(networth) + ")");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to claim daily"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.command.get_issuing_user().id);
            int64_t amount = static_cast<int64_t>(networth * 0.08);
            if (amount < 500) amount = 500;
            if (amount > 250000000) amount = 250000000; // $250M cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "daily", 86400)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.command.get_issuing_user().id, "daily");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you already claimed your daily! try again <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.command.get_issuing_user().id, amount)) {
                log_balance_change(db, event.command.get_issuing_user().id, "claimed daily reward +$" + format_number(amount));
                ::commands::pets::pet_hooks::on_daily(db, event.command.get_issuing_user().id);
                ::commands::daily_challenges::track_daily_stat(db, event.command.get_issuing_user().id, "coins_earned_today", amount);
                
                auto embed = bronx::success("claimed your daily reward of $" + format_number(amount) + "!\n(8% of your networth: $" + format_number(networth) + ")");
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to claim daily")));
            }
        });
    cmds.push_back(daily);
    
    // Weekly command
    static Command* weekly = new Command("weekly", "claim your weekly reward (50% of networth)", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.msg.author.id);
            int64_t amount = static_cast<int64_t>(networth * 0.5);
            if (amount < 1000) amount = 1000;
            if (amount > 1000000000LL) amount = 1000000000LL; // $1B cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.msg.author.id, "weekly", 604800)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.msg.author.id, "weekly");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you already claimed your weekly! try again <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.msg.author.id, amount)) {
                log_balance_change(db, event.msg.author.id, "claimed weekly reward +$" + format_number(amount));
                
                auto embed = bronx::success("claimed your weekly reward of $" + format_number(amount) + "!\n(50% of your networth: $" + format_number(networth) + ")");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to claim weekly"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.command.get_issuing_user().id);
            int64_t amount = static_cast<int64_t>(networth * 0.5);
            if (amount < 1000) amount = 1000;
            if (amount > 1000000000LL) amount = 1000000000LL; // $1B cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "weekly", 604800)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.command.get_issuing_user().id, "weekly");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you already claimed your weekly! try again <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.command.get_issuing_user().id, amount)) {
                log_balance_change(db, event.command.get_issuing_user().id, "claimed weekly reward +$" + format_number(amount));
                
                auto embed = bronx::success("claimed your weekly reward of $" + format_number(amount) + "!\n(50% of your networth: $" + format_number(networth) + ")");
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to claim weekly")));
            }
        });
    cmds.push_back(weekly);
    
    // Work command
    static Command* work = new Command("work", "work for some easy cash", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.msg.author.id);
            int64_t amount = static_cast<int64_t>(networth * 0.03);
            if (amount < 100) amount = 100;
            if (amount > 25000000) amount = 25000000; // $25M cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.msg.author.id, "work", 1800)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.msg.author.id, "work");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event,
                        bronx::error("you're exhausted! rest until <t:" + ::std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Random job generator for flavor
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::vector<::std::string> jobs = {
                "delivered packages", "cleaned dishes", "walked dogs",
                "mowed lawns", "tutored students", "drove for uber",
                "stocked shelves", "painted houses", "fixed computers",
                "served tables", "coded websites", "designed logos", "managed social media", "edited videos", "wrote articles",
                "translated documents", "provided customer support", "assembled products", "conducted surveys",
                "destroyed classified information", "hacked into the pentagon", "stole the declaration of independence",
                "saved the world from an alien invasion", "invented a time machine",
                "became the ruler of a small country", "discovered a new species of animal", "won the lottery", "died"
            };
            ::std::uniform_int_distribution<> job_dis(0, jobs.size() - 1);
            ::std::string job = jobs[job_dis(gen)];
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.msg.author.id, amount)) {
                log_balance_change(db, event.msg.author.id, "worked (" + job + ") +$" + format_number(amount));
                ::commands::pets::pet_hooks::on_work(db, event.msg.author.id);
                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "work_count_today", 1);
                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "coins_earned_today", amount);
                
                ::std::string description = "you " + job + " and earned $" + format_number(amount);
                
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to work"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Calculate reward first (before claiming cooldown)
            int64_t networth = db->get_networth(event.command.get_issuing_user().id);
            int64_t amount = static_cast<int64_t>(networth * 0.03);
            if (amount < 100) amount = 100;
            if (amount > 25000000) amount = 25000000; // $25M cap
            
            // Atomically try to claim the cooldown - prevents double-claim race condition
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "work", 1800)) {
                // Already on cooldown - show when it expires
                auto expiry = db->get_cooldown_expiry(event.command.get_issuing_user().id, "work");
                if (expiry) {
                    auto timestamp = ::std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(
                        bronx::error("you're exhausted! rest until <t:" + ::std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Random job generator for flavor
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::vector<::std::string> jobs = {
                "delivered packages", "cleaned dishes", "walked dogs",
                "mowed lawns", "tutored students", "drove for uber",
                "stocked shelves", "painted houses", "fixed computers",
                "served tables", "coded websites", "designed logos", "managed social media", "edited videos", "wrote articles",
                "translated documents", "provided customer support", "assembled products", "conducted surveys",
                "destroyed classified information", "hacked into the pentagon", "stole the declaration of independence",
                "saved the world from an alien invasion", "invented a time machine",
                "became the ruler of a small country", "discovered a new species of animal", "won the lottery", "died"
            };
            ::std::uniform_int_distribution<> job_dis(0, jobs.size() - 1);
            ::std::string job = jobs[job_dis(gen)];
            
            // Cooldown claimed - now give the reward
            if (db->update_wallet(event.command.get_issuing_user().id, amount)) {
                log_balance_change(db, event.command.get_issuing_user().id, "worked (" + job + ") +$" + format_number(amount));
                ::commands::pets::pet_hooks::on_work(db, event.command.get_issuing_user().id);
                ::commands::daily_challenges::track_daily_stat(db, event.command.get_issuing_user().id, "work_count_today", 1);
                ::commands::daily_challenges::track_daily_stat(db, event.command.get_issuing_user().id, "coins_earned_today", amount);
                
                ::std::string description = "you " + job + " and earned $" + format_number(amount);
                
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to work")));
            }
        });
    cmds.push_back(work);
    
    return cmds;
}

} // namespace commands
