#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_note_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "note", "add a moderation note to a user's record", "moderation",
        {}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to add notes"));
                return;
            }

            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: `note @user <note>`"));
                return;
            }

            uint64_t target_id = parse_mention(args[0]);
            if (target_id == 0) {
                bronx::send_message(bot, event, bronx::error("invalid user mention"));
                return;
            }

            std::string note_text;
            for (size_t i = 1; i < args.size(); ++i) {
                note_text += args[i] + (i == args.size() - 1 ? "" : " ");
            }

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_note_internal(bot, db, guild_id, target_id, mod_id, note_text, config,
                [event, target_id, &bot](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **note added to** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number)
                        + "\n**note:** " + inf.reason;
                    auto embed = bronx::create_embed(desc, get_action_color("note"));
                    bronx::add_invoker_footer(embed, event.msg.author);
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (is_quiet) msg.set_flags(dpp::m_ephemeral);
                    bronx::send_message(bot, event, msg);
                },
                [event, &bot](const std::string& err) {
                    bronx::send_message(bot, event, bronx::error(err));
                });
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to add notes")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto user_opt = event.get_parameter("user");
            uint64_t target_id = std::get<dpp::snowflake>(user_opt);

            auto note_opt = event.get_parameter("note_text");
            if (note_opt.index() == 0) {
                dpp::interaction_modal_response modal("mod_note_modal_" + std::to_string(target_id), "Add Note");
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("note_text")
                        .set_label("Note Text")
                        .set_text_style(dpp::text_paragraph)
                        .set_min_length(1)
                        .set_max_length(1000)
                        .set_required(true)
                    )
                );
                event.dialog(modal);
                return;
            }

            std::string note_text = std::get<std::string>(note_opt);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_note_internal(bot, db, guild_id, target_id, mod_id, note_text, config,
                [event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **note added to** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number)
                        + "\n**note:** " + inf.reason;
                    auto embed = bronx::create_embed(desc, get_action_color("note"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (is_quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        },
        {
            dpp::command_option(dpp::co_user, "user", "the user to add a note for", true),
            dpp::command_option(dpp::co_string, "note_text", "the note to add", false)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
