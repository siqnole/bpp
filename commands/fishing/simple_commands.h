#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../titles.h"
#include <dpp/dpp.h>
#include <algorithm>

using namespace bronx::db;

namespace commands {
namespace fishing {

// Lock/favorite fish command
inline Command* get_lockfish_command(Database* db) {
    static Command* lockfish = new Command("lockfish", "lock or unlock a fish to protect it from selling", "fishing", {"lock", "fav", "favourite"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("specify a fish ID to lock/unlock or use `auto` with criteria"));
                return;
            }

            // support auto-lock by criteria
            if (args[0] == "auto") {
                // parse pairs of criterion and threshold
                double value_thresh = -1;
                double rarity_thresh = -1;
                for (size_t i = 1; i + 1 < args.size(); i += 2) {
                    std::string crit = args[i];
                    std::transform(crit.begin(), crit.end(), crit.begin(), ::tolower);
                    if (crit == "value") {
                        try {
                            value_thresh = std::stod(args[i+1]);
                        } catch (...) {}
                    } else if (crit == "rarity") {
                        try {
                            rarity_thresh = std::stod(args[i+1]);
                        } catch (...) {}
                    }
                }
                if (value_thresh < 0 && rarity_thresh < 0) {
                    bronx::send_message(bot, event, bronx::error("usage: lockfish auto [value <min>] [rarity <max_percent>]"));
                    return;
                }

                auto inventory = db->get_inventory(event.msg.author.id);
                int locked_count = 0;
                int total_weight = 0;
                for (const auto& ft : fish_types) total_weight += ft.weight;

                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    // parse metadata
                    std::string metadata = item.metadata;
                    size_t value_pos = metadata.find("\"value\":");
                    int64_t fish_value = 0;
                    if (value_pos != std::string::npos) {
                        size_t start = value_pos + 8;
                        size_t end = metadata.find(",", start);
                        if (end == std::string::npos) end = metadata.find("}", start);
                        fish_value = std::stoll(metadata.substr(start, end - start));
                    }
                    // parse name to compute rarity
                    std::string fish_name;
                    size_t name_pos = metadata.find("\"name\":\"");
                    if (name_pos != std::string::npos) {
                        size_t start = name_pos + 8;
                        size_t end = metadata.find("\"", start);
                        fish_name = metadata.substr(start, end - start);
                    }

                    double catch_odds = 100.0;
                    for (const auto& ft : fish_types) {
                        if (ft.name == fish_name) {
                            catch_odds = (double)ft.weight / total_weight * 100.0;
                            break;
                        }
                    }
                    bool meets = false;
                    if (value_thresh >= 0 && fish_value >= value_thresh) meets = true;
                    if (rarity_thresh >= 0 && catch_odds <= rarity_thresh) meets = true;
                    if (!meets) continue;

                    // set locked flag
                    size_t locked_pos = metadata.find("\"locked\":");
                    bool is_locked = (locked_pos != std::string::npos && metadata.find("true", locked_pos) != std::string::npos);
                    if (!is_locked && locked_pos != std::string::npos) {
                        std::string new_meta = metadata;
                        size_t false_pos = new_meta.find("false", locked_pos);
                        if (false_pos != std::string::npos) {
                            new_meta.replace(false_pos, 5, "true");
                            if (db->update_item_metadata(event.msg.author.id, item.item_id, new_meta)) {
                                locked_count++;
                            }
                        }
                    }
                }
                ::std::string description = "locked " + std::to_string(locked_count) + " fish";
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            ::std::string fish_id = args[0];
            ::std::transform(fish_id.begin(), fish_id.end(), fish_id.begin(), ::toupper);
            
            auto inventory = db->get_inventory(event.msg.author.id);
            bool found = false;
            
            for (const auto& item : inventory) {
                if (item.item_id == fish_id && item.item_type == "collectible") {
                    found = true;
                    
                    ::std::string metadata = item.metadata;
                    size_t locked_pos = metadata.find("\"locked\":");
                    bool is_locked = (locked_pos != ::std::string::npos && metadata.find("true", locked_pos) != ::std::string::npos);
                    
                    // Toggle locked status
                    ::std::string new_metadata = metadata;
                    if (is_locked) {
                        // Unlock
                        size_t true_pos = new_metadata.find("true", locked_pos);
                        new_metadata.replace(true_pos, 4, "false");
                    } else {
                        // Lock
                        size_t false_pos = new_metadata.find("false", locked_pos);
                        new_metadata.replace(false_pos, 5, "true");
                    }
                    
                    if (db->update_item_metadata(event.msg.author.id, fish_id, new_metadata)) {
                        ::std::string description;
                        if (is_locked) {
                            description = "unlocked fish `" + fish_id + "`";
                        } else {
                            description = "❤️ locked fish `" + fish_id + "`";
                        }
                        auto embed = bronx::success(description);
                        bronx::add_invoker_footer(embed, event.msg.author);
                        bronx::send_message(bot, event, embed);
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to update fish status"));
                    }
                    break;
                }
            }
            
            if (!found) {
                bronx::send_message(bot, event, bronx::error("fish not found in your inventory"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // slash support currently not implemented for auto-lock criteria
            event.reply(dpp::message().add_embed(
                bronx::error("use the message command to lock/unlock or auto-lock by criteria")));
        },
        {
            dpp::command_option(dpp::co_string, "fish_id", "ID of the fish to lock/unlock (for manual mode)", false),
            dpp::command_option(dpp::co_string, "mode", "optional mode: 'auto' to lock by value/rarity", false),
            dpp::command_option(dpp::co_string, "criteria", "criteria string (e.g. value 1000 rarity 5)", false)
        });
    
    return lockfish;
}

// helper to convert an item id to a display name
static std::string friendly_item_name(Database* db, const std::string& item_id) {
    // Check title catalog first for title_ items
    if (item_id.rfind("title_", 0) == 0) {
        auto title_def = find_title_static(item_id);
        if (title_def) return title_def->display;
    }
    auto maybe = db->get_shop_item(item_id);
    if (maybe) return maybe->name;
    return item_id; // fallback
}

// helper to resolve a user-provided token into a specific item id using the
// shop catalog (allows short forms like "wood" or "common").
static std::optional<std::string> resolve_item_id(Database* db, const std::string& token) {
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Also create a version with spaces replaced by underscores
    std::string lower_underscored = lower;
    std::replace(lower_underscored.begin(), lower_underscored.end(), ' ', '_');
    
    auto items = db->get_shop_items();
    
    // 1. exact item_id match (try both with spaces and underscores)
    for (const auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (id == lower || id == lower_underscored) return it.item_id;
    }
    
    // 2. exact name match
    for (const auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name == lower) return it.item_id;
    }
    
    // 3. Special case for "pi" - prioritize bait_pi over bait_epic
    if (lower == "pi") {
        for (const auto &it : items) {
            if (it.item_id == "bait_pi") return it.item_id;
        }
    }
    
    // 4. item_id starts with token
    for (const auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (id.rfind(lower, 0) == 0) return it.item_id;
    }
    
    // 5. name starts with token 
    for (const auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.rfind(lower, 0) == 0) return it.item_id;
    }
    
