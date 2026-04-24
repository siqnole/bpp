#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "media.h"
#include <dpp/dpp.h>
#include <algorithm>

namespace commands {
namespace utility {

inline Command* get_serverconfig_command(bronx::db::Database* db) {
    if (!db) return nullptr;

    static Command serverconfig("serverconfig", "Configure custom server metadata", "utility", {"sc", "config"}, true,
        // ── text handler ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            if (guild_id == 0) return;

            // Check Administrator permission
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) return;
            auto member_it = g->members.find(event.msg.author.id);
            if (member_it == g->members.end()) return;
            if (!g->base_permissions(member_it->second).can(dpp::p_administrator)) {
                bronx::send_message(bot, event, bronx::error("you need administrator permission to use this command."));
                return;
            }

            if (args.empty()) {
                auto embed = bronx::create_embed(
                    "🔧 **server customization**\n\n"
                    "personalize your server profile for Bronx Bot.\n\n"
                    "**available commands:**\n"
                    "> `b.serverconfig bio <text>` — set the short server biography\n"
                    "> `b.serverconfig website <url>` — set the server website link\n"
                    "> `b.serverconfig banner <url/attachment>` — set a custom banner\n"
                    "> `b.serverconfig avatar <url/attachment>` — set a custom avatar\n"
                    "> `b.serverconfig clear <field>` — reset a custom field\n\n"
                    "view changes: `b.serverinfo`",
                    bronx::COLOR_DEFAULT);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            if (sub == "bio") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("provide the bio text."));
                    return;
                }
                std::string bio = "";
                for (size_t i = 1; i < args.size(); ++i) bio += args[i] + (i == args.size() - 1 ? "" : " ");
                
                if (bio.length() > 1024) {
                    bronx::send_message(bot, event, bronx::error("biography is too long (max 1024 chars)."));
                    return;
                }

