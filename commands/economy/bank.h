#pragma once
#include "helpers.h"
#include "../../database/operations/economy/loan_operations.h"
#include <map>

namespace commands {
namespace economy {

// Interest rate for bank upgrade loans (percentage)
constexpr double LOAN_INTEREST_RATE = 10.0;

// Bank upgrade rate: every $1 spent gives $2 of bank space
constexpr int64_t UPGRADE_RATIO = 2;

// Preset bank space increase amounts for quick-buy buttons
static std::vector<std::pair<std::string, int64_t>> upgrade_presets = {
    {"+5K",   5000},
    {"+10K",  10000},
    {"+25K",  25000},
    {"+50K",  50000},
    {"+100K", 100000},
};

// Active bank sessions (user_id -> {message_id, channel_id})
static std::map<uint64_t, std::pair<uint64_t, uint64_t>> active_bank_sessions;

// Helper to format money with commas
inline std::string format_money(int64_t amount) {
    return "$" + format_number(amount);
}

// Calculate cost for a given bank space increase
inline int64_t upgrade_cost_for(int64_t bank_increase) {
    return bank_increase / UPGRADE_RATIO;
}

// Calculate max bank increase affordable with a given wallet
inline int64_t max_upgrade_for_wallet(int64_t wallet) {
    return wallet * UPGRADE_RATIO;
}

// Create main bank menu embed
inline dpp::embed create_bank_menu_embed(const UserData& user, Database* db) {
    auto loan = db->get_loan(user.user_id);
    
    std::string description = "**Wallet:** " + format_money(user.wallet) + "\n";
    description += "**Bank:** " + format_money(user.bank) + " / " + format_money(user.bank_limit) + "\n";
    
    int64_t networth = user.wallet + user.bank;
    description += "**Net Worth:** " + format_money(networth) + "\n\n";
    
    if (loan) {
        description += "**Active Loan:**\n";
        description += "└ Principal: " + format_money(loan->principal) + "\n";
        description += "└ Interest: " + format_money(loan->interest) + " (" + 
                      std::to_string((int)LOAN_INTEREST_RATE) + "%)\n";
        description += "└ **Remaining: " + format_money(loan->remaining) + "**\n\n";
    }
    
    description += "*Use the buttons below to manage your bank account*";
    
    return dpp::embed()
        .set_color(0x3498db)
        .set_title("Bank Manager")
        .set_description(description);
}

// Create upgrade menu embed
inline dpp::embed create_upgrade_menu_embed(const UserData& user, Database* db) {
    std::string description = "**Current Limit:** " + format_money(user.bank_limit) + "\n";
    description += "**Wallet:** " + format_money(user.wallet) + "\n\n";
    
    auto loan = db->get_loan(user.user_id);
    if (loan) {
        description += "**Active Loan:** " + format_money(loan->remaining) + " remaining\n";
        description += "*Pay off your loan before taking another*\n\n";
    }
    
    // Show the rate
    description += "**Rate:** Every **$1** spent = **$" + std::to_string(UPGRADE_RATIO) + "** bank space\n\n";
    
    // Show what each preset would cost
    description += "**Quick Upgrades:**\n";
    for (const auto& [label, increase] : upgrade_presets) {
        int64_t cost = upgrade_cost_for(increase);
        std::string afford_icon = (user.wallet >= cost) ? "\u2705" : "\u274C";
        description += afford_icon + " **" + label + "** bank space — costs " + format_money(cost) + "\n";
    }
    
    // Show max upgrade
    if (user.wallet > 0) {
        int64_t max_increase = max_upgrade_for_wallet(user.wallet);
        description += "\n**Max upgrade:** " + format_money(max_increase) + " bank space for " + format_money(user.wallet) + "\n";
    }
    
    description += "\n*Pick a preset below, or use `.bank upgrade <amount>`*";
    
    return dpp::embed()
        .set_color(0xe67e22)
        .set_title("Bank Upgrades")
        .set_description(description);
}

// Create loan management embed
inline dpp::embed create_loan_menu_embed(const UserData& user, Database* db) {
    auto loan = db->get_loan(user.user_id);
    
    std::string description;
    
    if (!loan) {
        description = "**No Active Loan**\n\n";
        description += "You currently have no outstanding loans.\n\n";
        description += "*Loans are available when upgrading your bank limit.*\n";
        description += "*Interest rate: " + std::to_string((int)LOAN_INTEREST_RATE) + "%*";
    } else {
        description = "**Active Loan Details:**\n\n";
        description += "**Principal:** " + format_money(loan->principal) + "\n";
        description += "**Interest:** " + format_money(loan->interest) + " (" + 
                      std::to_string((int)LOAN_INTEREST_RATE) + "%)\n";
        description += "**Amount Paid:** " + format_money((loan->principal + loan->interest) - loan->remaining) + "\n";
        description += "**Remaining:** " + format_money(loan->remaining) + "\n\n";
        
        // Show payment options
        description += "**Your Wallet:** " + format_money(user.wallet) + "\n\n";
        
        if (user.wallet >= loan->remaining) {
            description += "*You can pay off this loan in full*\n";
        } else if (user.wallet > 0) {
            description += "*You can make a partial payment*\n";
        } else {
            description += "*You need money in your wallet to make payments*\n";
        }
    }
    
    return dpp::embed()
        .set_color(0x9b59b6)
        .set_title("Loan Management")
        .set_description(description);
}

// Update bank menu message
inline void update_bank_message(dpp::cluster& bot, uint64_t user_id, uint64_t channel_id, 
                               uint64_t message_id, const std::string& view, Database* db) {
    auto user_data = db->get_user(user_id);
    if (!user_data) return;
    
    dpp::embed embed;
    dpp::message msg(channel_id, "");
    msg.id = message_id;
    
    if (view == "main") {
        embed = create_bank_menu_embed(*user_data, db);
        
        // Main menu buttons
        dpp::component action_row1;
        action_row1.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Deposit")
            .set_style(dpp::cos_primary)
            .set_id("bank_deposit_" + std::to_string(user_id)));
        
        action_row1.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Withdraw")
            .set_style(dpp::cos_primary)
            .set_id("bank_withdraw_" + std::to_string(user_id)));
        
