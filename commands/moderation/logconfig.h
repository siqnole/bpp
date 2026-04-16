#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../database/operations/moderation/logging_operations.h"
#include "../../server_logger.h"
#include "../../feature_gate.h"
#include <iostream>

namespace commands {
namespace moderation {

inline Command* get_log_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "log", "configure server logging", "moderation",
        {}, true,
        // text handler - ignore text for this advanced setup command
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            bronx::send_message(bot, event, bronx::error("Please use the `/log` slash command to configure logging."));
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            // permission check (only admins should configure this)
            bool has_perm = bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_administrator)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to configure logging")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // check feature gate (replaces old beta_tester flag)
            FEATURE_CHECK_SLASH(event, "logging");

            // defer response since channel/webhook creation might take a few seconds
            event.thinking(true);

            auto subcmd = event.command.get_command_interaction().options[0];
            std::string sub_name = subcmd.name;

            if (sub_name == "aio") {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("Guild not found in cache.")));
                    return;
                }

                auto existing = bronx::db::logging_operations::get_all_log_configs(db, guild_id);
                if (!existing.empty()) {
                    // Check user input: should we error out or overwrite?
                    // For now, let's error if there's already configs. They can clear manually.
                    event.edit_original_response(dpp::message().add_embed(bronx::error("You already have log configurations. Please use `/log clear` first if you want to run AIO setup.")));
                    return;
                }

                // 1. Create Category `Bronx Logs`
                dpp::channel cat;
                cat.name = "Bronx Logs";
                cat.guild_id = guild_id;
                cat.set_type(dpp::CHANNEL_CATEGORY);

                // Restrict permissions for category
                cat.permission_overwrites.push_back(dpp::permission_overwrite(
                    guild_id, 0, dpp::p_view_channel, dpp::ot_role
                ));

                bot.channel_create(cat, [&bot, db, guild_id, event](const dpp::confirmation_callback_t& cb_cat) {
                    if (cb_cat.is_error()) {
                        event.edit_original_response(dpp::message().add_embed(bronx::error("Failed to create log category: " + cb_cat.get_error().message)));
                        return;
                    }
                    dpp::channel created_cat = std::get<dpp::channel>(cb_cat.value);
                    uint64_t cat_id = created_cat.id;

                    std::vector<std::string> types = {
                        bronx::logger::LOG_TYPE_MODERATION,
                        bronx::logger::LOG_TYPE_MESSAGES,
                        bronx::logger::LOG_TYPE_MEMBERS,
                        bronx::logger::LOG_TYPE_SERVER,
                        bronx::logger::LOG_TYPE_ECONOMY
                    };

                    // Function to create channels recursively to avoid rate limits / callback hell
                    auto create_next_channel = std::make_shared<std::function<void(size_t)>>();
                    *create_next_channel = [&bot, db, guild_id, cat_id, event, types, create_next_channel](size_t index) {
                        if (index >= types.size()) {
                            event.edit_original_response(dpp::message().add_embed(bronx::success("AIO Logging setup completed successfully!")));
                            return;
                        }

                        dpp::channel ch;
                        ch.name = types[index] + "-logs";
                        ch.guild_id = guild_id;
                        ch.parent_id = cat_id;
                        ch.set_type(dpp::CHANNEL_TEXT);
                        
                        bot.channel_create(ch, [&bot, db, guild_id, index, types, create_next_channel, event](const dpp::confirmation_callback_t& cb_ch) {
                            if (cb_ch.is_error()) {
                                std::cerr << "Failed to create channel for " << types[index] << ": " << cb_ch.get_error().message << std::endl;
                                (*create_next_channel)(index + 1);
                                return;
                            }
                            dpp::channel created_ch = std::get<dpp::channel>(cb_ch.value);
                            
                            // create webhook
                            dpp::webhook wh;
                            wh.channel_id = created_ch.id;
                            wh.name = "Bronx Bot - " + types[index] + " logger";
                            
                            bot.create_webhook(wh, [&bot, db, guild_id, index, types, create_next_channel, created_ch, event](const dpp::confirmation_callback_t& cb_wh) {
                                if (cb_wh.is_error()) {
                                    std::cerr << "Failed to create webhook for " << types[index] << ": " << cb_wh.get_error().message << std::endl;
                                    (*create_next_channel)(index + 1);
                                    return;
                                }
                                dpp::webhook created_wh = std::get<dpp::webhook>(cb_wh.value);
                                
                                // save to db
                                bronx::db::LogConfig cfg;
                                cfg.guild_id = guild_id;
                                cfg.log_type = types[index];
                                cfg.channel_id = created_ch.id;
                                cfg.webhook_url = "https://discord.com/api/webhooks/" + std::to_string(created_wh.id) + "/" + created_wh.token;
                                cfg.webhook_id = created_wh.id;
                                cfg.webhook_token = created_wh.token;
                                cfg.enabled = true;
                                
                                bronx::db::logging_operations::set_log_config(db, cfg);
                                
                                // Send test message
                                dpp::message test_msg;
                                test_msg.add_embed(bronx::create_embed("Logger successfully hooked up.", bronx::COLOR_SUCCESS));
                                dpp::webhook execute_wh(created_wh.id, created_wh.token);
                                bot.execute_webhook(execute_wh, test_msg);
                                
                                (*create_next_channel)(index + 1);
                            });
                        });
                    };

                    (*create_next_channel)(0);
                });
            } else if (sub_name == "clear") {
                if (bronx::db::logging_operations::clear_all_log_configs(db, guild_id)) {
                    event.edit_original_response(dpp::message().add_embed(bronx::success("All Discord log configurations have been cleared from the database.\n*(Channels/Webhooks remain open, please delete them manually if desired)*")));
                } else {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("Database error while clearing configurations.")));
                }
            } else {
                event.edit_original_response(dpp::message().add_embed(bronx::error("Unknown sub-command")));
            }
        },
        // options
        {
            dpp::command_option(dpp::co_sub_command, "aio", "All-In-One setup: Generates a 'Bronx Logs' category with separated private log channels and webhooks"),
            dpp::command_option(dpp::co_sub_command, "clear", "Clear all existing log configurations in the database")
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
