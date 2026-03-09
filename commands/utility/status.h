#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../command_handler.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

// generic pagination state used by both modules and commands
struct StatusPageState {
    std::vector<std::string> pages;
    int current_page = 0;
    uint64_t message_id = 0;   // id of the message containing the current page
    uint64_t channel_id = 0;   // channel where the message was posted
};

// maps keyed by guild id
static std::unordered_map<uint64_t, StatusPageState> modules_states;
static std::unordered_map<uint64_t, StatusPageState> commands_states;

namespace commands {
namespace utility {

// helper to construct module message from state
static dpp::message build_modules_message(uint64_t gid) {
    auto it = modules_states.find(gid);
    if (it == modules_states.end()) {
        return dpp::message().add_embed(bronx::error("no module state available"));
    }
    StatusPageState &st = it->second;
    int total = st.pages.size();
    if (total == 0) {
        return dpp::message().add_embed(bronx::error("no modules registered"));
    }
    int pages = total;
    int p = st.current_page;
    if (p < 0) p = 0;
    if (p >= pages) p = pages - 1;
    std::string desc = st.pages[p];
    if (pages > 1) {
        desc += "\n\n(page " + std::to_string(p+1) + " of " + std::to_string(pages) + ")";
    }
    auto embed = bronx::create_embed(desc);
    embed.set_title("🔧 Modules");
    dpp::message msg;
    msg.add_embed(embed);
    if (pages > 1) {
        dpp::component row;
        dpp::component prev;
        prev.set_type(dpp::cot_button)
            .set_label("◀")
            .set_style(dpp::cos_secondary)
            .set_id("status_mod_prev_" + std::to_string(gid));
        dpp::component next;
        next.set_type(dpp::cot_button)
            .set_label("▶")
            .set_style(dpp::cos_secondary)
            .set_id("status_mod_next_" + std::to_string(gid));
        row.add_component(prev);
        row.add_component(next);
        msg.add_component(row);
    }
    return msg;
}

// helper to construct commands message from state
static dpp::message build_commands_message(uint64_t gid) {
    auto it = commands_states.find(gid);
    if (it == commands_states.end()) {
        return dpp::message().add_embed(bronx::error("no command state available"));
    }
    StatusPageState &st = it->second;
    int total = st.pages.size();
    if (total == 0) {
        return dpp::message().add_embed(bronx::error("no commands registered"));
    }
    int pages = total;
    int p = st.current_page;
    if (p < 0) p = 0;
    if (p >= pages) p = pages - 1;
    std::string desc = st.pages[p];
    if (pages > 1) {
        desc += "\n\n(page " + std::to_string(p+1) + " of " + std::to_string(pages) + ")";
    }
    auto embed = bronx::create_embed(desc);
    embed.set_title("⚙️ Commands");
    dpp::message msg;
    msg.add_embed(embed);
    if (pages > 1) {
        dpp::component row;
        dpp::component prev;
        prev.set_type(dpp::cot_button)
            .set_label("◀")
            .set_style(dpp::cos_secondary)
            .set_id("status_cmd_prev_" + std::to_string(gid));
        dpp::component next;
        next.set_type(dpp::cot_button)
            .set_label("▶")
            .set_style(dpp::cos_secondary)
            .set_id("status_cmd_next_" + std::to_string(gid));
        row.add_component(prev);
        row.add_component(next);
        msg.add_component(row);
    }
    return msg;
}

// register interaction handlers for both paginators
inline void register_status_interactions(dpp::cluster& bot) {
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        std::string id = event.custom_id;
        uint64_t gid = 0;
        bool is_mod = false;
        bool next = false;
        if (id.rfind("status_mod_",0) == 0) {
            is_mod = true;
            next = (id.find("_next_") != std::string::npos);
            size_t pos = id.rfind('_');
            if (pos != std::string::npos) {
                gid = std::stoull(id.substr(pos+1));
            }
        } else if (id.rfind("status_cmd_",0) == 0) {
            is_mod = false;
            next = (id.find("_next_") != std::string::npos);
            size_t pos = id.rfind('_');
            if (pos != std::string::npos) {
                gid = std::stoull(id.substr(pos+1));
            }
        } else {
            return; // not our interaction
        }
        if (event.command.guild_id != gid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this interaction isn't for this server")).set_flags(dpp::m_ephemeral));
            return;
        }
        StatusPageState *st = nullptr;
        if (is_mod) {
            auto it = modules_states.find(gid);
            if (it == modules_states.end()) return;
            st = &it->second;
        } else {
            auto it = commands_states.find(gid);
            if (it == commands_states.end()) return;
            st = &it->second;
        }
        int pages = st->pages.size();
        if (pages <= 1) return;
        if (next) {
            if (st->current_page < pages-1) st->current_page++; else st->current_page = 0;
        } else {
            if (st->current_page > 0) st->current_page--; else st->current_page = pages-1;
        }
        dpp::message newmsg = is_mod ? build_modules_message(gid) : build_commands_message(gid);
        // acknowledge the interaction by updating the original message in-place
        newmsg.channel_id = event.command.channel_id;
        event.reply(dpp::ir_update_message, newmsg);
    });
}

