#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>

namespace commands {
namespace utility {

inline Command* get_userinfo_command() {
    static Command userinfo("userinfo", "display information about a user", "utility", {"ui", "whois"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            dpp::snowflake user_id;
            
            // Check if user was mentioned
            if (!event.msg.mentions.empty()) {
                user_id = event.msg.mentions.begin()->first.id;
            } else {
                user_id = event.msg.author.id;
            }

            // Check if a user ID was provided as an argument
            if (!args.empty()) {
                try {
                    user_id = ::std::stoull(args[0]); // Convert the first argument to a user ID
                } catch (const ::std::exception& e) {
                    bronx::send_message(bot, event, bronx::error("Invalid user ID provided."));
                    return;
                }
            }

            // Get guild member for additional info
            bot.guild_get_member(event.msg.guild_id, user_id, [&bot, event, user_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    bronx::send_message(bot, event, bronx::error("couldn't fetch user information"));
                    return;
                }

                auto member = ::std::get<dpp::guild_member>(callback.value);
                dpp::user* user_ptr = member.get_user();
                if (!user_ptr) return;
                dpp::user user = *user_ptr;
                
                ::std::string description = "**username:** " + user.username + "\n";
                
                // Show name field if display name and nickname are the same
                ::std::string nickname = member.get_nickname();
                ::std::string display_name = user.global_name;
                
                if (!display_name.empty() && !nickname.empty() && display_name == nickname) {
                    description += "**name:** " + display_name + "\n";
                } else {
                    if (!display_name.empty()) {
                        description += "**display name:** " + display_name + "\n";
                    }
                    if (!nickname.empty()) {
                        description += "**nickname:** " + nickname + "\n";
                    }
                }
                
                description += "**id:** " + ::std::to_string(user.id) + "\n";
                description += "**created:** <t:" + ::std::to_string((int64_t)user.id.get_creation_time()) + ":R>\n";
                description += "**joined:** <t:" + ::std::to_string((int64_t)member.joined_at) + ":R>\n";
                
                // Avatar links
                ::std::string avatar_url = user.get_avatar_url(256);
                ::std::string guild_avatar_url = member.get_avatar_url(256);
                
                description += "\n";
                if (!avatar_url.empty()) {
                    description += "[avatar](" + avatar_url + ")";
                }
                if (!guild_avatar_url.empty() && guild_avatar_url != avatar_url) {
                    if (!avatar_url.empty()) description += " | ";
                    description += "[server avatar](" + guild_avatar_url + ")";
                }

                auto embed = bronx::create_embed(description)
                    .set_thumbnail(user.get_avatar_url());
                
                bronx::add_invoker_footer(embed, event.msg.author);

                bronx::send_message(bot, event, embed);
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            dpp::snowflake user_id;
            
            // Get user from options or default to command user
            auto user_param = event.get_parameter("user");
            if (::std::holds_alternative<dpp::snowflake>(user_param)) {
                user_id = ::std::get<dpp::snowflake>(user_param);
            } else {
                user_id = event.command.get_issuing_user().id;
            }

            bot.guild_get_member(event.command.guild_id, user_id, [&bot, event, user_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    auto embed = bronx::error("couldn't fetch user information");
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                auto member = ::std::get<dpp::guild_member>(callback.value);
                dpp::user* user_ptr = member.get_user();
                if (!user_ptr) return;
                dpp::user user = *user_ptr;
                
                ::std::string description = "**username:** " + user.username + "\n";
                
                // Show name field if display name and nickname are the same
                ::std::string nickname = member.get_nickname();
                ::std::string display_name = user.global_name;
                
                if (!display_name.empty() && !nickname.empty() && display_name == nickname) {
                    description += "**name:** " + display_name + "\n";
                } else {
                    if (!display_name.empty()) {
                        description += "**display name:** " + display_name + "\n";
                    }
                    if (!nickname.empty()) {
                        description += "**nickname:** " + nickname + "\n";
                    }
                }
                
                description += "**id:** " + ::std::to_string(user.id) + "\n";
                description += "**created:** <t:" + ::std::to_string((int64_t)user.id.get_creation_time()) + ":R>\n";
                description += "**joined:** <t:" + ::std::to_string((int64_t)member.joined_at) + ":R>\n";
                
                // Avatar links
                ::std::string avatar_url = user.get_avatar_url(256);
                ::std::string guild_avatar_url = member.get_avatar_url(256);
                
                description += "\n";
                if (!avatar_url.empty()) {
                    description += "[avatar](" + avatar_url + ")";
                }
                if (!guild_avatar_url.empty() && guild_avatar_url != avatar_url) {
                    if (!avatar_url.empty()) description += " | ";
                    description += "[server avatar](" + guild_avatar_url + ")";
                }

                auto embed = bronx::create_embed(description)
                    .set_thumbnail(user.get_avatar_url());
                
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());

                event.reply(dpp::message().add_embed(embed));
            });
        },
        {dpp::command_option(dpp::co_user, "user", "the user to get info about", false)});
    
    return &userinfo;
}

} // namespace utility
} // namespace commands
