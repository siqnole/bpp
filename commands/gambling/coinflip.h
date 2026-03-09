#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "commands/skill_tree/skill_tree.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>
#include <algorithm>

using namespace bronx::db;

namespace commands {
namespace gambling {

inline Command* get_coinflip_command(Database* db) {
    static Command* coinflip = new Command("coinflip", "flip a coin and bet on the outcome", "gambling", {"cf", "flip"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "coinflip", 3)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait a few seconds between bets"));
                return;
            }
            
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: coinflip <amount> [heads|tails]"));
                return;
            }
            
            // Default to heads if no choice specified
            ::std::string choice = "heads";
            if (args.size() >= 2) {
                choice = args[1];
                ::std::transform(choice.begin(), choice.end(), choice.begin(), ::tolower);
                if (choice != "heads" && choice != "tails" && choice != "h" && choice != "t") {
                    bronx::send_message(bot, event, bronx::error("choose heads or tails"));
                    return;
                }
            }
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            int64_t bet;
            try {
                bet = parse_amount(args[0], user->wallet);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("invalid bet amount"));
                return;
            }
            
            if (bet < 50) {
                bronx::send_message(bot, event, bronx::error("minimum bet is $50"));
                return;
            }
            
            if (bet > MAX_BET) {
                bronx::send_message(bot, event, bronx::error("maximum bet is $2,000,000,000"));
                return;
            }
            
            if (bet > user->wallet) {
                bronx::send_message(bot, event, bronx::error("you don't have that much"));
                return;
            }
            
            // Apply Gambler Skill Tree Bonuses
            double luck_bonus = commands::skill_tree::get_skill_bonus(db, event.msg.author.id, "gambling_luck_bonus");
            double payout_bonus = commands::skill_tree::get_skill_bonus(db, event.msg.author.id, "gambling_payout_bonus");
            double crit_chance = commands::skill_tree::get_skill_bonus(db, event.msg.author.id, "critical_payout_chance");
            double loss_reduction = commands::skill_tree::get_skill_bonus(db, event.msg.author.id, "loss_reduction");

            // Flip coin (incorporate beginner's luck)
            // base = 50%, luck_bonus = +1% to win chance => up to 55%
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            
            bool player_chose_heads = (choice == "heads" || choice == "h");
            
            // Adjust probability: 0 = Heads, 1 = Tails
            // If they chose heads, they want 0. So we give 0 a probability of (50 + luck_bonus)%
            double target_prob = 50.0 + luck_bonus;
            double heads_prob = player_chose_heads ? target_prob : (100.0 - target_prob);
            
            ::std::uniform_real_distribution<> dis(0.0, 100.0);
            double roll = dis(gen);
            bool heads = (roll < heads_prob);
            
            bool won = (heads && player_chose_heads) || (!heads && !player_chose_heads);
            
            // 2% house edge on wins (pays 0.98:1 instead of 1:1)
            int64_t winnings = 0;
            if (won) {
                // Apply High Roller (+2% per level)
                double mult = 0.98 * (1.0 + (payout_bonus / 100.0));
                
                // Critical payout? (1% chance per level)
                if (crit_chance > 0) {
                    ::std::uniform_real_distribution<> crit_dis(0.0, 100.0);
                    if (crit_dis(gen) < crit_chance) {
                        mult *= 2.0; // 2x payout
                    }
                }
                
                winnings = static_cast<int64_t>(bet * mult);
            } else {
                // Apply Safety Net (reduces losses)
                double final_loss_mult = 1.0 - (loss_reduction / 100.0);
                winnings = -static_cast<int64_t>(bet * final_loss_mult);
            }
            
            db->update_wallet(event.msg.author.id, winnings);
            
            // Track gambling stats
            if (winnings > 0) {
                db->increment_stat(event.msg.author.id, "gambling_profit", winnings);
                // Check gambling profit achievements
                track_gambling_profit(bot, db, event.msg.channel_id, event.msg.author.id);
            } else {
                db->increment_stat(event.msg.author.id, "gambling_losses", -winnings);
            }
            
            // Track milestone
            track_gambling_result(bot, db, event.msg.channel_id, event.msg.author.id, won, winnings);
            
