#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <regex>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <sstream>

namespace commands {
namespace utility {

// download helper using libcurl
static size_t _write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto *buf = static_cast<::std::string*>(userp);
    buf->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static bool download_url(const ::std::string& url, ::std::string& out, long timeout_seconds = 10) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bronxbot/steal-command");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

inline Command* get_steal_command() {
    static Command steal("steal", "steal custom emojis into this server (manage emojis required)", "utility", {"clone"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("use this command inside a server"));
                return;
            }
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: steal :emoji1: :emoji2: ... or CDN links like https://cdn.discordapp.com/emojis/<id>.<ext>?name=<name>"));
                return;
            }

            // permission check: member must have Manage Emojis & Stickers or Administrator
            bool allowed = false;
            dpp::guild* g = dpp::find_guild(event.msg.guild_id);
            if (g && g->owner_id == event.msg.author.id) allowed = true;
            for (const auto &rid : event.msg.member.get_roles()) {
                dpp::role* r = dpp::find_role(rid);
                if (!r) continue;
                uint64_t perms = static_cast<uint64_t>(r->permissions);
                if (perms & static_cast<uint64_t>(dpp::p_administrator)) { allowed = true; break; }
                if (perms & static_cast<uint64_t>(dpp::p_manage_emojis_and_stickers)) { allowed = true; break; }
            }
            if (!allowed) {
                bronx::send_message(bot, event, bronx::error("you need Manage Emojis & Stickers or Administrator to use this command"));
                return;
            }

            // helper: extract URL from token (handles raw URL or markdown-style link)
            auto extract_url = [](const ::std::string &tok) -> ::std::string {
                size_t p = tok.find("http");
                if (p == ::std::string::npos) return "";
                size_t end = tok.find_first_of(")> \t\n\"", p);
                if (end == ::std::string::npos) end = tok.size();
                return tok.substr(p, end - p);
            };

            // parser: accepts <a:name:id>, Discord CDN emoji URLs, or markdown links containing a CDN URL
            auto parse_token = [&](const ::std::string &tok, ::std::string &name_out, ::std::string &id_out, ::std::string &ext_out, bool &animated_out) -> bool {
                ::std::smatch m;
                ::std::regex mention_re(R"(<a?:([A-Za-z0-9_]+):([0-9]+)>)");
                if (::std::regex_search(tok, m, mention_re)) {
                    animated_out = (tok.rfind("<a:", 0) == 0);
                    name_out = m[1];
                    id_out = m[2];
                    ext_out = animated_out ? "gif" : "png";
                    return true;
                }

                ::std::string url = extract_url(tok);
                if (url.empty()) return false;
                auto pos = url.find("/emojis/");
                if (pos == ::std::string::npos) return false;
                size_t id_start = pos + 8; // after /emojis/
                size_t dot = url.find('.', id_start);
                if (dot == ::std::string::npos) return false;
                id_out = url.substr(id_start, dot - id_start);
                size_t ext_start = dot + 1;
                size_t ext_end = url.find_first_of("?#&/ ", ext_start);
                if (ext_end == ::std::string::npos) ext_end = url.size();
                ext_out = url.substr(ext_start, ext_end - ext_start);
                ::std::transform(ext_out.begin(), ext_out.end(), ext_out.begin(), [](unsigned char c){ return ::std::tolower(c); });
                animated_out = (ext_out == "gif");

                // try to extract name=... from query string, otherwise fallback
                ::std::string name_found;
                size_t name_pos = url.find("name=");
                if (name_pos != ::std::string::npos) {
                    size_t name_start = name_pos + 5;
                    size_t name_end = url.find('&', name_start);
                    if (name_end == ::std::string::npos) name_end = url.size();
                    name_found = url.substr(name_start, name_end - name_start);
                    // optional: percent-decode name_found (skip for now)
                }
                if (!name_found.empty()) name_out = name_found;
                else name_out = ::std::string("emoji_") + id_out;
                return true;
            };

            struct ParsedEmoji { ::std::string name; ::std::string id; ::std::string ext; bool animated; ::std::string status; };
            ::std::vector<ParsedEmoji> parsed;
            for (const auto &tok : args) {
                ParsedEmoji p;
                if (!parse_token(tok, p.name, p.id, p.ext, p.animated)) continue;
                p.status = "⏳ pending";
                parsed.push_back(::std::move(p));
            }
            if (parsed.empty()) {
                bronx::send_message(bot, event, bronx::error("no valid custom emojis found"));
                return;
            }

            auto build_embed = [&](const ::std::vector<ParsedEmoji> &vec) {
                ::std::string desc = "Importing " + ::std::to_string(vec.size()) + " emoji(s)...\n\n";
                for (const auto &it : vec) desc += "• " + it.name + " — " + it.status + "\n";
                auto e = bronx::create_embed(desc);
                bronx::add_invoker_footer(e, event.msg.author);
                return e;
            };