    // 6. item_id contains token (but prefer shorter matches)
    std::vector<std::pair<std::string, size_t>> id_matches;
    for (const auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        auto pos = id.find(lower);
        if (pos != std::string::npos) {
            id_matches.push_back({it.item_id, id.length()});
        }
    }
    if (!id_matches.empty()) {
        std::sort(id_matches.begin(), id_matches.end(), [](const auto& a, const auto& b) {
            return a.second < b.second; // shorter item_id first
        });
        return id_matches[0].first;
    }
    
    // 7. name substring match as last resort
    for (const auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find(lower) != std::string::npos) return it.item_id;
    }
    
    return {};
}

// ============================================================================
// INVENTORY PAGINATION CONSTANTS
// ============================================================================
constexpr int INV_ITEMS_PER_PAGE = 6;

// Get filtered inventory items by type
static std::vector<InventoryItem> get_inv_items_by_type(Database* db, uint64_t uid, const std::string& type_filter) {
    auto inventory = db->get_inventory(uid);
    std::vector<InventoryItem> filtered;
    for (const auto& item : inventory) {
        if (item.item_type == "collectible") continue; // fish handled by finv
        
        bool matches = false;
        if (type_filter == "all") {
            matches = true;
        } else if (type_filter == "title") {
            // Match both item_type == "title" and item_id starting with "title_"
            matches = (item.item_type == "title" || item.item_id.rfind("title_", 0) == 0);
        } else {
            matches = (item.item_type == type_filter);
        }
        
        if (matches) {
            filtered.push_back(item);
        }
    }
    return filtered;
}

