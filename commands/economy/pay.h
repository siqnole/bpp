#pragma once
#include "helpers.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include "../../server_logger.h"

using namespace bronx::db::server_economy_operations;

// ─────────────────────────────────────────────────────────────────────────────
// Anti-bypass constants for the 100x pay limit
//
// PAY_COOLDOWN_SECONDS
//   Layer 3: minimum seconds between successive pays from the SAME sender to
//   the SAME recipient.  Prevents rapid-fire laddering regardless of amount.
//
// RECIPIENT_DAILY_RECEIVE_MULTIPLIER
//   Layer 2: a recipient cannot receive more than (starting_balance_today ×
//   this value) in a single calendar day.  "Starting balance" is reconstructed
//   as (current_wallet − coins_received_today + coins_paid_today_by_recipient).
//   Set to 0 to disable.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int64_t PAY_COOLDOWN_SECONDS             = 30;
static constexpr int64_t RECIPIENT_DAILY_RECEIVE_MULTIPLIER = 10;

namespace commands {
namespace economy {

// ─────────────────────────────────────────────────────────────────────────────
// do_pay_checks  –  shared validation logic for both prefix and slash handlers.
//
// Returns an empty string on success, or a user-facing error message string.
//
// Layers applied:
//   1. Snapshot-based 100x check  (reconstructs pre-today balance)
//   2. Daily received cap          (coins_received_today stat on recipient)
//   3. Per-sender/recipient cooldown (last_pay_<recipient_id> stat on sender)
// ─────────────────────────────────────────────────────────────────────────────
inline std::string do_pay_checks(
    Database* db,
    uint64_t sender_id,
    uint64_t recipient_id,
    int64_t  amount,
    const std::optional<uint64_t>& guild_id)
{
    // ── Layer 3: per-sender cooldown ─────────────────────────────────────────
    // We store the unix timestamp of the last pay to this specific recipient
    // under the sender's daily stats.  The key is intentionally scoped to the
    // recipient so two different recipients don't share a cooldown bucket.
    //
    // NOTE: track_daily_stat adds to a value; for timestamps we store the raw
    // epoch second.  If your implementation of get/set_daily_stat supports
    // arbitrary int64 reads/writes (not just cumulative adds) you can use
    // set_daily_stat here instead.  Adjust the call site to match your API.
    {
        std::string cooldown_key = "last_pay_to_" + std::to_string(recipient_id);
        int64_t last_pay_ts = ::commands::daily_challenges::get_daily_stat(db, sender_id, cooldown_key);
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (last_pay_ts > 0 && (now - last_pay_ts) < PAY_COOLDOWN_SECONDS) {
            int64_t wait = PAY_COOLDOWN_SECONDS - (now - last_pay_ts);
            return "you're paying that user too fast — wait " + std::to_string(wait) + "s";
        }
    }

    // ── Layer 1 + 2 setup: reconstruct recipient's balance before today ───────
    //
    // The live wallet has been inflated by payments received today and deflated
    // by payments sent today (if the recipient also pays others).  We undo the
    // inbound side to get the balance as it stood at the start of the day.
    //
    // baseline = current_wallet − coins_received_today
    //
    // We intentionally do NOT add back coins_paid_today for the recipient;
    // that would over-inflate the baseline and make the limit easier to bypass.
    // Using the conservative (lower) baseline is safer.
    int64_t recipient_current  = get_wallet_unified(db, recipient_id, guild_id);
    int64_t already_received   = ::commands::daily_challenges::get_daily_stat(db, recipient_id, "coins_received_today");
    int64_t recipient_baseline = recipient_current - already_received;
    if (recipient_baseline < 0) recipient_baseline = 0;

    // ── Layer 1: snapshot-based 100x check ───────────────────────────────────
    //
    // Original bug: checked against live wallet, which grew with each laddering
    // payment.  We now check against the pre-today baseline so the threshold
    // cannot be moved by the attacker paying small amounts first.
    //
    // Edge case: if baseline == 0 (brand-new user with nothing before today)
    // we fall back to a hard absolute cap of $1,000 to prevent gifting infinite
    // money to fresh accounts.
    if (recipient_baseline > 0) {
        if (amount > recipient_baseline * 99) {
            return "that payment would 100x their balance. try a smaller amount";
        }
    } else {
        // Recipient had no balance before today — apply hard bootstrap cap.
        static constexpr int64_t BOOTSTRAP_CAP = 1000;
        if (amount > BOOTSTRAP_CAP) {
            return "that user has no prior balance — max first-day payment is $" + format_number(BOOTSTRAP_CAP);
        }
    }

    // ── Layer 2: daily received cap ───────────────────────────────────────────
    //
    // Even if each individual payment passes the 100x check, the cumulative
    // total received today must not exceed (baseline × multiplier).
    // This catches the laddering pattern where the attacker sends 50 payments
    // of 99× each, each one passing the per-transaction check.
    if (RECIPIENT_DAILY_RECEIVE_MULTIPLIER > 0 && recipient_baseline > 0) {
        int64_t daily_cap     = recipient_baseline * RECIPIENT_DAILY_RECEIVE_MULTIPLIER;
        int64_t would_receive = already_received + amount;
        if (would_receive > daily_cap) {
            int64_t remaining = daily_cap - already_received;
            if (remaining <= 0) {
                return "that user has hit their daily receive limit";
            }
            return "that would exceed the daily receive limit — max you can send them today is $" + format_number(remaining);
        }
    }

    return ""; // all checks passed
}

// ─────────────────────────────────────────────────────────────────────────────
// record_pay_stats  –  called after a successful transfer.
//   • Updates coins_received_today on recipient  (feeds Layer 1 & 2 next call)
//   • Updates last_pay_to_<recipient> timestamp on sender  (feeds Layer 3)
//   • Updates coins_paid_today on sender  (existing daily challenge stat)
// ─────────────────────────────────────────────────────────────────────────────
inline void record_pay_stats(Database* db, uint64_t sender_id, uint64_t recipient_id, int64_t amount) {
    // Track how much the recipient has received today (used by Layer 1 & 2).
    ::commands::daily_challenges::track_daily_stat(db, recipient_id, "coins_received_today", amount);

    // Stamp the current unix time into the sender's cooldown key (Layer 3).
    // We store it as a direct value, not an accumulation.  If your
    // track_daily_stat always adds, use set_daily_stat instead.
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::string cooldown_key = "last_pay_to_" + std::to_string(recipient_id);
    ::commands::daily_challenges::set_daily_stat(db, sender_id, cooldown_key, now);

    // Existing daily challenge stat — unchanged.
    ::commands::daily_challenges::track_daily_stat(db, sender_id, "coins_paid_today", amount);
}

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
            
