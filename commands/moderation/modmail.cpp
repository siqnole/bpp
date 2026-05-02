#include "modmail.h"
#include "../../database/operations/moderation/modmail_operations.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../embed_style.h"
#include <algorithm>

namespace commands {
namespace moderation {

Command* get_modmail_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    auto text_handler = [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
        uint64_t guild_id = event.msg.guild_id;
        uint64_t mod_id = event.msg.author.id;

        if (args.empty()) {
            bronx::send_message(bot, event, bronx::error("usage: modmail <setup/close>"));
            return;
        }

        std::string sub = args[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        if (sub == "setup") {
            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can setup modmail"));
                return;
            }
            bronx::send_message(bot, event, bronx::info("Please use `/modmail setup` for configuration."));
        } else if (sub == "close") {
            auto thread = bronx::db::get_modmail_thread_by_id(db, event.msg.channel_id);
            if (!thread || thread->status != "open") {
                bronx::send_message(bot, event, bronx::error("this channel is not an active modmail thread"));
                return;
            }

            if (bronx::db::close_modmail_thread(db, event.msg.channel_id)) {
                bronx::send_message(bot, event, bronx::success("modmail thread closed"));
                
                dpp::message dm;
                dpp::guild* g = dpp::find_guild(guild_id);
                std::string guild_name = g ? g->name : "the server";
                dm.set_content("Your modmail thread in **" + guild_name + "** has been closed by staff.");
                bot.direct_message_create(thread->user_id, dm);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to close modmail thread in database"));
            }
        }
    };

    auto slash_handler = [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
        auto subcommand = event.command.get_command_interaction().options[0];
        uint64_t guild_id = event.command.guild_id;
        uint64_t mod_id = event.command.get_issuing_user().id;

        if (subcommand.name == "setup") {
            if (!bronx::db::permission_operations::is_admin(db, mod_id, guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can setup modmail")).set_flags(dpp::m_ephemeral));
                return;
            }

            bronx::db::ModmailConfig config;
            config.guild_id = guild_id;
            
            // Extract options from the subcommand
            for (auto& opt : subcommand.options) {
                if (opt.name == "category") config.category_id = static_cast<uint64_t>(std::get<dpp::snowflake>(opt.value));
                else if (opt.name == "staff-role") config.staff_role_id = static_cast<uint64_t>(std::get<dpp::snowflake>(opt.value));
                else if (opt.name == "log-channel") config.log_channel_id = static_cast<uint64_t>(std::get<dpp::snowflake>(opt.value));
            }
            
            config.enabled = true;

            if (bronx::db::set_modmail_config(db, config)) {
                event.reply(dpp::message().add_embed(bronx::success("modmail system configured successfully")));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to save modmail configuration")).set_flags(dpp::m_ephemeral));
            }
        } else if (subcommand.name == "close") {
            auto thread = bronx::db::get_modmail_thread_by_id(db, event.command.channel_id);
            if (!thread || thread->status != "open") {
                event.reply(dpp::message().add_embed(bronx::error("this channel is not an active modmail thread")).set_flags(dpp::m_ephemeral));
                return;
            }

            if (bronx::db::close_modmail_thread(db, event.command.channel_id)) {
                event.reply(dpp::message().add_embed(bronx::success("modmail thread closed")));
                
                dpp::message dm;
                dpp::guild* g = dpp::find_guild(guild_id);
                std::string guild_name = g ? g->name : "the server";
                dm.set_content("Your modmail thread in **" + guild_name + "** has been closed by staff.");
                bot.direct_message_create(thread->user_id, dm);
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to close modmail thread in database")).set_flags(dpp::m_ephemeral));
            }
        }
    };

    std::vector<dpp::command_option> options = {
        dpp::command_option(dpp::co_sub_command, "setup", "setup modmail system")
            .add_option(dpp::command_option(dpp::co_channel, "category", "category to create threads in", true).add_channel_type(dpp::CHANNEL_CATEGORY))
            .add_option(dpp::command_option(dpp::co_role, "staff-role", "role that can see threads", true))
            .add_option(dpp::command_option(dpp::co_channel, "log-channel", "channel for modmail logs", true).add_channel_type(dpp::CHANNEL_TEXT)),
        dpp::command_option(dpp::co_sub_command, "close", "close current modmail thread")
    };

    cmd = new Command("modmail", "manage modmail threads and configuration", "moderation", std::vector<std::string>{}, true,
        text_handler, slash_handler, options);

    return cmd;
}

} // namespace moderation
} // namespace commands
