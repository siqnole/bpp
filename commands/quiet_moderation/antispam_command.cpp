#include "antispam_config.h"
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <algorithm>

namespace commands {
namespace quiet_moderation {

Command* get_antispam_command() {
    static Command antispam("antispam", "Configure anti-spam protection", "moderation",
        {"as", "spam"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto member = event.msg.member;
            bool is_admin = false;
            if (event.msg.guild_id != 0) {
                dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                if (g && g->owner_id == event.msg.author.id) is_admin = true;
            }
            for (const auto &rid : member.get_roles()) {
                dpp::role* r = dpp::find_role(rid);
                if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) { is_admin = true; break; }
            }
            if (!is_admin) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You need Administrator permission to use this command")));
                return;
            }

            if (args.empty()) {
                auto& config = guild_antispam_configs[event.msg.guild_id];

                dpp::embed embed = bronx::info("Anti-Spam Configuration");
                embed.add_field("Status", config.enabled ? "🟢 Enabled" : "🔴 Disabled", true);
                embed.add_field("Message Limit", config.max_messages_per_interval > 0 ?
                                ::std::to_string(config.max_messages_per_interval) + " per " + ::std::to_string(config.message_interval_seconds) + "s" : "Off", true);
                embed.add_field("Detect Duplicates", config.detect_duplicates ? "Yes" : "No", true);
                embed.add_field("Mention Spam", config.detect_mention_spam ? "Yes" : "No", true);
                embed.add_field("Emoji Spam", config.detect_emoji_spam ? "Yes" : "No", true);
                embed.add_field("Caps Spam", config.detect_caps_spam ? "Yes" : "No", true);
                embed.add_field("Newline Spam", config.detect_newline_spam ? "Yes" : "No", true);

                ::std::string actions = "(global escalation)\n";
                actions += "timeout — place user in timeout for configured seconds\n";
                actions += "kick — remove user from server\n";
                actions += "ban — ban user from server\n\n";
                if (config.max_violations_before_timeout > 0) {
                    actions += "Timeout after " + ::std::to_string(config.max_violations_before_timeout) + " violations\n";
                }
                if (config.max_violations_before_kick > 0) {
                    actions += "Kick after " + ::std::to_string(config.max_violations_before_kick) + " violations\n";
                }
                if (config.max_violations_before_ban > 0) {
                    actions += "Ban after " + ::std::to_string(config.max_violations_before_ban) + " violations\n";
                }
                if (actions.empty()) actions = "No automatic actions";
                embed.add_field("Actions (global)", actions, false);

                ::std::string usage = "**Commands & flags:**\n";
                usage += "`b.antispam enable/disable`\n";
                usage += "`b.antispam ratelimit <messages> <seconds> [-action <none|delete|timeout|kick|ban> -log on/off -strict on/off]`\n";
                usage += "`b.antispam duplicates <on|off> [max] [-action ...] [-log on/off] [-strict on/off]`\n";
                usage += "`b.antispam mentions <on|off> [max] [roles_max] [-action ...] [-log on/off] [-strict on/off]`\n";
                usage += "`b.antispam emojis <on|off> [max] [-action ...] [-log on/off] [-strict on/off]`\n";
                usage += "`b.antispam caps <on|off> [min_length] [max_pct] [-action ...] [-log on/off] [-strict on/off]`\n";
                usage += "`b.antispam newlines <on|off> [max] [-action ...] [-log on/off] [-strict on/off]`\n";
                usage += "Examples:\n";
                usage += "`b.antispam duplicates yes 2 -action timeout 30 -log on -strict yes`\n";
                usage += "`b.antispam mentions yes 5 1 -action kick -strict no`\n";
                usage += "`b.antispam newlines yes -action delete`\n";
                usage += "`b.antispam timeout <violations> <seconds>` (global escalation)\n";
                usage += "`b.antispam kick <violations>` (global escalation)\n";
                usage += "`b.antispam ban <violations>` (global escalation)\n";
                embed.add_field("Usage & examples", usage, false);

                ::std::string whitelist_info = "";
                whitelist_info += "Roles: " + ::std::to_string(config.whitelist_roles.size()) + " ";
                whitelist_info += "Users: " + ::std::to_string(config.whitelist_users.size()) + " ";
                whitelist_info += "Channels: " + ::std::to_string(config.whitelist_channels.size());
                embed.add_field("Whitelisted", whitelist_info, false);

                // Show per-detector behavior summary
                ::std::string dets = "duplicates(action=" + config.duplicates_behavior.action + ", log=" + (config.duplicates_behavior.log?"on":"off") + ", strict=" + (config.duplicates_behavior.strict?"on":"off") + ")\n";
                dets += "mentions(action=" + config.mentions_behavior.action + ", log=" + (config.mentions_behavior.log?"on":"off") + ", strict=" + (config.mentions_behavior.strict?"on":"off") + ")\n";
                dets += "emojis(action=" + config.emojis_behavior.action + ", log=" + (config.emojis_behavior.log?"on":"off") + ", strict=" + (config.emojis_behavior.strict?"on":"off") + ")\n";
                dets += "caps(action=" + config.caps_behavior.action + ", log=" + (config.caps_behavior.log?"on":"off") + ", strict=" + (config.caps_behavior.strict?"on":"off") + ")\n";
                dets += "newlines(action=" + config.newlines_behavior.action + ", log=" + (config.newlines_behavior.log?"on":"off") + ", strict=" + (config.newlines_behavior.strict?"on":"off") + ")\n";
                embed.add_field("Detector behaviors (summary)", dets, false);

                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }

            auto& config = guild_antispam_configs[event.msg.guild_id];
            ::std::string subcommand = args[0];
            ::std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

            // Helper: parse optional flags for detector behaviors from args[start...]
            auto parse_behavior_flags = [&](size_t start_idx)->DetectionBehavior {
                DetectionBehavior b;
                for (size_t i = start_idx; i < args.size(); ++i) {
                    ::std::string key = args[i];
                    ::std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                    if (key == "-action" && i + 1 < args.size()) {
                        ::std::string val = args[++i];
                        ::std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                        b.action = val;
                        if (b.action == "timeout" && i + 1 < args.size()) {
                            try { b.timeout_seconds = ::std::stoul(args[++i]); } catch(...) { b.timeout_seconds = 0; }
                        }
                    } else if (key == "-log" && i + 1 < args.size()) {
                        ::std::string val = args[++i]; ::std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                        b.log = (val == "on" || val == "yes" || val == "true");
                    } else if (key == "-strict" && i + 1 < args.size()) {
                        ::std::string val = args[++i]; ::std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                        b.strict = (val == "on" || val == "yes" || val == "true");
                    }
                }
                return b;
            };

            if (subcommand == "enable") {
                config.enabled = true;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Anti-spam enabled")));
            }
            else if (subcommand == "disable") {
                config.enabled = false;
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Anti-spam disabled")));
            }
            else if (subcommand == "ratelimit" && args.size() > 2) {
                try {
                    config.max_messages_per_interval = ::std::stoi(args[1]);
                    config.message_interval_seconds = ::std::stoi(args[2]);

                    // parse optional flags after positional args
                    DetectionBehavior b = parse_behavior_flags(3);
                    if (!b.action.empty() && b.action != "none") config.ratelimit_behavior.action = b.action;
                    if (b.timeout_seconds) config.ratelimit_behavior.timeout_seconds = b.timeout_seconds;
                    config.ratelimit_behavior.log = b.log;
                    config.ratelimit_behavior.strict = b.strict;

                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Rate limit set (behavior updated if flags provided)")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid values")));
                }
            }
            else if (subcommand == "duplicates" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.detect_duplicates = (toggle == "on" || toggle == "true" || toggle == "yes");

                size_t next_idx = 2;
                if (args.size() > next_idx) {
                    // optional numeric max
                    try {
                        config.max_duplicate_messages = ::std::stoi(args[next_idx]);
                        next_idx++;
                    } catch (...) {
                        // not a number — treat as start of flags
                    }
                }

                // parse flags
                DetectionBehavior b = parse_behavior_flags(next_idx);
                if (!b.action.empty() && b.action != "none") config.duplicates_behavior.action = b.action;
                if (b.timeout_seconds) config.duplicates_behavior.timeout_seconds = b.timeout_seconds;
                config.duplicates_behavior.log = b.log;
                config.duplicates_behavior.strict = b.strict;

                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Duplicate detection: " + ::std::string(config.detect_duplicates ? "ON" : "OFF") + 
                                 " (max: " + ::std::to_string(config.max_duplicate_messages) + ")")));
            }
            else if (subcommand == "mentions" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.detect_mention_spam = (toggle == "on" || toggle == "true" || toggle == "yes");

                size_t next_idx = 2;
                if (args.size() > next_idx) {
                    try { config.max_mentions_per_message = ::std::stoi(args[next_idx]); next_idx++; } catch(...) {}
                }
                if (args.size() > next_idx) {
                    try { config.max_role_mentions_per_message = ::std::stoi(args[next_idx]); next_idx++; } catch(...) {}
                }

                DetectionBehavior b = parse_behavior_flags(next_idx);
                if (!b.action.empty() && b.action != "none") config.mentions_behavior.action = b.action;
                if (b.timeout_seconds) config.mentions_behavior.timeout_seconds = b.timeout_seconds;
                config.mentions_behavior.log = b.log;
                config.mentions_behavior.strict = b.strict;

                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Mention spam: " + ::std::string(config.detect_mention_spam ? "ON" : "OFF") + 
                                 " (users: " + ::std::to_string(config.max_mentions_per_message) + 
                                 ", roles: " + ::std::to_string(config.max_role_mentions_per_message) + ")")));
            }
            else if (subcommand == "emojis" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.detect_emoji_spam = (toggle == "on" || toggle == "true" || toggle == "yes");

                size_t next_idx = 2;
                if (args.size() > next_idx) {
                    try { config.max_emojis_per_message = ::std::stoi(args[next_idx]); next_idx++; } catch(...) {}
                }

                DetectionBehavior b = parse_behavior_flags(next_idx);
                if (!b.action.empty() && b.action != "none") config.emojis_behavior.action = b.action;
                if (b.timeout_seconds) config.emojis_behavior.timeout_seconds = b.timeout_seconds;
                config.emojis_behavior.log = b.log;
                config.emojis_behavior.strict = b.strict;

                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Emoji spam: " + ::std::string(config.detect_emoji_spam ? "ON" : "OFF") + 
                                 " (max: " + ::std::to_string(config.max_emojis_per_message) + ")")));
            }
            else if (subcommand == "caps" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.detect_caps_spam = (toggle == "on" || toggle == "true" || toggle == "yes");

                size_t next_idx = 2;
                if (args.size() > next_idx) {
                    try { config.min_caps_length = ::std::stoi(args[next_idx]); next_idx++; } catch(...) {}
                }
                if (args.size() > next_idx) {
                    try { config.max_caps_percentage = ::std::stof(args[next_idx]) / 100.0f; next_idx++; } catch(...) {}
                }

                DetectionBehavior b = parse_behavior_flags(next_idx);
                if (!b.action.empty() && b.action != "none") config.caps_behavior.action = b.action;
                if (b.timeout_seconds) config.caps_behavior.timeout_seconds = b.timeout_seconds;
                config.caps_behavior.log = b.log;
                config.caps_behavior.strict = b.strict;

                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Caps spam: " + ::std::string(config.detect_caps_spam ? "ON" : "OFF") + 
                                 " (min length: " + ::std::to_string(config.min_caps_length) + 
                                 ", max %: " + ::std::to_string(static_cast<int>(config.max_caps_percentage * 100)) + ")")));
            }
            else if (subcommand == "newlines" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.detect_newline_spam = (toggle == "on" || toggle == "true" || toggle == "yes");

                size_t next_idx = 2;
                if (args.size() > next_idx) {
                    try { config.max_newlines = ::std::stoi(args[next_idx]); next_idx++; } catch(...) {}
                }

                DetectionBehavior b = parse_behavior_flags(next_idx);
                if (!b.action.empty() && b.action != "none") config.newlines_behavior.action = b.action;
                if (b.timeout_seconds) config.newlines_behavior.timeout_seconds = b.timeout_seconds;
                config.newlines_behavior.log = b.log;
                config.newlines_behavior.strict = b.strict;

                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Newline spam: " + ::std::string(config.detect_newline_spam ? "ON" : "OFF") + 
                                 " (max: " + ::std::to_string(config.max_newlines) + ")")));
            }
            else if (subcommand == "timeout" && args.size() > 2) {
                try {
                    config.max_violations_before_timeout = ::std::stoi(args[1]);
                    config.timeout_duration = ::std::stoul(args[2]);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Timeout action configured")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid values")));
                }
            }
            else if (subcommand == "kick" && args.size() > 1) {
                try {
                    config.max_violations_before_kick = ::std::stoi(args[1]);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Kick action configured")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid value")));
                }
            }
            else if (subcommand == "ban" && args.size() > 1) {
                try {
                    config.max_violations_before_ban = ::std::stoi(args[1]);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Ban action configured")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid value")));
                }
            }
            else if (subcommand == "logchannel" && args.size() > 1) {
                try {
                    ::std::string channel_str = args[1];
                    if (channel_str.size() > 3 && channel_str.substr(0, 2) == "<#") {
                        channel_str = channel_str.substr(2, channel_str.size() - 3);
                    }
                    config.log_channel = ::std::stoull(channel_str);
                    config.log_violations = true;
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Log channel set")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid channel")));
                }
            }
            else if (subcommand == "delete" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.delete_messages = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Delete messages: " + ::std::string(config.delete_messages ? "ON" : "OFF"))));
            }
            else if (subcommand == "warn" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.warn_user = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("Warn users: " + ::std::string(config.warn_user ? "ON" : "OFF"))));
            }
            else if (subcommand == "whitelist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];

                // Parse mention or ID
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }

                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.whitelist_roles.insert(id);
                    else if (type == "user") config.whitelist_users.insert(id);
                    else if (type == "channel") config.whitelist_channels.insert(id);

                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Added to whitelist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "unwhitelist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];

                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }

                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.whitelist_roles.erase(id);
                    else if (type == "user") config.whitelist_users.erase(id);
                    else if (type == "channel") config.whitelist_channels.erase(id);

                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Removed from whitelist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "blacklist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.blacklist_roles.insert(id);
                    else if (type == "user") config.blacklist_users.insert(id);
                    else if (type == "channel") config.blacklist_channels.insert(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Added to blacklist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "unblacklist" && args.size() > 2) {
                ::std::string type = args[1];
                ::std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                ::std::string id_str = args[2];
                if (id_str.find("<@") == 0 || id_str.find("<#") == 0) {
                    size_t start = id_str.find_first_of("0123456789");
                    size_t end = id_str.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        id_str = id_str.substr(start, end - start + 1);
                    }
                }
                try {
                    dpp::snowflake id = ::std::stoull(id_str);
                    if (type == "role") config.blacklist_roles.erase(id);
                    else if (type == "user") config.blacklist_users.erase(id);
                    else if (type == "channel") config.blacklist_channels.erase(id);
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("Removed from blacklist")));
                } catch (...) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("Invalid ID")));
                }
            }
            else if (subcommand == "listblacklist") {
                ::std::string out = "Roles: " + ::std::to_string(config.blacklist_roles.size()) + "\n";
                out += "Users: " + ::std::to_string(config.blacklist_users.size()) + "\n";
                out += "Channels: " + ::std::to_string(config.blacklist_channels.size());
                dpp::embed eb = bronx::info("Antispam Blacklist");
                eb.set_description(out);
                bot.message_create(dpp::message(event.msg.channel_id, eb));
            }
            else if (subcommand == "usewhitelist" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.use_whitelist = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success(::std::string("use_whitelist: ") + (config.use_whitelist ? "ON" : "OFF"))));
            }
            else if (subcommand == "useblacklist" && args.size() > 1) {
                ::std::string toggle = args[1];
                ::std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
                config.use_blacklist = (toggle == "on" || toggle == "true" || toggle == "yes");
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success(::std::string("use_blacklist: ") + (config.use_blacklist ? "ON" : "OFF"))));
            }
            else if (subcommand == "reset") {
                antispam_violations[event.msg.guild_id].clear();
                user_message_tracking[event.msg.guild_id].clear();
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("All violations cleared")));
            }
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid subcommand")));
            }
        }
    );

    return &antispam;
}

} // namespace quiet_moderation
} // namespace commands
