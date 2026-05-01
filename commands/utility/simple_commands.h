#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>

namespace commands {
namespace utility {

// Avatar command (text only)
inline Command* get_avatar_command() {
    static Command avatar("avatar", "display a user's avatar", "utility", {"av", "pfp"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            dpp::snowflake target_id;
            // Check for mentions first
            if (!event.msg.mentions.empty()) {
                target_id = event.msg.mentions.begin()->first.id;
            } else if (!args.empty()) {
                // Try to parse UID from argument
                try {
                    target_id = ::std::stoull(args[0]);
                } catch (...) {
                    target_id = event.msg.author.id;
                }
            } else {
                target_id = event.msg.author.id;
            }

            // Fetch user object
            bot.user_get(target_id, [&bot, event, target_id](const dpp::confirmation_callback_t& user_callback) {
                uint32_t accent_color = 0;
                ::std::string display_name;
                dpp::user target;
                if (!user_callback.is_error()) {
                    dpp::user_identified full_user = user_callback.get<dpp::user_identified>();
                    accent_color = full_user.accent_color;
                    display_name = full_user.global_name.empty() ? full_user.username : full_user.global_name;
                    target = full_user;
                } else {
                    // fallback to author
                    display_name = event.msg.author.global_name.empty() ? event.msg.author.username : event.msg.author.global_name;
                    target = event.msg.author;
                }

                // Fetch guild member to get server-specific avatar
                bot.guild_get_member(event.msg.guild_id, target_id, [&bot, event, display_name, target, accent_color](const dpp::confirmation_callback_t& callback) {
                    ::std::string avatar_url;
                    if (callback.is_error()) {
                        avatar_url = target.get_avatar_url(4096);
                    } else {
                        dpp::guild_member member = callback.get<dpp::guild_member>();
                        avatar_url = member.get_avatar_url(4096);
                        if (avatar_url.empty()) {
                            avatar_url = target.get_avatar_url(4096);
                        }
                    }

                    auto embed = bronx::create_embed(display_name + "'s avatar")
                        .set_image(avatar_url);
                    if (accent_color != 0) {
                        embed.set_color(accent_color);
                    }
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bot.message_create(dpp::message(event.msg.channel_id, embed));
                });
            });
        });
    
    return &avatar;
}

// Banner command (text only)
inline Command* get_banner_command() {
    static Command banner("banner", "display a user's banner", "utility", {"bn", "banana"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            dpp::user target = event.msg.mentions.empty() ? event.msg.author : event.msg.mentions.begin()->first;
            ::std::string display_name = target.global_name.empty() ? target.username : target.global_name;
            
            // Capture only what we need to avoid dangling references
            dpp::snowflake guild_id = event.msg.guild_id;
            dpp::snowflake channel_id = event.msg.channel_id;
            dpp::user author = event.msg.author;
            dpp::snowflake target_id = target.id;
            
            // First fetch guild member to get server-specific banner
            bot.guild_get_member(guild_id, target_id, [&bot, channel_id, author, display_name, target_id](const dpp::confirmation_callback_t& member_callback) {
                ::std::string banner_url;
                
                if (!member_callback.is_error()) {
                    dpp::guild_member member = member_callback.get<dpp::guild_member>();
                    // Try to get server-specific banner from member
                    banner_url = member.get_avatar_url(4096, dpp::i_jpg, true); // true = try to get banner
                }
                
                // Fetch global user profile for banner and accent color
                bot.user_get(target_id, [&bot, channel_id, author, display_name, banner_url](const dpp::confirmation_callback_t& callback) {
                    if (callback.is_error()) {
                        auto error_embed = bronx::error("Failed to fetch user data");
                        bronx::add_invoker_footer(error_embed, author);
                        bot.message_create(dpp::message(channel_id, error_embed));
                        return;
                    }
                    
                    dpp::user_identified full_user = callback.get<dpp::user_identified>();
                    ::std::string final_banner = banner_url.empty() ? full_user.get_banner_url(4096) : banner_url;
                    uint32_t accent_color = full_user.accent_color;
                    
                    auto embed = final_banner.empty() 
                        ? bronx::create_embed(display_name + " has no banner set")
                        : bronx::create_embed(display_name + "'s banner").set_image(final_banner);
                    
                    if (accent_color != 0) {
                        embed.set_color(accent_color);
                    }
                    
                    bronx::add_invoker_footer(embed, author);
                    
                    bot.message_create(dpp::message(channel_id, embed));
                });
            });
        });
    
    return &banner;
}

// Invite command (text only)
inline Command* get_invite_command() {
    static Command invite("invite", "get the bot invite link", "utility", {"oauth", "iwantthisbot", "support"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto embed = bronx::create_embed("get bronx")
                .set_description("bronx is currently in beta, but if you want to do anything go ahead");
            
            bronx::add_invoker_footer(embed, event.msg.author);
            
            dpp::message msg(event.msg.channel_id, embed);
            msg.add_component(
                dpp::component()
                    .set_type(dpp::cot_action_row)
                    .add_component(
                        dpp::component()
                            .set_type(dpp::cot_button)
                            .set_label("invite")
                            .set_style(dpp::cos_link)
                            .set_url("https://discord.com/oauth2/authorize?client_id=" + ::std::to_string(bot.me.id) + "&permissions=7610260541407217&integration_type=0&scope=bot+applications.commands")
                    )
                    .add_component(
                        dpp::component()
                            .set_type(dpp::cot_button)
                            .set_label("support server")
                            .set_style(dpp::cos_link)
                            .set_url(bronx::SUPPORT_SERVER_URL)
                    )
            );
            
            bot.message_create(msg);
        });
    
    return &invite;
}

// Server Avatar command (text only)
inline Command* get_serveravatar_command() {
    static Command serveravatar("serveravatar", "display the server's avatar/icon", "utility", {"svav", "servericon", "svicon"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Capture data we need before async call
            dpp::snowflake guild_id = event.msg.guild_id;
            dpp::snowflake channel_id = event.msg.channel_id;
            dpp::user author = event.msg.author;
            
            bot.guild_get(guild_id, [&bot, channel_id, author](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    auto error_embed = bronx::error("Failed to fetch server data");
                    bronx::add_invoker_footer(error_embed, author);
                    bot.message_create(dpp::message(channel_id, error_embed));
                    return;
                }
                
                dpp::guild guild = callback.get<dpp::guild>();
                ::std::string icon_url = guild.get_icon_url(4096);
                
                auto embed = icon_url.empty()
                    ? bronx::create_embed(guild.name + " has no icon set")
                    : bronx::create_embed(guild.name + "'s icon").set_image(icon_url);
                
                bronx::add_invoker_footer(embed, author);
                
                bot.message_create(dpp::message(channel_id, embed));
            });
        });
    
