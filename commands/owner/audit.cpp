#include "audit.h"
#include "owner_utils.h"
#include "../owner.h"
#include "../economy/helpers.h"
#include "../../database/operations/community/suggestion_operations.h"
#include "../../utils/logger.h"
#include <iomanip>
#include <sstream>

namespace commands {
using economy::format_number;
namespace owner {

// --- Command History (Economy Audit) ---
std::map<uint64_t, CmdHistoryState> cmdhistory_states;
std::recursive_mutex cmdhistory_mutex;
static constexpr int HISTORY_PER_PAGE = 10;

static std::string format_history_entry(const bronx::db::HistoryEntry& e) {
    std::string result = "[**" + e.entry_type + "**] " + e.description;
    if (e.amount != 0) {
        std::string sign = e.amount > 0 ? "+" : "";
        result += " (" + sign + commands::format_number(e.amount) + ")";
    }
    return result;
}

static std::string format_relative_time(std::chrono::system_clock::time_point tp) {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - tp).count();
    
    if (diff < 60) return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    return std::to_string(diff / 86400) + "d ago";
}

dpp::message build_cmdhistory_message(bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
    CmdHistoryState& state = cmdhistory_states[owner_id];
    
    if (state.target_user == 0) {
        auto embed = bronx::create_embed("no user selected. use `.cmdh <user>` to view history.");
        dpp::message msg;
        msg.add_embed(embed);
        return msg;
    }
    
    int total = db->get_history_count(state.target_user);
    int pages = std::max(1, (total + HISTORY_PER_PAGE - 1) / HISTORY_PER_PAGE);
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;
    
    int offset = state.current_page * HISTORY_PER_PAGE;
    auto entries = db->fetch_history(state.target_user, HISTORY_PER_PAGE, offset);
    
    std::string desc;
    if (entries.empty()) {
        desc = "no history recorded for this user";
    } else {
        for (const auto& e : entries) {
            desc += format_history_entry(e);
            desc += " _" + format_relative_time(e.created_at) + "_\n";
        }
    }
    desc += "\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(pages) + "_";
    desc += " • _" + std::to_string(total) + " total entries_";
    
    auto embed = bronx::create_embed(desc)
        .set_title("command history for <@" + std::to_string(state.target_user) + ">")
        .set_color(0x7289DA);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    // Navigation row
    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ previous")
        .set_style(dpp::cos_secondary)
        .set_id("cmdh_nav_prev")
        .set_disabled(pages <= 1);
    nav_row.add_component(prev_btn);
    
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("cmdh_nav_next")
        .set_disabled(pages <= 1);
    nav_row.add_component(next_btn);
    
    dpp::component refresh_btn;
    refresh_btn.set_type(dpp::cot_button)
        .set_label("🔄 refresh")
        .set_style(dpp::cos_primary)
        .set_id("cmdh_refresh");
    nav_row.add_component(refresh_btn);
    
    dpp::component clear_btn;
    clear_btn.set_type(dpp::cot_button)
        .set_label("🗑 clear all")
        .set_style(dpp::cos_danger)
        .set_id("cmdh_clear")
        .set_disabled(total == 0);
    nav_row.add_component(clear_btn);
    
    msg.add_component(nav_row);
    
    return msg;
}

// --- Suggestions Audit ---
std::map<uint64_t, SuggestState> suggest_states;
std::recursive_mutex suggest_mutex;
static constexpr int SUGGESTIONS_PER_PAGE = 1;

dpp::message build_suggestions_message(bronx::db::Database* db, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
    SuggestState& state = suggest_states[owner_id];
    
    std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
    auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
    int per_page = SUGGESTIONS_PER_PAGE;
    int total = suggestions.size();
    int pages = (total + per_page - 1) / per_page;
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;

    int start = state.current_page * per_page;
    int end = std::min(start + per_page, total);

    std::string desc;
    if (total == 0) {
        desc = "no suggestions yet";
    } else {
        for (int i = start; i < end; ++i) {
            const auto& s = suggestions[i];
            desc += "**" + std::to_string(s.id) + "** ";
            desc += s.read ? bronx::EMOJI_CHECK : "🆕";
            desc += " <@" + std::to_string(s.user_id) + "> ";
            desc += "(`" + commands::format_number(s.networth) + "`)";
            desc += "\n" + s.suggestion;
            if (i < end - 1) desc += "\n\n";
        }
    }
    desc += "\n\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(std::max(1, pages)) + "_";

    auto embed = bronx::create_embed(desc).set_title("user suggestions");

    dpp::message msg;
    msg.add_embed(embed);

    for (int i = start; i < end; ++i) {
        const auto& s = suggestions[i];
        dpp::component row;
        row.set_type(dpp::cot_action_row);
        dpp::component read_btn;
        read_btn.set_type(dpp::cot_button)
            .set_label("mark read")
            .set_style(dpp::cos_primary)
            .set_id("suggest_read_" + std::to_string(s.id))
            .set_disabled(s.read);
        row.add_component(read_btn);
        dpp::component del_btn;
        del_btn.set_type(dpp::cot_button)
            .set_label("delete")
            .set_style(dpp::cos_danger)
            .set_id("suggest_del_" + std::to_string(s.id));
        row.add_component(del_btn);
        msg.add_component(row);
    }

    dpp::component nav_row1;
    nav_row1.set_type(dpp::cot_action_row);
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ previous")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_nav_prev");
    nav_row1.add_component(prev_btn);
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_nav_next");
    nav_row1.add_component(next_btn);