            // ── Anti-bypass checks (Layers 1, 2, 3) ──────────────────────────
            {
                std::string err = do_pay_checks(db, event.msg.author.id, recipient_id, amount, guild_id);
                if (!err.empty()) {
                    bronx::send_message(bot, event, bronx::error(err));
                    return;
                }
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
                
                // Record stats for all three anti-bypass layers
                record_pay_stats(db, event.msg.author.id, recipient_id, amount);
                
                // Log to economy-logs
                if (guild_id) {
                    dpp::embed log_emb = bronx::info("Economy Transaction: Payment")
                        .set_color(0x00FF00)
                        .add_field("Sender", "<@" + std::to_string(event.msg.author.id) + ">", true)
                        .add_field("Recipient", "<@" + std::to_string(recipient_id) + ">", true)
                        .add_field("Amount", "$" + format_number(amount), true);
                    if (tax > 0) log_emb.add_field("Tax", "$" + format_number(tax), true);
                    log_emb.set_timestamp(time(0));
                    bronx::logger::ServerLogger::get().log_embed(*guild_id, bronx::logger::LOG_TYPE_ECONOMY, log_emb);
                }
                
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
            
            // ── Anti-bypass checks (Layers 1, 2, 3) ──────────────────────────
            uint64_t sender_id = event.command.get_issuing_user().id;
            {
                std::string err = do_pay_checks(db, sender_id, recipient_id, amount, guild_id);
                if (!err.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error(err)));
                    return;
                }
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
                
                // Record stats for all three anti-bypass layers
                record_pay_stats(db, sender_id, recipient_id, amount);
                
                // Log to economy-logs
                if (guild_id) {
                    dpp::embed log_emb = bronx::info("Economy Transaction: Payment")
                        .set_color(0x00FF00)
                        .add_field("Sender", "<@" + std::to_string(sender_id) + ">", true)
                        .add_field("Recipient", "<@" + std::to_string(recipient_id) + ">", true)
                        .add_field("Amount", "$" + format_number(amount), true);
                    if (tax > 0) log_emb.add_field("Tax", "$" + format_number(tax), true);
                    log_emb.set_timestamp(time(0));
                    bronx::logger::ServerLogger::get().log_embed(*guild_id, bronx::logger::LOG_TYPE_ECONOMY, log_emb);
                }
                
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