// list all modules and show whether they're enabled for this guild
inline Command* get_modules_command(CommandHandler* handler, bronx::db::Database* db) {
    static Command* cmd = new Command("modules",
                                      "display toggleable modules and their enabled/disabled state",
                                      "utility", {}, false,
        [handler, db](dpp::cluster& bot, const dpp::message_create_t& event,
                      const std::vector<std::string>& /*args*/) {
            if (!event.msg.guild_id) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a server.")));
                return;
            }
            uint64_t gid = event.msg.guild_id;
            uint64_t channel_id = event.msg.channel_id;
            uint64_t user_id = event.msg.author.id;
            auto role_snowflakes = event.msg.member.get_roles();
            std::vector<uint64_t> roles(role_snowflakes.begin(), role_snowflakes.end());
            auto categories = handler->get_commands_by_category();

            // build one page containing all modules (paginate at 15 entries)
            StatusPageState& st = modules_states[gid];
            st.pages.clear();
            st.current_page = 0;
            std::string cur_page;
            int per_page = 15, count = 0;
            for (const auto& [cat, cat_cmds] : categories) {
                bool enabled = true;
                if (db) {
                    try {
                        enabled = db->is_guild_module_enabled(gid, cat, user_id, channel_id, roles);
                    } catch (...) {}
                }
                cur_page += "**" + cat + "** : " + (enabled ? bronx::EMOJI_CHECK + " enabled" : bronx::EMOJI_DENY + " disabled") + "\n";
                if (++count >= per_page) {
                    st.pages.push_back(cur_page);
                    cur_page.clear();
                    count = 0;
                }
            }
            if (!cur_page.empty()) st.pages.push_back(cur_page);
            if (st.pages.empty()) st.pages.push_back("<no modules registered>");

            dpp::message msg = build_modules_message(gid);
            msg.channel_id = event.msg.channel_id;
            msg.set_reference(event.msg.id);
            if (!msg.embeds.empty()) {
                bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            }
            bot.message_create(msg, [gid](const dpp::confirmation_callback_t& cb) {
                if (!cb.is_error()) {
                    dpp::message sent = cb.get<dpp::message>();
                    modules_states[gid].message_id = sent.id;
                    modules_states[gid].channel_id = sent.channel_id;
                } else {
                    std::cerr << "[modules] message_create failed: " << cb.get_error().message << "\n";
                }
            });
        });
    return cmd;
}

// show a help-style list of commands with enabled/disabled status
inline Command* get_commands_status_command(CommandHandler* handler, bronx::db::Database* db) {
    static Command* cmd = new Command("commands",
                                      "show all commands and whether they are enabled",
                                      "utility", {}, false,
        [handler, db](dpp::cluster& bot, const dpp::message_create_t& event,
                      const std::vector<std::string>& /*args*/) {
            if (!event.msg.guild_id) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a server.")));
                return;
            }
            uint64_t gid = event.msg.guild_id;
            uint64_t channel_id = event.msg.channel_id;
            uint64_t user_id = event.msg.author.id;
            auto role_snowflakes = event.msg.member.get_roles();
            std::vector<uint64_t> roles(role_snowflakes.begin(), role_snowflakes.end());
            auto categories = handler->get_commands_by_category();

            StatusPageState& st = commands_states[gid];
            st.pages.clear();
            st.current_page = 0;
            for (const auto& [cat, cmds] : categories) {
                // check module state once — if the module is off, every command
                // in it is considered disabled regardless of per-command state
                bool module_enabled = true;
                if (db) {
                    try { module_enabled = db->is_guild_module_enabled(gid, cat, user_id, channel_id, roles); } catch (...) {}
                }

                std::string page = (module_enabled ? bronx::EMOJI_CHECK + " " : bronx::EMOJI_DENY + " ") + std::string("__") + cat + "__\n";
                std::set<std::string> seen;
                for (const auto& cmdptr : cmds) {
                    if (!seen.insert(cmdptr->name).second) continue;
                    bool cmd_enabled = module_enabled; // inherit module state
                    if (module_enabled && db) {
                        // only bother querying per-command toggle if module is on
                        try { cmd_enabled = db->is_guild_command_enabled(gid, cmdptr->name, user_id, channel_id, roles); } catch (...) {}
                    }
                    page += (cmd_enabled ? bronx::EMOJI_CHECK + " " : bronx::EMOJI_DENY + " ") + cmdptr->name + "\n";
                }
                st.pages.push_back(page);
            }
            if (st.pages.empty()) {
                st.pages.push_back("<no commands registered>");
            }

            dpp::message msg = build_commands_message(gid);
            msg.channel_id = event.msg.channel_id;
            msg.set_reference(event.msg.id);
            if (!msg.embeds.empty()) {
                bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            }
            bot.message_create(msg, [gid](const dpp::confirmation_callback_t& cb) {
                if (!cb.is_error()) {
                    dpp::message sent = cb.get<dpp::message>();
                    commands_states[gid].message_id = sent.id;
                    commands_states[gid].channel_id = sent.channel_id;
                } else {
                    std::cerr << "[commands] message_create failed: " << cb.get_error().message << "\n";
                }
            });
        });
    return cmd;
}

} // namespace utility
} // namespace commands