                if (db->update_guild_bio(guild_id, bio)) {
                    bronx::send_message(bot, event, bronx::success("server biography updated."));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to update bio."));
                }
            }
            else if (sub == "website") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("provide the website url."));
                    return;
                }
                std::string website = args[1];
                if (website.length() > 255) {
                    bronx::send_message(bot, event, bronx::error("url is too long."));
                    return;
                }

                if (db->update_guild_website(guild_id, website)) {
                    bronx::send_message(bot, event, bronx::success("server website updated."));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to update website."));
                }
            }
            else if (sub == "banner" || sub == "avatar") {
                std::string url = "";
                if (!event.msg.attachments.empty()) {
                    url = event.msg.attachments[0].url;
                } else if (args.size() >= 2) {
                    url = args[1];
                } else if (event.msg.message_reference.message_id) {
                    // Try to resolve from replied message
                    bot.message_get(event.msg.message_reference.message_id, event.msg.channel_id, [bot = &bot, event, db, sub, guild_id](const dpp::confirmation_callback_t& cb) {
                        if (cb.is_error()) return;
                        auto msg = std::get<dpp::message>(cb.value);
                        auto source = resolve_media_source(msg);
                        if (source.empty()) {
                            bronx::send_message(*bot, event, bronx::error("no image found in reply."));
                            return;
                        }
                        
                        bool ok = (sub == "banner") ? db->update_guild_banner(guild_id, source.url) : db->update_guild_avatar(guild_id, source.url);
                        if (ok) {
                            bronx::send_message(*bot, event, bronx::success("server " + sub + " updated."));
                        } else {
                            bronx::send_message(*bot, event, bronx::error("failed to update " + sub + "."));
                        }
                    });
                    return;
                }

                if (url.empty()) {
                    bronx::send_message(bot, event, bronx::error("provide an image url or attachment."));
                    return;
                }

                // Async validation & update
                std::thread([bot = &bot, event, db, sub, url, guild_id]() {
                    auto resp = http_get_sync(url);
                    if (resp.status != 200 || resp.content_type.find("image/") == std::string::npos) {
                        bronx::send_message(*bot, event, bronx::error("invalid or unreachable image url. make sure it's a direct image link."));
                        return;
                    }

                    bool ok = (sub == "banner") ? db->update_guild_banner(guild_id, url) : db->update_guild_avatar(guild_id, url);
                    if (ok) {
                        bronx::send_message(*bot, event, bronx::success("server " + sub + " updated."));
                    } else {
                        bronx::send_message(*bot, event, bronx::error("failed to update " + sub + "."));
                    }
                }).detach();
            }
            else if (sub == "clear") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("specify which field to clear (bio, website, banner, avatar, all)."));
                    return;
                }
                std::string field = args[1];
                std::transform(field.begin(), field.end(), field.begin(), ::tolower);
                
                if (db->clear_guild_profile_field(guild_id, field)) {
                    bronx::send_message(bot, event, bronx::success("custom " + field + " has been cleared."));
                } else {
                    bronx::send_message(bot, event, bronx::error("invalid field name."));
                }
            }
            else {
                bronx::send_message(bot, event, bronx::error("unknown sub-command. use `b.serverconfig` for help."));
            }
        },
        // ── slash handler ───────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            if (guild_id == 0) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " this command can only be used in a server.").set_flags(dpp::m_ephemeral));
                return;
            }

            // dpp automatically handles basic permissions if set in command_option, 
            // but we double check for safety here since we handle logic.
            dpp::guild* g = dpp::find_guild(guild_id);
            if (!g) return;
            auto member_it = g->members.find(event.command.usr.id);
            if (member_it == g->members.end() || !g->base_permissions(member_it->second).can(dpp::p_administrator)) {
                event.reply(dpp::message(bronx::EMOJI_DENY + " you need administrator permission to use this command.").set_flags(dpp::m_ephemeral));
                return;
            }

            auto command_interaction = std::get<dpp::command_interaction>(event.command.data);
            auto& sub = command_interaction.options[0];

            if (sub.name == "bio") {
                std::string bio = std::get<std::string>(sub.options[0].value);
                if (db->update_guild_bio(guild_id, bio)) {
                    event.reply(dpp::message(bronx::EMOJI_CHECK + " server biography updated.").set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " failed to update bio.").set_flags(dpp::m_ephemeral));
                }
            }
            else if (sub.name == "website") {
                std::string website = std::get<std::string>(sub.options[0].value);
                if (db->update_guild_website(guild_id, website)) {
                    event.reply(dpp::message(bronx::EMOJI_CHECK + " server website updated.").set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " failed to update website.").set_flags(dpp::m_ephemeral));
                }
            }
            else if (sub.name == "banner" || sub.name == "avatar") {
                std::string url = "";
                if (sub.options.size() > 0) {
                    for (auto& opt : sub.options) {
                        if (opt.name == "url") url = std::get<std::string>(opt.value);
                        else if (opt.name == "image") {
                            dpp::snowflake aid = std::get<dpp::snowflake>(opt.value);
                            auto it = event.command.resolved.attachments.find(aid);
                            if (it != event.command.resolved.attachments.end()) {
                                url = it->second.url;
                            }
                        }
                    }
                }

                if (url.empty()) {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " provide an image url or attachment.").set_flags(dpp::m_ephemeral));
                    return;
                }

                event.thinking(true);
                std::thread([bot = &bot, event, db, sub_name = sub.name, url, guild_id]() {
                    auto resp = http_get_sync(url);
                    if (resp.status != 200 || resp.content_type.find("image/") == std::string::npos) {
                        event.edit_response(dpp::message(bronx::EMOJI_DENY + " invalid or unreachable image url."));
                        return;
                    }

                    bool ok = (sub_name == "banner") ? db->update_guild_banner(guild_id, url) : db->update_guild_avatar(guild_id, url);
                    if (ok) {
                        event.edit_response(dpp::message(bronx::EMOJI_CHECK + " server " + sub_name + " updated."));
                    } else {
                        event.edit_response(dpp::message(bronx::EMOJI_DENY + " failed to update " + sub_name + "."));
                    }
                }).detach();
            }
            else if (sub.name == "clear") {
                std::string field = std::get<std::string>(sub.options[0].value);
                if (db->clear_guild_profile_field(guild_id, field)) {
                    event.reply(dpp::message(bronx::EMOJI_CHECK + " custom " + field + " has been cleared.").set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message(bronx::EMOJI_DENY + " failed to clear field.").set_flags(dpp::m_ephemeral));
                }
            }
        },
        // ── slash command options ───────────────────────────────────────────
        {
            dpp::command_option(dpp::co_sub_command, "bio", "Set the server biography")
                .add_option(dpp::command_option(dpp::co_string, "text", "The new server bio", true)),
            dpp::command_option(dpp::co_sub_command, "website", "Set the server's external website link")
                .add_option(dpp::command_option(dpp::co_string, "url", "The website URL", true)),
            dpp::command_option(dpp::co_sub_command, "banner", "Set a custom server banner")
                .add_option(dpp::command_option(dpp::co_string, "url", "Image URL", false))
                .add_option(dpp::command_option(dpp::co_attachment, "image", "Image file", false)),
            dpp::command_option(dpp::co_sub_command, "avatar", "Set a custom server avatar icon")
                .add_option(dpp::command_option(dpp::co_string, "url", "Image URL", false))
                .add_option(dpp::command_option(dpp::co_attachment, "image", "Image file", false)),
            dpp::command_option(dpp::co_sub_command, "clear", "Reset a server customization field")
                .add_option(dpp::command_option(dpp::co_string, "field", "The field to clear", true)
                    .add_choice(dpp::command_option_choice("Biography", "bio"))
                    .add_choice(dpp::command_option_choice("Website", "website"))
                    .add_choice(dpp::command_option_choice("Banner", "banner"))
                    .add_choice(dpp::command_option_choice("Avatar", "avatar"))
                    .add_choice(dpp::command_option_choice("All", "all")))
        }
    );

    serverconfig.extended_description = "allows server administrators to set a custom profile for the server that appears in `b.serverinfo`. this includes a bio, website link, and custom graphics.";
    serverconfig.examples = {"b.serverconfig bio safe space for coders", "b.serverconfig banner https://i.imgur.com/example.png", "b.serverconfig clear website"};

    return &serverconfig;
}

} // namespace utility
} // namespace commands
