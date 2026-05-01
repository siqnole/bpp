#pragma once
#include "../command.h"
#include "fishing/fishing_helpers.h"
#include "fishing/fish.h"
#include "fishing/inventory.h"
#include "fishing/sell.h"
#include "fishing/simple_commands.h"
#include "fishing/finfo.h"
#include "fishing/suggest_fish.h"
#include "fishing/fishdex.h"
#include "fishing/crews.h"
#include "fishing/fish_parent.h"
#include <vector>
#include <algorithm>
#include "../utils/logger.h"
#include "../log.h"

namespace commands {

// Main entry point for all fishing commands
inline ::std::vector<Command*> get_fishing_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Only initialize once
    if (cmds.empty()) {
        // CONSOLIDATED: Main fishing commands now use /fish parent command with subcommands
        // This replaces: fish, finv, sellfish, lockfish, finfo, equip, suggestfish, crew, autofisher
        // Savings: 8-9 slash commands
        cmds.push_back(::commands::fishing::create_fish_parent_command(db));
        
        // --- Re-inject text-only aliases for fishing commands (fish, finv, sellfish, lockfish, finfo, equip, suggestfish, crew) ---
        auto actions = ::commands::fishing::get_fishing_actions(db);
        for (const auto& action : actions) {
            Command* text_cmd = action.getter(db);
            if (text_cmd) {
                text_cmd->is_slash_command = false;
                
                // Add old aliases specifically for fish (cast) to support "fish", "fsh"
                if (action.name == "cast") {
                    text_cmd->aliases.push_back("fish");
                    text_cmd->aliases.push_back("fsh");
                }
                
                cmds.push_back(text_cmd);
            }
        }
        
        // KEPT SEPARATE: Fishdex commands (specialized fish encyclopedia functionality)
        // Can be consolidated to /fishdex parent later if needed
        auto fishdex_cmds = ::commands::fishing::get_fishdex_commands(db);
        for (auto* cmd : fishdex_cmds) {
            cmds.push_back(cmd);
        }
    }
    
    return cmds;
}

