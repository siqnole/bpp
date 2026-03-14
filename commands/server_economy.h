#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/economy/server_economy_operations.h"
#include "../database/operations/moderation/permission_operations.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>

using namespace bronx::db;
using namespace bronx::db::server_economy_operations;

namespace commands {
namespace server_economy {

inline Command* create_servereconomy_command(Database* db) {
    return new Command(
        "servereconomy",
        "Manage server-specific economy settings",
        "admin",
        {"se", "serverecon"},
        true,
        // Text handler (not used for this command)
        nullptr,
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!event.command.guild_id) {
                event.reply(dpp::message("This command can only be used in servers!").set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t guild_id = event.command.guild_id;
            uint64_t user_id = event.command.usr.id;

            // Check admin permissions via Discord perms
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " Could not find guild.").set_flags(dpp::m_ephemeral));
                return;
            }
            auto member_it = g->members.find(user_id);
            if (member_it == g->members.end()) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " Member data not found.").set_flags(dpp::m_ephemeral));
                return;
            }
            dpp::permission perms = g->base_permissions(member_it->second);
            if (!perms.can(dpp::p_administrator)) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " You need administrator permissions to use this command!").set_flags(dpp::m_ephemeral));
                return;
            }

            auto subcommand = event.command.get_command_interaction().options[0];

            if (subcommand.name == "toggle") {
                // --- TOGGLE ---
                std::string mode = std::get<std::string>(subcommand.options[0].value);

                if (set_economy_mode(db, guild_id, mode)) {
                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(bronx::EMOJI_CHECK + " Economy Mode Updated")
                        .set_description(mode == "server"
                            ? "This server now uses a **server-specific economy**!\n\n"
                              "All economy commands will now operate on server-specific balances.\n"
                              "Users start fresh with configured starting amounts.\n\n"
                              "Use `/servereconomy config` to customize settings."
                            : "This server now uses the **global economy**!\n\n"
                              "All economy commands will use global user balances shared across all servers.")
                        .set_footer(dpp::embed_footer().set_text("Server Economy System"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to update economy mode. Please try again.").set_flags(dpp::m_ephemeral));
                }

            } else if (subcommand.name == "status") {
                // --- STATUS ---
                auto settings = get_guild_economy_settings(db, guild_id);

                if (!settings) {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to retrieve economy settings.").set_flags(dpp::m_ephemeral));
                    return;
                }

                dpp::embed embed = dpp::embed()
                    .set_color(settings->economy_mode == "server" ? bronx::COLOR_INFO : 0x95a5a6)
                    .set_title("\xF0\x9F\x8F\xA6 Server Economy Settings")
                    .add_field(
                        "Economy Mode",
                        settings->economy_mode == "server" ? "\xF0\x9F\x8C\x90 **Server Economy**" : "\xF0\x9F\x8C\x8D **Global Economy**",
                        false
                    );

                if (settings->economy_mode == "server") {
                    std::ostringstream starting_vals;
                    starting_vals << "\xF0\x9F\x92\xB5 Wallet: **" << settings->starting_wallet << "**\n"
                                  << "\xF0\x9F\x8F\xA6 Bank Limit: **" << settings->starting_bank_limit << "**\n"
                                  << "\xF0\x9F\x93\x88 Interest Rate: **" << settings->default_interest_rate << "%**";
                    embed.add_field("Starting Values", starting_vals.str(), true);

                    std::ostringstream multipliers;
                    multipliers << "\xF0\x9F\x92\xBC Work: **" << settings->work_multiplier << "x**\n"
                                << "\xF0\x9F\x8E\xB0 Gambling: **" << settings->gambling_multiplier << "x**\n"
                                << "\xF0\x9F\x8E\xA3 Fishing: **" << settings->fishing_multiplier << "x**";
                    embed.add_field("Multipliers", multipliers.str(), true);

                    std::string features;
                    features += (settings->allow_gambling ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + std::string(" Gambling\n");
                    features += (settings->allow_fishing ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + std::string(" Fishing\n");
                    features += (settings->allow_trading ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + std::string(" Trading\n");
                    features += (settings->allow_robbery ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + std::string(" Robbery");
                    embed.add_field("Features", features, true);

                    if (settings->enable_tax) {
                        std::ostringstream tax_info;
                        tax_info << bronx::EMOJI_CHECK << " Enabled\n\xF0\x9F\x92\xB8 Rate: **" << settings->transaction_tax_percent << "%**";
                        embed.add_field("Tax System", tax_info.str(), false);
                    }
                }

                embed.set_footer(dpp::embed_footer().set_text("Use /servereconomy config to modify settings"))
                    .set_timestamp(time(0));

                event.reply(dpp::message().add_embed(embed));

            } else if (subcommand.name == "config") {
                // --- CONFIG ---
                auto settings = get_guild_economy_settings(db, guild_id);

                if (!settings) {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to retrieve economy settings.").set_flags(dpp::m_ephemeral));
                    return;
                }

                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET ";
                std::vector<std::string> updates;

                for (const auto& opt : subcommand.options) {
                    if (opt.name == "starting_wallet") {
                        int64_t value = std::get<int64_t>(opt.value);
                        updates.push_back("starting_wallet = " + std::to_string(value));
                    } else if (opt.name == "starting_bank_limit") {
                        int64_t value = std::get<int64_t>(opt.value);
                        updates.push_back("starting_bank_limit = " + std::to_string(value));
                    } else if (opt.name == "work_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("work_multiplier = " + std::to_string(value));
                    } else if (opt.name == "gambling_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("gambling_multiplier = " + std::to_string(value));
                    } else if (opt.name == "fishing_multiplier") {
                        double value = std::get<double>(opt.value);
                        updates.push_back("fishing_multiplier = " + std::to_string(value));
                    }
                }

                if (updates.empty()) {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " No settings to update.").set_flags(dpp::m_ephemeral));
                    db->get_pool()->release(conn);
                    return;
                }

                query += updates[0];
                for (size_t i = 1; i < updates.size(); i++) {
                    query += ", " + updates[i];
                }
                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(bronx::EMOJI_CHECK + " Economy Settings Updated")
                        .set_description("Server economy configuration has been updated successfully!")
                        .set_footer(dpp::embed_footer().set_text("Use /servereconomy status to view all settings"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to update settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);

            } else if (subcommand.name == "features") {
                // --- FEATURES ---
                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET ";
                std::vector<std::string> updates;
                std::vector<std::string> changes;

                for (const auto& opt : subcommand.options) {
                    bool value = std::get<bool>(opt.value);
                    std::string db_value = value ? "TRUE" : "FALSE";

                    if (opt.name == "gambling") {
                        updates.push_back("allow_gambling = " + db_value);
                        changes.push_back((value ? bronx::EMOJI_CHECK + " Enabled" : bronx::EMOJI_DENY + " Disabled") + std::string(" gambling"));
                    } else if (opt.name == "fishing") {
                        updates.push_back("allow_fishing = " + db_value);
                        changes.push_back((value ? bronx::EMOJI_CHECK + " Enabled" : bronx::EMOJI_DENY + " Disabled") + std::string(" fishing"));
                    } else if (opt.name == "trading") {
                        updates.push_back("allow_trading = " + db_value);
                        changes.push_back((value ? bronx::EMOJI_CHECK + " Enabled" : bronx::EMOJI_DENY + " Disabled") + std::string(" trading"));
                    } else if (opt.name == "robbery") {
                        updates.push_back("allow_robbery = " + db_value);
                        changes.push_back((value ? bronx::EMOJI_CHECK + " Enabled" : bronx::EMOJI_DENY + " Disabled") + std::string(" robbery"));
                    }
                }

                if (updates.empty()) {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " No features to update.").set_flags(dpp::m_ephemeral));
                    db->get_pool()->release(conn);
                    return;
                }

                query += updates[0];
                for (size_t i = 1; i < updates.size(); i++) {
                    query += ", " + updates[i];
                }
                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    std::string description;
                    for (const auto& change : changes) {
                        description += change + "\n";
                    }

                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(bronx::EMOJI_CHECK + " Feature Settings Updated")
                        .set_description(description)
                        .set_footer(dpp::embed_footer().set_text("Server Economy System"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to update feature settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);

            } else if (subcommand.name == "tax") {
                // --- TAX ---
                bool enabled = std::get<bool>(subcommand.options[0].value);
                double rate = 0.0;

                if (subcommand.options.size() > 1) {
                    rate = std::get<double>(subcommand.options[1].value);
                    if (rate < 0 || rate > 100) {
                        event.reply(dpp::message(bronx::EMOJI_DENY + " Tax rate must be between 0 and 100.").set_flags(dpp::m_ephemeral));
                        return;
                    }
                }

                auto conn = db->get_pool()->acquire();
                std::string query = "UPDATE guild_settings SET enable_tax = " +
                                  std::string(enabled ? "TRUE" : "FALSE");

                if (subcommand.options.size() > 1) {
                    query += ", transaction_tax_percent = " + std::to_string(rate);
                }

                query += " WHERE guild_id = " + std::to_string(guild_id);

                if (mysql_query(conn->get(), query.c_str()) == 0) {
                    std::ostringstream desc;
                    if (enabled) {
                        desc << "Transaction tax is now **enabled**\n\xF0\x9F\x92\xB8 Rate: **" << rate << "%**\n\n"
                             << "This tax will be applied to all player-to-player transfers.";
                    } else {
                        desc << "Transaction tax has been **disabled**.";
                    }

                    dpp::embed embed = dpp::embed()
                        .set_color(bronx::COLOR_SUCCESS)
                        .set_title(enabled ? bronx::EMOJI_CHECK + " Tax System Enabled" : "\xF0\x9F\x94\x93 Tax System Disabled")
                        .set_description(desc.str())
                        .set_footer(dpp::embed_footer().set_text("Server Economy System"))
                        .set_timestamp(time(0));

                    event.reply(dpp::message().add_embed(embed));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " Failed to update tax settings.").set_flags(dpp::m_ephemeral));
                }

                db->get_pool()->release(conn);
            }
        },
        // Slash command options (subcommands)
        {
            dpp::command_option(dpp::co_sub_command, "toggle", "Toggle between global and server economy")
                .add_option(dpp::command_option(dpp::co_string, "mode", "Economy mode", true)
                    .add_choice(dpp::command_option_choice("Global Economy", std::string("global")))
                    .add_choice(dpp::command_option_choice("Server Economy", std::string("server")))
                ),
            dpp::command_option(dpp::co_sub_command, "status", "View current server economy settings"),
            dpp::command_option(dpp::co_sub_command, "config", "Configure server economy settings")
                .add_option(dpp::command_option(dpp::co_integer, "starting_wallet", "Starting wallet amount", false))
                .add_option(dpp::command_option(dpp::co_integer, "starting_bank_limit", "Starting bank limit", false))
                .add_option(dpp::command_option(dpp::co_number, "work_multiplier", "Work earnings multiplier", false))
                .add_option(dpp::command_option(dpp::co_number, "gambling_multiplier", "Gambling multiplier", false))
                .add_option(dpp::command_option(dpp::co_number, "fishing_multiplier", "Fishing rewards multiplier", false)),
            dpp::command_option(dpp::co_sub_command, "features", "Toggle economy features")
                .add_option(dpp::command_option(dpp::co_boolean, "gambling", "Allow gambling", false))
                .add_option(dpp::command_option(dpp::co_boolean, "fishing", "Allow fishing", false))
                .add_option(dpp::command_option(dpp::co_boolean, "trading", "Allow trading", false))
                .add_option(dpp::command_option(dpp::co_boolean, "robbery", "Allow robbery", false)),
            dpp::command_option(dpp::co_sub_command, "tax", "Configure transaction tax")
                .add_option(dpp::command_option(dpp::co_boolean, "enabled", "Enable tax system", true))
                .add_option(dpp::command_option(dpp::co_number, "rate", "Tax percentage (0-100)", false))
        }
    );
}

// Get all server economy commands
inline std::vector<Command*> get_server_economy_commands(Database* db) {
    static std::vector<Command*> cmds;
    static bool initialized = false;

    if (!initialized) {
        cmds.push_back(create_servereconomy_command(db));
        initialized = true;
    }

    return cmds;
}

} // namespace server_economy
} // namespace commands
