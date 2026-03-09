#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>
#include <algorithm>
#include <sstream>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

inline Command* get_frogger_command(Database* db) {
    static Command* frogger = new Command("frogger", "play frogger - hop across logs without falling!", "gambling", {"frog"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.msg.author.id, "frogger", 3)) {
                bronx::send_message(bot, event, bronx::error("slow down! wait a few seconds between games"));
                return;
            }
            
            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: frogger <easy|medium|hard> <amount>"));
                return;
            }
            
            ::std::string diff_str = args[0];
            ::std::transform(diff_str.begin(), diff_str.end(), diff_str.begin(), ::tolower);
            
            int difficulty = 1;
            if (diff_str == "medium" || diff_str == "med" || diff_str == "m") difficulty = 2;
            else if (diff_str == "hard" || diff_str == "h") difficulty = 3;
            
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            int64_t bet;
            try {
                bet = parse_amount(args[1], user->wallet);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("invalid bet amount"));
                return;
            }
            
            if (bet < 100) {
                bronx::send_message(bot, event, bronx::error("minimum bet is $100"));
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
            
            db->update_wallet(event.msg.author.id, -bet);
            log_gambling(db, event.msg.author.id, "started frogger bet $" + format_number(bet));
            
            // Generate initial logs (5 rows ahead)
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> crack_dis(0, 100);
            
            // Crack chances: easy 20%, medium 35%, hard 45% (was 50%)
            int crack_chance = (difficulty == 1) ? 20 : (difficulty == 2) ? 35 : 45;
            
            FroggerGame game;
            game.user_id = event.msg.author.id;
            game.initial_bet = bet;
            game.logs_hopped = 0;
            game.difficulty = difficulty;
            game.frog_lane = 1; // Start in middle
            game.active = true;
            
            // Generate 5 rows of logs
            for (int i = 0; i < 5; i++) {
                ::std::vector<bool> row;
                for (int j = 0; j < 3; j++) {
                    bool is_safe = crack_dis(gen) > crack_chance;
                    row.push_back(is_safe);
                }
                // Ensure at least one safe log per row
                if (!row[0] && !row[1] && !row[2]) {
                    row[gen() % 3] = true;
                }
                game.upcoming_logs.push_back(row);
            }
            
            // Create message
            // Easy: 15% per log, Medium: 25% per log, Hard: 40% per log
            double multiplier_per_log = (difficulty == 1) ? 0.15 : (difficulty == 2) ? 0.25 : 0.40;
            auto multiplier = 1.0 + (game.logs_hopped * multiplier_per_log);
            ::std::string title = "🐸 FROGGER | Logs: " + ::std::to_string(game.logs_hopped) + " | " + 
                              ::std::to_string((int)(multiplier * 100)) + "% multiplier";
            
            ::std::string description = "difficulty: **" + ::std::string((difficulty == 1) ? "easy" : (difficulty == 2) ? "medium" : "hard") + "**\n";
            description += "bet: **$" + format_number(bet) + "**\n\n";
            
            // Render game (show 5 logs - all appear as safe)
            for (int i = 4; i >= 0; i--) {
                description += "│ ";
                for (int j = 0; j < 3; j++) {
                    if (i == 0 && j == game.frog_lane) {
                        description += "🐸 │ ";
                    } else {
                        description += "🪵 │ ";
                    }
                }
                // Calculate multiplier for this row
                double row_multiplier = 1.0 + ((game.logs_hopped + i) * multiplier_per_log);
                description += "`" + ::std::to_string(row_multiplier).substr(0, ::std::to_string(row_multiplier).find('.') + 3) + "X`";
                description += "\n";
            }
            
            description += "\n**choose a lane to hop!**\n";
            description += "💰 current payout: **$" + format_number((int64_t)(bet * multiplier)) + "**";
            
            auto embed = bronx::create_embed(description);
            embed.set_title(title);
            embed.set_color(0x2ECC71);
            
            // Create lane buttons
            dpp::component left_btn;
            left_btn.set_type(dpp::cot_button);
            left_btn.set_style(dpp::cos_primary);
            left_btn.set_label("⬅️ left");
            left_btn.set_id("frogger_lane_0_" + ::std::to_string(event.msg.author.id));
            
            dpp::component middle_btn;
            middle_btn.set_type(dpp::cot_button);
            middle_btn.set_style(dpp::cos_primary);
            middle_btn.set_label("⬆️ middle");
            middle_btn.set_id("frogger_lane_1_" + ::std::to_string(event.msg.author.id));
            
            dpp::component right_btn;
            right_btn.set_type(dpp::cot_button);
            right_btn.set_style(dpp::cos_primary);
            right_btn.set_label("➡️ right");
            right_btn.set_id("frogger_lane_2_" + ::std::to_string(event.msg.author.id));
            
            dpp::component lane_row;
            lane_row.set_type(dpp::cot_action_row);
            lane_row.add_component(left_btn);
            lane_row.add_component(middle_btn);
            lane_row.add_component(right_btn);
            
            // Cashout button
            dpp::component cashout_btn;
            cashout_btn.set_type(dpp::cot_button);
            cashout_btn.set_style(dpp::cos_success);
            cashout_btn.set_label("💰 cash out");
            cashout_btn.set_id("frogger_cashout_" + ::std::to_string(event.msg.author.id));
            
            dpp::component button_row;
            button_row.set_type(dpp::cot_action_row);
            button_row.add_component(cashout_btn);
            
            dpp::message msg(event.msg.channel_id, embed);
            msg.add_component(lane_row);
            msg.add_component(button_row);
            
            bot.message_create(msg, [game, event](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    auto sent_msg = ::std::get<dpp::message>(callback.value);
                    game.message_id = sent_msg.id;
                    active_frogger_games[event.msg.author.id] = game;
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Anti-spam cooldown (3 seconds) - prevents double-tap exploit
            if (!db->try_claim_cooldown(event.command.get_issuing_user().id, "frogger", 3)) {
                event.reply(dpp::message().add_embed(bronx::error("slow down! wait a few seconds between games")));
                return;
            }
            
            auto diff_param = event.get_parameter("difficulty");
            ::std::string diff_str;
            if (std::holds_alternative<std::string>(diff_param)) {
                diff_str = std::get<std::string>(diff_param);
            } else {
                event.reply(dpp::message().add_embed(bronx::error("please provide a difficulty")));
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
            
            int difficulty = 1;
            if (diff_str == "medium") difficulty = 2;
            else if (diff_str == "hard") difficulty = 3;
            
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
            
            if (bet < 100) {
                event.reply(dpp::message().add_embed(bronx::error("minimum bet is $100")));
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
            
            db->update_wallet(event.command.get_issuing_user().id, -bet);
            log_gambling(db, event.command.get_issuing_user().id, "started frogger bet $" + format_number(bet));
            
            ::std::random_device rd;
            ::std::mt19937 gen(rd());
            ::std::uniform_int_distribution<> crack_dis(0, 100);
            
            int crack_chance = (difficulty == 1) ? 20 : (difficulty == 2) ? 35 : 45;
            
            FroggerGame game;
            game.user_id = event.command.get_issuing_user().id;
            game.initial_bet = bet;
            game.logs_hopped = 0;
            game.difficulty = difficulty;
            game.frog_lane = 1;
            game.active = true;
            
            for (int i = 0; i < 5; i++) {
                ::std::vector<bool> row;
                for (int j = 0; j < 3; j++) {
                    bool is_safe = crack_dis(gen) > crack_chance;
                    row.push_back(is_safe);
                }
                if (!row[0] && !row[1] && !row[2]) {
                    row[gen() % 3] = true;
                }
                game.upcoming_logs.push_back(row);
            }
            
            // Easy: 15% per log, Medium: 25% per log, Hard: 40% per log
            double multiplier_per_log = (difficulty == 1) ? 0.15 : (difficulty == 2) ? 0.25 : 0.40;
            auto multiplier = 1.0 + (game.logs_hopped * multiplier_per_log);
            ::std::string title = "🐸 FROGGER | Logs: " + ::std::to_string(game.logs_hopped) + " | " + 
                              ::std::to_string((int)(multiplier * 100)) + "% multiplier";
            
            ::std::string description = "difficulty: **" + ::std::string((difficulty == 1) ? "easy" : (difficulty == 2) ? "medium" : "hard") + "**\n";
            description += "bet: **$" + format_number(bet) + "**\n\n";
            
            // All logs appear safe to the player
            for (int i = 4; i >= 0; i--) {
                description += "│ ";
                for (int j = 0; j < 3; j++) {
                    if (i == 0 && j == game.frog_lane) {
                        description += "🐸 │ ";
                    } else {
                        description += "🪵 │ ";
                    }
                }
                // Calculate multiplier for this row
                double row_multiplier = 1.0 + ((game.logs_hopped + i) * multiplier_per_log);
                description += "`" + ::std::to_string(row_multiplier).substr(0, ::std::to_string(row_multiplier).find('.') + 3) + "x`";
                description += "\n";
            }
            
            description += "\n**choose a lane to hop!**\n";
            description += "💰 current payout: **$" + format_number((int64_t)(bet * multiplier)) + "**";
            
            auto embed = bronx::create_embed(description);
            embed.set_title(title);
            embed.set_color(0x2ECC71);
            
            // Create lane buttons
            dpp::component left_btn;
            left_btn.set_type(dpp::cot_button);
            left_btn.set_style(dpp::cos_primary);
            left_btn.set_label("⬅️ left");
            left_btn.set_id("frogger_lane_0_" + ::std::to_string(event.command.get_issuing_user().id));
            
            dpp::component middle_btn;
            middle_btn.set_type(dpp::cot_button);
            middle_btn.set_style(dpp::cos_primary);
            middle_btn.set_label("⬆️ middle");
            middle_btn.set_id("frogger_lane_1_" + ::std::to_string(event.command.get_issuing_user().id));
            
            dpp::component right_btn;
            right_btn.set_type(dpp::cot_button);
            right_btn.set_style(dpp::cos_primary);
            right_btn.set_label("➡️ right");
            right_btn.set_id("frogger_lane_2_" + ::std::to_string(event.command.get_issuing_user().id));
            
            dpp::component lane_row;
            lane_row.set_type(dpp::cot_action_row);
            lane_row.add_component(left_btn);
            lane_row.add_component(middle_btn);
            lane_row.add_component(right_btn);
            
            dpp::component cashout_btn;
            cashout_btn.set_type(dpp::cot_button);
            cashout_btn.set_style(dpp::cos_success);
            cashout_btn.set_label("💰 cash out");
            cashout_btn.set_id("frogger_cashout_" + ::std::to_string(event.command.get_issuing_user().id));
            
            dpp::component button_row;
            button_row.set_type(dpp::cot_action_row);
            button_row.add_component(cashout_btn);
            
            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(lane_row);
            msg.add_component(button_row);
            
            event.reply(msg, [game, event](const dpp::confirmation_callback_t& callback) mutable {
                if (!callback.is_error()) {
                    // Store game state
                    active_frogger_games[event.command.get_issuing_user().id] = game;
                }
            });
        },
        {
            dpp::command_option(dpp::co_string, "difficulty", "game difficulty", true)
                .add_choice(dpp::command_option_choice("easy", ::std::string("easy")))
                .add_choice(dpp::command_option_choice("medium", ::std::string("medium")))
                .add_choice(dpp::command_option_choice("hard", ::std::string("hard"))),
            dpp::command_option(dpp::co_string, "amount", "amount to bet", true)
        });
    
    return frogger;
}

// Register frogger interactions
inline void register_gambling_interactions(dpp::cluster& bot, Database* db) {
    // Handle frogger lane selection
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.find("frogger_lane_") != 0) return;
        
        // Parse lane from custom_id: frogger_lane_X_userid
        ::std::string custom_id = event.custom_id;
        size_t first_underscore = custom_id.find('_');
        size_t second_underscore = custom_id.find('_', first_underscore + 1);
        size_t third_underscore = custom_id.find('_', second_underscore + 1);
        
        int chosen_lane = ::std::stoi(custom_id.substr(second_underscore + 1, third_underscore - second_underscore - 1));
        
        uint64_t user_id = event.command.get_issuing_user().id;
        
        if (active_frogger_games.find(user_id) == active_frogger_games.end()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game not found or expired")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        FroggerGame& game = active_frogger_games[user_id];
        
        if (!game.active) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game is not active")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Check if the chosen log is safe
        bool is_safe = game.upcoming_logs[0][chosen_lane];
        
        if (!is_safe) {
            // Game over - hit cracked log
            game.active = false;
            
            // Track gambling stats
            db->increment_stat(user_id, "gambling_losses", game.initial_bet);
            
            // Track milestone (loss)
            track_gambling_result(bot, db, event.command.channel_id, user_id, false);
            
            log_gambling(db, user_id, "lost frogger for $" + format_number(game.initial_bet));
            
            ::std::string description = "💥 **GAME OVER!**\n\n";
            description += "you jumped on a cracked log and fell into the water!\n\n";
            description += "logs hopped: **" + ::std::to_string(game.logs_hopped) + "**\n";
            description += "lost: **$" + format_number(game.initial_bet) + "**";
            
            auto embed = bronx::error(description);
            embed.set_title("🐸 FROGGER - FAILED");
            
            event.reply(dpp::ir_update_message, dpp::message().add_embed(embed));
            active_frogger_games.erase(user_id);
            return;
        }
        
        // Safe jump!
        game.logs_hopped++;
        game.frog_lane = chosen_lane;
        
        // Remove first row and add new row
        game.upcoming_logs.erase(game.upcoming_logs.begin());
        
        // Generate new row
        ::std::random_device rd;
        ::std::mt19937 gen(rd());
        ::std::uniform_int_distribution<> crack_dis(0, 100);
        int crack_chance = (game.difficulty == 1) ? 20 : (game.difficulty == 2) ? 35 : 45;
        
        ::std::vector<bool> new_row;
        for (int j = 0; j < 3; j++) {
            bool is_safe_log = crack_dis(gen) > crack_chance;
            new_row.push_back(is_safe_log);
        }
        if (!new_row[0] && !new_row[1] && !new_row[2]) {
            new_row[gen() % 3] = true;
        }
        game.upcoming_logs.push_back(new_row);
        
        // Calculate new multiplier
        // Easy: 15% per log, Medium: 25% per log, Hard: 40% per log
        double multiplier_per_log = (game.difficulty == 1) ? 0.15 : (game.difficulty == 2) ? 0.25 : 0.40;
        auto multiplier = 1.0 + (game.logs_hopped * multiplier_per_log);
        ::std::string title = "🐸 FROGGER | Logs: " + ::std::to_string(game.logs_hopped) + " | " + 
                          ::std::to_string((int)(multiplier * 100)) + "% multiplier";
        
        ::std::string description = "difficulty: **" + ::std::string((game.difficulty == 1) ? "easy" : (game.difficulty == 2) ? "medium" : "hard") + "**\n";
        description += "bet: **$" + format_number(game.initial_bet) + "**\n\n";
        
        // Render updated game (all logs appear safe)
        for (int i = 4; i >= 0; i--) {
            description += "│ ";
            for (int j = 0; j < 3; j++) {
                if (i == 0 && j == game.frog_lane) {
                    description += "🐸 │ ";
                } else {
                    description += "🪵 │ ";
                }
            }
            // Calculate multiplier for this row
            double row_multiplier = 1.0 + ((game.logs_hopped + i) * multiplier_per_log);
            description += "`" + ::std::to_string(row_multiplier).substr(0, ::std::to_string(row_multiplier).find('.') + 3) + "x`";
            description += "\n";
        }
        
        description += "\n**choose a lane to hop!**\n";
        description += "💰 current payout: **$" + format_number((int64_t)(game.initial_bet * multiplier)) + "**";
        
        auto embed = bronx::create_embed(description);
        embed.set_title(title);
        embed.set_color(0x2ECC71);
        
        // Keep the same components
        dpp::component left_btn;
        left_btn.set_type(dpp::cot_button);
        left_btn.set_style(dpp::cos_primary);
        left_btn.set_label("⬅️ left");
        left_btn.set_id("frogger_lane_0_" + ::std::to_string(user_id));
        
        dpp::component middle_btn;
        middle_btn.set_type(dpp::cot_button);
        middle_btn.set_style(dpp::cos_primary);
        middle_btn.set_label("⬆️ middle");
        middle_btn.set_id("frogger_lane_1_" + ::std::to_string(user_id));
        
        dpp::component right_btn;
        right_btn.set_type(dpp::cot_button);
        right_btn.set_style(dpp::cos_primary);
        right_btn.set_label("➡️ right");
        right_btn.set_id("frogger_lane_2_" + ::std::to_string(user_id));
        
        dpp::component lane_row;
        lane_row.set_type(dpp::cot_action_row);
        lane_row.add_component(left_btn);
        lane_row.add_component(middle_btn);
        lane_row.add_component(right_btn);
        
        dpp::component cashout_btn;
        cashout_btn.set_type(dpp::cot_button);
        cashout_btn.set_style(dpp::cos_success);
        cashout_btn.set_label("💰 cash out");
        cashout_btn.set_id("frogger_cashout_" + ::std::to_string(user_id));
        
        dpp::component button_row;
        button_row.set_type(dpp::cot_action_row);
        button_row.add_component(cashout_btn);
        
        dpp::message msg;
        msg.add_embed(embed);
        msg.add_component(lane_row);
        msg.add_component(button_row);
        
        event.reply(dpp::ir_update_message, msg);
    });
    
    // Handle frogger cashout
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.find("frogger_cashout_") != 0) return;
        
        uint64_t user_id = event.command.get_issuing_user().id;
        
        if (active_frogger_games.find(user_id) == active_frogger_games.end()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game not found or expired")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        FroggerGame& game = active_frogger_games[user_id];
        
        if (!game.active) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("game is not active")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Calculate winnings
        // Easy: 15% per log, Medium: 25% per log, Hard: 40% per log
        double multiplier_per_log = (game.difficulty == 1) ? 0.15 : (game.difficulty == 2) ? 0.25 : 0.40;
        auto multiplier = 1.0 + (game.logs_hopped * multiplier_per_log);
        int64_t payout = (int64_t)(game.initial_bet * multiplier);
        
        db->update_wallet(user_id, payout);
        game.active = false;
        
        int64_t profit = payout - game.initial_bet;
        
        // Track gambling stats
        if (profit > 0) {
            db->increment_stat(user_id, "gambling_profit", profit);
            // Check gambling profit achievements
            track_gambling_profit(bot, db, event.command.channel_id, user_id);
        }
        
        // Track milestone (win if profit > 0)
        track_gambling_result(bot, db, event.command.channel_id, user_id, profit > 0, profit);
        
        log_gambling(db, user_id, "won frogger for $" + format_number(profit) + " (" + ::std::to_string(game.logs_hopped) + " logs)");
        
        ::std::string description = bronx::EMOJI_CHECK + " **CASHED OUT!**\n\n";
        description += "logs hopped: **" + ::std::to_string(game.logs_hopped) + "**\n";
        description += "multiplier: **" + ::std::to_string((int)(multiplier * 100)) + "%**\n\n";
        description += "bet: **$" + format_number(game.initial_bet) + "**\n";
        description += "payout: **$" + format_number(payout) + "**\n";
        description += "profit: **$" + format_number(payout - game.initial_bet) + "**";
        
        auto embed = bronx::success(description);
        embed.set_title("🐸 FROGGER - CASHED OUT");
        
        event.reply(dpp::ir_update_message, dpp::message().add_embed(embed));
        active_frogger_games.erase(user_id);
    });
    
    // Roulette interactions - need to be in a separate header since roulette.h declares them
    // Blackjack interactions - need to be in a separate header since blackjack.h declares them
}

} // namespace gambling
} // namespace commands
