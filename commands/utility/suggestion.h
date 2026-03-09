#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/community/suggestion_operations.h"
#include "../../commands/economy_core.h" // for format_number
#include <dpp/dpp.h>
#include <chrono>
#include <map>
#include <sstream>

namespace commands {
namespace utility {

// simple cooldown map to prevent spam
static std::map<uint64_t, std::chrono::system_clock::time_point> last_suggestion_time;

// join args into single string
inline std::string join_args(const std::vector<std::string>& args) {
    std::ostringstream ss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) ss << " ";
        ss << args[i];
    }
    return ss.str();
}

inline Command* get_suggestion_command(bronx::db::Database* db) {
    static Command suggestion("suggest", "submit a suggestion for the bot", "utility", {"suggestion", "sug"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: b.suggest <your suggestion>"));
                return;
            }

            uint64_t user_id = event.msg.author.id;
            auto now = std::chrono::system_clock::now();
            auto it = last_suggestion_time.find(user_id);
            if (it != last_suggestion_time.end() && now - it->second < std::chrono::seconds(60)) {
                bronx::send_message(bot, event, bronx::error("please wait a minute before submitting another suggestion"));
                return;
            }

            std::string text = join_args(args);
            if (text.size() > 1000) {
                text = text.substr(0, 1000);
            }

            int64_t networth = 0;
            auto user = db->get_user(user_id);
            if (user) {
                networth = user->wallet + user->bank;
            }

            if (!bronx::db::suggestion_operations::add_suggestion(db, user_id, text, networth)) {
                bronx::send_message(bot, event, bronx::error("failed to submit suggestion"));
                return;
            }

            last_suggestion_time[user_id] = now;
            auto embed = bronx::success("Suggestion submitted – thank you!");
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // slash variant
            auto text_param = event.get_parameter("text");
            if (!std::holds_alternative<std::string>(text_param)) {
                event.reply(dpp::message().add_embed(bronx::error("please provide a suggestion")));
                return;
            }
            std::string text = std::get<std::string>(text_param);
            if (text.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("please provide a suggestion")));
                return;
            }

            uint64_t user_id = event.command.get_issuing_user().id;
            auto now = std::chrono::system_clock::now();
            auto it = last_suggestion_time.find(user_id);
            if (it != last_suggestion_time.end() && now - it->second < std::chrono::seconds(60)) {
                event.reply(dpp::message().add_embed(bronx::error("please wait a minute before submitting another suggestion")));
                return;
            }

            if (text.size() > 1000) text = text.substr(0,1000);
            int64_t networth = 0;
            auto user = db->get_user(user_id);
            if (user) networth = user->wallet + user->bank;

            if (!bronx::db::suggestion_operations::add_suggestion(db, user_id, text, networth)) {
                event.reply(dpp::message().add_embed(bronx::error("failed to submit suggestion")));
                return;
            }

            last_suggestion_time[user_id] = now;
            auto embed = bronx::success("Suggestion submitted – thank you!");
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        { dpp::command_option(dpp::co_string, "text", "your suggestion", true) });

    return &suggestion;
}

} // namespace utility
} // namespace commands
