#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <string>
#include <random>

using namespace bronx::db;

namespace commands {
namespace gambling {

// ============================================================
// Progressive Jackpot System
// ============================================================
// 1% of all gambling losses feed the global jackpot pool.
// Any gambling win has a 0.01% chance of triggering the jackpot.
// The /jackpot command shows the current pool and recent winners.
// ============================================================

// Jackpot contribution rate (1% of losses)
constexpr double JACKPOT_CONTRIBUTION_RATE = 0.01;

// Jackpot win chance per gambling win (0.01% = 1 in 10,000)
constexpr double JACKPOT_WIN_CHANCE = 0.0001;

// Called after every gambling loss to feed the pool
inline void jackpot_on_loss(Database* db, int64_t loss_amount) {
    if (loss_amount <= 0) return;
    int64_t contribution = static_cast<int64_t>(loss_amount * JACKPOT_CONTRIBUTION_RATE);
    if (contribution < 1) contribution = 1; // always contribute at least $1
    db->contribute_to_jackpot(contribution);
}

// Called after every gambling win to check for jackpot trigger.
// Returns the jackpot amount if won (0 otherwise).
// The caller is responsible for adding the jackpot winnings to the user's wallet.
inline int64_t jackpot_on_win(Database* db, uint64_t user_id) {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    if (dist(rng) < JACKPOT_WIN_CHANCE) {
        return db->try_win_jackpot(user_id);
    }
    return 0;
}

// Build a jackpot win announcement embed
inline dpp::embed build_jackpot_win_embed(uint64_t user_id, int64_t amount) {
    std::string desc = "# 🎰💰 PROGRESSIVE JACKPOT WON! 💰🎰\n\n";
    desc += "<@" + std::to_string(user_id) + "> just hit the **PROGRESSIVE JACKPOT**!\n\n";
    desc += "🏆 **Won: $" + economy::format_number(amount) + "**\n\n";
    desc += "the jackpot pool has been reset. keep gambling to build it back up!";

    auto embed = bronx::create_embed(desc, 0xFFD700); // gold
    embed.set_title("🎰 JACKPOT WINNER!");
    embed.set_footer(dpp::embed_footer().set_text("progressive jackpot • every loss contributes 1%"));
    return embed;
}

// /jackpot command — view pool, stats, and history
inline Command* get_jackpot_command(Database* db) {
    static Command* cmd = new Command("jackpot", "view the progressive jackpot pool", "gambling", {"jp", "progressive"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            auto jackpot = db->get_jackpot();
            if (!jackpot) {
                bronx::send_message(bot, event, bronx::error("couldn't fetch jackpot data"));
                return;
            }

            std::string desc;

            // Current pool
            desc += "# 🎰 Progressive Jackpot\n\n";
            desc += "💰 **current pool: $" + economy::format_number(jackpot->pool) + "**\n\n";

            // How it works
            desc += "**how it works:**\n";
            desc += "• 1% of all gambling losses feed the pool\n";
            desc += "• every gambling win has a 0.01% chance to trigger the jackpot\n";
            desc += "• the pool grows until someone wins it all!\n\n";

            // Stats
            desc += "📊 **stats**\n";
            desc += "• times won: **" + std::to_string(jackpot->times_won) + "**\n";
            desc += "• total paid out: **$" + economy::format_number(jackpot->total_won_all_time) + "**\n";
            if (jackpot->last_winner_id > 0) {
                desc += "• last winner: <@" + std::to_string(jackpot->last_winner_id) + "> ($" +
                        economy::format_number(jackpot->last_won_amount) + ")\n";
            }

            // Recent history
            auto history = db->get_jackpot_history(5);
            if (!history.empty()) {
                desc += "\n📜 **recent winners**\n";
                for (const auto& entry : history) {
                    desc += "• <@" + std::to_string(entry.user_id) + "> — $" +
                            economy::format_number(entry.amount) + "\n";
                }
            }

            auto embed = bronx::create_embed(desc, 0xFFD700);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::maybe_add_support_link(embed);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto jackpot = db->get_jackpot();
            if (!jackpot) {
                event.reply(dpp::message().add_embed(bronx::error("couldn't fetch jackpot data")));
                return;
            }

            std::string desc;

            desc += "# 🎰 Progressive Jackpot\n\n";
            desc += "💰 **current pool: $" + economy::format_number(jackpot->pool) + "**\n\n";

            desc += "**how it works:**\n";
            desc += "• 1% of all gambling losses feed the pool\n";
            desc += "• every gambling win has a 0.01% chance to trigger the jackpot\n";
            desc += "• the pool grows until someone wins it all!\n\n";

            desc += "📊 **stats**\n";
            desc += "• times won: **" + std::to_string(jackpot->times_won) + "**\n";
            desc += "• total paid out: **$" + economy::format_number(jackpot->total_won_all_time) + "**\n";
            if (jackpot->last_winner_id > 0) {
                desc += "• last winner: <@" + std::to_string(jackpot->last_winner_id) + "> ($" +
                        economy::format_number(jackpot->last_won_amount) + ")\n";
            }

            auto history = db->get_jackpot_history(5);
            if (!history.empty()) {
                desc += "\n📜 **recent winners**\n";
                for (const auto& entry : history) {
                    desc += "• <@" + std::to_string(entry.user_id) + "> — $" +
                            economy::format_number(entry.amount) + "\n";
                }
            }

            auto embed = bronx::create_embed(desc, 0xFFD700);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            bronx::maybe_add_support_link(embed);
            event.reply(dpp::message().add_embed(embed));
        }
    );
    return cmd;
}

} // namespace gambling
} // namespace commands