    dpp::component goto_btn;
    goto_btn.set_type(dpp::cot_button)
        .set_label("goto")
        .set_style(dpp::cos_secondary)
        .set_id("suggest_goto")
        .set_disabled(total == 0);
    nav_row1.add_component(goto_btn);

    dpp::component delete_page_btn;
    delete_page_btn.set_type(dpp::cot_button)
        .set_label("delete page")
        .set_style(dpp::cos_danger)
        .set_id("suggest_delete_page")
        .set_disabled(total == 0);
    nav_row1.add_component(delete_page_btn);

    msg.add_component(nav_row1);

    std::string date_label = "date" + std::string(state.order_by == "submitted_at" ? (state.asc ? " ▲" : " ▼") : "");
    std::string net_label = "balance" + std::string(state.order_by == "networth" ? (state.asc ? " ▲" : " ▼") : "");
    std::string alpha_label = "alphabetical" + std::string(state.order_by == "suggestion" ? (state.asc ? " ▲" : " ▼") : "");

    dpp::component nav_row2;
    nav_row2.set_type(dpp::cot_action_row);
    dpp::component sort_date;
    sort_date.set_type(dpp::cot_button)
        .set_label(date_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_date");
    nav_row2.add_component(sort_date);

    dpp::component sort_net;
    sort_net.set_type(dpp::cot_button)
        .set_label(net_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_networth");
    nav_row2.add_component(sort_net);

    dpp::component sort_alpha;
    sort_alpha.set_type(dpp::cot_button)
        .set_label(alpha_label)
        .set_style(dpp::cos_success)
        .set_id("suggest_sort_alpha");
    nav_row2.add_component(sort_alpha);

    msg.add_component(nav_row2);
    return msg;
}

// --- Server List ---
std::map<uint64_t, ServerListState> server_list_states;
std::recursive_mutex server_list_mutex;
static constexpr int SERVERS_PER_PAGE = 3;

dpp::message build_servers_message(dpp::cluster& bot, uint64_t owner_id) {
    std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
    ServerListState& state = server_list_states[owner_id];
    
    std::vector<dpp::guild*> guilds;
    dpp::cache<dpp::guild>* guild_cache = dpp::get_guild_cache();
    
    {
        std::shared_lock l(guild_cache->get_mutex());
        for (auto& [id, guild_ptr] : guild_cache->get_container()) {
            if (guild_ptr) {
                guilds.push_back(guild_ptr);
            }
        }
    }
    
    std::sort(guilds.begin(), guilds.end(), [&state](dpp::guild* a, dpp::guild* b) {
        bool result = false;
        if (state.sort_by == "name") result = a->name < b->name;
        else if (state.sort_by == "members") result = a->member_count < b->member_count;
        else if (state.sort_by == "id") result = a->id < b->id;
        return state.asc ? result : !result;
    });
    
    int total = guilds.size();
    int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
    if (state.current_page < 0) state.current_page = 0;
    if (state.current_page >= pages) state.current_page = pages - 1;
    
    int start = state.current_page * SERVERS_PER_PAGE;
    int end = std::min(start + SERVERS_PER_PAGE, total);
    
    std::string desc;
    if (total == 0) {
        desc = "no servers found";
    } else {
        for (int i = start; i < end; ++i) {
            auto* g = guilds[i];
            desc += "**" + g->name + "**\n";
            desc += "• id: `" + std::to_string(g->id) + "`\n";
            desc += "• members: " + std::to_string(g->member_count) + "\n";
            desc += "• owner: <@" + std::to_string(g->owner_id) + ">\n";
            if (i < end - 1) desc += "\n";
        }
    }
    desc += "\n_page " + std::to_string(state.current_page + 1) + " of " + std::to_string(pages) + "_";
    desc += " • _" + std::to_string(total) + " total servers_";
    
    auto embed = bronx::create_embed(desc).set_title("server list");
    embed.set_color(0x5865F2);
    
    dpp::message msg;
    msg.add_embed(embed);
    
    for (int i = start; i < end; ++i) {
        auto* g = guilds[i];
        dpp::component row;
        row.set_type(dpp::cot_action_row);
        dpp::component leave_btn;
        leave_btn.set_type(dpp::cot_button)
            .set_label("leave " + g->name.substr(0, std::min(40, (int)g->name.length())))
            .set_style(dpp::cos_danger)
            .set_id("serverlist_leave_" + std::to_string(g->id));
        row.add_component(leave_btn);
        msg.add_component(row);
    }
    
    dpp::component nav_row;
    nav_row.set_type(dpp::cot_action_row);
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_label("◀ previous")
        .set_style(dpp::cos_secondary)
        .set_id("serverlist_nav_prev")
        .set_disabled(pages <= 1);
    nav_row.add_component(prev_btn);
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_label("next ▶")
        .set_style(dpp::cos_secondary)
        .set_id("serverlist_nav_next")
        .set_disabled(pages <= 1);
    nav_row.add_component(next_btn);
    dpp::component refresh_btn;
    refresh_btn.set_type(dpp::cot_button)
        .set_label("🔄 refresh")
        .set_style(dpp::cos_primary)
        .set_id("serverlist_refresh");
    nav_row.add_component(refresh_btn);
    msg.add_component(nav_row);
    
    std::string name_label = "name" + std::string(state.sort_by == "name" ? (state.asc ? " ▲" : " ▼") : "");
    std::string members_label = "members" + std::string(state.sort_by == "members" ? (state.asc ? " ▲" : " ▼") : "");
    std::string id_label = "id" + std::string(state.sort_by == "id" ? (state.asc ? " ▲" : " ▼") : "");
    
    dpp::component sort_row;
    sort_row.set_type(dpp::cot_action_row);
    dpp::component sort_name;
    sort_name.set_type(dpp::cot_button)
        .set_label(name_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_name");
    sort_row.add_component(sort_name);
    dpp::component sort_members;
    sort_members.set_type(dpp::cot_button)
        .set_label(members_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_members");
    sort_row.add_component(sort_members);
    dpp::component sort_id;
    sort_id.set_type(dpp::cot_button)
        .set_label(id_label)
        .set_style(dpp::cos_success)
        .set_id("serverlist_sort_id");
    sort_row.add_component(sort_id);
    msg.add_component(sort_row);
    
    return msg;
}

bool handle_audit_interaction(const dpp::interaction_create_t& event, dpp::cluster& bot, bronx::db::Database* db) {
    uint64_t user_id = event.command.get_issuing_user().id;
    if (!commands::is_owner(user_id)) return false;

    if (event.command.type == dpp::it_modal_submit) {
        const auto& modal_data = static_cast<const dpp::form_submit_t&>(event);
        if (modal_data.custom_id == "suggest_goto_modal") {
            if (modal_data.components.empty()) return true;
            std::string page_str;
            try { 
                // In D++ 10.x form_submit_t components are often the nested ones (rows)
                // or sometimes they are flattened. If they are rows, we need modal_data.components[0].components[0].value.
                // But the original code used .components[0].value.
                // I will try to see if modal_data.components[0].value is valid (it is a member of component).
                page_str = std::get<std::string>(modal_data.components[0].value); 
            } catch (...) { 
                try {
                    // Try nested just in case
                    if (!modal_data.components[0].components.empty()) {
                         page_str = std::get<std::string>(modal_data.components[0].components[0].value);
                    } else {
                         return true;
                    }
                } catch (...) {
                    return true;
                }
            }
            int page = 0;
            try { page = std::stoi(page_str) - 1; } catch (...) { return true; }
            if (page < 0) page = 0;
            {
                std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
                SuggestState& state = suggest_states[user_id];
                std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
                auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
                int total = suggestions.size();
                int pages = std::max(1, (total + SUGGESTIONS_PER_PAGE - 1) / SUGGESTIONS_PER_PAGE);
                if (page >= pages) page = pages - 1;
                state.current_page = page;
                event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
            }
            return true;
        }
        return false;
    }

    if (event.command.type == dpp::it_component_button) {
        const auto& comp_data = std::get<dpp::component_interaction>(event.command.data);
        std::string custom_id = comp_data.custom_id;

        // Suggestions
        if (custom_id.rfind("suggest_", 0) == 0) {
            std::lock_guard<std::recursive_mutex> lock(suggest_mutex);
            SuggestState& state = suggest_states[user_id];

            if (custom_id == "suggest_nav_prev" || custom_id == "suggest_nav_next") {
                std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
                auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
                int total = suggestions.size();
                int pages = std::max(1, (total + SUGGESTIONS_PER_PAGE - 1) / SUGGESTIONS_PER_PAGE);
                if (custom_id == "suggest_nav_prev") {
                    if (state.current_page > 0) state.current_page--;
                    else state.current_page = pages - 1;
                } else {
                    if (state.current_page < pages - 1) state.current_page++;
                    else state.current_page = 0;
                }
                event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
            } else if (custom_id == "suggest_delete_page") {
                std::string clause = state.order_by + (state.asc ? " ASC" : " DESC");
                auto suggestions = bronx::db::suggestion_operations::fetch_suggestions(db, clause);
                int start = state.current_page * SUGGESTIONS_PER_PAGE;
                int end = std::min(start + SUGGESTIONS_PER_PAGE, (int)suggestions.size());
                for (int i = start; i < end; ++i) {
                    bronx::db::suggestion_operations::delete_suggestion(db, suggestions[i].id);
                }
                event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
            } else if (custom_id == "suggest_goto") {
                dpp::interaction_modal_response modal("suggest_goto_modal", "Go to page");
                modal.add_component(dpp::component().set_label("Page number").set_id("page_input").set_type(dpp::cot_text).set_placeholder("Enter page number").set_min_length(1).set_max_length(10).set_text_style(dpp::text_short));
                event.dialog(modal);
            } else if (custom_id.find("suggest_sort_") == 0) {
                std::string field;
                if (custom_id == "suggest_sort_date") field = "submitted_at";
                else if (custom_id == "suggest_sort_networth") field = "networth";
                else if (custom_id == "suggest_sort_alpha") field = "suggestion";
                if (!field.empty()) {
                    if (state.order_by == field) state.asc = !state.asc;
                    else { state.order_by = field; state.asc = false; state.current_page = 0; }
                    event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
                }
            } else if (custom_id.find("suggest_read_") == 0) {
                uint64_t sid = std::stoull(custom_id.substr(13));
                bronx::db::suggestion_operations::mark_read(db, sid);
                event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
            } else if (custom_id.find("suggest_del_") == 0) {
                uint64_t sid = std::stoull(custom_id.substr(12));
                bronx::db::suggestion_operations::delete_suggestion(db, sid);
                event.reply(dpp::ir_update_message, build_suggestions_message(db, user_id));
            }
            return true;
        }

        // Server List
        if (custom_id.rfind("serverlist_", 0) == 0) {
            std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
            ServerListState& srv_state = server_list_states[user_id];

            if (custom_id == "serverlist_nav_prev" || custom_id == "serverlist_nav_next" || custom_id == "serverlist_refresh") {
                int total = dpp::get_guild_cache()->count();
                int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
                if (custom_id == "serverlist_nav_prev") {
                    if (srv_state.current_page > 0) srv_state.current_page--;
                    else srv_state.current_page = pages - 1;
                } else if (custom_id == "serverlist_nav_next") {
                    if (srv_state.current_page < pages - 1) srv_state.current_page++;
                    else srv_state.current_page = 0;
                }
                event.reply(dpp::ir_update_message, build_servers_message(bot, user_id));
            } else if (custom_id.find("serverlist_sort_") == 0) {
                std::string field;
                if (custom_id == "serverlist_sort_name") field = "name";
                else if (custom_id == "serverlist_sort_members") field = "members";
                else if (custom_id == "serverlist_sort_id") field = "id";
                if (!field.empty()) {
                    if (srv_state.sort_by == field) srv_state.asc = !srv_state.asc;
                    else { srv_state.sort_by = field; srv_state.asc = true; srv_state.current_page = 0; }
                    event.reply(dpp::ir_update_message, build_servers_message(bot, user_id));
                }
            } else if (custom_id.find("serverlist_leave_") == 0) {
                uint64_t guild_id = std::stoull(custom_id.substr(17));
                dpp::guild* g = dpp::find_guild(guild_id);
                std::string guild_name = g ? g->name : "Unknown";
                
                event.reply(dpp::ir_deferred_update_message, dpp::message());
                bot.current_user_leave_guild(guild_id, [&bot, event, user_id, guild_name, guild_id](const dpp::confirmation_callback_t& callback) {
                    if (callback.is_error()) {
                        bot.interaction_response_edit(event.command.token, dpp::message().add_embed(bronx::error("Failed to leave **" + guild_name + "**\n" + callback.get_error().message)));
                    } else {
                        dpp::message msg;
                        {
                            std::lock_guard<std::recursive_mutex> lock(server_list_mutex);
                            int total = dpp::get_guild_cache()->count();
                            int pages = std::max(1, (total + SERVERS_PER_PAGE - 1) / SERVERS_PER_PAGE);
                            if (server_list_states[user_id].current_page >= pages && pages > 0) server_list_states[user_id].current_page = pages - 1;
                            msg = build_servers_message(bot, user_id);
                        }
                        bot.interaction_response_edit(event.command.token, msg);
                        bot.interaction_followup_create(event.command.token, dpp::message().add_embed(bronx::success("Left server: **" + guild_name + "**")).set_flags(dpp::m_ephemeral));
                    }
                });
            }
            return true;
        }

        // Command History
        if (custom_id.rfind("cmdh_", 0) == 0) {
            std::lock_guard<std::recursive_mutex> lock(cmdhistory_mutex);
            CmdHistoryState& hstate = cmdhistory_states[user_id];
            if (hstate.target_user == 0) {
                event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(bronx::error("No user selected")).set_flags(dpp::m_ephemeral));
                return true;
            }
            if (custom_id == "cmdh_nav_prev" || custom_id == "cmdh_nav_next" || custom_id == "cmdh_refresh") {
                int total = db->get_history_count(hstate.target_user);
                int pages = std::max(1, (total + HISTORY_PER_PAGE - 1) / HISTORY_PER_PAGE);
                if (custom_id == "cmdh_nav_prev") {
                    if (hstate.current_page > 0) hstate.current_page--;
                    else hstate.current_page = pages - 1;
                } else if (custom_id == "cmdh_nav_next") {
                    if (hstate.current_page < pages - 1) hstate.current_page++;
                    else hstate.current_page = 0;
                }
                event.reply(dpp::ir_update_message, build_cmdhistory_message(db, user_id));
            } else if (custom_id == "cmdh_clear") {
                db->clear_history(hstate.target_user);
                hstate.current_page = 0;
                event.reply(dpp::ir_update_message, build_cmdhistory_message(db, user_id));
            }
            return true;
        }
    }

    return false;
}

} // namespace owner
} // namespace commands
