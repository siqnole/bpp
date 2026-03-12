#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <string>

namespace commands {
namespace gambling {

inline Command* get_stats_command(bronx::db::Database* db) {
    static Command gstats("gstats", "view your gambling and game statistics", "gambling", {"gamestats"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t target_id = event.msg.author.id;
            
            // Allow checking another user's stats
            if (!event.msg.mentions.empty()) {
                target_id = event.msg.mentions[0].first.id;
            } else if (!args.empty()) {
                // Try to parse as user ID
                uint64_t parsed_id = 0;
                for (char c : args[0]) {
                    if (isdigit((unsigned char)c)) {
                        parsed_id = parsed_id * 10 + (c - '0');
                    }
                }
                if (parsed_id > 0) {
                    target_id = parsed_id;
                }
            }
            
            // Fetch stats from database
            int64_t games_played = db->get_stat(target_id, "games_played");
            int64_t gambling_wins = db->get_stat(target_id, "gambling_wins");
            int64_t gambling_profit = db->get_stat(target_id, "gambling_profit");
            int64_t gambling_losses = db->get_stat(target_id, "gambling_losses");
            
            // Calculate derived stats
            int64_t gambling_losses_count = games_played - gambling_wins;
            if (gambling_losses_count < 0) gambling_losses_count = 0;
            
            int64_t net_profit = gambling_profit - gambling_losses;
            
            // Win rate percentage
            double win_rate = games_played > 0 ? (double)gambling_wins / games_played * 100.0 : 0.0;
            
            // Build embed description
            std::string description;
            
            // Header with user mention
            if (target_id != event.msg.author.id) {
                description += "**stats for** <@" + std::to_string(target_id) + ">\n\n";
            }
            
            // Gambling section
            description += "🎰 **gambling**\n";
            description += "• games played: **" + format_number(games_played) + "**\n";
            description += "• wins: **" + format_number(gambling_wins) + "**\n";
            description += "• losses: **" + format_number(gambling_losses_count) + "**\n";
            
            // Win rate
            char rate_buf[32];
            snprintf(rate_buf, sizeof(rate_buf), "%.1f%%", win_rate);
            description += "• win rate: **" + std::string(rate_buf) + "**\n\n";
            
            // Profit section
            description += "💰 **profit/loss**\n";
            description += "• total won: **$" + format_number(gambling_profit) + "**\n";
            description += "• total lost: **$" + format_number(gambling_losses) + "**\n";
            
            // Net profit with color indicator
            std::string profit_color = net_profit >= 0 ? "+" : "";
            description += "• net profit: **" + profit_color + "$" + format_number(net_profit) + "**\n";
            
            auto embed = bronx::create_embed(description);
            
            // Color based on net profit
            if (net_profit > 0) {
                embed.set_color(0x43B581); // Green for profit
            } else if (net_profit < 0) {
                embed.set_color(0xF04747); // Red for loss
            } else {
                embed.set_color(0x7289DA); // Blue for neutral
            }
            
            bronx::add_invoker_footer(embed, event.msg.author);
            bot.message_create(dpp::message(event.msg.channel_id, embed));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t target_id = event.command.get_issuing_user().id;
            
            // Check for user parameter
            auto user_param = event.get_parameter("user");
            if (std::holds_alternative<dpp::snowflake>(user_param)) {
                target_id = std::get<dpp::snowflake>(user_param);
            }
            
            // Fetch stats from database
            int64_t games_played = db->get_stat(target_id, "games_played");
            int64_t gambling_wins = db->get_stat(target_id, "gambling_wins");
            int64_t gambling_profit = db->get_stat(target_id, "gambling_profit");
            int64_t gambling_losses = db->get_stat(target_id, "gambling_losses");
            
            // Calculate derived stats
            int64_t gambling_losses_count = games_played - gambling_wins;
            if (gambling_losses_count < 0) gambling_losses_count = 0;
            
            int64_t net_profit = gambling_profit - gambling_losses;
            
            // Win rate percentage
            double win_rate = games_played > 0 ? (double)gambling_wins / games_played * 100.0 : 0.0;
            
            // Build embed description
            std::string description;
            
            // Header with user mention
            if (target_id != event.command.get_issuing_user().id) {
                description += "**stats for** <@" + std::to_string(target_id) + ">\n\n";
            }
            
            // Gambling section
            description += "🎰 **gambling**\n";
            description += "• games played: **" + format_number(games_played) + "**\n";
            description += "• wins: **" + format_number(gambling_wins) + "**\n";
            description += "• losses: **" + format_number(gambling_losses_count) + "**\n";
            
            // Win rate
            char rate_buf[32];
            snprintf(rate_buf, sizeof(rate_buf), "%.1f%%", win_rate);
            description += "• win rate: **" + std::string(rate_buf) + "**\n\n";
            
            // Profit section
            description += "💰 **profit/loss**\n";
            description += "• total won: **$" + format_number(gambling_profit) + "**\n";
            description += "• total lost: **$" + format_number(gambling_losses) + "**\n";
            
            // Net profit with color indicator
            std::string profit_color = net_profit >= 0 ? "+" : "";
            description += "• net profit: **" + profit_color + "$" + format_number(net_profit) + "**\n";
            
            auto embed = bronx::create_embed(description);
            
            // Color based on net profit
            if (net_profit > 0) {
                embed.set_color(0x43B581); // Green for profit
            } else if (net_profit < 0) {
                embed.set_color(0xF04747); // Red for loss
            } else {
                embed.set_color(0x7289DA); // Blue for neutral
            }
            
            event.reply(dpp::message().add_embed(embed));
        },
        { dpp::command_option(dpp::co_user, "user", "user to view stats for", false) }
    );
    
    return &gstats;
}

} // namespace gambling
} // namespace commands