        action_row1.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Upgrade")
            .set_style(dpp::cos_success)
            .set_id("bank_upgrade_menu_" + std::to_string(user_id)));
        
        dpp::component action_row2;
        action_row2.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Manage Loan")
            .set_style(dpp::cos_secondary)
            .set_id("bank_loan_menu_" + std::to_string(user_id)));
        
        action_row2.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Close")
            .set_style(dpp::cos_danger)
            .set_id("bank_close_" + std::to_string(user_id)));
        
        msg.add_embed(embed);
        msg.add_component(action_row1);
        msg.add_component(action_row2);
        
    } else if (view == "upgrade") {
        embed = create_upgrade_menu_embed(*user_data, db);
        msg.add_embed(embed);
        
        // Preset upgrade buttons (one row)
        dpp::component preset_row;
        for (size_t i = 0; i < upgrade_presets.size(); i++) {
            int64_t cost = upgrade_cost_for(upgrade_presets[i].second);
            bool can_afford = user_data->wallet >= cost;
            
            preset_row.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label(upgrade_presets[i].first)
                .set_id("bank_upgrade_" + std::to_string(user_id) + "_" + std::to_string(i))
                .set_style(can_afford ? dpp::cos_success : dpp::cos_secondary)
                .set_disabled(!can_afford));
        }
        msg.add_component(preset_row);
        
        // Max upgrade button (separate row)
        dpp::component max_row;
        bool can_afford_any = user_data->wallet > 0;
        max_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Upgrade Max")
            .set_id("bank_upgrade_max_" + std::to_string(user_id))
            .set_style(dpp::cos_primary)
            .set_disabled(!can_afford_any));
        msg.add_component(max_row);
        
        // Back button
        dpp::component nav_row;
        nav_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("« Back")
            .set_style(dpp::cos_secondary)
            .set_id("bank_main_menu_" + std::to_string(user_id)));
        msg.add_component(nav_row);
        
    } else if (view == "loan") {
        embed = create_loan_menu_embed(*user_data, db);
        
        auto loan = db->get_loan(user_id);
        
        if (loan && user_data->wallet > 0) {
            dpp::component payment_row;
            
            // Pay 25% button
            int64_t quarter = loan->remaining / 4;
            if (quarter > 0 && user_data->wallet >= quarter) {
                payment_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Pay 25%")
                    .set_style(dpp::cos_success)
                    .set_id("bank_loan_pay25_" + std::to_string(user_id)));
            }
            
            // Pay 50% button
            int64_t half = loan->remaining / 2;
            if (half > 0 && user_data->wallet >= half) {
                payment_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Pay 50%")
                    .set_style(dpp::cos_success)
                    .set_id("bank_loan_pay50_" + std::to_string(user_id)));
            }
            
            // Pay All button
            if (user_data->wallet >= loan->remaining) {
                payment_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Pay All")
                    .set_style(dpp::cos_success)
                    .set_id("bank_loan_payall_" + std::to_string(user_id)));
            }
            
            if (payment_row.components.size() > 0) {
                msg.add_embed(embed);
                msg.add_component(payment_row);
            } else {
                msg.add_embed(embed);
            }
        } else {
            msg.add_embed(embed);
        }
        
        // Back button
        dpp::component nav_row;
        nav_row.add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("« Back")
            .set_style(dpp::cos_secondary)
            .set_id("bank_main_menu_" + std::to_string(user_id)));
        msg.add_component(nav_row);
    }
    
    bot.message_edit(msg);
}