    return &serveravatar;
}

// Server Banner command (text only)
inline Command* get_serverbanner_command() {
    static Command serverbanner("serverbanner", "display the server's banner", "utility", {"svbn", "svbanner", "svbanana"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Capture data we need before async call
            dpp::snowflake guild_id = event.msg.guild_id;
            dpp::snowflake channel_id = event.msg.channel_id;
            dpp::user author = event.msg.author;
            
            bot.guild_get(guild_id, [&bot, channel_id, author](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    auto error_embed = bronx::error("Failed to fetch server data");
                    bronx::add_invoker_footer(error_embed, author);
                    bot.message_create(dpp::message(channel_id, error_embed));
                    return;
                }
                
                dpp::guild guild = callback.get<dpp::guild>();
                ::std::string banner_url = guild.get_banner_url(4096);
                
                auto embed = banner_url.empty()
                    ? bronx::create_embed(guild.name + " has no banner set")
                    : bronx::create_embed(guild.name + "'s banner").set_image(banner_url);
                
                bronx::add_invoker_footer(embed, author);
                
                bot.message_create(dpp::message(channel_id, embed));
            });
        });
    
    return &serverbanner;
}

// Disconnect-Me command (text only) — quickly kicks the author out of their voice channel
inline Command* get_dcme_command() {
    static Command dcme("dcme", "disconnect yourself from your current voice channel", "utility", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server."));
                return;
            }

            dpp::snowflake guild_id   = event.msg.guild_id;
            dpp::snowflake author_id  = event.msg.author.id;
            dpp::snowflake channel_id = event.msg.channel_id;
            dpp::user author          = event.msg.author;

            // Quick local cache check: if the guild is cached we can verify VC membership
            // without an extra REST call. If not cached, just attempt and let Discord reject.
            dpp::guild* g = dpp::find_guild(guild_id);
            if (g) {
                auto vs_it = g->voice_members.find(author_id);
                if (vs_it == g->voice_members.end() || vs_it->second.channel_id == 0) {
                    auto err = bronx::error("you're not in a voice channel.");
                    bronx::add_invoker_footer(err, author);
                    bot.message_create(dpp::message(channel_id, err));
                    return;
                }
            }

            // guild_member_move with channel_id = 0 disconnects the user from voice
            bot.guild_member_move(0, guild_id, author_id,
                [&bot, channel_id, author](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        auto err = bronx::error("failed to disconnect you — do i have the `Move Members` permission?");
                        bronx::add_invoker_footer(err, author);
                        bot.message_create(dpp::message(channel_id, err));
                    } else {
                        auto ok = bronx::success("disconnected you from voice. 👋");
                        bronx::add_invoker_footer(ok, author);
                        bot.message_create(dpp::message(channel_id, ok));
                    }
                });
        });

    return &dcme;
}

