#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_purge_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "purge", "delete multiple messages in the current channel", "moderation",
        {"clear", "clean"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_manage_messages)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to manage messages"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `purge <amount> [@user]`"));
                return;
            }

            int amount = 0;
            try { amount = std::stoi(args[0]); } catch (...) {}
            if (amount < 1 || amount > 100) {
                bronx::send_message(bot, event, bronx::error("amount must be between 1 and 100"));
                return;
            }

            uint64_t target_user = 0;
            if (args.size() > 1) {
                target_user = parse_mention(args[1]);
            }

            // fetch messages
            bot.messages_get(event.msg.channel_id, 0, 0, 0, amount + 1, [guild_id, mod_id, target_user, &bot, event](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    bronx::send_message(bot, event, bronx::error("failed to fetch messages"));
                    return;
                }
                auto msgs = std::get<dpp::message_map>(callback.value);
                std::vector<dpp::snowflake> to_delete;
                for (const auto& [id, msg] : msgs) {
                    // skip messages older than 14 days (discord limitation for bulk delete)
                    if (time(0) - msg.sent > 14 * 24 * 60 * 60) continue;
                    if (target_user != 0 && msg.author.id != target_user) continue;
                    to_delete.push_back(id);
                }

                if (to_delete.empty()) {
                    bronx::send_message(bot, event, bronx::error("no valid messages found to delete"));
                    return;
                }

                bot.message_delete_bulk(to_delete, event.msg.channel_id, [to_delete, event, &bot, guild_id, target_user, mod_id](const dpp::confirmation_callback_t& ccb) {
                    if (ccb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to delete messages"));
                    } else {
                        auto embed = bronx::create_embed("purged **" + std::to_string(to_delete.size()) + "** messages", get_action_color("purge"));
                        if (target_user != 0) embed.set_description("purged **" + std::to_string(to_delete.size()) + "** messages from <@" + std::to_string(target_user) + ">");
                        bronx::add_invoker_footer(embed, event.msg.author);
                        dpp::message reply;
                        reply.add_embed(embed);
                        bot.message_create(reply.set_channel_id(event.msg.channel_id));
                    }
                });
            });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_manage_messages)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to manage messages")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto amount_opt = event.get_parameter("amount");
            if (amount_opt.index() == 0) { // missing
                // Trigger modal
                dpp::interaction_modal_response modal("mod_purge_modal_" + std::to_string(event.command.channel_id), "Purge Messages");
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("amount")
                        .set_label("Amount (1-100)")
                        .set_text_style(dpp::text_short)
                        .set_min_length(1)
                        .set_max_length(3)
                        .set_required(true)
                    )
                );
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("user_id")
                        .set_label("User ID (Optional)")
                        .set_text_style(dpp::text_short)
                        .set_required(false)
                    )
                );
                event.dialog(modal);
                return;
            }

            int amount = std::get<int64_t>(amount_opt);
            if (amount < 1 || amount > 100) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be between 1 and 100")).set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t target_user = 0;
            auto user_opt = event.get_parameter("user");
            if (user_opt.index() != 0) {
                target_user = std::get<dpp::snowflake>(user_opt);
            }

            // Defer reply because fetching and deleting might take >3s
            event.thinking(true);

            bot.messages_get(event.command.channel_id, 0, 0, 0, amount, [guild_id, mod_id, target_user, &bot, event](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("failed to fetch messages")));
                    return;
                }
                auto msgs = std::get<dpp::message_map>(callback.value);
                std::vector<dpp::snowflake> to_delete;
                for (const auto& [id, msg] : msgs) {
                    if (time(0) - msg.sent > 14 * 24 * 60 * 60) continue;
                    if (target_user != 0 && msg.author.id != target_user) continue;
                    to_delete.push_back(id);
                }

                if (to_delete.empty()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("no valid messages found to delete")));
                    return;
                }

                bot.message_delete_bulk(to_delete, event.command.channel_id, [to_delete, event, target_user, mod_id](const dpp::confirmation_callback_t& ccb) {
                    if (ccb.is_error()) {
                        event.edit_original_response(dpp::message().add_embed(bronx::error("failed to delete messages")));
                    } else {
                        auto embed = bronx::create_embed("purged **" + std::to_string(to_delete.size()) + "** messages", get_action_color("purge"));
                        if (target_user != 0) embed.set_description("purged **" + std::to_string(to_delete.size()) + "** messages from <@" + std::to_string(target_user) + ">");
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.edit_original_response(dpp::message().add_embed(embed));
                    }
                });
            });
        },
        {
            dpp::command_option(dpp::co_integer, "amount", "number of messages to delete (1-100)", true)
                .set_min_value(1).set_max_value(100),
            dpp::command_option(dpp::co_user, "user", "only delete messages from this user", false)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
