#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <chrono>

using namespace bronx::db;

namespace commands {
namespace passive {

// ============================================================================
// BANK INTEREST — Daily interest on bank balance
// ============================================================================
// Small daily interest (0.1–0.5%) on bank balance, scaling with prestige level.
// Gives a reason to bank money instead of hoarding in wallet.
//
// /interest       — claim your daily interest
// /interest rate  — view your current rate & next claim time
//
// Rates:
//   P0: 0.10%  |  P1: 0.15%  |  P2: 0.20%  |  P3: 0.25%
//   P4: 0.30%  |  P5: 0.35%  |  P6: 0.40%  |  P7: 0.45%
//   P8+: 0.50% (cap)
//
// Max payout capped at $500,000 per claim to prevent runaway inflation.
// ============================================================================

static constexpr double BASE_RATE = 0.001;      // 0.1%
static constexpr double RATE_PER_PRESTIGE = 0.0005; // +0.05% per prestige
static constexpr double MAX_RATE = 0.005;        // 0.5% cap
static constexpr int64_t MAX_PAYOUT = 500000;    // $500k cap per claim
static constexpr int COOLDOWN_HOURS = 24;

static double get_interest_rate(int prestige_level) {
    double rate = BASE_RATE + (prestige_level * RATE_PER_PRESTIGE);
    return std::min(rate, MAX_RATE);
}

inline Command* get_interest_command(Database* db) {
    static Command* cmd = new Command(
        "interest",
        "claim daily interest on your bank balance",
        "passive",
        {"bankinterest", "bi"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            db->ensure_user_exists(uid);
            
            std::string sub = args.empty() ? "claim" : args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            int prestige = db->get_prestige(uid);
            double rate = get_interest_rate(prestige);
            
            if (sub == "rate" || sub == "info") {
                int64_t bank = db->get_bank(uid);
                int64_t potential = std::min((int64_t)(bank * rate), MAX_PAYOUT);
                
                dpp::embed embed;
                embed.set_color(0x66BB6A);
                embed.set_title("🏦 Bank Interest");
                embed.set_description(
                    "**Your Rate:** " + std::to_string(rate * 100).substr(0, 4) + "%\n"
                    "**Prestige Level:** " + std::to_string(prestige) + "\n"
                    "**Bank Balance:** $" + economy::format_number(bank) + "\n"
                    "**Potential Payout:** $" + economy::format_number(potential) + "\n\n"
                    "*higher prestige = higher rates (max 0.5%)*\n"
                    "*max payout: $" + economy::format_number(MAX_PAYOUT) + " per claim*"
                );
                
                // Show rate progression
                std::string rates;
                for (int p = 0; p <= 8; p++) {
                    double r = get_interest_rate(p);
                    std::string marker = (p == prestige) ? " ◀" : "";
                    rates += "P" + std::to_string(p) + ": " + std::to_string(r * 100).substr(0, 4) + "%" + marker + "\n";
                }
                embed.add_field("📊 Rate Table", "```\n" + rates + "```", false);
                
                // Check cooldown
                auto expiry = db->get_cooldown_expiry(uid, "bank_interest");
                if (expiry) {
                    auto ts = std::chrono::system_clock::to_time_t(*expiry);
                    embed.add_field("⏰ Next Claim", "<t:" + std::to_string(ts) + ":R>", true);
                } else {
                    embed.add_field("⏰ Next Claim", "✅ Ready now!", true);
                }
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Claim interest
            if (!db->try_claim_cooldown(uid, "bank_interest", COOLDOWN_HOURS * 3600)) {
                auto expiry = db->get_cooldown_expiry(uid, "bank_interest");
                if (expiry) {
                    auto ts = std::chrono::system_clock::to_time_t(*expiry);
                    bronx::send_message(bot, event, bronx::error("you already claimed interest! next claim <t:" + std::to_string(ts) + ":R>"));
                }
                return;
            }
            
            int64_t bank = db->get_bank(uid);
            if (bank <= 0) {
                bronx::send_message(bot, event, bronx::error("you don't have any money in your bank!"));
                return;
            }
            
            int64_t payout = std::min((int64_t)(bank * rate), MAX_PAYOUT);
            if (payout <= 0) payout = 1; // minimum 1 coin
            
            db->update_wallet(uid, payout);
            
            auto embed = bronx::success("🏦 claimed **$" + economy::format_number(payout) + "** in bank interest!\n"
                "📊 Rate: " + std::to_string(rate * 100).substr(0, 4) + "% on $" + economy::format_number(bank));
            if (prestige < 8) {
                embed.set_footer(dpp::embed_footer().set_text("prestige to P" + std::to_string(prestige + 1) + " for " + std::to_string(get_interest_rate(prestige + 1) * 100).substr(0, 4) + "% rate"));
            }
            bronx::send_message(bot, event, embed);
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            db->ensure_user_exists(uid);
            
            std::string sub = "claim";
            auto ci_options = event.command.get_command_interaction().options;
            if (ci_options.size() > 0) sub = ci_options[0].name;
            
            int prestige = db->get_prestige(uid);
            double rate = get_interest_rate(prestige);
            
            if (sub == "rate") {
                int64_t bank = db->get_bank(uid);
                int64_t potential = std::min((int64_t)(bank * rate), MAX_PAYOUT);
                
                dpp::embed embed;
                embed.set_color(0x66BB6A);
                embed.set_title("🏦 Bank Interest");
                embed.set_description(
                    "**Rate:** " + std::to_string(rate * 100).substr(0, 4) + "% • **P" + std::to_string(prestige) + "**\n"
                    "**Bank:** $" + economy::format_number(bank) + "\n"
                    "**Potential:** $" + economy::format_number(potential)
                );
                auto expiry = db->get_cooldown_expiry(uid, "bank_interest");
                if (expiry) {
                    auto ts = std::chrono::system_clock::to_time_t(*expiry);
                    embed.add_field("⏰ Next", "<t:" + std::to_string(ts) + ":R>", true);
                } else {
                    embed.add_field("⏰ Next", "✅ Ready!", true);
                }
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            if (!db->try_claim_cooldown(uid, "bank_interest", COOLDOWN_HOURS * 3600)) {
                auto expiry = db->get_cooldown_expiry(uid, "bank_interest");
                if (expiry) {
                    auto ts = std::chrono::system_clock::to_time_t(*expiry);
                    event.reply(dpp::message().add_embed(bronx::error("next claim <t:" + std::to_string(ts) + ":R>")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            
            int64_t bank = db->get_bank(uid);
            if (bank <= 0) { event.reply(dpp::message().add_embed(bronx::error("no money in bank!")).set_flags(dpp::m_ephemeral)); return; }
            
            int64_t payout = std::min((int64_t)(bank * rate), MAX_PAYOUT);
            if (payout <= 0) payout = 1;
            db->update_wallet(uid, payout);
            
            event.reply(dpp::message().add_embed(bronx::success("🏦 claimed **$" + economy::format_number(payout) + "** interest (" + std::to_string(rate * 100).substr(0, 4) + "% on $" + economy::format_number(bank) + ")")));
        },
        // slash options
        {
            dpp::command_option(dpp::co_sub_command, "claim", "claim your daily bank interest"),
            dpp::command_option(dpp::co_sub_command, "rate", "view your current interest rate"),
        }
    );
    return cmd;
}

} // namespace passive
} // namespace commands