// Register button handlers for bank UI
inline void register_bank_handlers(dpp::cluster& bot, Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        std::string custom_id = event.custom_id;
        
        // Only handle bank buttons
        if (custom_id.find("bank_") != 0) return;
        
        uint64_t user_id = event.command.get_issuing_user().id;
        
        // Extract target user ID from button
        size_t last_underscore = custom_id.find_last_of('_');
        if (last_underscore == std::string::npos) return;
        
        uint64_t target_user_id;
        try {
            std::string last_part = custom_id.substr(last_underscore + 1);
            // These button formats have USERID_INDEX as trailing segments:
            //   bank_upgrade_USERID_INDEX, bank_confirm_loan_USERID_INDEX
            // All other bank buttons have USERID as the last segment.
            bool has_index_suffix = 
                (custom_id.find("bank_upgrade_") == 0
                    && custom_id.find("bank_upgrade_max_") != 0
                    && custom_id.find("bank_upgrade_menu_") != 0)
                || custom_id.find("bank_confirm_loan_") == 0;
            
            if (has_index_suffix) {
                size_t second_last = custom_id.find_last_of('_', last_underscore - 1);
                target_user_id = std::stoull(custom_id.substr(second_last + 1, last_underscore - second_last - 1));
            } else {
                target_user_id = std::stoull(last_part);
            }
        } catch (...) {
            return;
        }
        
        // Only the original user can interact
        if (user_id != target_user_id) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("This is not your bank menu!")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        uint64_t message_id = event.command.message_id;
        uint64_t channel_id = event.command.channel_id;
        
        // Handle different button actions
        if (custom_id.find("bank_close_") == 0) {
            active_bank_sessions.erase(user_id);
            bot.message_delete(message_id, channel_id);
            return;
        }
        
        if (custom_id.find("bank_main_menu_") == 0) {
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            update_bank_message(bot, user_id, channel_id, message_id, "main", db);
            return;
        }
        
        if (custom_id.find("bank_upgrade_menu_") == 0) {
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            update_bank_message(bot, user_id, channel_id, message_id, "upgrade", db);
            return;
        }
        
        if (custom_id.find("bank_loan_menu_") == 0) {
            event.reply(dpp::ir_deferred_update_message, dpp::message());
            update_bank_message(bot, user_id, channel_id, message_id, "loan", db);
            return;
        }
        
        // Handle "Upgrade Max" button
        if (custom_id.find("bank_upgrade_max_") == 0) {
            auto user_data = db->get_user(user_id);
            if (!user_data) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Failed to load user data")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (user_data->wallet <= 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("You have no money in your wallet!")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            int64_t cost = user_data->wallet;
            int64_t bank_increase = max_upgrade_for_wallet(cost);
            int64_t new_limit = user_data->bank_limit + bank_increase;
            
            if (db->update_bank_limit(user_id, new_limit)) {
                db->update_wallet(user_id, -cost);
                log_balance_change(db, user_id, "upgraded bank limit by " + format_money(bank_increase) + " to " + format_money(new_limit) + " for " + format_money(cost));
                
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success(
                        "**Bank upgraded!**\n\n" 
                        "Limit: " + format_money(user_data->bank_limit) + " → " + format_money(new_limit) + "\n"
                        "Space added: +" + format_money(bank_increase) + "\n"
                        "Cost: " + format_money(cost)
                    )).set_flags(dpp::m_ephemeral));
                
                update_bank_message(bot, user_id, channel_id, message_id, "upgrade", db);
            } else {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Failed to upgrade bank limit")).set_flags(dpp::m_ephemeral));
            }
            return;
        }
        
        // Handle preset upgrade selection
        if (custom_id.find("bank_upgrade_") == 0) {
            // Extract preset index from bank_upgrade_USERID_INDEX
            int upgrade_index = std::stoi(custom_id.substr(last_underscore + 1));
            
            auto user_data = db->get_user(user_id);
            if (!user_data) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Failed to load user data")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (upgrade_index < 0 || upgrade_index >= (int)upgrade_presets.size()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Invalid upgrade selection")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            int64_t bank_increase = upgrade_presets[upgrade_index].second;
            int64_t cost = upgrade_cost_for(bank_increase);
            int64_t new_limit = user_data->bank_limit + bank_increase;
            
            // Check if user can afford it
            if (user_data->wallet >= cost) {
                // Pay with cash
                if (db->update_bank_limit(user_id, new_limit)) {
                    db->update_wallet(user_id, -cost);
                    log_balance_change(db, user_id, "upgraded bank limit by " + format_money(bank_increase) + " to " + format_money(new_limit) + " for " + format_money(cost));
                    
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success(
                            "**Bank upgraded!**\n\n"
                            "Limit: " + format_money(user_data->bank_limit) + " → " + format_money(new_limit) + "\n"
                            "Space added: +" + format_money(bank_increase) + "\n"
                            "Cost: " + format_money(cost)
                        )).set_flags(dpp::m_ephemeral));
                    
                    update_bank_message(bot, user_id, channel_id, message_id, "upgrade", db);
                } else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Failed to upgrade bank limit")).set_flags(dpp::m_ephemeral));
                }
            } else {
                // Need a loan
                auto existing_loan = db->get_loan(user_id);
                if (existing_loan) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("You must pay off your current loan before taking another!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                // Create loan confirmation
                int64_t interest = static_cast<int64_t>(cost * (LOAN_INTEREST_RATE / 100.0));
                int64_t total_cost = cost + interest;
                
                std::string confirm_desc = "You're about to take a loan to upgrade your bank:\n\n";
                confirm_desc += "**Space added:** +" + format_money(bank_increase) + "\n";
                confirm_desc += "**New limit:** " + format_money(new_limit) + "\n";
                confirm_desc += "**Loan Amount:** " + format_money(cost) + "\n";
                confirm_desc += "**Interest (" + std::to_string((int)LOAN_INTEREST_RATE) + "%):** " + format_money(interest) + "\n";
                confirm_desc += "**Total to Repay:** " + format_money(total_cost) + "\n\n";
                confirm_desc += "Confirm this loan?";
                
                dpp::embed confirm_embed = dpp::embed()
                    .set_color(0xe74c3c)
                    .set_title("Confirm Loan")
                    .set_description(confirm_desc);
                
                dpp::message confirm_msg;
                confirm_msg.add_embed(confirm_embed);
                confirm_msg.set_flags(dpp::m_ephemeral);
                
                dpp::component confirm_row;
                confirm_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Confirm Loan")
                    .set_style(dpp::cos_danger)
                    .set_id("bank_confirm_loan_" + std::to_string(user_id) + "_" + std::to_string(upgrade_index)));
                
                confirm_row.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Cancel")
                    .set_style(dpp::cos_secondary)
                    .set_id("bank_cancel_loan_" + std::to_string(user_id)));
                
                confirm_msg.add_component(confirm_row);
                event.reply(dpp::ir_channel_message_with_source, confirm_msg);
            }
            return;
        }
        
        // Handle loan confirmation
        if (custom_id.find("bank_confirm_loan_") == 0) {
            // Extract preset index
            size_t idx_pos = custom_id.find_last_of('_');
            int upgrade_index = std::stoi(custom_id.substr(idx_pos + 1));
            
            auto user_data = db->get_user(user_id);
            if (!user_data) {
                event.reply(dpp::ir_update_message,
                    dpp::message().add_embed(bronx::error("Failed to load user data")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            if (upgrade_index < 0 || upgrade_index >= (int)upgrade_presets.size()) {
                event.reply(dpp::ir_update_message,
                    dpp::message().add_embed(bronx::error("Invalid upgrade selection")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            int64_t bank_increase = upgrade_presets[upgrade_index].second;
            int64_t cost = upgrade_cost_for(bank_increase);
            int64_t new_limit = user_data->bank_limit + bank_increase;
            
            // Create the loan
            if (db->create_loan(user_id, cost, LOAN_INTEREST_RATE)) {
                // Add funds to wallet then immediately spend on upgrade
                db->update_wallet(user_id, cost);
                
                if (db->update_bank_limit(user_id, new_limit)) {
                    db->update_wallet(user_id, -cost);
                    
                    int64_t interest = static_cast<int64_t>(cost * (LOAN_INTEREST_RATE / 100.0));
                    int64_t total_owed = cost + interest;
                    
                    log_balance_change(db, user_id, "took loan of " + format_money(cost) + " to upgrade bank by " + format_money(bank_increase) + " to " + format_money(new_limit));
                    
                    std::string success_msg = "**Loan Approved & Bank Upgraded!**\n\n";
                    success_msg += "Space added: +" + format_money(bank_increase) + "\n";
                    success_msg += "New limit: " + format_money(new_limit) + "\n";
                    success_msg += "Loan debt: " + format_money(total_owed) + "\n\n";
                    success_msg += "*Remember to pay back your loan!*";
                    
                    event.reply(dpp::ir_update_message,
                        dpp::message().add_embed(bronx::success(success_msg)).set_flags(dpp::m_ephemeral));
                    
                    update_bank_message(bot, user_id, channel_id, message_id, "upgrade", db);
                } else {
                    db->payoff_loan(user_id);
                    db->update_wallet(user_id, -cost);
                    
                    event.reply(dpp::ir_update_message,
                        dpp::message().add_embed(bronx::error("Failed to upgrade bank limit")).set_flags(dpp::m_ephemeral));
                }
            } else {
                event.reply(dpp::ir_update_message,
                    dpp::message().add_embed(bronx::error("Failed to create loan")).set_flags(dpp::m_ephemeral));
            }
            return;
        }
        
        // Handle loan cancellation
        if (custom_id.find("bank_cancel_loan_") == 0) {
            event.reply(dpp::ir_update_message,
                dpp::message().add_embed(bronx::info("Loan cancelled")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Handle loan payments
        if (custom_id.find("bank_loan_pay") == 0) {
            auto user_data = db->get_user(user_id);
            auto loan = db->get_loan(user_id);
            
            if (!user_data || !loan) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Failed to load loan data")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            int64_t payment_amount = 0;
            
            if (custom_id.find("bank_loan_pay25_") == 0) {
                payment_amount = loan->remaining / 4;
            } else if (custom_id.find("bank_loan_pay50_") == 0) {
                payment_amount = loan->remaining / 2;
            } else if (custom_id.find("bank_loan_payall_") == 0) {
                payment_amount = loan->remaining;
            }
            
            if (payment_amount <= 0 || payment_amount > user_data->wallet) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Insufficient funds for this payment")).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Make the payment
            auto new_balance = db->make_loan_payment(user_id, payment_amount);
            if (new_balance) {
                db->update_wallet(user_id, -payment_amount);
                
                std::string msg;
                if (*new_balance == 0) {
                    msg = "**Loan Paid Off!**\n\nYou paid " + format_money(payment_amount) + " and cleared your debt!";
                    log_balance_change(db, user_id, "paid off loan with final payment of " + format_money(payment_amount));
                } else {
                    msg = "**Payment Successful!**\n\nYou paid " + format_money(payment_amount) + "\nRemaining: " + format_money(*new_balance);
                    log_balance_change(db, user_id, "made loan payment of " + format_money(payment_amount));
                }
                
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success(msg)).set_flags(dpp::m_ephemeral));
                
                update_bank_message(bot, user_id, channel_id, message_id, "loan", db);
            } else {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("Failed to process payment")).set_flags(dpp::m_ephemeral));
            }
            return;
        }
        
        // Handle deposit/withdraw (open modal for amount input - simplified: just use default amounts)
        if (custom_id.find("bank_deposit_") == 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::info("Use `.bank deposit <amount>` or `.dep <amount>` to deposit money")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (custom_id.find("bank_withdraw_") == 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::info("Use `.bank withdraw <amount>` or `.with <amount>` to withdraw money")).set_flags(dpp::m_ephemeral));
            return;
        }
    });
}

// Create bank command
inline Command* create_bank_command(Database* db) {
    auto cmd = new Command("bank", "manage your bank account with deposits, withdrawals, upgrades, and loans",
                          "economy", {"dep", "d", "deposit", "with", "w", "withdraw"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;
            
            // If no args, show interactive menu
            if (args.empty()) {
                auto embed = create_bank_menu_embed(*user, db);
                
                dpp::message msg(event.msg.channel_id, embed);
                
                // Main menu buttons
                dpp::component action_row1;
                action_row1.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Deposit")
                    .set_style(dpp::cos_primary)
                    .set_id("bank_deposit_" + ::std::to_string(event.msg.author.id)));
                
                action_row1.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Withdraw")
                    .set_style(dpp::cos_primary)
                    .set_id("bank_withdraw_" + ::std::to_string(event.msg.author.id)));
                
                action_row1.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Upgrade")
                    .set_style(dpp::cos_success)
                    .set_id("bank_upgrade_menu_" + ::std::to_string(event.msg.author.id)));
                
                dpp::component action_row2;
                action_row2.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Manage Loan")
                    .set_style(dpp::cos_secondary)
                    .set_id("bank_loan_menu_" + ::std::to_string(event.msg.author.id)));
                
                action_row2.add_component(dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("Close")
                    .set_style(dpp::cos_danger)
                    .set_id("bank_close_" + ::std::to_string(event.msg.author.id)));
                
                msg.add_component(action_row1);
                msg.add_component(action_row2);
                
                bot.message_create(msg, [user_id = event.msg.author.id](const dpp::confirmation_callback_t& callback) {
                    if (!callback.is_error()) {
                        auto created_msg = ::std::get<dpp::message>(callback.value);
                        active_bank_sessions[user_id] = {created_msg.id, created_msg.channel_id};
                    }
                });
                return;
            }
            
            // Parse old-style text commands
            ::std::string subcmd = args[0];
            ::std::transform(subcmd.begin(), subcmd.end(), subcmd.begin(), ::tolower);
            
            // Check if using shorthand deposit/withdraw (first arg is a number/amount, not a subcommand)
            bool is_deposit_shorthand = false;
            bool is_withdraw_shorthand = false;
            
            // If first arg is not a recognized subcommand, try to parse as amount
            if (subcmd != "deposit" && subcmd != "dep" && subcmd != "d" && 
                subcmd != "withdraw" && subcmd != "with" && subcmd != "w" &&
                subcmd != "upgrade") {
                // Try to detect if it's an amount (starts with digit, or is "all", "half", etc.)
                if (!args[0].empty() && (std::isdigit(args[0][0]) || args[0] == "all" || 
                    args[0] == "half" || args[0] == "max" || args[0].find('%') != std::string::npos ||
                    args[0].find('k') != std::string::npos || args[0].find('m') != std::string::npos)) {
                    is_deposit_shorthand = true;  // Default to deposit for shortcuts like .d, .dep
                }
            }
            
            // Handle deposit (explicit or shorthand)
            if (subcmd == "deposit" || subcmd == "dep" || subcmd == "d" || is_deposit_shorthand) {
                ::std::string amount_str;
                if (is_deposit_shorthand) {
                    amount_str = args[0];  // First arg is the amount
                } else if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("Usage: `.bank deposit <amount>` or `.d <amount>`"));
                    return;
                } else {
                    amount_str = args[1];  // Second arg is the amount
                }
                
                int64_t amount;
                try {
                    amount = parse_amount(amount_str, user->wallet);
                } catch (const std::invalid_argument& e) {
                    bronx::send_message(bot, event, bronx::error(e.what()));
                    return;
                }
                
                if (amount <= 0) {
                    bronx::send_message(bot, event, bronx::error("Amount must be positive"));
                    return;
                }
                if (amount > user->wallet) {
                    bronx::send_message(bot, event, bronx::error("You don't have that much in your wallet"));
                    return;
                }
                if (user->bank + amount > user->bank_limit) {
                    bronx::send_message(bot, event, bronx::error("Deposit would exceed your bank limit of " + format_money(user->bank_limit)));
                    return;
                }
                
                if (db->deposit(event.msg.author.id, amount)) {
                    log_balance_change(db, event.msg.author.id, "deposited " + format_money(amount) + " to bank");
                    auto embed = bronx::success("Deposited " + format_money(amount) + " into your bank");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("Failed to deposit"));
                }
                return;
            }
            
            // Handle withdraw
            if (subcmd == "withdraw" || subcmd == "with" || subcmd == "w") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("Usage: `.bank withdraw <amount>` or `.w <amount>`"));
                    return;
                }
                
                int64_t amount;
                try {
                    amount = parse_amount(args[1], user->bank);
                } catch (const std::invalid_argument& e) {
                    bronx::send_message(bot, event, bronx::error(e.what()));
                    return;
                }
                
                if (amount <= 0) {
                    bronx::send_message(bot, event, bronx::error("Amount must be positive"));
                    return;
                }
                if (amount > user->bank) {
                    bronx::send_message(bot, event, bronx::error("You don't have that much in your bank"));
                    return;
                }
                
                if (db->withdraw(event.msg.author.id, amount)) {
                    log_balance_change(db, event.msg.author.id, "withdrew " + format_money(amount) + " from bank");
                    auto embed = bronx::success("Withdrew " + format_money(amount) + " from your bank");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("Failed to withdraw"));
                }
                return;
            }
            
            // Handle upgrade via text command: .bank upgrade <amount|max>
            if (subcmd == "upgrade") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("Usage: `.bank upgrade <amount>` or `.bank upgrade max`\n\nRate: $1 spent = $" + std::to_string(UPGRADE_RATIO) + " bank space"));
                    return;
                }
                
                ::std::string amount_str = args[1];
                ::std::transform(amount_str.begin(), amount_str.end(), amount_str.begin(), ::tolower);
                
                int64_t bank_increase;
                int64_t cost;
                
                if (amount_str == "max" || amount_str == "all") {
                    if (user->wallet <= 0) {
                        bronx::send_message(bot, event, bronx::error("You have no money in your wallet!"));
                        return;
                    }
                    cost = user->wallet;
                    bank_increase = max_upgrade_for_wallet(cost);
                } else {
                    // Parse the amount as bank space to add
                    try {
                        bank_increase = parse_amount(amount_str, max_upgrade_for_wallet(user->wallet));
                    } catch (const std::invalid_argument& e) {
                        bronx::send_message(bot, event, bronx::error(e.what()));
                        return;
                    }
                    
                    if (bank_increase <= 0) {
                        bronx::send_message(bot, event, bronx::error("Amount must be positive"));
                        return;
                    }
                    
                    cost = upgrade_cost_for(bank_increase);
                    
                    if (cost > user->wallet) {
                        bronx::send_message(bot, event, bronx::error("Not enough money! You need " + format_money(cost) + " but only have " + format_money(user->wallet)));
                        return;
                    }
                }
                
                int64_t new_limit = user->bank_limit + bank_increase;
                
                if (db->update_bank_limit(event.msg.author.id, new_limit)) {
                    db->update_wallet(event.msg.author.id, -cost);
                    log_balance_change(db, event.msg.author.id, "upgraded bank limit by " + format_money(bank_increase) + " to " + format_money(new_limit) + " for " + format_money(cost));
                    
                    auto embed = bronx::success(
                        "**Bank upgraded!**\n\n"
                        "Limit: " + format_money(user->bank_limit) + " → " + format_money(new_limit) + "\n"
                        "Space added: +" + format_money(bank_increase) + "\n"
                        "Cost: " + format_money(cost));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("Failed to upgrade bank limit"));
                }
                return;
            }
            
            // Unknown subcommand - show help
            bronx::send_message(bot, event, bronx::error("Unknown command. Use `.bank` for the interactive menu, `.d <amount>` to deposit, or `.w <amount>` to withdraw"));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("User not found")));
                return;
            }
            
            // Show interactive menu
            auto embed = create_bank_menu_embed(*user, db);
            dpp::message msg;
            msg.add_embed(embed);
            
            // Main menu buttons
            dpp::component action_row1;
            action_row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Deposit")
                .set_style(dpp::cos_primary)
                .set_id("bank_deposit_" + ::std::to_string(event.command.get_issuing_user().id)));
            
            action_row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Withdraw")
                .set_style(dpp::cos_primary)
                .set_id("bank_withdraw_" + ::std::to_string(event.command.get_issuing_user().id)));
            
            action_row1.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Upgrade")
                .set_style(dpp::cos_success)
                .set_id("bank_upgrade_menu_" + ::std::to_string(event.command.get_issuing_user().id)));
            
            dpp::component action_row2;
            action_row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Manage Loan")
                .set_style(dpp::cos_secondary)
                .set_id("bank_loan_menu_" + ::std::to_string(event.command.get_issuing_user().id)));
            
            action_row2.add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Close")
                .set_style(dpp::cos_danger)
                .set_id("bank_close_" + ::std::to_string(event.command.get_issuing_user().id)));
            
            msg.add_component(action_row1);
            msg.add_component(action_row2);
            
            event.reply(msg, [&bot, user_id = event.command.get_issuing_user().id, token = event.command.token](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) return;
                // event.reply() returns confirmation, not message — fetch the original response to get the message ID
                bot.interaction_response_get_original(token, [user_id](const dpp::confirmation_callback_t& resp) {
                    if (resp.is_error()) return;
                    auto created_msg = resp.get<dpp::message>();
                    active_bank_sessions[user_id] = {created_msg.id, created_msg.channel_id};
                });
            });
        },
        {}  // No slash command options - we use the interactive UI
    );
    return cmd;
}

} // namespace economy
} // namespace commands
