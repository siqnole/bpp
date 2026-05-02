#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "infraction_engine.h"

namespace commands {
namespace moderation {

inline Command* get_reason_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;

    cmd = new Command(
        "reason", "update the reason for an existing infraction case", "moderation",
        {}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            uint64_t mod_id = event.msg.author.id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                bronx::send_message(bot, event, bronx::error("you don't have permission to update reasons"));
                return;
            }

            if (args.size() < 2) {
                bronx::send_message(bot, event, bronx::error("usage: `reason <case_number> <new_reason>`"));
                return;
            }

            uint32_t case_number = 0;
            try { case_number = std::stoul(args[0]); } catch (...) {}
            if (case_number == 0) {
                bronx::send_message(bot, event, bronx::error("invalid case number"));
                return;
            }

            std::string new_reason;
            for (size_t i = 1; i < args.size(); ++i) {
                new_reason += args[i] + (i == args.size() - 1 ? "" : " ");
            }

            if (bronx::db::infraction_operations::update_infraction_reason(db, guild_id, case_number, new_reason)) {
                auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
                if (inf_opt) {
                    auto embed = bronx::create_embed("updated reason for case **#" + std::to_string(case_number) + "**", get_action_color("warn"));
                    embed.add_field("user", "<@" + std::to_string(inf_opt->user_id) + ">", true);
                    embed.add_field("new reason", new_reason, false);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    
                    // send mod log update
                    send_mod_log(bot, db, guild_id, *inf_opt);
                } else {
                    bronx::send_message(bot, event, bronx::success("reason updated (case record not found for preview)"));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("failed to update reason. check if the case number exists."));
            }
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            bool has_perm = bronx::db::permission_operations::is_mod(db, mod_id, guild_id)
                         || bronx::db::permission_operations::is_admin(db, mod_id, guild_id);
            if (!has_perm) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have permission to update reasons")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto case_opt = event.get_parameter("case");
            uint32_t case_number = (uint32_t)std::get<int64_t>(case_opt);

            auto reason_opt = event.get_parameter("new_reason");
            if (reason_opt.index() == 0) {
                dpp::interaction_modal_response modal("mod_reason_modal_" + std::to_string(case_number), "Update Case Reason");
                modal.add_component(
                    dpp::component().set_type(dpp::cot_action_row)
                    .add_component(dpp::component()
                        .set_type(dpp::cot_text)
                        .set_id("new_reason")
                        .set_label("New Reason")
                        .set_text_style(dpp::text_paragraph)
                        .set_min_length(1)
                        .set_max_length(1000)
                        .set_required(true)
                    )
                );
                event.dialog(modal);
                return;
            }

            std::string new_reason = std::get<std::string>(reason_opt);

            if (bronx::db::infraction_operations::update_infraction_reason(db, guild_id, case_number, new_reason)) {
                auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
                if (inf_opt) {
                    auto embed = bronx::create_embed("updated reason for case **#" + std::to_string(case_number) + "**", get_action_color("warn"));
                    embed.add_field("user", "<@" + std::to_string(inf_opt->user_id) + ">", true);
                    embed.add_field("new reason", new_reason, false);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    
                    // send mod log update
                    send_mod_log(bot, db, guild_id, *inf_opt);
                } else {
                    event.reply(dpp::message().add_embed(bronx::success("reason updated")).set_flags(dpp::m_ephemeral));
                }
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to update reason")).set_flags(dpp::m_ephemeral));
            }
        },
        {
            dpp::command_option(dpp::co_integer, "case", "the case number to update", true),
            dpp::command_option(dpp::co_string, "new_reason", "the new reason for this case", false)
        }
    );
    return cmd;
}

} // namespace moderation
} // namespace commands