// Build inventory page description
static std::string build_inv_page_description(Database* db, uint64_t uid, const std::vector<InventoryItem>& items,
                                               const std::string& type_filter, int page, int total_pages,
                                               const std::pair<std::string, std::string>& gear) {
    std::string type_emoji = (type_filter == "rod") ? "🎣" : (type_filter == "bait") ? "🪱" : 
                             (type_filter == "title") ? "🏷️" : "📦";
    std::string desc = type_emoji + " **your items";
    if (type_filter != "all") desc += " – " + type_filter + "s";
    desc += "**\n";
    desc += "*page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + "*\n\n";
    
    // Show equipped gear at top
    if (!gear.first.empty()) {
        desc += "equipped rod: **" + friendly_item_name(db, gear.first) + "**\n";
    }
    if (!gear.second.empty()) {
        desc += "equipped bait: **" + friendly_item_name(db, gear.second) + "**\n";
    }
    if (!gear.first.empty() || !gear.second.empty()) desc += "\n";
    
    if (items.empty()) {
        desc += "*no items found*";
        return desc;
    }
    
    int start = page * INV_ITEMS_PER_PAGE;
    int end = std::min(start + INV_ITEMS_PER_PAGE, static_cast<int>(items.size()));
    
    for (int i = start; i < end; ++i) {
        const auto& item = items[i];
        desc += "• **" + friendly_item_name(db, item.item_id) + "** x" + std::to_string(item.quantity);
        if (item.level > 1) desc += " (lvl " + std::to_string(item.level) + ")";
        if (item.item_id == gear.first || item.item_id == gear.second) {
            desc += " " + bronx::EMOJI_CHECK;
        }
        desc += "\n";
    }
    
    // Show fish count at bottom
    auto full_inv = db->get_inventory(uid);
    int fish_count = 0;
    for (const auto& item : full_inv) {
        if (item.item_type == "collectible") fish_count += item.quantity;
    }
    if (fish_count > 0) {
        desc += "\n🐟 fish in net: " + std::to_string(fish_count) + " (use `finv`)";
    }
    
    return desc;
}

// Build inventory page components (shop.h style)
static void build_inv_page_components(dpp::message& msg, const std::vector<InventoryItem>& items,
                                       const std::string& type_filter, int page, int total_pages,
                                       const std::string& user_id_str) {
    // Row 1: Navigation buttons ◀️ | page/total | ▶️
    dpp::component nav_row;
    
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_emoji("◀️")
        .set_style(dpp::cos_secondary)
        .set_id("inv_page_" + user_id_str + "_" + type_filter + "_" + std::to_string(page - 1))
        .set_disabled(page <= 0);
    nav_row.add_component(prev_btn);
    
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button)
        .set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages))
        .set_style(dpp::cos_secondary)
        .set_id("inv_pageinfo_" + user_id_str)
        .set_disabled(true);
    nav_row.add_component(page_btn);
    
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_emoji("▶️")
        .set_style(dpp::cos_secondary)
        .set_id("inv_page_" + user_id_str + "_" + type_filter + "_" + std::to_string(page + 1))
        .set_disabled(page >= total_pages - 1);
    nav_row.add_component(next_btn);
    
    msg.add_component(nav_row);
    
    // Row 2: Type filter dropdown
    dpp::component select_row;
    dpp::component type_menu;
    type_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("filter by type")
        .set_id("inv_type_" + user_id_str);
    
    type_menu.add_select_option(dpp::select_option("📦 all items", "all", "show all items").set_default(type_filter == "all"));
    type_menu.add_select_option(dpp::select_option("🎣 rods", "rod", "show only rods").set_default(type_filter == "rod"));
    type_menu.add_select_option(dpp::select_option("🪱 bait", "bait", "show only bait").set_default(type_filter == "bait"));
    type_menu.add_select_option(dpp::select_option("🏷️ titles", "title", "show only titles").set_default(type_filter == "title"));
    
    select_row.add_component(type_menu);
    msg.add_component(select_row);
}