// Register fishing interaction handlers
inline void register_fishing_interactions(dpp::cluster& bot, Database* db) {
    ::commands::fishing::register_finfo_interactions(bot, db);
    ::commands::fishing::register_finv_interactions(bot, db);
    ::commands::fishing::register_inv_interactions(bot, db);
    ::commands::fishing::register_fishdex_interactions(bot, db);

    // ---- Fishing minigame reel-in button handler ----
    bot.on_button_click([db,&bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("fish_reel_", 0) != 0) return;

        // parse user id from fish_reel_<uid>
        uint64_t uid;
        try {
            uid = std::stoull(event.custom_id.substr(strlen("fish_reel_")));
        } catch (...) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("invalid button format")).set_flags(dpp::m_ephemeral));
            return;
        }

        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this isn't your fishing rod!")).set_flags(dpp::m_ephemeral));
            return;
        }

        ::commands::fishing::handle_reel_in(bot, db, uid, event.command.channel_id, event);
    });

    // pagination buttons and quick-sell-all for fish receipt
    bot.on_button_click([db,&bot](const dpp::button_click_t& event) {
        bool has_replied = false;
        try {
            // we respond to either navigation controls or the sell‑all button
            if (event.custom_id.rfind("fish_nav_", 0) != 0 &&
                event.custom_id.rfind("fish_sellall_", 0) != 0) return;
                
            bronx::logger::debug("fishing 🎣", "button handler processing: " + event.custom_id + " from user " + std::to_string(event.command.get_issuing_user().id));
            // parse user id from the custom_id. navigation buttons include only one
            // underscore before the user id (e.g. fish_nav_prev_<uid>), while the
            // sellall button appends a page number after the uid, so we must handle
            // it separately to avoid accidentally parsing the page as the user id.
            uint64_t uid;
            if (event.custom_id.rfind("fish_sellall_", 0) == 0) {
                // format: fish_sellall_<uid>_<page>
                std::string rest = event.custom_id.substr(strlen("fish_sellall_"));
                size_t sep = rest.find('_');
                if (sep == std::string::npos) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("invalid button format")).set_flags(dpp::m_ephemeral));
                    has_replied = true;
                    return;
                }
                uid = std::stoull(rest.substr(0, sep));
            } else {
                // navigation buttons like fish_nav_prev_<uid> or fish_nav_next_<uid>
                size_t pos = event.custom_id.rfind('_');
                if (pos == std::string::npos) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("invalid button format")).set_flags(dpp::m_ephemeral));
                    has_replied = true;
                    return;
                }
                uid = std::stoull(event.custom_id.substr(pos+1));
            }
            if (event.command.get_issuing_user().id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this interaction isn't for you")).set_flags(dpp::m_ephemeral));
                has_replied = true;
                return;
            }
            auto it = ::commands::fishing::fish_states.find(uid);
            if (it == ::commands::fishing::fish_states.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("fishing receipt not found (may have expired)")).set_flags(dpp::m_ephemeral));
                has_replied = true;
                return;
            }

        // Acknowledge the interaction immediately to prevent token expiry (>3s)
        event.reply(dpp::ir_deferred_update_message, dpp::message());
        has_replied = true;

        ::commands::fishing::FishReceiptState &st = it->second;
        int per_page = 5;
        int total = st.log.size();
        int pages = (total + per_page - 1) / per_page;

        // prepare sale counters so they are visible after the branch
        int sold_count = 0;
        int64_t total_value = 0;
        bool is_sell = false;

        // navigation logic (wraparound)
        if (event.custom_id.rfind("fish_nav_prev_",0) == 0) {
            if (pages > 1) {
                if (st.current_page > 0) st.current_page--; else st.current_page = pages-1;
            }
        } else if (event.custom_id.rfind("fish_nav_next_",0) == 0) {
            if (pages > 1) {
                if (st.current_page < pages-1) st.current_page++; else st.current_page = 0;
            }
        } else if (event.custom_id.rfind("fish_sellall_",0) == 0) {
            is_sell = true;
            // sell every uncaught fish on the current page
            if (total > 0) {
                int p = st.current_page;
                if (p < 0) p = 0;
                if (p >= pages) p = pages - 1;
                int start = p * per_page;
                int end = std::min(total, start + per_page);
                auto inventory = db->get_inventory(uid);
                std::vector<std::string> ids_to_remove;
                for (int i = start; i < end; ++i) {
                    for (const auto &item : inventory) {
                        if (item.item_id == st.log[i].item_id && item.item_type == "collectible") {
                            std::string meta = item.metadata;
                            bool is_locked = false;
                            size_t locked_pos = meta.find("\"locked\":");
                            if (locked_pos != std::string::npos && meta.find("true", locked_pos) != std::string::npos) {
                                is_locked = true;
                            }
                            if (!is_locked) {
                                size_t value_pos = meta.find("\"value\":");
                                int64_t fish_value = 0;
                                if (value_pos != std::string::npos) {
                                    size_t startv = value_pos + 8;
                                    size_t endv = meta.find(",", startv);
                                    if (endv == std::string::npos) endv = meta.find("}", startv);
                                    fish_value = std::stoll(meta.substr(startv, endv - startv));
                                }
                                if (db->remove_item(uid, st.log[i].item_id, 1)) {
                                    total_value += fish_value;
                                    sold_count++;
                                    ids_to_remove.push_back(st.log[i].item_id);
                                }
                            }
                            break;
                        }
                    }
                }
                st.log.erase(std::remove_if(st.log.begin(), st.log.end(), [&](const ::commands::fishing::CatchInfo &c){
                    return std::find(ids_to_remove.begin(), ids_to_remove.end(), c.item_id) != ids_to_remove.end();
                }), st.log.end());
                int total2 = st.log.size();
                int pages2 = (total2 + per_page - 1) / per_page;
                if (pages2 == 0) pages2 = 1;

                // if we sold at least one fish and the current page is now empty
                // but there are still later pages, advance to the next page so
                // the user doesn't stay staring at a blank page.
                if (sold_count > 0) {
                    int start2 = st.current_page * per_page;
                    if (start2 >= total2 && st.current_page < pages2 - 1) {
                        st.current_page++;
                    }
                }

                if (st.current_page >= pages2) st.current_page = pages2 - 1;

                if (sold_count > 0) {
                    db->update_wallet(uid, total_value);
                    // Track global boss fish profit from quick-sell
                    global_boss::on_fish_profit(db, uid, total_value);
                }
            }
        }

        // update the main receipt after any interaction
        dpp::message ret = ::commands::fishing::build_fish_message(uid);
        if (!ret.embeds.empty()) bronx::add_invoker_footer(ret.embeds[0], event.command.get_issuing_user());
        
        // add sell result embed only for sellall interactions
        if (is_sell) {
            if (sold_count > 0) {
                auto embed = bronx::success("sold " + std::to_string(sold_count) + " fish for $" + format_number(total_value));
                ret.add_embed(embed);
            } else {
                auto embed = bronx::error("no fish were sold (maybe they were locked or already gone)");
                ret.add_embed(embed);
            }
        }
        
        // edit the message (interaction already deferred above — use webhook to avoid 50013)
        ret.id = event.command.msg.id;
        ret.channel_id = event.command.channel_id;
        event.edit_response(ret);
        } catch (const std::exception &e) {
            bronx::logger::error("fishing 🎣", "button handler exception: " + std::string(e.what()));
            if (!has_replied) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an error occurred processing this interaction")).set_flags(dpp::m_ephemeral));
            }
        } catch (...) {
            bronx::logger::error("fishing 🎣", "button handler unknown exception");
            if (!has_replied) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an unknown error occurred")).set_flags(dpp::m_ephemeral));
            }
        }
    });

    // handler for quicksell dropdown
    bot.on_select_click([db,&bot](const dpp::select_click_t& event) {
        bool has_replied = false;
        try {
            if (event.custom_id.rfind("fish_sell_",0) != 0) return;
            
            bronx::logger::debug("fishing 🎣", "select handler processing: " + event.custom_id + " from user " + std::to_string(event.command.get_issuing_user().id));
            std::string rest = event.custom_id.substr(strlen("fish_sell_"));
            std::vector<std::string> parts;
            std::stringstream ss(rest);
            std::string part;
            while (std::getline(ss, part, '_')) parts.push_back(part);
            if (parts.empty()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("invalid select menu format")).set_flags(dpp::m_ephemeral));
                has_replied = true;
                return;
            }
            uint64_t uid = std::stoull(parts[0]);
            int page = 1;
            if (parts.size() >= 2) page = std::stoi(parts[1]);

            if (event.command.get_issuing_user().id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
                has_replied = true;
                return;
            }

            auto it = ::commands::fishing::fish_states.find(uid);
            if (it == ::commands::fishing::fish_states.end()) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("fishing receipt not found (may have expired)")).set_flags(dpp::m_ephemeral));
                has_replied = true;
                return;
            }

        // Acknowledge the interaction immediately to prevent token expiry (>3s)
        event.reply(dpp::ir_deferred_update_message, dpp::message());
        has_replied = true;

        ::commands::fishing::FishReceiptState &st = it->second;

        auto inventory = db->get_inventory(uid);
        int sold_count = 0;
        int64_t total_value = 0;
        for (const auto &fish_id : event.values) {
            for (const auto &item : inventory) {
                if (item.item_id == fish_id && item.item_type == "collectible") {
                    std::string meta = item.metadata;
                    bool is_locked = false;
                    size_t locked_pos = meta.find("\"locked\":");
                    if (locked_pos != std::string::npos && meta.find("true", locked_pos) != std::string::npos) {
                        continue;
                    }
                    size_t value_pos = meta.find("\"value\":");
                    int64_t fish_value = 0;
                    if (value_pos != std::string::npos) {
                        size_t start = value_pos + 8;
                        size_t end = meta.find(",", start);
                        if (end == std::string::npos) end = meta.find("}", start);
                        fish_value = std::stoll(meta.substr(start, end - start));
                    }
                    if (db->remove_item(uid, fish_id, 1)) {
                        total_value += fish_value;
                        sold_count++;
                    }
                    break;
                }
            }
        }
        if (sold_count > 0) {
            db->update_wallet(uid, total_value);
            st.log.erase(std::remove_if(st.log.begin(), st.log.end(), [&](const ::commands::fishing::CatchInfo &c){
                return std::find(event.values.begin(), event.values.end(), c.item_id) != event.values.end();
            }), st.log.end());
            int per_page = 5;
            int total2 = st.log.size();
            int pages2 = (total2 + per_page - 1) / per_page;
            if (pages2 == 0) pages2 = 1;
            // advance page if current page emptied
            int start2 = st.current_page * per_page;
            if (start2 >= total2 && st.current_page < pages2 - 1) {
                st.current_page++;
            }
            if (st.current_page >= pages2) st.current_page = pages2 - 1;
        }

        // edit the message (interaction already deferred above — use webhook to avoid 50013)
        dpp::message ret = ::commands::fishing::build_fish_message(uid);
        if (!ret.embeds.empty()) bronx::add_invoker_footer(ret.embeds[0], event.command.get_issuing_user());

        if (sold_count > 0) {
            auto embed = bronx::success("sold " + std::to_string(sold_count) + " fish for $" + format_number(total_value));
            ret.add_embed(embed);
        } else {
            auto embed = bronx::error("no fish were sold (maybe they were locked or already gone)");
            ret.add_embed(embed);
        }

        ret.id = event.command.msg.id;
        ret.channel_id = event.command.channel_id;
        event.edit_response(ret);
        } catch (const std::exception &e) {
            bronx::logger::error("fishing 🎣", "select handler exception: " + std::string(e.what()));
            if (!has_replied) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an error occurred processing this interaction")).set_flags(dpp::m_ephemeral));
            }
        } catch (...) {
            bronx::logger::error("fishing 🎣", "select handler unknown exception");
            if (!has_replied) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("an unknown error occurred")).set_flags(dpp::m_ephemeral));
            }
        }
    });
}

} // namespace commands
