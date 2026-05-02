#pragma once
#include <dpp/dpp.h>
#include <sstream>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_masstimeout_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "masstimeout", "timeout multiple users at once", "moderation",
        {"mtimeout"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_moderate_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to timeout members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `masstimeout <user1> [user2...] [duration: 1h] [reason: reason text]`"));
                return;
            }

            std::vector<uint64_t> targets;
            std::string reason = "no reason provided";
            uint32_t duration = 0;
            bool gathering_reason = false;

            for (const auto& arg : args) {
                if (arg.rfind("reason:", 0) == 0) {
                    reason = arg.substr(7);
                    if (reason.empty()) reason = "no reason provided";
                    gathering_reason = true;
                    continue;
                }
                if (arg.rfind("duration:", 0) == 0) {
                    duration = commands::moderation::parse_duration(arg.substr(9));
                    continue;
                }
                if (gathering_reason) {
                    reason += " " + arg;
                    continue;
                }

                uint64_t tid = parse_mention(arg);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                bronx::send_message(bot, event, bronx::error("no valid users to timeout provided"));
                return;
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};
            if (duration == 0) duration = config.default_duration_timeout;

            auto embed = bronx::create_embed("timing out **" + std::to_string(targets.size()) + "** users...", get_action_color("timeout"));
            bot.message_create(dpp::message(event.msg.channel_id, embed), [targets, &bot, db, guild_id, mod_id, reason, duration, config, event](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::snowflake msg_id = std::get<dpp::message>(cb.value).id;

                struct Context {
                    int count = 0;
                    int success = 0;
                    int fail = 0;
                };
                auto ctx = std::make_shared<Context>();

                for (uint64_t tid : targets) {
                    apply_timeout_internal(bot, db, guild_id, tid, mod_id, duration, reason, config,
                        [ctx, targets, &bot, event, msg_id](const bronx::db::InfractionRow&, bool) {
                            ctx->count++;
                            ctx->success++;
                            if (ctx->count == (int)targets.size()) {
                                auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                                bronx::add_invoker_footer(final_embed, event.msg.author);
                                dpp::message msg(event.msg.channel_id, final_embed);
                                msg.id = msg_id;
                                bot.message_edit(msg);
                            }
                        },
                        [ctx, targets, &bot, event, msg_id](const std::string&) {
                            ctx->count++;
                            ctx->fail++;
                            if (ctx->count == (int)targets.size()) {
                                auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                                bronx::add_invoker_footer(final_embed, event.msg.author);
                                dpp::message msg(event.msg.channel_id, final_embed);
                                msg.id = msg_id;
                                bot.message_edit(msg);
                            }
                        });
                }
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
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_moderate_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to timeout members")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            auto users_opt = event.get_parameter("users");
            if (users_opt.index() == 0) {
                dpp::interaction_modal_response modal("mod_masstimeout_modal", "Mass Timeout Users");
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("users")
                        .set_label("User IDs/Mentions (space separated)")
                        .set_text_style(dpp::text_paragraph)
                        .set_required(true)
                    )
                );
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("duration")
                        .set_label("Duration (e.g. 1h, 1d)")
                        .set_text_style(dpp::text_short)
                        .set_required(false)
                    )
                );
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("reason")
                        .set_label("Reason")
                        .set_text_style(dpp::text_short)
                        .set_required(false)
                    )
                );
                event.dialog(modal);
                return;
            }

            std::string users_str = std::get<std::string>(users_opt);
            std::string reason = "mass timeout via interaction";
            auto reason_opt = event.get_parameter("reason");
            if (reason_opt.index() != 0) reason = std::get<std::string>(reason_opt);

            uint32_t duration = 0;
            auto duration_opt = event.get_parameter("duration");
            if (duration_opt.index() != 0) duration = commands::moderation::parse_duration(std::get<std::string>(duration_opt));

            std::vector<uint64_t> targets;
            std::stringstream ss(users_str);
            std::string item;
            while (ss >> item) {
                uint64_t tid = parse_mention(item);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no valid users to timeout provided")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.thinking(false);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};
            if (duration == 0) duration = config.default_duration_timeout;

            struct Context {
                int count = 0;
                int success = 0;
                int fail = 0;
            };
            auto ctx = std::make_shared<Context>();

            for (uint64_t tid : targets) {
                apply_timeout_internal(bot, db, guild_id, tid, mod_id, duration, reason, config,
                    [ctx, targets, &bot, event](const bronx::db::InfractionRow&, bool) {
                        ctx->count++;
                        ctx->success++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    },
                    [ctx, targets, &bot, event](const std::string&) {
                        ctx->count++;
                        ctx->fail++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    });
            }
        },
        {
            dpp::command_option(dpp::co_string, "users", "list of users to timeout (IDs or mentions)", true),
            dpp::command_option(dpp::co_string, "duration", "duration of the timeout (e.g. 1h, 1d)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for the timeout", false)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