            // Log gambling result to history
            int64_t new_balance = db->get_wallet(event.msg.author.id);
            std::string log_desc = won ? "won coinflip for $" + format_number(bet) : "lost coinflip for $" + format_number(bet);
            bronx::db::history_operations::log_gambling(db, event.msg.author.id, log_desc, winnings, new_balance);
            
            ::std::string coin_emoji = heads ? "🪙" : "🪙";
            ::std::string result = heads ? "heads" : "tails";
            
            ::std::string description = coin_emoji + " the coin landed on **" + result + "**!\n\n";
            if (won) {
                description += "you won $" + format_number(bet) + "!";
            } else {
                description += "you lost $" + format_number(bet);
            }
            
            auto embed = won ? bronx::success(description) : bronx::error(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "coinflip", 3)) {
                event.reply(dpp::message().add_embed(bronx::error("slow down! wait a few seconds between bets")));
                return;
            }
            
            auto amount_param = event.get_parameter("amount");
            ::std::string amount_str;
            if (std::holds_alternative<std::string>(amount_param)) {
                amount_str = std::get<std::string>(amount_param);
            } else if (std::holds_alternative<int64_t>(amount_param)) {
                amount_str = std::to_string(std::get<int64_t>(amount_param));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide a bet amount")));
                return;
            }
            
            // Default to heads if no choice specified
            ::std::string choice = "heads";
            auto choice_param = event.get_parameter("choice");
            if (std::holds_alternative<std::string>(choice_param)) {
                choice = std::get<std::string>(choice_param);
                ::std::transform(choice.begin(), choice.end(), choice.begin(), ::tolower);
            }
            
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }
            
            int64_t bet;
            try {
                bet = parse_amount(amount_str, user->wallet);
            } catch (const std::exception& e) {
                event.reply(dpp::message().add_embed(bronx::error("invalid bet amount")));
                return;
            }
            
            if (bet < 50) {
                event.reply(dpp::message().add_embed(bronx::error("minimum bet is $50")));
                return;
            }
            
            if (bet > MAX_BET) {
                event.reply(dpp::message().add_embed(bronx::error("maximum bet is $2,000,000,000")));
                return;
            }
            
            if (bet > user->wallet) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have that much")));
                return;
            }
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> dis(0, 1);
            bool heads = dis(gen) == 0;
            
            bool won = (heads && choice == "heads") || (!heads && choice == "tails");
            
            // 2% house edge on wins (pays 0.98:1 instead of 1:1)
            int64_t winnings = won ? static_cast<int64_t>(bet * 0.98) : -bet;
            db->update_wallet(event.command.get_issuing_user().id, winnings);
            
            // Track gambling stats
            uint64_t user_id = event.command.get_issuing_user().id;
            if (winnings > 0) {
                db->increment_stat(user_id, "gambling_profit", winnings);
                // Check gambling profit achievements
                track_gambling_profit(bot, db, event.command.channel_id, user_id);
            } else {
                db->increment_stat(user_id, "gambling_losses", -winnings);
            }
            
            // Track milestone
            track_gambling_result(bot, db, event.command.channel_id, user_id, won, winnings);
            
            // Log gambling result to history
            int64_t new_balance = db->get_wallet(user_id);
            std::string log_desc = won ? "won coinflip for $" + format_number(bet) : "lost coinflip for $" + format_number(bet);
            bronx::db::history_operations::log_gambling(db, user_id, log_desc, winnings, new_balance);
            
            ::std::string coin_emoji = "🪙";
            ::std::string result = heads ? "heads" : "tails";
            
            ::std::string description = coin_emoji + " the coin landed on **" + result + "**!\n\n";
            if (won) {
                description += "you won $" + format_number(bet) + "!";
            } else {
                description += "you lost $" + format_number(bet);
            }
            
            auto embed = won ? bronx::success(description) : bronx::error(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to bet (supports all, half, 50%, 1k, etc)", true),
            dpp::command_option(dpp::co_string, "choice", "heads or tails (default: heads)", false)
                .add_choice(dpp::command_option_choice("heads", ::std::string("heads")))
                .add_choice(dpp::command_option_choice("tails", ::std::string("tails")))
        });
    
    return coinflip;
}

} // namespace gambling
} // namespace commands
