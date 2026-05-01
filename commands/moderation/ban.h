#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_ban_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "ban", "ban a user from the server", "moderation",
        {"b"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.msg.member).has(dpp::p_ban_members)) {
                    bronx::send_message(bot, event, bronx::error("you don't have permission to ban members"));
                    return;
                }
            }

            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `ban @user [duration] [delete_days] [reason]`"));
                return;
            }

            uint64_t target_id = parse_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            if (target_id == mod_id) {
                bronx::send_message(bot, event, bronx::error("you can't ban yourself"));
                return;
            }

            if (target_id == bot.me.id) {
                bronx::send_message(bot, event, bronx::error("you can't ban the bot"));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("i can't ban that user — their role is higher than mine"));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    bronx::send_message(bot, event, bronx::error("you can't ban that user — their role is higher than yours"));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // parse args: [duration] [delete_days] [reason...]
            // duration is a time string like "7d", delete_days is a plain number 0-7
            uint32_t duration = 0;
            uint32_t delete_days = 0;
            std::string reason;
            size_t reason_start = 1;

            if (args.size() > 1) {
                // try to parse duration
                duration = parse_duration(args[1]);
                if (duration > 0) {
                    reason_start = 2;
                    // try to parse delete_days
                    if (args.size() > 2) {
                        try {
                            uint32_t dd = std::stoul(args[2]);
                            if (dd <= 7) {
                                delete_days = dd;
                                reason_start = 3;
                            }
                        } catch (...) {}
                    }
                } else {
                    // might be delete_days as first optional arg
                    try {
                        uint32_t dd = std::stoul(args[1]);
                        if (dd <= 7) {
                            delete_days = dd;
                            reason_start = 2;
                        }
                    } catch (...) {
                        reason_start = 1;
                    }
                }
            }

            if (duration == 0) {
                duration = config.value().default_duration_ban;
            }

            // clamp delete days
            if (delete_days > 7) delete_days = 7;

            for (size_t i = reason_start; i < args.size(); i++) {
                if (!reason.empty()) reason += " ";
                reason += args[i];
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user BEFORE banning (they won't receive DM after being banned)
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "ban", reason, duration, config.value().point_ban);
            }

            // dpp::guild_ban_add takes delete_message_seconds, not days
            uint32_t delete_message_seconds = delete_days * 86400;

            // apply ban
            apply_ban_internal(bot, db, guild_id, target_id, mod_id, duration, delete_message_seconds, reason, config.value(),
                [&bot, &event, target_id](const bronx::db::InfractionRow& inf, bool quiet) {
                    bool is_temp = inf.duration_seconds > 0 && inf.duration_seconds < 15552000;
                    std::string desc = bronx::EMOJI_CHECK + " **banned** <@" + std::to_string(target_id) + ">";
                    if (is_temp) {
                        desc += "\n**duration:** " + format_duration(inf.duration_seconds);
                    } else {
                        desc += "\n**duration:** permanent";
                    }
                    desc += "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                },
                [&bot, &event](const std::string& error_msg) {
                    bronx::send_message(bot, event, bronx::error(error_msg));
                });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            // permission check
            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                auto* guild = dpp::find_guild(guild_id);
                if (!guild || !guild->base_permissions(event.command.member).has(dpp::p_ban_members)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have permission to ban members")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get user param
            auto user_param = event.get_parameter("user");
            if (!std::holds_alternative<dpp::snowflake>(user_param)) {
                event.reply(dpp::message().add_embed(bronx::error("please mention a user")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t target_id = std::get<dpp::snowflake>(user_param);

            if (target_id == mod_id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't ban yourself")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (target_id == bot.me.id) {
                event.reply(dpp::message().add_embed(bronx::error("you can't ban the bot")).set_flags(dpp::m_ephemeral));
                return;
            }

            // hierarchy check
            auto* guild = dpp::find_guild(guild_id);
            if (guild) {
                if (!check_hierarchy(*guild, bot.me.id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("i can't ban that user — their role is higher than mine")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (!check_hierarchy(*guild, mod_id, target_id)) {
                    event.reply(dpp::message().add_embed(bronx::error("you can't ban that user — their role is higher than yours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            // get config
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            // parse optional duration
            bool duration_provided = false;
            uint32_t duration = 0;
            std::string duration_str;
            auto dur_param = event.get_parameter("duration");
            if (std::holds_alternative<std::string>(dur_param)) {
                duration_str = std::get<std::string>(dur_param);
                duration = parse_duration(duration_str);
                duration_provided = true;
            }

            // parse optional delete_messages (days)
            uint32_t delete_days = 0;
            auto del_param = event.get_parameter("delete_messages");
            if (std::holds_alternative<int64_t>(del_param)) {
                int64_t val = std::get<int64_t>(del_param);
                if (val >= 0 && val <= 7) delete_days = static_cast<uint32_t>(val);
            }

            // parse optional reason
            bool reason_provided = false;
            std::string reason;
            auto reason_param = event.get_parameter("reason");
            if (std::holds_alternative<std::string>(reason_param)) {
                reason = std::get<std::string>(reason_param);
                reason_provided = !reason.empty();
            }

            // if either is missing, show a modal
            if (!duration_provided || !reason_provided) {
                dpp::interaction_modal_response modal("mod_ban_modal_" + std::to_string(target_id) + "_" + std::to_string(delete_days), "Ban User");
                modal.add_row();
                modal.add_component(dpp::component()
                    .set_label("Duration (e.g. 7d, 30d, 0 for perm)")
                    .set_id("duration")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Enter duration (0 = permanent)")
                    .set_default_value(duration_provided ? duration_str : "0")
                    .set_min_length(1)
                    .set_max_length(20)
                    .set_text_style(dpp::text_short));
                modal.add_row();
                modal.add_component(dpp::component()
                    .set_label("Reason")
                    .set_id("reason")
                    .set_type(dpp::cot_text)
                    .set_placeholder("Enter reason for ban...")
                    .set_default_value(reason_provided ? reason : "")
                    .set_min_length(1)
                    .set_max_length(512)
                    .set_text_style(dpp::text_paragraph));
                
                event.dialog(modal);
                return;
            }

            std::string guild_name = guild ? guild->name : "unknown server";

            // dm user BEFORE banning
            if (config.has_value() && config.value().dm_on_action) {
                dm_user_action(bot, target_id, guild_name, "ban", reason, duration, config.value().point_ban);
            }

            // dpp::guild_ban_add takes delete_message_seconds
            uint32_t delete_message_seconds = delete_days * 86400;

            // apply ban
            apply_ban_internal(bot, db, guild_id, target_id, mod_id, duration, delete_message_seconds, reason, config.value(),
                [event](const bronx::db::InfractionRow& inf, bool quiet) {
                    bool is_temp = inf.duration_seconds > 0 && inf.duration_seconds < 15552000;
                    std::string desc = bronx::EMOJI_CHECK + " **banned** <@" + std::to_string(inf.user_id) + ">";
                    if (is_temp) {
                        desc += "\n**duration:** " + format_duration(inf.duration_seconds);
                    } else {
                        desc += "\n**duration:** permanent";
                    }
                    desc += "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());

                    dpp::message msg;
                    msg.add_embed(embed);
                    if (quiet) msg.set_flags(dpp::m_ephemeral);
                    
                    event.reply(msg);
                },
                [event](const std::string& error_msg) {
                    event.reply(dpp::message().add_embed(bronx::error(error_msg)).set_flags(dpp::m_ephemeral));
                });
        },
        // options
        {
            dpp::command_option(dpp::co_user, "user", "the user to ban", true),
            dpp::command_option(dpp::co_string, "duration", "ban duration for tempban (e.g. 7d, 30d) — omit for permanent", false),
            dpp::command_option(dpp::co_integer, "delete_messages", "days of messages to delete (0-7)", false),
            dpp::command_option(dpp::co_string, "reason", "reason for the ban", false)
        }
    );

    return cmd;
}

} // namespace moderation
} // namespace commands