// inventory/gear viewing command
inline Command* get_inv_command(Database* db) {
    static Command* invcmd = new Command("inv", "view your inventory and equipped gear", "fishing", {"inventory", "items"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t uid = event.msg.author.id;
            std::string user_id_str = std::to_string(uid);
            std::string type_filter = "all";
            int page = 0;
            
            // Parse optional type filter argument
            if (!args.empty()) {
                std::string arg = args[0];
                std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
                if (arg == "rod" || arg == "rods") type_filter = "rod";
                else if (arg == "bait" || arg == "baits") type_filter = "bait";
                else if (arg == "title" || arg == "titles") type_filter = "title";
            }
            
            auto items = get_inv_items_by_type(db, uid, type_filter);
            auto gear = db->get_active_fishing_gear(uid);
            int total_pages = items.empty() ? 1 : (static_cast<int>(items.size()) + INV_ITEMS_PER_PAGE - 1) / INV_ITEMS_PER_PAGE;
            
            std::string desc = build_inv_page_description(db, uid, items, type_filter, page, total_pages, gear);
            auto embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.msg.author);
            
            dpp::message msg;
            msg.add_embed(embed);
            msg.channel_id = event.msg.channel_id;
            build_inv_page_components(msg, items, type_filter, page, total_pages, user_id_str);
            
            bot.message_create(msg, [ch = event.msg.channel_id, gid = event.msg.guild_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[fishing:inventory] failed to send inventory in channel " << ch
                              << " (guild " << gid << "): " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            std::string user_id_str = std::to_string(uid);
            std::string type_filter = "all";
            int page = 0;
            
            auto items = get_inv_items_by_type(db, uid, type_filter);
            auto gear = db->get_active_fishing_gear(uid);
            int total_pages = items.empty() ? 1 : (static_cast<int>(items.size()) + INV_ITEMS_PER_PAGE - 1) / INV_ITEMS_PER_PAGE;
            
            std::string desc = build_inv_page_description(db, uid, items, type_filter, page, total_pages, gear);
            auto embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            
            dpp::message msg;
            msg.add_embed(embed);
            build_inv_page_components(msg, items, type_filter, page, total_pages, user_id_str);
            
            event.reply(msg);
        },
        {
            dpp::command_option(dpp::co_string, "type", "filter by item type", false)
                .add_choice(dpp::command_option_choice("all", "all"))
                .add_choice(dpp::command_option_choice("rod", "rod"))
                .add_choice(dpp::command_option_choice("bait", "bait"))
                .add_choice(dpp::command_option_choice("title", "title"))
        });
    return invcmd;
}

// Register inventory interaction handlers (pagination and type filter)
inline void register_inv_interactions(dpp::cluster& bot, Database* db) {
    // Page navigation buttons: inv_page_<user>_<type>_<page>
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("inv_page_", 0) != 0) return;
        
        std::string rest = event.custom_id.substr(9); // after "inv_page_"
        size_t first_sep = rest.find('_');
        if (first_sep == std::string::npos) return;
        size_t last_sep = rest.rfind('_');
        if (last_sep == first_sep) return;
        
        std::string user_id_str = rest.substr(0, first_sep);
        std::string type_filter = rest.substr(first_sep + 1, last_sep - first_sep - 1);
        int page = 0;
        try { page = std::stoi(rest.substr(last_sep + 1)); } catch(...) {}
        
        uint64_t uid = std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("these buttons aren't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        auto items = get_inv_items_by_type(db, uid, type_filter);
        auto gear = db->get_active_fishing_gear(uid);
        int total_pages = items.empty() ? 1 : (static_cast<int>(items.size()) + INV_ITEMS_PER_PAGE - 1) / INV_ITEMS_PER_PAGE;
        
        if (page < 0) page = 0;
        if (page >= total_pages) page = total_pages - 1;
        
        std::string desc = build_inv_page_description(db, uid, items, type_filter, page, total_pages, gear);
        auto embed = bronx::create_embed(desc);
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
        
        dpp::message msg;
        msg.add_embed(embed);
        build_inv_page_components(msg, items, type_filter, page, total_pages, user_id_str);
        
        event.reply(dpp::ir_update_message, msg);
    });
    
    // Type filter dropdown: inv_type_<user>
    bot.on_select_click([db, &bot](const dpp::select_click_t& event) {
        if (event.custom_id.rfind("inv_type_", 0) != 0) return;
        
        std::string user_id_str = event.custom_id.substr(9);
        uint64_t uid = std::stoull(user_id_str);
        
        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        std::string type_filter = event.values[0];
        int page = 0;
        
        auto items = get_inv_items_by_type(db, uid, type_filter);
        auto gear = db->get_active_fishing_gear(uid);
        int total_pages = items.empty() ? 1 : (static_cast<int>(items.size()) + INV_ITEMS_PER_PAGE - 1) / INV_ITEMS_PER_PAGE;
        
        std::string desc = build_inv_page_description(db, uid, items, type_filter, page, total_pages, gear);
        auto embed = bronx::create_embed(desc);
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
        
        dpp::message msg;
        msg.add_embed(embed);
        build_inv_page_components(msg, items, type_filter, page, total_pages, user_id_str);
        
        event.reply(dpp::ir_update_message, msg);
    });
}

