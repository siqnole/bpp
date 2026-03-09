#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "fishing_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <sstream>

using namespace bronx::db;

namespace commands {
namespace fishing {

inline Command* get_finv_command(Database* db) {
    static Command* finv = new Command("finv", "view your fish inventory", "fishing", {"fishnet", "fishinv", "inv", "equip"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto inventory = db->get_inventory(event.msg.author.id);
            
            ::std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) {
                if (item.item_type == "collectible") {
                    fish_items.push_back(item);
                }
            }
            
            if (fish_items.empty()) {
                ::std::string description = "**your fishing net** 🎣\n\n";
                description += "you haven't caught any fish yet!\n";
                description += "use `fish` to start catching fish";
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // interactive pagination helper
            auto render_page = [&](int page, bool update=false, const dpp::message& orig_msg = dpp::message()) {
                const int per_page = 16;
                int total_pages = (fish_items.size() + per_page - 1) / per_page;
                if (page < 0) page = 0;
                if (page >= total_pages) page = total_pages - 1;
                int start_idx = page * per_page;
                int end_idx = ::std::min(start_idx + per_page, (int)fish_items.size());

                ::std::string description = "**your fishing net** 🎣 (page " + ::std::to_string(page + 1) + "/" + ::std::to_string(total_pages) + ")\n\n";
                // calculate how much value is on the current page and how much
                // would be earned by selling *all* unlocked fish in the inventory.
                int64_t page_value = 0;
                int64_t global_value = 0;

                for (int i = 0; i < (int)fish_items.size(); ++i) {
                    const auto& item = fish_items[i];
                    ::std::string metadata = item.metadata;
                    
                    int64_t fish_value = 0;
                    bool is_locked = false;
                    
                    size_t value_pos = metadata.find("\"value\":");
                    if (value_pos != ::std::string::npos) {
                        size_t start = value_pos + 8;
                        size_t end = metadata.find(",", start);
                        if (end == ::std::string::npos) end = metadata.find("}", start);
                        fish_value = ::std::stoll(metadata.substr(start, end - start));
                    }
                    size_t locked_pos = metadata.find("\"locked\":");
                    if (locked_pos != ::std::string::npos) {
                        is_locked = (metadata.find("true", locked_pos) != ::std::string::npos);
                    }

                    if (!is_locked) {
                        global_value += fish_value;
                        if (i >= start_idx && i < end_idx) {
                            page_value += fish_value;
                        }
                    }
                }

                // render each fish on the current page (emoji/ID formatting unchanged)
                for (int i = start_idx; i < end_idx; i++) {
                    const auto& item = fish_items[i];
                    ::std::string metadata = item.metadata;
                    
                    ::std::string fish_name = "";
                    int64_t fish_value = 0;
                    bool is_locked = false;
                    
                    size_t name_pos = metadata.find("\"name\":\"");
                    if (name_pos != ::std::string::npos) {
                        size_t start = name_pos + 8;
                        size_t end = metadata.find("\"", start);
                        fish_name = metadata.substr(start, end - start);
                    }
                    
                    size_t value_pos = metadata.find("\"value\":");
                    if (value_pos != ::std::string::npos) {
                        size_t start = value_pos + 8;
                        size_t end = metadata.find(",", start);
                        if (end == ::std::string::npos) end = metadata.find("}", start);
                        fish_value = ::std::stoll(metadata.substr(start, end - start));
                    }
                    
                    size_t locked_pos = metadata.find("\"locked\":");
                    if (locked_pos != ::std::string::npos) {
                        is_locked = (metadata.find("true", locked_pos) != ::std::string::npos);
                    }
                    
                    ::std::string fish_emoji = "🐟";
                    for (const auto& fish_type : fish_types) {
                        if (fish_type.name == fish_name) {
                            fish_emoji = fish_type.emoji;
                            break;
                        }
                    }
                    
                    description += fish_emoji + " `" + item.item_id + "` ";
                    if (is_locked) description += "❤️";
                    if ((i - start_idx + 1) % 4 == 0) description += "\n";
                }
                
                description += "\n\n**total unlocked value:** $" + format_number(page_value);
                if (global_value != page_value) {
                    description += " / **sell value:** $" + format_number(global_value);
                }
                description += " (" + ::std::to_string(fish_items.size()) + " fish)";

                // build components
                std::vector<dpp::component> rows;
                dpp::component nav_row;
                // Row 1: ◀️ | page count | ▶️ (shop.h style)
                {
                    dpp::component prev_btn;
                    prev_btn.set_type(dpp::cot_button);
                    prev_btn.set_emoji("◀️");
                    prev_btn.set_style(dpp::cos_secondary);
                    prev_btn.set_id("finv_nav_" + ::std::to_string(page) + "_" + ::std::to_string(event.msg.author.id) + "_prev");
                    prev_btn.set_disabled(page <= 0);
                    nav_row.add_component(prev_btn);

                    dpp::component page_btn;
                    page_btn.set_type(dpp::cot_button);
                    page_btn.set_label(::std::to_string(page + 1) + "/" + ::std::to_string(total_pages));
                    page_btn.set_style(dpp::cos_secondary);
                    page_btn.set_id("finv_pageinfo_" + ::std::to_string(event.msg.author.id));
                    page_btn.set_disabled(true);
                    nav_row.add_component(page_btn);

                    dpp::component next_btn;
                    next_btn.set_type(dpp::cot_button);
                    next_btn.set_emoji("▶️");
                    next_btn.set_style(dpp::cos_secondary);
                    next_btn.set_id("finv_nav_" + ::std::to_string(page) + "_" + ::std::to_string(event.msg.author.id) + "_next");
                    next_btn.set_disabled(page >= total_pages - 1);
                    nav_row.add_component(next_btn);
                }
                rows.push_back(nav_row);

                // quicksell dropdown similar to fish receipt (always offered regardless of page count)
                {
                    dpp::component select_row;
                    dpp::component select_menu;
                    select_menu.set_type(dpp::cot_selectmenu)
                        .set_placeholder("quick sell fish on this page")
                        .set_id("finv_sell_" + ::std::to_string(event.msg.author.id) + "_" + ::std::to_string(page));
                    bool any_option = false;
                    for (int i = start_idx; i < end_idx; ++i) {
                        const auto &item = fish_items[i];
                        // skip locked fish
                        std::string meta = item.metadata;
                        bool is_locked = false;
                        size_t locked_pos = meta.find("\"locked\":");
                        if (locked_pos != ::std::string::npos && meta.find("true", locked_pos) != ::std::string::npos) {
                            is_locked = true;
                        }
                        if (is_locked) continue;
                        // determine fish name for label
                        std::string fish_name;
                        size_t name_pos = meta.find("\"name\":\"");
                        if (name_pos != ::std::string::npos) {
                            size_t start = name_pos + 8;
                            size_t end = meta.find("\"", start);
                            fish_name = meta.substr(start, end - start);
                        }
                        std::string label = fish_name.empty() ? item.item_id : (fish_name + " (" + item.item_id + ")");
                        select_menu.add_select_option(dpp::select_option(label, item.item_id));
                        any_option = true;
                    }
                    if (any_option) {
                        select_row.add_component(select_menu);
                        rows.push_back(select_row);
                    }
                }

                dpp::message msg;
                msg.add_embed(bronx::create_embed(description));
                for (auto &r : rows) msg.add_component(r);

                if (update) {
                    event.reply(msg);
                } else {
                    bronx::send_message(bot, event, msg);
                }
            };

            int page = 0;
            if (!args.empty()) {
                try { page = ::std::max(0, ::std::stoi(args[0]) - 1); } catch(...) {}
            }
            render_page(page);
            return;        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto inventory = db->get_inventory(event.command.get_issuing_user().id);
            
            ::std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) {
                if (item.item_type == "collectible") {
                    fish_items.push_back(item);
                }
            }
            
            if (fish_items.empty()) {
                ::std::string description = "**your fishing net** 🎣\n\n";
                description += "you haven't caught any fish yet!\n";
                description += "use `/fish` to start catching fish";
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            ::std::string description = "**your fishing net** 🎣\n\n";
            int64_t total_value = 0;
            
            for (const auto& item : fish_items) {
                ::std::string metadata = item.metadata;
                
                ::std::string fish_name = "";
                int64_t fish_value = 0;
                bool is_locked = false;
                
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos != ::std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start, end - start);
                }
                
                size_t value_pos = metadata.find("\"value\":");
                if (value_pos != ::std::string::npos) {
                    size_t start = value_pos + 8;
                    size_t end = metadata.find(",", start);
                    if (end == ::std::string::npos) end = metadata.find("}", start);
                    fish_value = ::std::stoll(metadata.substr(start, end - start));
                }
                
                size_t locked_pos = metadata.find("\"locked\":");
                if (locked_pos != ::std::string::npos) {
                    is_locked = (metadata.find("true", locked_pos) != ::std::string::npos);
                }
                
                ::std::string fish_emoji = "🐟";
                for (const auto& fish_type : fish_types) {
                    if (fish_type.name == fish_name) {
                        fish_emoji = fish_type.emoji;
                        break;
                    }
                }
                
                description += fish_emoji + " `" + item.item_id + "` **" + fish_name + "** - $" + format_number(fish_value);
                if (is_locked) {
                    description += " ❤️";
                }
                description += "\n";
                
                if (!is_locked) {
                    total_value += fish_value;
                }
            }
            
            description += "\n**total unlocked value:** $" + format_number(total_value);
            description += " / **sell value:** $" + format_number(total_value);
            description += "\n\nuse `/lockfish <ID>` to favorite a fish";
            description += "\nuse `/sellfish <ID|all>` to sell fish";
            
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        });
    
