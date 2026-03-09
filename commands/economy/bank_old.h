#pragma once
#include "helpers.h"

namespace commands {
namespace economy {

inline Command* create_bank_command(Database* db) {
    static std::vector<int64_t> upgrade_costs = {
        250,500,750,1000,1500,2000,
        3000,5000,7500,10000,15000,
        25000,50000,75000,100000,150000,
        250000,500000,750000,1000000
    };
    auto is_upgrade_amount = [](int64_t amt) {
        return std::find(upgrade_costs.begin(), upgrade_costs.end(), amt) != upgrade_costs.end();
    };

    static Command* bank_cmd = new Command("bank", "deposit money into your bank or upgrade your bank limit",
                                           "economy", {"dep", "d"}, true,
        [db, is_upgrade_amount](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("please specify an amount or `upgrade <amount>`"));
                return;
            }

            auto user = db->get_user(event.msg.author.id);
            if (!user) return;

            bool doing_upgrade = false;
            bool do_max_upgrade = false;
            int64_t amount = 0;
            if (args[0] == "upgrade") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("please specify an amount to upgrade or 'max'"));
                    return;
                }
                doing_upgrade = true;
                
                // Check for max/all keyword
                ::std::string upgrade_arg = args[1];
                ::std::transform(upgrade_arg.begin(), upgrade_arg.end(), upgrade_arg.begin(), ::tolower);
                if (upgrade_arg == "max" || upgrade_arg == "all") {
                    do_max_upgrade = true;
                } else {
                    try {
                        amount = parse_amount(args[1], user->wallet);
                    } catch (const std::invalid_argument& e) {
                        bronx::send_message(bot, event, bronx::error(e.what()));
                        return;
                    }
                }
            } else {
                try {
                    amount = parse_amount(args[0], user->wallet);
                } catch (const std::invalid_argument& e) {
                    bronx::send_message(bot, event, bronx::error(e.what()));
                    return;
                }
            }

            if (!do_max_upgrade) {
                if (amount <= 0) {
                    bronx::send_message(bot, event, bronx::error("amount must be positive"));
                    return;
                }
                if (amount > user->wallet) {
                    bronx::send_message(bot, event, bronx::error("you don't have that much in your wallet"));
                    return;
                }
            }

            if (doing_upgrade) {
                if (do_max_upgrade) {
                    // Auto-upgrade as much as possible
                    int64_t total_spent = 0;
                    int64_t total_increase = 0;
                    int upgrades_done = 0;
                    int64_t remaining_wallet = user->wallet;
                    
                    for (int64_t cost : upgrade_costs) {
                        while (remaining_wallet >= cost) {
                            remaining_wallet -= cost;
                            total_spent += cost;
                            total_increase += cost;
                            upgrades_done++;
                        }
                    }
                    
                    if (upgrades_done == 0) {
                        bronx::send_message(bot, event, bronx::error("you don't have enough money for any upgrades (cheapest: $" + format_number(upgrade_costs[0]) + ")"));
                        return;
                    }
                    
                    int64_t new_limit = user->bank_limit + total_increase;
                    if (db->update_bank_limit(event.msg.author.id, new_limit)) {
                        db->update_wallet(event.msg.author.id, -total_spent);
                        auto embed = bronx::success("upgraded your bank limit " + ::std::to_string(upgrades_done) + " time(s)!\n" +
                            "new limit: $" + format_number(new_limit) + "\n" +
                            "total spent: $" + format_number(total_spent));
                        bronx::add_invoker_footer(embed, event.msg.author);
                        bronx::send_message(bot, event, embed);
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to upgrade bank limit"));
                    }
                    return;
                }
                
                if (!is_upgrade_amount(amount)) {
                    bronx::send_message(bot, event, bronx::error("invalid upgrade amount. valid amounts: " + 
                        ::std::to_string(upgrade_costs[0]) + ", " + ::std::to_string(upgrade_costs[1]) + ", etc."));
                    return;
                }
                // perform upgrade: increase bank_limit by the amount
                int64_t new_limit = user->bank_limit + amount;
                if (db->update_bank_limit(event.msg.author.id, new_limit)) {
                    // deduct cost from wallet
                    db->update_wallet(event.msg.author.id, -amount);
                    auto embed = bronx::success("bank limit increased to $" + format_number(new_limit) +
                        " for $" + format_number(amount));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to upgrade bank limit"));
                }
                return;
            }

            // normal deposit flow
            if (user->bank + amount > user->bank_limit) {
                bronx::send_message(bot, event, bronx::error("deposit would exceed your bank limit of $" + format_number(user->bank_limit) + ". consider upgrading your bank limit with `bank upgrade <amount>`"));
                return;
            }
            
            if (db->deposit(event.msg.author.id, amount)) {
                log_balance_change(db, event.msg.author.id, "deposited $" + format_number(amount) + " to bank");
                auto embed = bronx::success("deposited $" + format_number(amount) + " into your bank");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to deposit"));
            }
        },
        [db, is_upgrade_amount](dpp::cluster& bot, const dpp::slashcommand_t& event) {
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
            bool upgrade_flag = false;
            if (std::holds_alternative<bool>(event.get_parameter("upgrade"))) {
                upgrade_flag = std::get<bool>(event.get_parameter("upgrade"));
            }

            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }

            // Check for max/all keyword
            ::std::string amount_lower = amount_str;
            ::std::transform(amount_lower.begin(), amount_lower.end(), amount_lower.begin(), ::tolower);
            bool do_max_upgrade = (upgrade_flag && (amount_lower == "max" || amount_lower == "all"));

            int64_t amount = 0;
            if (!do_max_upgrade) {
                try {
                    amount = parse_amount(amount_str, user->wallet);
                } catch (const std::invalid_argument& e) {
                    event.reply(dpp::message().add_embed(bronx::error(e.what())));
                    return;
                }

                if (amount <= 0) {
                    event.reply(dpp::message().add_embed(bronx::error("amount must be positive")));
                    return;
                }
                if (amount > user->wallet) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have that much in your wallet")));
                    return;
                }
            }

            if (upgrade_flag) {
                if (do_max_upgrade) {
                    // Auto-upgrade as much as possible
                    int64_t total_spent = 0;
                    int64_t total_increase = 0;
                    int upgrades_done = 0;
                    int64_t remaining_wallet = user->wallet;
                    
                    for (int64_t cost : upgrade_costs) {
                        while (remaining_wallet >= cost) {
                            remaining_wallet -= cost;
                            total_spent += cost;
                            total_increase += cost;
                            upgrades_done++;
                        }
                    }
                    
                    if (upgrades_done == 0) {
                        event.reply(dpp::message().add_embed(bronx::error("you don't have enough money for any upgrades (cheapest: $" + format_number(upgrade_costs[0]) + ")")));
                        return;
                    }
                    
                    int64_t new_limit = user->bank_limit + total_increase;
                    if (db->update_bank_limit(event.command.get_issuing_user().id, new_limit)) {
                        db->update_wallet(event.command.get_issuing_user().id, -total_spent);
                        auto embed = bronx::success("upgraded your bank limit " + ::std::to_string(upgrades_done) + " time(s)!\n" +
                            "new limit: $" + format_number(new_limit) + "\n" +
                            "total spent: $" + format_number(total_spent));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.reply(dpp::message().add_embed(embed));
                    } else {
                        event.reply(dpp::message().add_embed(bronx::error("failed to upgrade bank limit")));
                    }
                    return;
                }
                
                if (!is_upgrade_amount(amount)) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid upgrade amount. valid amounts: " + 
                        ::std::to_string(upgrade_costs[0]) + ", " + ::std::to_string(upgrade_costs[1]) + ", etc.")));
                    return;
                }
                int64_t new_limit = user->bank_limit + amount;
                if (db->update_bank_limit(event.command.get_issuing_user().id, new_limit)) {
                    db->update_wallet(event.command.get_issuing_user().id, -amount);
                    auto embed = bronx::success("bank limit increased to $" + format_number(new_limit) +
                        " for $" + format_number(amount));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("failed to upgrade bank limit")));
                }
            } else {
                if (user->bank + amount > user->bank_limit) {
                    event.reply(dpp::message().add_embed(bronx::error("deposit would exceed your bank limit of $" + format_number(user->bank_limit) + " consider upgrading your bank limit with `bank upgrade <amount>`")));
                    return;
                }
                if (db->deposit(event.command.get_issuing_user().id, amount)) {
                    log_balance_change(db, event.command.get_issuing_user().id, "deposited $" + format_number(amount) + " to bank");
                    auto embed = bronx::success("deposited $" + format_number(amount) + " into your bank");
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("failed to deposit")));
                }
            }
        },
        {
            dpp::command_option(dpp::co_string, "amount", "amount to deposit or upgrade (supports all, half, 50%, 1k, etc)", true),
            dpp::command_option(dpp::co_boolean, "upgrade", "treat the amount as a bank-limit upgrade", false)
        });
    return bank_cmd;
}

} // namespace economy
} // namespace commands
