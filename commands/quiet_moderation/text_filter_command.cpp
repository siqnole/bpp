#include "text_filter_config.h"
#include "../../embed_style.h"
#include "../../command_handler.h"
#include <sstream>

namespace commands {
namespace quiet_moderation {

static ::std::vector<::std::string> split_args(const ::std::string& s) {
    ::std::istringstream iss(s);
    ::std::vector<::std::string> parts;
    ::std::string token;
    while (iss >> ::std::quoted(token) || iss >> token) {
        parts.push_back(token);
    }
    return parts;
}

Command* get_text_filter_command() {
    static Command cmd(
        "textfilter",
        "Configure the text filter (whitelists, blacklists, toggles).",
        "moderation",
        {"tf", "wordfilter", "filter"},
        false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Usage: textfilter <subcommand>")));
                return;
            }

            auto sub = args[0];
            auto guild_id = event.msg.guild_id;

            if (guild_text_filters.find(guild_id) == guild_text_filters.end()) {
                guild_text_filters[guild_id] = TextFilterConfig();
            }
            auto& cfg = guild_text_filters[guild_id];

            if (sub == "enable") {
                cfg.enabled = true;
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Text filter enabled")));
                return;
            }
            if (sub == "disable") {
                cfg.enabled = false;
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Text filter disabled")));
                return;
            }
            if (sub == "add") {
                if (args.size() < 3) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Usage: textfilter add <blacklist|whitelist> <value>")));
                    return;
                }
                auto list = args[1];
                auto value = args[2];
                if (list == "blacklist") {
                    cfg.blocked_words.insert(value);
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Added to blacklist: `" + value + "`")));
                    return;
                } else if (list == "whitelist") {
                    try { cfg.whitelist_users.insert(::std::stoull(value)); }
                    catch(...) { }
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Added to whitelist: `" + value + "`")));
                    return;
                }
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Unknown list type. Use 'blacklist' or 'whitelist'")));
                return;
            }
            if (sub == "remove") {
                if (args.size() < 3) {
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Usage: textfilter remove <blacklist|whitelist> <value>")));
                    return;
                }
                auto list = args[1];
                auto value = args[2];
                if (list == "blacklist") {
                    cfg.blocked_words.erase(value);
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Removed from blacklist: `" + value + "`")));
                    return;
                } else if (list == "whitelist") {
                    try { cfg.whitelist_users.erase(::std::stoull(value)); } catch(...) {}
                    bot.message_create(dpp::message(event.msg.channel_id, bronx::success("Removed from whitelist: `" + value + "`")));
                    return;
                }
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Unknown list type. Use 'blacklist' or 'whitelist'")));
                return;
            }
            if (sub == "list") {
                ::std::string out = "Text filter settings:\n";
                out += "Enabled: " + ::std::string(cfg.enabled ? "yes" : "no") + "\n";
                out += "Blocked words: ";
                for (const auto& s : cfg.blocked_words) out += "`" + s + "` ";
                out += "\nWhitelisted users: ";
                for (auto u : cfg.whitelist_users) out += "`" + ::std::to_string(u) + "` ";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::info(out)));
                return;
            }

            bot.message_create(dpp::message(event.msg.channel_id, bronx::error("Unknown subcommand")));
        }
    );

    return &cmd;
}

} // namespace quiet_moderation
} // namespace commands