            // send single status message then edit it as we progress
            bot.message_create(dpp::message(event.msg.channel_id, build_embed(parsed)), [&, items_ptr = ::std::make_shared<::std::vector<ParsedEmoji>>(parsed)](const dpp::confirmation_callback_t& cb) mutable {
                if (cb.is_error()) return;
                auto status_msg = ::std::get<dpp::message>(cb.value);
                auto update_status_message = [&](void) {
                    ::std::string desc = "Importing " + ::std::to_string(items_ptr->size()) + " emoji(s)...\n\n";
                    for (const auto &it : *items_ptr) desc += "• " + it.name + " — " + it.status + "\n";
                    auto embed = bronx::create_embed(desc);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    dpp::message edit_msg = status_msg;
                    edit_msg.embeds.clear();
                    edit_msg.add_embed(embed);
                    bronx::safe_message_edit(bot, edit_msg);
                };

                for (size_t i = 0; i < items_ptr->size(); ++i) {
                    auto &it = (*items_ptr)[i];
                    it.status = "⏳ downloading";
                    update_status_message();

                    ::std::string url = "https://cdn.discordapp.com/emojis/" + it.id + "." + it.ext;
                    ::std::string data;
                    if (!download_url(url, data)) {
                        it.status = bronx::EMOJI_DENY + " failed to download";
                        update_status_message();
                        continue;
                    }

                    if (data.size() > 256 * 1024) {
                        it.status = bronx::EMOJI_DENY + " too large";
                        update_status_message();
                        continue;
                    }

                    dpp::emoji newemoji(it.name, 0, it.animated ? dpp::emoji_flags::e_animated : 0);
                    try {
                        dpp::image_type img_type = dpp::i_png;
                        if (it.ext == "gif") img_type = dpp::i_gif;
                        else if (it.ext == "webp") img_type = dpp::i_webp;
                        else if (it.ext == "jpg" || it.ext == "jpeg") img_type = dpp::i_jpg;
                        newemoji.load_image(data, img_type);
                    } catch (const ::std::exception& ex) {
                        it.status = bronx::EMOJI_DENY + " failed to load: " + ex.what();
                        update_status_message();
                        continue;
                    }

                    it.status = "⏳ creating";
                    update_status_message();

                    // keep items_ptr and status_msg alive for the async callback
                    bot.guild_emoji_create(event.msg.guild_id, newemoji, [items_ptr, status_msg, i, author = event.msg.author, b = &bot](const dpp::confirmation_callback_t& cb2) mutable {
                        if (cb2.is_error()) (*items_ptr)[i].status = bronx::EMOJI_DENY + " failed to create";
                        else (*items_ptr)[i].status = bronx::EMOJI_CHECK + " imported";

                        ::std::string desc = "Importing " + ::std::to_string(items_ptr->size()) + " emoji(s)...\n\n";
                        for (const auto &it2 : *items_ptr) desc += "• " + it2.name + " — " + it2.status + "\n";
                        auto embed2 = bronx::create_embed(desc);
                        bronx::add_invoker_footer(embed2, author);
                        dpp::message edit_msg2 = status_msg;
                        edit_msg2.embeds.clear();
                        edit_msg2.add_embed(embed2);
                        b->message_edit(edit_msg2);
                    });
                }
            });
        },
        // slash variant: accepts a single string with emojis separated by spaces
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!event.command.guild_id) {
                event.reply(dpp::message().add_embed(bronx::error("use this command inside a server")));
                return;
            }

            uint64_t user_id = event.command.get_issuing_user().id;
            // permission check removed temporarily; TODO reimplement after fixing dpp API

                ::std::string emojis = ::std::get<::std::string>(event.get_parameter("emojis"));
                // helper: extract URL from token (handles raw URL or markdown-style link)
                auto extract_url = [](const ::std::string &tok) -> ::std::string {
                    size_t p = tok.find("http");
                    if (p == ::std::string::npos) return "";
                    size_t end = tok.find_first_of(")> \t\n\"", p);
                    if (end == ::std::string::npos) end = tok.size();
                    return tok.substr(p, end - p);
                };

                auto parse_token = [&](const ::std::string &tok, ::std::string &name_out, ::std::string &id_out, ::std::string &ext_out, bool &animated_out) -> bool {
                    ::std::smatch m;
                    ::std::regex mention_re(R"(<a?:([A-Za-z0-9_]+):([0-9]+)>)");
                    if (::std::regex_search(tok, m, mention_re)) {
                        animated_out = (tok.rfind("<a:", 0) == 0);
                        name_out = m[1];
                        id_out = m[2];
                        ext_out = animated_out ? "gif" : "png";
                        return true;
                    }
                    ::std::string url = extract_url(tok);
                    if (url.empty()) return false;
                    auto pos = url.find("/emojis/");
                    if (pos == ::std::string::npos) return false;
                    size_t id_start = pos + 8; // after /emojis/
                    size_t dot = url.find('.', id_start);
                    if (dot == ::std::string::npos) return false;
                    id_out = url.substr(id_start, dot - id_start);
                    size_t ext_start = dot + 1;
                    size_t ext_end = url.find_first_of("?#&/ ", ext_start);
                    if (ext_end == ::std::string::npos) ext_end = url.size();
                    ext_out = url.substr(ext_start, ext_end - ext_start);
                    ::std::transform(ext_out.begin(), ext_out.end(), ext_out.begin(), [](unsigned char c){ return ::std::tolower(c); });
                    animated_out = (ext_out == "gif");
                    ::std::string name_found;
                    size_t name_pos = url.find("name=");
                    if (name_pos != ::std::string::npos) {
                        size_t name_start = name_pos + 5;
                        size_t name_end = url.find('&', name_start);
                        if (name_end == ::std::string::npos) name_end = url.size();
                        name_found = url.substr(name_start, name_end - name_start);
                    }
                    if (!name_found.empty()) name_out = name_found;
                    else name_out = ::std::string("emoji_") + id_out;
                    return true;
                };

                struct ParsedEmoji { ::std::string name; ::std::string id; ::std::string ext; bool animated; ::std::string status; };
                ::std::vector<ParsedEmoji> parsed;
                ::std::istringstream iss(emojis);
                ::std::string tok;
                while (iss >> tok) {
                    ParsedEmoji p;
                    if (!parse_token(tok, p.name, p.id, p.ext, p.animated)) continue;
                    p.status = "⏳ pending";
                    parsed.push_back(::std::move(p));
                }
                if (parsed.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("no valid custom emojis found")));
                    return;
                }

                auto build_embed = [&](const ::std::vector<ParsedEmoji> &vec) {
                    ::std::string desc = "Importing " + ::std::to_string(vec.size()) + " emoji(s)...\n\n";
                    for (const auto &it : vec) desc += "• " + it.name + " — " + it.status + "\n";
                    auto e = bronx::create_embed(desc);
                    bronx::add_invoker_footer(e, event.command.get_issuing_user());
                    return e;
                };

                event.reply(dpp::message().add_embed(build_embed(parsed)), [&, items_ptr = ::std::make_shared<::std::vector<ParsedEmoji>>(parsed)](const dpp::confirmation_callback_t& cb) mutable {
                    if (cb.is_error()) return;
                    if (!std::holds_alternative<dpp::message>(cb.value)) return;
                    auto status_msg = ::std::get<dpp::message>(cb.value);
                    auto update_status_message = [&](void) {
                        ::std::string desc = "Importing " + ::std::to_string(items_ptr->size()) + " emoji(s)...\n\n";
                        for (const auto &it : *items_ptr) desc += "• " + it.name + " — " + it.status + "\n";
                        auto embed = bronx::create_embed(desc);
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        dpp::message edit_msg = status_msg;
                        edit_msg.embeds.clear();
                        edit_msg.add_embed(embed);
                        bronx::safe_message_edit(bot, edit_msg);
                    };

                    for (size_t i = 0; i < items_ptr->size(); ++i) {
                        auto &it = (*items_ptr)[i];
                        it.status = "⏳ downloading";
                        update_status_message();

                        ::std::string url = "https://cdn.discordapp.com/emojis/" + it.id + "." + it.ext;
                        ::std::string data;
                        if (!download_url(url, data)) {
                            it.status = bronx::EMOJI_DENY + " failed to download";
                            update_status_message();
                            continue;
                        }

                        if (data.size() > 256 * 1024) {
                            it.status = bronx::EMOJI_DENY + " too large";
                            update_status_message();
                            continue;
                        }

                        dpp::emoji newemoji(it.name, 0, it.animated ? dpp::emoji_flags::e_animated : 0);
                        try {
                            dpp::image_type img_type = dpp::i_png;
                            if (it.ext == "gif") img_type = dpp::i_gif;
                            else if (it.ext == "webp") img_type = dpp::i_webp;
                            else if (it.ext == "jpg" || it.ext == "jpeg") img_type = dpp::i_jpg;
                            newemoji.load_image(data, img_type);
                        } catch (const ::std::exception& ex) {
                            it.status = bronx::EMOJI_DENY + " failed to load: " + ex.what();
                            update_status_message();
                            continue;
                        }

                        it.status = "⏳ creating";
                        update_status_message();

                        bot.guild_emoji_create(event.command.guild_id, newemoji, [items_ptr, status_msg, i, user = event.command.get_issuing_user(), b = &bot](const dpp::confirmation_callback_t& cb2) mutable {
                            if (cb2.is_error()) (*items_ptr)[i].status = bronx::EMOJI_DENY + " failed to create";
                            else (*items_ptr)[i].status = bronx::EMOJI_CHECK + " imported";

                            ::std::string desc = "Importing " + ::std::to_string(items_ptr->size()) + " emoji(s)...\n\n";
                            for (const auto &it2 : *items_ptr) desc += "• " + it2.name + " — " + it2.status + "\n";
                            auto embed2 = bronx::create_embed(desc);
                            bronx::add_invoker_footer(embed2, user);
                            dpp::message edit_msg2 = status_msg;
                            edit_msg2.embeds.clear();
                            edit_msg2.add_embed(embed2);
                            b->message_edit(edit_msg2);
                        });
                    }
                });
        },
        {
            dpp::command_option(dpp::co_string, "emojis", "space-separated emojis to steal (custom emojis only)", true)
        }
    );

    return &steal;
}

} // namespace utility
} // namespace commands