// equip command for rods/bait, can equip both rod and bait in one command
inline Command* get_equip_command(Database* db) {
    static Command* eq = new Command("equip", "equip or unequip a rod and/or bait", "fishing", {"gear"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t uid = event.msg.author.id;
            // lambda to report the user's currently equipped gear.  If a
            // header text is provided, it will be prepended (used for success
            // messages after equipping/un-equipping).  Otherwise the full usage
            // instructions are included.
            auto send_current = [&](dpp::cluster& bot, const dpp::message_create_t& event,
                                    const std::string& header = std::string()) {
                auto gear = db->get_active_fishing_gear(uid);
                std::string desc;
                if (header.empty()) {
                    desc = "**usage:** equip [rod] [bait] | equip <item> | equip none\n";
                    desc += "• equip both: `equip <rod_name> <bait_name>`\n";
                    desc += "• equip one: `equip <item_name>`\n";
                    desc += "• clear all: `equip none`\n\n";
                } else {
                    desc = header + "\n\n";
                }
                desc += "**currently equipped:**\n";
                if (!gear.first.empty()) {
                    desc += "• Rod: **" + friendly_item_name(db, gear.first) + "**\n";
                } else {
                    desc += "• Rod: _none_\n";
                }
                if (!gear.second.empty()) {
                    int qty = db->get_item_quantity(uid, gear.second);
                    desc += "• Bait: **" + friendly_item_name(db, gear.second) + "** x" + std::to_string(qty) + "\n";
                } else {
                    desc += "• Bait: _none_\n";
                }
                auto embed = bronx::create_embed(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            };

            if (args.empty()) {
                send_current(bot, event);
                return;
            }

            // handle clear command
            if (args.size() == 1 && args[0] == "none") {
                db->set_active_rod(uid, "");
                db->set_active_bait(uid, "");
                send_current(bot, event, "cleared all equipped gear");
                return;
            }

            // Get user's inventory
            auto inventory = db->get_inventory(uid);
            
            // Helper to find item in inventory
            auto find_item = [&](const std::string& search_term) -> std::optional<InventoryItem> {
                std::string search = search_term;
                std::transform(search.begin(), search.end(), search.begin(), ::tolower);
                
                // Try to resolve alias first
                if (auto resolved = resolve_item_id(db, search)) {
                    search = *resolved;
                }
                
                // Find in inventory
                for (const auto& item : inventory) {
                    if (item.item_id == search) {
                        return item;
                    }
                }
                return std::nullopt;
            };

            std::vector<std::string> success_messages;
            std::vector<std::string> error_messages;

            // First, try joining all args as a single item name (e.g., "diamond rod")
            std::vector<std::string> args_to_process;
            if (args.size() > 1) {
                std::string combined;
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) combined += " ";
                    combined += args[i];
                }
                auto combined_item = find_item(combined);
                if (combined_item) {
                    // Found a single item matching the combined name
                    args_to_process.push_back(combined);
                } else {
                    // Fall back to individual args
                    args_to_process = args;
                }
            } else {
                args_to_process = args;
            }

            // Process each argument
            for (const std::string& arg : args_to_process) {
                auto item_opt = find_item(arg);
                
                if (!item_opt) {
                    error_messages.push_back("you don't have item: " + arg);
                    continue;
                }
                
                auto& item = *item_opt;
                
                if (item.item_type == "rod") {
                    // autofisher upgrades are stored as rods but can't be equipped directly
                    if (item.item_id.rfind("autofisher_", 0) == 0) {
                        error_messages.push_back("**" + friendly_item_name(db, item.item_id) + "** is an autofisher upgrade, not a fishing rod");
                        continue;
                    }
                    // prevent sharing a rod with the autofisher
                    auto af_cfg = db->get_autofisher_config(uid);
                    if (af_cfg && af_cfg->af_rod_id == item.item_id) {
                        error_messages.push_back("**" + friendly_item_name(db, item.item_id) +
                            "** is in use by your autofisher – equip a different rod to it first");
                        continue;
                    }
                    db->set_active_rod(uid, item.item_id);
                    success_messages.push_back("equipped rod: **" + friendly_item_name(db, item.item_id) + "**");
                } else if (item.item_type == "bait") {
                    // prevent sharing bait with the autofisher
                    auto af_cfg = db->get_autofisher_config(uid);
                    if (af_cfg && af_cfg->af_bait_id == item.item_id) {
                        error_messages.push_back("**" + friendly_item_name(db, item.item_id) +
                            "** is in use by your autofisher – equip a different bait to it first");
                        continue;
                    }
                    db->set_active_bait(uid, item.item_id);
                    success_messages.push_back("equipped bait: **" + friendly_item_name(db, item.item_id) + "**");
                } else {
                    error_messages.push_back(arg + " is not a rod or bait");
                }
            }

            // Build result message
            std::string result_header;
            if (!success_messages.empty()) {
                result_header = bronx::EMOJI_CHECK + " " + success_messages[0];
                for (size_t i = 1; i < success_messages.size(); ++i) {
                    result_header += "\n" + bronx::EMOJI_CHECK + " " + success_messages[i];
                }
            }
            
            if (!error_messages.empty()) {
                if (!result_header.empty()) result_header += "\n\n";
                result_header += bronx::EMOJI_DENY + " " + error_messages[0];
                for (size_t i = 1; i < error_messages.size(); ++i) {
                    result_header += "\n" + bronx::EMOJI_DENY + " " + error_messages[i];
                }
            }

            // If we had any success, show current gear; otherwise just show error
            if (!success_messages.empty()) {
                send_current(bot, event, result_header);
            } else {
                auto embed = bronx::create_embed(result_header, bronx::COLOR_ERROR);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("use message command to equip gear")));
        });
    return eq;
}

} // namespace fishing
} // namespace commands