// Say (echo) command - allow users to echo messages as the bot
inline Command* get_say_command() {
    static Command say("say", "echo a message as the bot", "utility", {"echo"}, true,
        // TEXT HANDLER
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("please provide a message to echo."));
                return;
            }
            
            ::std::string text;
            for (const auto& arg : args) text += arg + " ";
            if (!text.empty()) text.pop_back();
            
            // Delete the original message if possible to make it look like the bot is speaking
            bot.message_delete(event.msg.id, event.msg.channel_id);
            
            // send as embed to show who said it
auto embed = bronx::create_embed(text).set_description("");
bronx::add_invoker_footer(embed, event.msg.author);
bot.message_create(dpp::message(event.msg.channel_id, embed));
        },
        // SLASH HANDLER
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ::std::string message = ::std::get<::std::string>(event.get_parameter("message"));
            dpp::snowflake channel_id = event.command.channel_id;
            
            auto channel_param = event.get_parameter("channel");
            if (::std::holds_alternative<dpp::snowflake>(channel_param)) {
                channel_id = ::std::get<dpp::snowflake>(channel_param);
            }
            
                // Build embed with who-sent button
                auto embed = bronx::create_embed(message);
                dpp::message msg(channel_id, embed);
                msg.add_component(
                    dpp::component()
                        .set_type(dpp::cot_action_row)
                        .add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_label("who sent?")
                                .set_style(dpp::cos_primary)
                                .set_id("who_sent_" + std::to_string(event.command.get_issuing_user().id))
                        )
                );
                bronx::add_invoker_footer(embed, event.command.usr);
                bot.message_create(msg, [&bot, event, channel_id](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().set_content(bronx::EMOJI_DENY + " failed to send message — do i have permissions in <#" + ::std::to_string(channel_id) + ">?").set_flags(dpp::m_ephemeral));
                    } else {
                        event.reply(dpp::message().set_content(bronx::EMOJI_CHECK + " message sent!").set_flags(dpp::m_ephemeral));
                    }
                });
        },
        {
            dpp::command_option(dpp::co_string, "message", "the message to echo", true),
            dpp::command_option(dpp::co_channel, "channel", "optional channel to send to", false)
        }
    );
    return &say;
}

} // namespace utility
} // namespace commands