    return finv;
}

// register button handlers for finv pagination
inline void register_finv_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        std::string cid = event.custom_id;
        if (cid.rfind("finv_nav_", 0) != 0) return;
        std::vector<std::string> parts;
        std::stringstream ss(cid);
        std::string part;
        while (std::getline(ss, part, '_')) parts.push_back(part);
        if (parts.size() != 5) return;
        int page = std::stoi(parts[2]);
        uint64_t uid = std::stoull(parts[3]);
        std::string dir = parts[4];
        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("these buttons aren't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        auto inventory = db->get_inventory(uid);
        std::vector<InventoryItem> fish_items;
        for (const auto& item : inventory) if (item.item_type == "collectible") fish_items.push_back(item);
        if (fish_items.empty()) return;
        const int per_page = 16;
        int total = fish_items.size();
        int total_pages = (total + per_page - 1) / per_page;
        if (total_pages > 1) {
            if (dir == "prev") {
                if (page > 0) page--; else page = total_pages - 1;
            } else if (dir == "next") {
                if (page < total_pages - 1) page++; else page = 0;
            }
        }

        int start_idx = page * per_page;
        int end_idx = std::min(start_idx + per_page, total);
        std::string description = "**your fishing net** 🎣 (page " + std::to_string(page+1) + "/" + std::to_string(total_pages) + ")\n\n";
        int64_t total_value = 0;
        for (int i = start_idx; i < end_idx; ++i) {
            const auto& item = fish_items[i];
            std::string metadata = item.metadata;
            std::string fish_name="";
            int64_t fish_value=0;
            bool is_locked=false;
            size_t name_pos = metadata.find("\"name\":\"");
            if (name_pos!=std::string::npos) {
                size_t start = name_pos+8;
                size_t end = metadata.find("\"", start);
                fish_name = metadata.substr(start,end-start);
            }
            size_t value_pos = metadata.find("\"value\":");
            if (value_pos!=std::string::npos) {
                size_t start = value_pos+8;
                size_t end = metadata.find(",", start);
                if (end==std::string::npos) end = metadata.find("}", start);
                fish_value = std::stoll(metadata.substr(start,end-start));
            }
            size_t locked_pos = metadata.find("\"locked\":");
            if (locked_pos!=std::string::npos) {
                is_locked = (metadata.find("true", locked_pos) != std::string::npos);
            }
            std::string fish_emoji="🐟";
            for (const auto& ft : fish_types) if (ft.name==fish_name) { fish_emoji=ft.emoji; break; }
            description += fish_emoji + " `" + item.item_id + "` ";
            if (is_locked) description += "❤️";
            if ((i - start_idx + 1) % 4 == 0) description += "\n";
            if (!is_locked) total_value += fish_value;
        }
        description += "\n\n**total unlocked value:** $" + format_number(total_value);
        description += " (" + std::to_string(fish_items.size()) + " fish)";
        std::vector<dpp::component> rows;
        dpp::component nav_row;
        // Row 1: ◀️ | page count | ▶️ (shop.h style)
        {
            dpp::component prev_btn;
            prev_btn.set_type(dpp::cot_button);
            prev_btn.set_emoji("◀️");
            prev_btn.set_style(dpp::cos_secondary);
            prev_btn.set_id("finv_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_prev");
            prev_btn.set_disabled(page <= 0);
            nav_row.add_component(prev_btn);

            dpp::component page_btn;
            page_btn.set_type(dpp::cot_button);
            page_btn.set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages));
            page_btn.set_style(dpp::cos_secondary);
            page_btn.set_id("finv_pageinfo_" + std::to_string(uid));
            page_btn.set_disabled(true);
            nav_row.add_component(page_btn);

            dpp::component next_btn;
            next_btn.set_type(dpp::cot_button);
            next_btn.set_emoji("▶️");
            next_btn.set_style(dpp::cos_secondary);
            next_btn.set_id("finv_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_next");
            next_btn.set_disabled(page >= total_pages - 1);
            nav_row.add_component(next_btn);
        }
        rows.push_back(nav_row);
        // dropdown row for quicksell (same logic as in render_page)
        {
            dpp::component select_row;
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("quick sell fish on this page")
                .set_id("finv_sell_" + std::to_string(uid) + "_" + std::to_string(page));
            bool any_option = false;
            for (int i = start_idx; i < end_idx; ++i) {
                const auto &item = fish_items[i];
                std::string meta = item.metadata;
                bool is_locked = false;
                size_t locked_pos = meta.find("\"locked\":");
                if (locked_pos != std::string::npos && meta.find("true", locked_pos) != std::string::npos) {
                    is_locked = true;
                }
                if (is_locked) continue;
                std::string fish_name;
                size_t name_pos = meta.find("\"name\":\"");
                if (name_pos != std::string::npos) {
                    size_t start = name_pos+8;
                    size_t end = meta.find("\"", start);
                    fish_name = meta.substr(start,end-start);
                }
                std::string label = fish_name.empty() ? item.item_id : (fish_name + " (" + item.item_id + ")");
                select_menu.add_select_option(dpp::select_option(label, item.item_id));
                any_option = true;
            }
            if (any_option) {
                select_row.add_component(select_menu);
                rows.push_back(select_row);
            }
        }
        dpp::message msg;
        msg.add_embed(bronx::create_embed(description));
        for (auto &r : rows) msg.add_component(r);
        // edit the original message instead of sending a new one to prevent spam
        event.reply(dpp::ir_update_message, msg);
        return;
    });

    // handler for quicksell dropdown on finv pages
    bot.on_select_click([db,&bot](const dpp::select_click_t& event) {
        if (event.custom_id.rfind("finv_sell_",0) != 0) return;
        std::string rest = event.custom_id.substr(strlen("finv_sell_"));
        std::vector<std::string> parts;
        std::stringstream ss(rest);
        std::string part;
        while (std::getline(ss, part, '_')) parts.push_back(part);
        if (parts.empty()) return;
        uint64_t uid = std::stoull(parts[0]);
        int page = 0;
        if (parts.size() >= 2) page = std::stoi(parts[1]);

        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }

        auto inventory = db->get_inventory(uid);
        std::vector<InventoryItem> fish_items;
        for (const auto &item : inventory) {
            if (item.item_type == "collectible") {
                fish_items.push_back(item);
            }
        }

        int total = fish_items.size();
        int per_page = 16;
        int total_pages = (total + per_page - 1) / per_page;
        if (total_pages == 0) total_pages = 1;
        if (page < 0) page = 0;
        if (page >= total_pages) page = total_pages - 1;

        int start_idx = page * per_page;
        int end_idx = std::min(start_idx + per_page, total);

        int sold_count = 0;
        int64_t total_value = 0;
        for (const auto &fish_id : event.values) {
            for (const auto &item : inventory) {
                if (item.item_id == fish_id && item.item_type == "collectible") {
                    std::string meta = item.metadata;
                    bool is_locked = false;
                    size_t locked_pos = meta.find("\"locked\":");
                    if (locked_pos != std::string::npos && meta.find("true", locked_pos) != std::string::npos) {
                        is_locked = true;
                    }
                    if (is_locked) continue;
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
            // re-render the current page
            // we can call the same render_page defined in the command handler;
            // unfortunately it's not accessible here, so rebuild description manually.
            // easiest: simulate a button click by constructing a dummy event? Instead
            // for simplicity, just edit message with a new render using the existing
            // logic from above by invoking the button handler again via code duplication.
            // But client already recalculates inventory and pages when buttons used,
            // so replicate that logic here.

            // compute new message
            int pages = total_pages; // reuse
            // rebuild description and rows like in button handler
            std::string description = "**your fishing net** 🎣 (page " + std::to_string(page+1) + "/" + std::to_string(total_pages) + ")\n\n";
            int64_t tv = 0;
            std::vector<dpp::component> rows;
            dpp::component nav_row;
            for (int i = start_idx; i < end_idx; ++i) {
                const auto &item = fish_items[i];
                std::string metadata = item.metadata;
                std::string fish_name="";
                int64_t fish_value=0;
                bool is_locked=false;
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos!=std::string::npos) {
                    size_t start = name_pos+8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start,end-start);
                }
                size_t value_pos = metadata.find("\"value\":");
                if (value_pos!=std::string::npos) {
                    size_t start = value_pos+8;
                    size_t end = metadata.find(",", start);
                    if (end==std::string::npos) end = metadata.find("}", start);
                    fish_value = std::stoll(metadata.substr(start,end-start));
                }
                size_t locked_pos = metadata.find("\"locked\":");
                if (locked_pos!=std::string::npos) {
                    is_locked = (metadata.find("true", locked_pos) != std::string::npos);
                }
                std::string fish_emoji="🐟";
                for (const auto& ft : fish_types) if (ft.name==fish_name) { fish_emoji=ft.emoji; break; }
                description += fish_emoji + " `" + item.item_id + "` ";
                if (is_locked) description += "❤️";
                if ((i - start_idx + 1) % 4 == 0) description += "\n";
                if (!is_locked) tv += fish_value;
            }
            description += "\n\n**total unlocked value:** $" + format_number(tv);
            description += " (" + std::to_string(fish_items.size()) + " fish)";
            // Row 1: ◀️ | page count | ▶️ (shop.h style)
            {
                dpp::component prev_btn;
                prev_btn.set_type(dpp::cot_button);
                prev_btn.set_emoji("◀️");
                prev_btn.set_style(dpp::cos_secondary);
                prev_btn.set_id("finv_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_prev");
                prev_btn.set_disabled(page <= 0);
                nav_row.add_component(prev_btn);

                dpp::component page_btn;
                page_btn.set_type(dpp::cot_button);
                page_btn.set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages));
                page_btn.set_style(dpp::cos_secondary);
                page_btn.set_id("finv_pageinfo_" + std::to_string(uid));
                page_btn.set_disabled(true);
                nav_row.add_component(page_btn);

                dpp::component next_btn;
                next_btn.set_type(dpp::cot_button);
                next_btn.set_emoji("▶️");
                next_btn.set_style(dpp::cos_secondary);
                next_btn.set_id("finv_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_next");
                next_btn.set_disabled(page >= total_pages - 1);
                nav_row.add_component(next_btn);
            }
            rows.push_back(nav_row);
            // dropdown row
            dpp::component select_row;
            dpp::component select_menu2;
            select_menu2.set_type(dpp::cot_selectmenu)
                .set_placeholder("quick sell fish on this page")
                .set_id("finv_sell_" + std::to_string(uid) + "_" + std::to_string(page));
            bool any_opt2 = false;
            for (int i = start_idx; i < end_idx; ++i) {
                const auto &item = fish_items[i];
                std::string meta = item.metadata;
                bool is_locked2=false;
                size_t locked_pos2 = meta.find("\"locked\":");
                if (locked_pos2!=std::string::npos && meta.find("true", locked_pos2)!=std::string::npos) is_locked2=true;
                if (is_locked2) continue;
                std::string fish_name2;
                size_t name_pos2 = meta.find("\"name\":\"");
                if (name_pos2!=std::string::npos) {
                    size_t start = name_pos2+8;
                    size_t end = meta.find("\"", start);
                    fish_name2 = meta.substr(start,end-start);
                }
                std::string label = fish_name2.empty() ? item.item_id : (fish_name2 + " (" + item.item_id + ")");
                select_menu2.add_select_option(dpp::select_option(label, item.item_id));
                any_opt2 = true;
            }
            if (any_opt2) {
                select_row.add_component(select_menu2);
                rows.push_back(select_row);
            }

            dpp::message ret;
            ret.add_embed(bronx::create_embed(description));
            for (auto &r : rows) ret.add_component(r);
            event.reply(dpp::ir_update_message, ret);

            auto embed = bronx::success("sold " + std::to_string(sold_count) + " fish for $" + format_number(total_value));
            event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
        } else {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("no fish were sold (maybe they were locked or already gone)")).set_flags(dpp::m_ephemeral));
        }
    });
}

} // namespace fishing
} // namespace commands

