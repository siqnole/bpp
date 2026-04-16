// ============================================================================
// shop.cpp — All shop command implementations.
// Declarations are in shop.h.
// ============================================================================
#include "shop.h"

#include "../../embed_style.h"
#include "../../database/operations/economy/history_operations.h"
#include "../owner.h"
#include "../titles.h"
#include "lootbox_catalog.h"
#include <sstream>
#include <set>
#include <map>
#include <chrono>

namespace commands {

// ── File-local helpers ───────────────────────────────────────────────

static ::std::vector<ShopItem> load_shop_items(bronx::db::Database* db) {
    return db->get_shop_items();
}

// compute the unix timestamp (seconds) of the next weekly rotation boundary
static time_t next_rotation_time() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = duration_cast<seconds>(now.time_since_epoch()).count();
    const int64_t week = 7 * 24 * 60 * 60;
    int64_t next = ((secs / week) + 1) * week;
    return static_cast<time_t>(next);
}

// Build a title-shop embed description for the current rotation
static std::string build_titles_description(bronx::db::Database* db, bool slash_mode) {
    auto available = get_available_titles(db);
    int live_slot = (current_utc_week() % TITLE_ROTATION_SLOTS) + 1;
    time_t expiry = next_rotation_time();
    std::string expiry_str;
    if (expiry > 0) {
        expiry_str = " <t:" + std::to_string(expiry) + ":R>"; // relative timestamp
    }

    std::string description = "**bronx shop – titles** *(weekly rotation)" + (expiry_str.empty()?"":(" until" + expiry_str)) + "*\n\n";
    if (available.empty()) {
        description += "*no titles available this week*\n";
    } else {
        for (const auto& t : available) {
            // skip any award-only titles (price == 0)
            if (t.price <= 0) continue;

            // Show the short ID (strip the "title_" prefix) so users don't
            // need to type "title_" when buying or equipping.
            std::string short_id = t.item_id;
            if (short_id.rfind("title_", 0) == 0) short_id = short_id.substr(6);
            description += "**" + t.display + "** (`" + short_id + "`) — $" + format_number(t.price);
            // Show remaining stock for limited titles
            if (t.purchase_limit > 0) {
                int sold = db ? db->count_item_owners(t.item_id) : 0;
                int remaining = t.purchase_limit - sold;
                description += " *(" + std::to_string(remaining) + "/" + std::to_string(t.purchase_limit) + " left)*";
            }
            // indicate rotation slot if not permanent
            if (t.rotation_slot != 0) {
                description += " *(rotating slot " + std::to_string(t.rotation_slot) + ")*";
            }
            description += "\n" + t.shop_desc + "\n\n";
        }
    }
    description += "use `" + std::string(slash_mode ? "/" : "") + "buy <item_id>` to purchase";
    return description;
}



// ============================================================================
// SHOP PAGINATION HELPERS
// ============================================================================

// Get filtered and sorted items for a category
// If user_prestige >= 0, only shows items the user has unlocked (default -1 = show all)
static std::vector<ShopItem> get_category_items(Database* db, const std::string& category, int user_prestige = -1) {
    auto items = db->get_shop_items();
    std::vector<ShopItem> filtered;
    for (const auto& it : items) {
        std::string icat = normalize_category(it.category);
        if (icat != category) continue;
        
        // Filter by prestige requirement if user_prestige is specified
        if (user_prestige >= 0) {
            int req_prestige = get_required_prestige(it.metadata);
            if (req_prestige > user_prestige) continue; // User doesn't have required prestige
        }
        
        filtered.push_back(it);
    }
    // Sort by level first (for progression), then by price
    std::sort(filtered.begin(), filtered.end(), [](const ShopItem& a, const ShopItem& b) {
        if (a.level != b.level) return a.level < b.level;
        return a.price < b.price;
    });
    return filtered;
}

// Calculate total pages for a category
static int get_total_pages(const std::vector<ShopItem>& items) {
    if (items.empty()) return 1;
    return (static_cast<int>(items.size()) + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
}

// Build embed description for a page of items
static std::string build_page_description(Database* db, const std::vector<ShopItem>& items, 
                                          const std::string& category, int page, int total_pages) {
    std::string cat_emoji = (category == "rod") ? "🎣" : (category == "bait") ? "🪱" : (category == "pickaxe") ? "⛏️" : (category == "minecart") ? "🛒" : (category == "bag") ? "🎒" : "📦";
    std::string description = cat_emoji + " **bronx shop – " + category + "s**\n";
    description += "*page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + "*\n\n";
    
    if (items.empty()) {
        description += "*no items found*";
        return description;
    }
    
    int start = page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, static_cast<int>(items.size()));
    
    for (int i = start; i < end; ++i) {
        const auto& item = items[i];
        int req_prestige = get_required_prestige(item.metadata);
        std::string prestige_tag = prestige_indicator(req_prestige);
        
        description += "**" + item.name + "**" + prestige_tag + "\n";
        description += "`" + item.item_id + "` • $" + format_number(item.price);
        description += " • lvl " + std::to_string(item.level) + "\n";
        description += "*" + item.description + "*\n\n";
    }
    
    description += "select an item below to purchase";
    return description;
}

// Build components for a paginated shop page
static void build_shop_page_components(dpp::message& msg, Database* db, const std::vector<ShopItem>& items,
                                       const std::string& category, int page, int total_pages,
                                       const std::string& user_id_str) {
    int start = page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, static_cast<int>(items.size()));
    
    // Item select dropdown (limited to items on current page)
    if (!items.empty() && end > start) {
        dpp::component item_menu;
        item_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select an item to purchase")
            .set_id("shop_item_" + user_id_str + "_" + category + "_" + std::to_string(page));
        
        for (int i = start; i < end; ++i) {
            const auto& item = items[i];
            int req_prestige = get_required_prestige(item.metadata);
            std::string label = item.name;
            if (req_prestige > 0) label += " " + bronx::EMOJI_STAR + "P" + std::to_string(req_prestige);
            std::string desc = "$" + format_number(item.price) + " • " + item.description.substr(0, 50);
            if (item.description.length() > 50) desc += "...";
            
            auto option = dpp::select_option(label, item.item_id, desc);
            item_menu.add_select_option(option);
        }
        msg.add_component(dpp::component().add_component(item_menu));
    }
    
    // Navigation buttons row
    dpp::component nav_row;
    
    // Previous page button
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_emoji("◀️")
        .set_style(dpp::cos_secondary)
        .set_id("shop_page_" + user_id_str + "_" + category + "_" + std::to_string(page - 1))
        .set_disabled(page <= 0);
    nav_row.add_component(prev_btn);
    
    // Page indicator (disabled button showing current page)
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button)
        .set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages))
        .set_style(dpp::cos_secondary)
        .set_id("shop_pageinfo_" + user_id_str)
        .set_disabled(true);
    nav_row.add_component(page_btn);
    
    // Next page button
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_emoji("▶️")
        .set_style(dpp::cos_secondary)
        .set_id("shop_page_" + user_id_str + "_" + category + "_" + std::to_string(page + 1))
        .set_disabled(page >= total_pages - 1);
    nav_row.add_component(next_btn);
    
    // Back to categories button
    dpp::component back_btn;
    back_btn.set_type(dpp::cot_button)
        .set_label("back")
        .set_style(dpp::cos_danger)
        .set_id("shop_back_" + user_id_str);
    nav_row.add_component(back_btn);
    
    msg.add_component(nav_row);
}

// ============================================================================
// TITLE PAGINATION HELPERS
// ============================================================================

// Get total pages for titles
static int get_title_total_pages(const std::vector<TitleDef>& titles) {
    // Filter out non-purchasable titles (price <= 0)
    int count = 0;
    for (const auto& t : titles) if (t.price > 0) count++;
    if (count == 0) return 1;
    return (count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
}

// Get purchasable titles only (filtered list)
static std::vector<TitleDef> get_purchasable_titles(Database* db) {
    auto all = get_available_titles(db);
    std::vector<TitleDef> result;
    for (const auto& t : all) {
        if (t.price > 0) result.push_back(t);
    }
    // Sort by price for consistency
    std::sort(result.begin(), result.end(), [](const TitleDef& a, const TitleDef& b) {
        return a.price < b.price;
    });
    return result;
}

// Build paginated title description
static std::string build_title_page_description(Database* db, const std::vector<TitleDef>& titles, 
                                                 int page, int total_pages) {
    int live_slot = (current_utc_week() % TITLE_ROTATION_SLOTS) + 1;
    time_t expiry = next_rotation_time();
    std::string expiry_str;
    if (expiry > 0) {
        expiry_str = " <t:" + std::to_string(expiry) + ":R>";
    }
    
    std::string description = "🏷️ **bronx shop – titles** *(weekly rotation)";
    if (!expiry_str.empty()) description += " until" + expiry_str;
    description += "*\n";
    description += "*page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + "*\n\n";
    
    if (titles.empty()) {
        description += "*no titles available*";
        return description;
    }
    
    int start = page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, static_cast<int>(titles.size()));
    
    for (int i = start; i < end; ++i) {
        const auto& t = titles[i];
        std::string short_id = t.item_id;
        if (short_id.rfind("title_", 0) == 0) short_id = short_id.substr(6);
        
        description += "**" + t.display + "** (`" + short_id + "`) — $" + format_number(t.price);
        
        // Show remaining stock for limited titles
        if (t.purchase_limit > 0) {
            int sold = db ? db->count_item_owners(t.item_id) : 0;
            int remaining = t.purchase_limit - sold;
            description += " *(" + std::to_string(remaining) + "/" + std::to_string(t.purchase_limit) + " left)*";
        }
        // Indicate rotation slot if not permanent
        if (t.rotation_slot != 0) {
            description += " *(rotating)*";
        }
        description += "\n" + t.shop_desc + "\n\n";
    }
    
    description += "select a title below to purchase";
    return description;
}

// Build components for paginated title page
static void build_title_page_components(dpp::message& msg, Database* db, const std::vector<TitleDef>& titles,
                                        int page, int total_pages, const std::string& user_id_str) {
    int start = page * ITEMS_PER_PAGE;
    int end = std::min(start + ITEMS_PER_PAGE, static_cast<int>(titles.size()));
    
    // Title select dropdown
    if (!titles.empty() && end > start) {
        dpp::component item_menu;
        item_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a title to purchase")
            .set_id("shop_item_" + user_id_str + "_title_" + std::to_string(page));
        
        for (int i = start; i < end; ++i) {
            const auto& t = titles[i];
            std::string label = t.display;
            // Clean up markdown for dropdown label
            std::string clean_label;
            for (char c : label) {
                if (c != '*' && c != '_' && c != '~') clean_label += c;
            }
            if (clean_label.length() > 25) clean_label = clean_label.substr(0, 22) + "...";
            
            std::string desc = "$" + format_number(t.price);
            if (t.purchase_limit > 0) {
                int sold = db ? db->count_item_owners(t.item_id) : 0;
                int remaining = t.purchase_limit - sold;
                desc += " • " + std::to_string(remaining) + " left";
            }
            
            auto option = dpp::select_option(clean_label, t.item_id, desc);
            item_menu.add_select_option(option);
        }
        msg.add_component(dpp::component().add_component(item_menu));
    }
    
    // Navigation buttons row
    dpp::component nav_row;
    
    // Previous page button
    dpp::component prev_btn;
    prev_btn.set_type(dpp::cot_button)
        .set_emoji("◀️")
        .set_style(dpp::cos_secondary)
        .set_id("shop_page_" + user_id_str + "_title_" + std::to_string(page - 1))
        .set_disabled(page <= 0);
    nav_row.add_component(prev_btn);
    
    // Page indicator
    dpp::component page_btn;
    page_btn.set_type(dpp::cot_button)
        .set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages))
        .set_style(dpp::cos_secondary)
        .set_id("shop_pageinfo_" + user_id_str)
        .set_disabled(true);
    nav_row.add_component(page_btn);
    
    // Next page button
    dpp::component next_btn;
    next_btn.set_type(dpp::cot_button)
        .set_emoji("▶️")
        .set_style(dpp::cos_secondary)
        .set_id("shop_page_" + user_id_str + "_title_" + std::to_string(page + 1))
        .set_disabled(page >= total_pages - 1);
    nav_row.add_component(next_btn);
    
    // Back button
    dpp::component back_btn;
    back_btn.set_type(dpp::cot_button)
        .set_label("back")
        .set_style(dpp::cos_danger)
        .set_id("shop_back_" + user_id_str);
    nav_row.add_component(back_btn);
    
    msg.add_component(nav_row);
}

// Build main shop category selection message (filtered by user prestige)
static dpp::message build_shop_main_menu(Database* db, const std::string& user_id_str, const dpp::user& user) {
    int user_prestige = db->get_prestige(user.id);
    auto items = db->get_shop_items();
    std::set<std::string> cats;
    for (const auto& item : items) {
        if (!item.category.empty()) cats.insert(normalize_category(item.category));
    }
    
    // Count items per category for display (filtered by prestige)
    std::map<std::string, int> cat_counts;
    for (const auto& item : items) {
        std::string cat = normalize_category(item.category);
        int req_prestige = get_required_prestige(item.metadata);
        if (req_prestige <= user_prestige) {
            cat_counts[cat]++;
        }
    }
    
    std::string description = "🛒 **bronx shop**\n\n";
    description += "browse our selection of gear and equipment!\n\n";
    
    // Rod count
    if (cat_counts.count("rod")) {
        description += "🎣 **rods** — " + std::to_string(cat_counts["rod"]) + " available\n";
    }
    // Bait count
    if (cat_counts.count("bait")) {
        description += "🪱 **bait** — " + std::to_string(cat_counts["bait"]) + " available\n";
    }
    // Titles
    auto titles = get_available_titles(db);
    int title_count = 0;
    for (const auto& t : titles) if (t.price > 0) title_count++;
    description += "🏷️ **titles** — " + std::to_string(title_count) + " available *(rotating)*\n";
    
    // Mining categories
    if (cat_counts.count("pickaxe")) {
        description += "⛏️ **pickaxes** — " + std::to_string(cat_counts["pickaxe"]) + " available\n";
    }
    if (cat_counts.count("minecart")) {
        description += "🛒 **minecarts** — " + std::to_string(cat_counts["minecart"]) + " available\n";
    }
    if (cat_counts.count("bag")) {
        description += "🎒 **bags** — " + std::to_string(cat_counts["bag"]) + " available\n";
    }
    if (cat_counts.count("automation")) {
        description += "🤖 **automation** — " + std::to_string(cat_counts["automation"]) + " available\n";
    }
    
    // Lootbox count (from code catalog, not DB)
    int lootbox_count = 0;
    for (const auto& lb : use_item::get_lootbox_catalog()) {
        if (lb.price > 0) lootbox_count++;
    }
    if (lootbox_count > 0) {
        description += "📦 **lootboxes** — " + std::to_string(lootbox_count) + " available\n";
    }
    description += "\n";
    
    description += "*select a category below to browse*";
    
    auto embed = bronx::create_embed(description);
    bronx::add_invoker_footer(embed, user);
    
    // Build category select menu with counts
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("select a category")
        .set_id("shop_cat_" + user_id_str);
    
    if (cat_counts.count("rod")) {
        select_menu.add_select_option(dpp::select_option(
            "🎣 rods (" + std::to_string(cat_counts["rod"]) + ")", "rod", "fishing rods for catching fish"));
    }
    if (cat_counts.count("bait")) {
        select_menu.add_select_option(dpp::select_option(
            "🪱 bait (" + std::to_string(cat_counts["bait"]) + ")", "bait", "bait to attract different fish"));
    }
    select_menu.add_select_option(dpp::select_option(
        "🏷️ titles (" + std::to_string(title_count) + ")", "title", "cosmetic titles (weekly rotation)"));
    
    // Mining categories
    if (cat_counts.count("pickaxe")) {
        select_menu.add_select_option(dpp::select_option(
            "⛏️ pickaxes (" + std::to_string(cat_counts["pickaxe"]) + ")", "pickaxe", "mining pickaxes for ore extraction"));
    }
    if (cat_counts.count("minecart")) {
        select_menu.add_select_option(dpp::select_option(
            "🛒 minecarts (" + std::to_string(cat_counts["minecart"]) + ")", "minecart", "minecarts for ore delivery speed"));
    }
    if (cat_counts.count("bag")) {
        select_menu.add_select_option(dpp::select_option(
            "🎒 bags (" + std::to_string(cat_counts["bag"]) + ")", "bag", "bags for ore storage capacity"));
    }
    if (cat_counts.count("automation")) {
        select_menu.add_select_option(dpp::select_option(
            "🤖 automation (" + std::to_string(cat_counts["automation"]) + ")", "automation", "autofishers and automation tools"));
    }
    if (lootbox_count > 0) {
        select_menu.add_select_option(dpp::select_option(
            "📦 lootboxes (" + std::to_string(lootbox_count) + ")", "lootbox", "open for random rewards"));
    }
    
    dpp::message msg;
    msg.add_embed(embed);
    msg.add_component(dpp::component().add_component(select_menu));
    return msg;
}

::std::vector<Command*> get_shop_commands(Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Shop command - now with pagination!
    static Command* shop = new Command("shop", "browse items available for purchase", "shop", {"store"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string user_id_str = std::to_string(event.msg.author.id);
            
            // if user supplied a category argument, show that category with pagination
            if (!args.empty()) {
                std::string cat = normalize_category(args[0]);

                // Titles are code-driven, not in the DB
                if (cat == "title") {
                    std::string description = build_titles_description(db, false);
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    dpp::component back;
                    back.set_type(dpp::cot_button)
                        .set_label("back")
                        .set_style(dpp::cos_danger)
                        .set_id("shop_back_" + user_id_str);
                    dpp::message msg(event.msg.channel_id, embed);
                    msg.add_component(dpp::component().add_component(back));
                    bronx::send_message(bot, event, msg);
                    return;
                }

                // Lootboxes are code-driven
                if (cat == "lootbox") {
                    std::string description = "\xf0\x9f\x93\xa6 **bronx shop \xe2\x80\x93 lootboxes**\n\n";
                    for (const auto& lb : use_item::get_lootbox_catalog()) {
                        if (lb.price <= 0) continue;
                        description += lb.emoji + " **" + lb.name + "** \xe2\x80\x94 $" + format_number(lb.price) + "\n";
                        description += "`" + lb.item_id + "` \xe2\x80\xa2 " + std::to_string(lb.min_rolls) + "-" + std::to_string(lb.max_rolls) + " rewards\n";
                        description += "*" + lb.description + "*\n\n";
                    }
                    description += "use `buy <lootbox_id>` to purchase";
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    dpp::component back;
                    back.set_type(dpp::cot_button)
                        .set_label("back")
                        .set_style(dpp::cos_danger)
                        .set_id("shop_back_" + user_id_str);
                    dpp::message msg(event.msg.channel_id, embed);
                    msg.add_component(dpp::component().add_component(back));
                    bronx::send_message(bot, event, msg);
                    return;
                }

                // Show category with pagination (filtered by user prestige)
                int user_prestige = db->get_prestige(event.msg.author.id);
                auto items = get_category_items(db, cat, user_prestige);
                int total_pages = get_total_pages(items);
                std::string description = build_page_description(db, items, cat, 0, total_pages);
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                
                dpp::message msg(event.msg.channel_id, embed);
                build_shop_page_components(msg, db, items, cat, 0, total_pages, user_id_str);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            // default behaviour: show category select menu
            dpp::message msg = build_shop_main_menu(db, user_id_str, event.msg.author);
            msg.channel_id = event.msg.channel_id;
            bot.message_create(msg, [ch = event.msg.channel_id, gid = event.msg.guild_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    std::cerr << "[shop] failed to send shop menu in channel " << ch
                              << " (guild " << gid << "): " << cb.get_error().code << " - " << cb.get_error().message << "\n";
                }
            });
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string user_id_str = std::to_string(event.command.get_issuing_user().id);
            
            // optionally filter by category parameter
            std::string cat;
            auto cat_param = event.get_parameter("category");
            if (std::holds_alternative<std::string>(cat_param)) {
                cat = normalize_category(std::get<std::string>(cat_param));
            }
            
            if (!cat.empty()) {
                // Titles category is code-driven
                if (cat == "title") {
                    std::string description = build_titles_description(db, true);
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    dpp::component back;
                    back.set_type(dpp::cot_button)
                        .set_label("back")
                        .set_style(dpp::cos_danger)
                        .set_id("shop_back_" + user_id_str);
                    dpp::message msg;
                    msg.add_embed(embed);
                    msg.add_component(dpp::component().add_component(back));
                    event.reply(msg);
                    return;
                }

                // Lootbox category is code-driven
                if (cat == "lootbox") {
                    std::string description = "\xf0\x9f\x93\xa6 **bronx shop \xe2\x80\x93 lootboxes**\n\n";
                    for (const auto& lb : use_item::get_lootbox_catalog()) {
                        if (lb.price <= 0) continue;
                        description += lb.emoji + " **" + lb.name + "** \xe2\x80\x94 $" + format_number(lb.price) + "\n";
                        description += "`" + lb.item_id + "` \xe2\x80\xa2 " + std::to_string(lb.min_rolls) + "-" + std::to_string(lb.max_rolls) + " rewards\n";
                        description += "*" + lb.description + "*\n\n";
                    }
                    description += "use `/buy <lootbox_id>` to purchase";
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                // Show category with pagination (filtered by user prestige)
                int user_prestige = db->get_prestige(event.command.get_issuing_user().id);
                auto items = get_category_items(db, cat, user_prestige);
                int total_pages = get_total_pages(items);
                std::string description = build_page_description(db, items, cat, 0, total_pages);
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                
                dpp::message msg;
                msg.add_embed(embed);
                build_shop_page_components(msg, db, items, cat, 0, total_pages, user_id_str);
                event.reply(msg);
            } else {
                // Default: show category select menu
                dpp::message msg = build_shop_main_menu(db, user_id_str, event.command.get_issuing_user());
                event.reply(msg);
            }
        },
        { 
            dpp::command_option(dpp::co_string, "category", "filter by category", false)
                .add_choice(dpp::command_option_choice("rod", "rod"))
                .add_choice(dpp::command_option_choice("bait", "bait"))
                .add_choice(dpp::command_option_choice("titles", "title"))
                .add_choice(dpp::command_option_choice("lootboxes", "lootbox"))
        }
        );
    cmds.push_back(shop);
    
    // Buy command
    static Command* buy = new Command("buy", "purchase an item from the shop (specify amount for consumables)", "shop", {"purchase"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("specify an item to buy"));
                return;
            }

            // Parse arguments: check if last argument is amount (max/all/number)
            ::std::string item_name;
            int64_t amount = 1;
            bool found_amount = false;
            
            if (args.size() >= 2) {
                std::string last_arg = args.back();
                std::transform(last_arg.begin(), last_arg.end(), last_arg.begin(), ::tolower);
                
                // Check if last argument is max/all or a number
                if (last_arg == "max" || last_arg == "all") {
                    found_amount = true;
                    // Join all arguments except the last as item name
                    for (size_t i = 0; i < args.size() - 1; ++i) {
                        if (i > 0) item_name += " ";
                        item_name += args[i];
                    }
                } else {
                    // Try to parse as number
                    try {
                        amount = ::std::stoll(last_arg);
                        if (amount > 0) {
                            found_amount = true;
                            // Join all arguments except the last as item name
                            for (size_t i = 0; i < args.size() - 1; ++i) {
                                if (i > 0) item_name += " ";
                                item_name += args[i];
                            }
                        }
                    } catch (...) {
                        // Not a number, treat as part of item name
                    }
                }
            }
            
            if (!found_amount) {
                // No amount specified, join all arguments as item name
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) item_name += " ";
                    item_name += args[i];
                }
                amount = 1;
            }

            ::std::transform(item_name.begin(), item_name.end(), item_name.begin(), ::tolower);

            auto maybe_item = find_shop_item(db, item_name);
            // Title fallback: check rotating catalog if not in DB shop
            if (!maybe_item) {
                auto title_def = find_title(db, item_name);
                if (!title_def) {
                    // Also try matching against available titles by partial id
                    for (const auto& t : get_available_titles(db)) {
                        std::string tid = t.item_id;
                        std::transform(tid.begin(), tid.end(), tid.begin(), ::tolower);
                        if (tid.find(item_name) != ::std::string::npos) { title_def = t; break; }
                    }
                }
                if (title_def) {
                    // Check the title is currently available
                    bool available = false;
                    for (const auto& avail : get_available_titles(db)) {
                        if (avail.item_id == title_def->item_id) { available = true; break; }
                    }
                    if (!available) {
                        bronx::send_message(bot, event, bronx::error("that title isn't in the shop this week"));
                        return;
                    }
                    // Sold out? (purchase_limit reached)
                    if (is_title_sold_out(db, *title_def)) {
                        bronx::send_message(bot, event, bronx::error("that title is sold out — all copies have been claimed"));
                        return;
                    }
                    // Already owned?
                    if (db->has_item(event.msg.author.id, title_def->item_id)) {
                        bronx::send_message(bot, event, bronx::error("you already own that title"));
                        return;
                    }
                    auto user = db->get_user(event.msg.author.id);
                    if (!user || user->wallet < title_def->price) {
                        bronx::send_message(bot, event, bronx::error("you can't afford this title"));
                        return;
                    }
                    // Metadata must be valid JSON for the inventory.metadata
                    // column constraint — encode display as {"display":"VALUE"}.
                    std::string short_id = title_def->item_id;
                    if (short_id.rfind("title_", 0) == 0) short_id = short_id.substr(6);
                    if (db->update_wallet(event.msg.author.id, -title_def->price) &&
                        db->add_item(event.msg.author.id, title_def->item_id, "title", 1,
                                     title_display_to_json(title_def->display), 1)) {
                        bronx::send_message(bot, event,
                            bronx::success("purchased title **" + title_def->display + "** for $" + format_number(title_def->price) +
                                "\nuse `title equip " + short_id + "` to equip it"));
                    } else {
                        db->update_wallet(event.msg.author.id, title_def->price); // refund
                        bronx::send_message(bot, event, bronx::error("purchase failed"));
                    }
                    return;
                }
                // Lootbox fallback: check code-driven lootbox catalog
                const use_item::LootboxDef* lb_match = nullptr;
                for (const auto& lb : use_item::get_lootbox_catalog()) {
                    if (lb.price <= 0) continue;
                    std::string lid = lb.item_id;
                    std::transform(lid.begin(), lid.end(), lid.begin(), ::tolower);
                    std::string lname = lb.name;
                    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                    if (lid == item_name || lname == item_name) { lb_match = &lb; break; }
                }
                if (!lb_match) {
                    // Fuzzy match on lootbox names
                    for (const auto& lb : use_item::get_lootbox_catalog()) {
                        if (lb.price <= 0) continue;
                        std::string lid = lb.item_id;
                        std::transform(lid.begin(), lid.end(), lid.begin(), ::tolower);
                        std::string lname = lb.name;
                        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                        if (lid.find(item_name) != std::string::npos || lname.find(item_name) != std::string::npos) { lb_match = &lb; break; }
                    }
                }
                if (lb_match) {
                    // Handle max/all
                    if (found_amount && (args.back() == "max" || args.back() == "all")) {
                        auto user = db->get_user(event.msg.author.id);
                        if (user && lb_match->price > 0) amount = user->wallet / lb_match->price;
                    }
                    if (amount <= 0) amount = 1;
                    if (amount > 100) { bronx::send_message(bot, event, bronx::error("you can buy up to 100 lootboxes at a time")); return; }
                    int64_t cost = lb_match->price * amount;
                    auto user = db->get_user(event.msg.author.id);
                    if (!user || user->wallet < cost) {
                        bronx::send_message(bot, event, bronx::error("you can't afford " + std::to_string(amount) + "x **" + lb_match->name + "** ($" + format_number(cost) + ")"));
                        return;
                    }
                    db->update_wallet(event.msg.author.id, -cost);
                    db->add_item(event.msg.author.id, lb_match->item_id, "other", (int)amount, "{}", 1);
                    int64_t new_bal = db->get_wallet(event.msg.author.id);
                    std::string log_desc = "bought " + lb_match->name;
                    if (amount > 1) log_desc += " x" + std::to_string(amount);
                    log_desc += " ($" + format_number(cost) + ")";
                    bronx::db::history_operations::log_shop(db, event.msg.author.id, log_desc, -cost, new_bal);
                    std::string desc = "purchased " + lb_match->emoji + " **" + lb_match->name + "**";
                    if (amount > 1) desc += " x" + std::to_string(amount);
                    desc += " for $" + format_number(cost) + "\nuse `use " + lb_match->item_id + "` to open!";
                    auto embed = bronx::success(desc);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
                bronx::send_message(bot, event, bronx::error("item not found in shop"));
                return;
            }
            ShopItem item = *maybe_item;

            // Handle max/all amount calculation
            if (found_amount && (args.back() == "max" || args.back() == "all")) {
                std::string last_arg = args.back();
                std::transform(last_arg.begin(), last_arg.end(), last_arg.begin(), ::tolower);
                if (last_arg == "max" || last_arg == "all") {
                    auto user = db->get_user(event.msg.author.id);
                    if (!user || item.price <= 0) {
                        bronx::send_message(bot, event, bronx::error("unable to calculate max amount"));
                        return;
                    }
                    amount = user->wallet / item.price;
                }
            }
            
            if (amount <= 0) {
                // If somehow still invalid, default to 1
                amount = 1;
            }
            const int64_t MAX_Q = 100000; // sanity cap
            if (amount > MAX_Q) {
                bronx::send_message(bot, event, bronx::error("amount too large"));
                return;
            }

            // determine type from category (shop entries keep this accurate)
            ::std::string item_type = normalize_category(item.category);
            if (item_type.empty()) {
                if (item.item_id.rfind("rod_", 0) == 0) item_type = "rod";
                else if (item.item_id.rfind("bait_", 0) == 0) item_type = "bait";
                else item_type = "other";
            }
            if (item_type != "bait" && amount > 1) {
                bronx::send_message(bot, event, bronx::error("you can only purchase one of that item"));
                return;
            }

            int64_t cost = item.price * amount;
            auto user = db->get_user(event.msg.author.id);
            if (!user || user->wallet < cost) {
                bronx::send_message(bot, event, bronx::error("you can't afford this item"));
                return;
            }

            // Check prestige requirement from metadata
            int req_prestige = get_required_prestige(item.metadata);
            if (req_prestige > 0) {
                int user_prestige = db->get_prestige(event.msg.author.id);
                if (user_prestige < req_prestige) {
                    bronx::send_message(bot, event, bronx::error("you need prestige **" + std::to_string(req_prestige) + "** to purchase this item (you are prestige " + std::to_string(user_prestige) + ")"));
                    return;
                }
            }

            // Deduct money and add to inventory
            if (!db->update_wallet(event.msg.author.id, -cost)) {
                bronx::send_message(bot, event, bronx::error("failed to complete purchase"));
                return;
            }
            int success_count = 0;
            for (int i = 0; i < amount; ++i) {
                if (db->add_item(event.msg.author.id, item.item_id, item_type, 1, item.metadata, item.level)) {
                    success_count++;
                } else {
                    break; // stop if a write fails
                }
            }
            if (success_count == amount) {
                // Log shop purchase to history
                int64_t new_balance = db->get_wallet(event.msg.author.id);
                std::string log_desc = "bought " + item.name;
                if (amount > 1) log_desc += " x" + std::to_string(amount);
                log_desc += " ($" + format_number(cost) + ")";
                bronx::db::history_operations::log_shop(db, event.msg.author.id, log_desc, -cost, new_balance);
                
                ::std::string description = "purchased **" + item.name + "**";
                if (amount > 1) description += " x" + std::to_string(amount);
                description += " for $" + format_number(cost);
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                // refund difference if partial
                int64_t refund = (amount - success_count) * item.price;
                if (refund > 0) db->update_wallet(event.msg.author.id, refund);
                bronx::send_message(bot, event, bronx::error("only " + std::to_string(success_count) + " of " + std::to_string(amount) + " were added to inventory"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ::std::string item_name = ::std::get<::std::string>(event.get_parameter("item"));
            ::std::transform(item_name.begin(), item_name.end(), item_name.begin(), ::tolower);

            auto maybe_item = db->get_shop_item(item_name);
            if (!maybe_item) {
                // Check lootbox catalog
                const use_item::LootboxDef* lb_match = nullptr;
                for (const auto& lb : use_item::get_lootbox_catalog()) {
                    if (lb.price <= 0) continue;
                    std::string lid = lb.item_id;
                    std::transform(lid.begin(), lid.end(), lid.begin(), ::tolower);
                    std::string lname = lb.name;
                    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                    if (lid == item_name || lname == item_name || lid.find(item_name) != std::string::npos || lname.find(item_name) != std::string::npos) {
                        lb_match = &lb; break;
                    }
                }
                if (lb_match) {
                    int64_t lb_amount = 1;
                    try {
                        auto amt_param = event.get_parameter("amount");
                        if (std::holds_alternative<std::string>(amt_param)) {
                            std::string s = std::get<std::string>(amt_param);
                            if (!s.empty()) lb_amount = std::stoll(s);
                        }
                    } catch (...) {}
                    if (lb_amount <= 0) lb_amount = 1;
                    if (lb_amount > 100) { event.reply(dpp::message().add_embed(bronx::error("max 100 lootboxes at a time"))); return; }
                    int64_t cost = lb_match->price * lb_amount;
                    auto user = db->get_user(event.command.get_issuing_user().id);
                    if (!user || user->wallet < cost) {
                        event.reply(dpp::message().add_embed(bronx::error("you can't afford this")));
                        return;
                    }
                    db->update_wallet(event.command.get_issuing_user().id, -cost);
                    db->add_item(event.command.get_issuing_user().id, lb_match->item_id, "other", (int)lb_amount, "{}", 1);
                    int64_t new_bal = db->get_wallet(event.command.get_issuing_user().id);
                    bronx::db::history_operations::log_shop(db, event.command.get_issuing_user().id, "bought " + lb_match->name + " x" + std::to_string(lb_amount), -cost, new_bal);
                    std::string desc = "purchased " + lb_match->emoji + " **" + lb_match->name + "**";
                    if (lb_amount > 1) desc += " x" + std::to_string(lb_amount);
                    desc += " for $" + format_number(cost) + "\nuse `/use " + lb_match->item_id + "` to open!";
                    event.reply(dpp::message().add_embed(bronx::success(desc)));
                    return;
                }
                event.reply(dpp::message().add_embed(bronx::error("item not found in shop")));
                return;
            }
            ShopItem item = *maybe_item;

            // Parse optional amount (slash option)
            int64_t amount = 1;
            try {
                auto amt_param = event.get_parameter("amount");
                if (::std::holds_alternative<::std::string>(amt_param)) {
                    ::std::string amount_str = ::std::get<::std::string>(amt_param);
                    if (!amount_str.empty()) {
                        amount = ::std::stoll(amount_str);
                    }
                }
            } catch (...) {}

            if (amount <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be positive")));
                return;
            }
            const int64_t MAX_Q = 100000;
            if (amount > MAX_Q) {
                event.reply(dpp::message().add_embed(bronx::error("amount too large")));
                return;
            }

            ::std::string item_type = normalize_category(item.category);
            if (item_type.empty()) {
                if (item.item_id.rfind("rod_", 0) == 0) item_type = "rod";
                else if (item.item_id.rfind("bait_", 0) == 0) item_type = "bait";
                else item_type = "other";
            }
            if (item_type != "bait" && amount > 1) {
                event.reply(dpp::message().add_embed(bronx::error("you can only purchase one of that item")));
                return;
            }

            int64_t cost = item.price * amount;
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user || user->wallet < cost) {
                event.reply(dpp::message().add_embed(bronx::error("you can't afford this item")));
                return;
            }

            // Check prestige requirement from metadata
            int req_prestige = get_required_prestige(item.metadata);
            if (req_prestige > 0) {
                int user_prestige = db->get_prestige(event.command.get_issuing_user().id);
                if (user_prestige < req_prestige) {
                    event.reply(dpp::message().add_embed(bronx::error("you need prestige **" + std::to_string(req_prestige) + "** to purchase this item (you are prestige " + std::to_string(user_prestige) + ")")));
                    return;
                }
            }

            if (!db->update_wallet(event.command.get_issuing_user().id, -cost)) {
                event.reply(dpp::message().add_embed(bronx::error("failed to complete purchase")));
                return;
            }
            int success_count = 0;
            for (int i = 0; i < amount; ++i) {
                if (db->add_item(event.command.get_issuing_user().id, item.item_id, item_type, 1, item.metadata, item.level)) {
                    success_count++;
                } else {
                    break;
                }
            }
            if (success_count == amount) {
                // Log shop purchase to history
                uint64_t uid = event.command.get_issuing_user().id;
                int64_t new_balance = db->get_wallet(uid);
                std::string log_desc = "bought " + item.name;
                if (amount > 1) log_desc += " x" + std::to_string(amount);
                log_desc += " ($" + format_number(cost) + ")";
                bronx::db::history_operations::log_shop(db, uid, log_desc, -cost, new_balance);
                
                ::std::string description = "purchased **" + item.name + "**";
                if (amount > 1) description += " x" + std::to_string(amount);
                description += " for $" + format_number(cost);
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                int64_t refund = (amount - success_count) * item.price;
                if (refund > 0) db->update_wallet(event.command.get_issuing_user().id, refund);
                event.reply(dpp::message().add_embed(bronx::error("only " + std::to_string(success_count) + " of " + std::to_string(amount) + " were added to inventory")));
            }
        },
        {
            dpp::command_option(dpp::co_string, "item", "item to purchase", true),
            dpp::command_option(dpp::co_string, "amount", "quantity to purchase (consumables only)", false)
        });
    cmds.push_back(buy);
    
    // Sell command - sell items back for 40% of original price
    static Command* sell_item = new Command("sellitem", "sell shop items back for 40% of original value", "shop", {"si", "sellback"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: sellitem <item> [amount]\nsells items for 40% of their shop price"));
                return;
            }
            
            // Parse item
            std::string item_name = args[0];
            auto maybe = find_shop_item(db, item_name);
            if (!maybe) {
                bronx::send_message(bot, event, bronx::error("item not found in shop — only purchasable items can be sold back"));
                return;
            }
            
            const ShopItem& item = *maybe;
            if (item.price <= 0) {
                bronx::send_message(bot, event, bronx::error("this item cannot be sold"));
                return;
            }
            
            // Parse amount (default 1)
            int amount = 1;
            if (args.size() >= 2) {
                std::string amt_str = args[1];
                std::transform(amt_str.begin(), amt_str.end(), amt_str.begin(), ::tolower);
                if (amt_str == "all" || amt_str == "max") {
                    amount = db->get_item_quantity(event.msg.author.id, item.item_id);
                } else {
                    try {
                        amount = std::stoi(amt_str);
                    } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid amount"));
                        return;
                    }
                }
            }
            
            if (amount <= 0) {
                bronx::send_message(bot, event, bronx::error("amount must be at least 1"));
                return;
            }
            
            // Check inventory
            int owned = db->get_item_quantity(event.msg.author.id, item.item_id);
            if (owned < amount) {
                bronx::send_message(bot, event, bronx::error("you only have **" + std::to_string(owned) + "** " + item.name));
                return;
            }
            
            // Calculate sell price (40% of original)
            int64_t sell_price = static_cast<int64_t>(item.price * 0.40) * amount;
            if (sell_price <= 0) {
                bronx::send_message(bot, event, bronx::error("this item has no sell value"));
                return;
            }
            
            // Remove from inventory and add money
            if (!db->remove_item(event.msg.author.id, item.item_id, amount)) {
                bronx::send_message(bot, event, bronx::error("failed to remove item from inventory"));
                return;
            }
            
            if (!db->update_wallet(event.msg.author.id, sell_price)) {
                // Attempt to restore item
                db->add_item(event.msg.author.id, item.item_id, item.category, amount, item.metadata, item.level);
                bronx::send_message(bot, event, bronx::error("failed to add money to wallet"));
                return;
            }
            
            // Log sale to history
            int64_t new_balance = db->get_wallet(event.msg.author.id);
            std::string log_desc = "sold " + item.name;
            if (amount > 1) log_desc += " x" + std::to_string(amount);
            log_desc += " ($" + format_number(sell_price) + ")";
            bronx::db::history_operations::log_shop(db, event.msg.author.id, log_desc, sell_price, new_balance);
            
            std::string desc = "sold **" + item.name + "**";
            if (amount > 1) desc += " x" + std::to_string(amount);
            desc += " for $" + format_number(sell_price);
            desc += "\n*(40% of shop price)*";
            
            auto embed = bronx::success(desc);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string item_name = std::get<std::string>(event.get_parameter("item"));
            int amount = 1;
            
            if (std::holds_alternative<int64_t>(event.get_parameter("amount"))) {
                amount = static_cast<int>(std::get<int64_t>(event.get_parameter("amount")));
            }
            
            auto maybe = find_shop_item(db, item_name);
            if (!maybe) {
                event.reply(dpp::message().add_embed(bronx::error("item not found in shop — only purchasable items can be sold back")));
                return;
            }
            
            const ShopItem& item = *maybe;
            if (item.price <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("this item cannot be sold")));
                return;
            }
            
            if (amount <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be at least 1")));
                return;
            }
            
            int owned = db->get_item_quantity(event.command.get_issuing_user().id, item.item_id);
            if (owned < amount) {
                event.reply(dpp::message().add_embed(bronx::error("you only have **" + std::to_string(owned) + "** " + item.name)));
                return;
            }
            
            int64_t sell_price = static_cast<int64_t>(item.price * 0.40) * amount;
            if (sell_price <= 0) {
                event.reply(dpp::message().add_embed(bronx::error("this item has no sell value")));
                return;
            }
            
            if (!db->remove_item(event.command.get_issuing_user().id, item.item_id, amount)) {
                event.reply(dpp::message().add_embed(bronx::error("failed to remove item from inventory")));
                return;
            }
            
            if (!db->update_wallet(event.command.get_issuing_user().id, sell_price)) {
                db->add_item(event.command.get_issuing_user().id, item.item_id, item.category, amount, item.metadata, item.level);
                event.reply(dpp::message().add_embed(bronx::error("failed to add money to wallet")));
                return;
            }
            
            int64_t new_balance = db->get_wallet(event.command.get_issuing_user().id);
            std::string log_desc = "sold " + item.name;
            if (amount > 1) log_desc += " x" + std::to_string(amount);
            log_desc += " ($" + format_number(sell_price) + ")";
            bronx::db::history_operations::log_shop(db, event.command.get_issuing_user().id, log_desc, sell_price, new_balance);
            
            std::string desc = "sold **" + item.name + "**";
            if (amount > 1) desc += " x" + std::to_string(amount);
            desc += " for $" + format_number(sell_price);
            desc += "\n*(40% of shop price)*";
            
            auto embed = bronx::success(desc);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "item", "item to sell", true),
            dpp::command_option(dpp::co_integer, "amount", "quantity to sell (default: 1)", false)
        });
    cmds.push_back(sell_item);
    
    // Owner-only item management
    static Command* itemadmin = new Command("item", "manage shop items (owner only)", "shop", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!commands::is_owner(event.msg.author.id)) {
                bronx::send_message(bot, event, bronx::error("this command is restricted to the bot owner."));
                return;
            }
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: item add|price|delete|update ..."));
                return;
            }
            ::std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            if (sub == "add") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: item add <id> <price> [name] [category]"));
                    return;
                }
                ShopItem it;
                it.item_id = args[1];
                try { it.price = std::stoll(args[2]); } catch(...) { it.price = 0; }
                it.name = (args.size() >= 4 ? args[3] : it.item_id);
                it.category = (args.size() >= 5 ? args[4] : "");
                it.required_level = 0;
                it.level = 1;
                it.max_quantity = -1;
                it.usable = true;
                it.metadata = "";
                if (db->create_shop_item(it)) {
                    bronx::send_message(bot, event, bronx::success("created item " + it.item_id));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to create item"));
                }
            } else if (sub == "price") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: item price <id> <newprice>"));
                    return;
                }
                ::std::string id = args[1];
                int64_t p = 0;
                try { p = std::stoll(args[2]); } catch(...) {}
                if (db->update_shop_item_price(id, p)) {
                    bronx::send_message(bot, event, bronx::success("price updated"));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to update price"));
                }
            } else if (sub == "delete") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: item delete <id>"));
                    return;
                }
                if (db->delete_shop_item(args[1])) {
                    bronx::send_message(bot, event, bronx::success("deleted item " + args[1]));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to delete item"));
                }
            } else if (sub == "update") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: item update <id> <field>=<value> [...]"));
                    return;
                }
                auto maybe_it = db->get_shop_item(args[1]);
                if (!maybe_it) {
                    bronx::send_message(bot, event, bronx::error("item not found"));
                    return;
                }
                ShopItem it = *maybe_it;
                // parse field=value pairs
                for (size_t i = 2; i < args.size(); ++i) {
                    auto pos = args[i].find('=');
                    if (pos == ::std::string::npos) continue;
                    ::std::string field = args[i].substr(0,pos);
                    ::std::string val = args[i].substr(pos+1);
                    if (field == "price") {
                        try { it.price = std::stoll(val); } catch(...){}
                    } else if (field == "name") {
                        it.name = val;
                    } else if (field == "category") {
                        it.category = val;
                    } else if (field == "level") {
                        try { it.level = std::stoi(val); } catch(...){}
                    } else if (field == "required_level") {
                        try { it.required_level = std::stoi(val); } catch(...){}
                    } else if (field == "usable") {
                        it.usable = (val == "1" || val == "true");
                    } else if (field == "metadata") {
                        it.metadata = val;
                    }
                }
                if (db->update_shop_item(it)) {
                    bronx::send_message(bot, event, bronx::success("updated item " + it.item_id));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to update item"));
                }
            } else {
                bronx::send_message(bot, event, bronx::error("unknown subcommand"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply(dpp::message().add_embed(bronx::error("use message mode for item management")));
        });
    cmds.push_back(itemadmin);

    // command to run price tuning based on fishing logs
    static Command* tuneprices = new Command("tuneprices", "adjust bait prices using logged fishing data (owner only)", "shop", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!commands::is_owner(event.msg.author.id)) {
                bronx::send_message(bot, event, bronx::error("this command is restricted to the bot owner."));
                return;
            }
            // first, show current report
            std::string report = db->get_bait_tuning_report(50);
            bronx::send_message(bot, event, bronx::info("tuning report:\n" + report));
            // then apply adjustments
            if (db->tune_bait_prices_from_logs(50)) {
                bronx::send_message(bot, event, bronx::success("bait prices tuned based on fishing logs"));
            } else {
                bronx::send_message(bot, event, bronx::error("failed to tune prices"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!commands::is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("owner-only command")));
                return;
            }
            std::string report = db->get_bait_tuning_report(50);
            bool ok = db->tune_bait_prices_from_logs(50);
            std::string combined = "**tuning report**\n" + report + "\n" +
                (ok ? bronx::EMOJI_CHECK + " bait prices tuned based on fishing logs" : bronx::EMOJI_DENY + " failed to tune prices");
            event.reply(dpp::message().add_embed(bronx::info(combined)));
        });
    cmds.push_back(tuneprices);

    return cmds;
}

// Register select menu handlers for shop browsing and quick purchasing
void register_shop_interactions(dpp::cluster& bot, Database* db) {
    // ========================================================================
    // CATEGORY SELECTION - show first page of selected category
    // ========================================================================
    bot.on_select_click([db, &bot](const dpp::select_click_t& event) {
        if (event.custom_id.rfind("shop_cat_", 0) != 0) return;
        
        ::std::string user_id_str = event.custom_id.substr(9);
        dpp::snowflake expected = ::std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != expected) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        std::string category = normalize_category(event.values[0]);

        // Titles category - now with pagination
        if (category == "title") {
            auto titles = get_purchasable_titles(db);
            int total_pages = get_title_total_pages(titles);
            std::string description = build_title_page_description(db, titles, 0, total_pages);
            
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            
            dpp::message resp;
            resp.add_embed(embed);
            build_title_page_components(resp, db, titles, 0, total_pages, user_id_str);
            event.reply(dpp::ir_update_message, resp);
            return;
        }

        // Lootbox category - code-driven
        if (category == "lootbox") {
            std::string description = "\xf0\x9f\x93\xa6 **bronx shop \xe2\x80\x93 lootboxes**\n\n";
            for (const auto& lb : use_item::get_lootbox_catalog()) {
                if (lb.price <= 0) continue;
                description += lb.emoji + " **" + lb.name + "** \xe2\x80\x94 $" + format_number(lb.price) + "\n";
                description += "`" + lb.item_id + "` \xe2\x80\xa2 " + std::to_string(lb.min_rolls) + "-" + std::to_string(lb.max_rolls) + " rewards\n";
                description += "*" + lb.description + "*\n\n";
            }
            description += "use `buy <lootbox_id>` to purchase";
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            dpp::component back;
            back.set_type(dpp::cot_button)
                .set_label("back")
                .set_style(dpp::cos_danger)
                .set_id("shop_back_" + user_id_str);
            dpp::message resp;
            resp.add_embed(embed);
            resp.add_component(dpp::component().add_component(back));
            event.reply(dpp::ir_update_message, resp);
            return;
        }
        
        // Show first page of category with pagination (filtered by user prestige)
        int user_prestige = db->get_prestige(event.command.get_issuing_user().id);
        auto items = get_category_items(db, category, user_prestige);
        int total_pages = get_total_pages(items);
        std::string description = build_page_description(db, items, category, 0, total_pages);
        
        auto embed = bronx::create_embed(description);
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
        
        dpp::message resp;
        resp.add_embed(embed);
        build_shop_page_components(resp, db, items, category, 0, total_pages, user_id_str);
        event.reply(dpp::ir_update_message, resp);
    });

    // ========================================================================
    // PAGE NAVIGATION - handle prev/next page buttons
    // Format: shop_page_<user>_<category>_<page>
    // ========================================================================
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("shop_page_", 0) != 0) return;
        
        // Parse: shop_page_<user>_<category>_<page>
        std::string rest = event.custom_id.substr(10); // after "shop_page_"
        size_t first_sep = rest.find('_');
        if (first_sep == std::string::npos) return;
        size_t last_sep = rest.rfind('_');
        if (last_sep == first_sep) return;
        
        std::string user_id_str = rest.substr(0, first_sep);
        std::string category = rest.substr(first_sep + 1, last_sep - first_sep - 1);
        int page = 0;
        try { page = std::stoi(rest.substr(last_sep + 1)); } catch(...) {}
        
        dpp::snowflake expected = std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != expected) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Handle title category separately (uses TitleDef, not ShopItem)
        if (category == "title") {
            auto titles = get_purchasable_titles(db);
            int total_pages = get_title_total_pages(titles);
            
            // Clamp page to valid range
            if (page < 0) page = 0;
            if (page >= total_pages) page = total_pages - 1;
            
            std::string description = build_title_page_description(db, titles, page, total_pages);
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            
            dpp::message resp;
            resp.add_embed(embed);
            build_title_page_components(resp, db, titles, page, total_pages, user_id_str);
            event.reply(dpp::ir_update_message, resp);
            return;
        }
        
        // Filter by user prestige
        int user_prestige = db->get_prestige(event.command.get_issuing_user().id);
        auto items = get_category_items(db, category, user_prestige);
        int total_pages = get_total_pages(items);
        
        // Clamp page to valid range
        if (page < 0) page = 0;
        if (page >= total_pages) page = total_pages - 1;
        
        std::string description = build_page_description(db, items, category, page, total_pages);
        auto embed = bronx::create_embed(description);
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
        
        dpp::message resp;
        resp.add_embed(embed);
        build_shop_page_components(resp, db, items, category, page, total_pages, user_id_str);
        event.reply(dpp::ir_update_message, resp);
    });

    // ========================================================================
    // ITEM SELECTION - purchase from dropdown
    // Format: shop_item_<user>_<category>_<page>
    // ========================================================================
    bot.on_select_click([db, &bot](const dpp::select_click_t& event) {
        if (event.custom_id.rfind("shop_item_", 0) != 0) return;
        
        // Parse: shop_item_<user>_<category>_<page>
        std::string rest = event.custom_id.substr(10);
        size_t first_sep = rest.find('_');
        if (first_sep == std::string::npos) return;
        
        std::string user_id_str = rest.substr(0, first_sep);
        dpp::snowflake expected = std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != expected) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        std::string item_id = event.values[0];
        auto maybe_item = db->get_shop_item(item_id);
        if (!maybe_item) {
            // Title fallback: titles are in a hardcoded catalog, not shop_items table
            auto title_def = find_title(db, item_id);
            if (title_def) {
                uint64_t uid = event.command.get_issuing_user().id;
                // Check availability
                bool available = false;
                for (const auto& avail : get_available_titles(db)) {
                    if (avail.item_id == title_def->item_id) { available = true; break; }
                }
                if (!available) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("that title isn't in the shop this week")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (is_title_sold_out(db, *title_def)) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("that title is sold out")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (db->has_item(uid, title_def->item_id)) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("you already own that title")).set_flags(dpp::m_ephemeral));
                    return;
                }
                auto user = db->get_user(uid);
                if (!user || user->wallet < title_def->price) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("you can't afford this title ($" + format_number(title_def->price) + ")")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string short_id = title_def->item_id;
                if (short_id.rfind("title_", 0) == 0) short_id = short_id.substr(6);
                if (db->update_wallet(uid, -title_def->price) &&
                    db->add_item(uid, title_def->item_id, "title", 1,
                                 title_display_to_json(title_def->display), 1)) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success("purchased title **" + title_def->display + "** for $" + format_number(title_def->price) +
                            "\nuse `title equip " + short_id + "` to equip it")).set_flags(dpp::m_ephemeral));
                } else {
                    db->update_wallet(uid, title_def->price); // refund
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("purchase failed")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("item not found in shop")).set_flags(dpp::m_ephemeral));
            return;
        }
        ShopItem item = *maybe_item;
        
        // If bait, show quantity modal
        std::string item_cat = normalize_category(item.category);
        if (item_cat == "bait") {
            dpp::interaction_modal_response modal("shop_bait_modal_" + user_id_str + "_" + item_id, "Buy " + item.name);
            modal.add_component(
                dpp::component()
                    .set_label("Amount (or 'max')")
                    .set_id("bait_amount")
                    .set_type(dpp::cot_text)
                    .set_placeholder("How many to buy?")
                    .set_min_length(1)
                    .set_max_length(10)
                    .set_text_style(dpp::text_short)
            );
            event.dialog(modal);
            return;
        }
        
        // For rods, purchase directly (quantity = 1)
        int64_t cost = item.price;
        uint64_t uid = event.command.get_issuing_user().id;
        auto user = db->get_user(uid);
        
        if (!user || user->wallet < cost) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you can't afford this item ($" + format_number(cost) + ")")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Check prestige requirement
        int req_prestige = get_required_prestige(item.metadata);
        if (req_prestige > 0) {
            int user_prestige = db->get_prestige(uid);
            if (user_prestige < req_prestige) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you need prestige **" + std::to_string(req_prestige) + "** to purchase this item (you are P" + std::to_string(user_prestige) + ")")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        
        if (db->update_wallet(uid, -cost) &&
            db->add_item(uid, item.item_id, item_cat, 1, item.metadata, item.level)) {
            // Log purchase
            int64_t new_balance = db->get_wallet(uid);
            bronx::db::history_operations::log_shop(db, uid, "bought " + item.name + " ($" + format_number(cost) + ")", -cost, new_balance);
            
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::success("🎣 purchased **" + item.name + "** for $" + format_number(cost)))
                    .set_flags(dpp::m_ephemeral));
        } else {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("purchase failed")).set_flags(dpp::m_ephemeral));
        }
    });

    // ========================================================================
    // BACK BUTTON - return to main shop menu
    // ========================================================================
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        if (event.custom_id.rfind("shop_back_", 0) != 0) return;
        
        std::string user_id_str = event.custom_id.substr(10);
        dpp::snowflake expected = std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != expected) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Return to main shop menu
        dpp::message resp = build_shop_main_menu(db, user_id_str, event.command.get_issuing_user());
        event.reply(dpp::ir_update_message, resp);
    });

    // handle modal submissions for bait amount
    bot.on_form_submit([db, &bot](const dpp::form_submit_t& event) {
        if (event.custom_id.rfind("shop_bait_modal_", 0) != 0) return;
        // format: shop_bait_modal_<user>_<itemid>
        size_t prefix = strlen("shop_bait_modal_");
        size_t sep = event.custom_id.find('_', prefix);
        if (sep == std::string::npos) return;
        ::std::string user_id_str = event.custom_id.substr(prefix, sep - prefix);
        ::std::string item_id = event.custom_id.substr(sep + 1);
        dpp::snowflake expected = ::std::stoull(user_id_str);
        if (event.command.get_issuing_user().id != expected) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this modal isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        if (event.components.empty()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("no amount provided")).set_flags(dpp::m_ephemeral));
            return;
        }
        ::std::string amtstr;
        try {
            amtstr = ::std::get<::std::string>(event.components[0].value);
        } catch (...) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("invalid amount")).set_flags(dpp::m_ephemeral));
            return;
        }
        int64_t amount = 1;
        
        // Handle "all" or "max" keywords
        std::string lower_amt = amtstr;
        std::transform(lower_amt.begin(), lower_amt.end(), lower_amt.begin(), ::tolower);
        if (lower_amt == "all" || lower_amt == "max") {
            auto maybe_item = db->get_shop_item(item_id);
            if (!maybe_item) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("item not found")).set_flags(dpp::m_ephemeral));
                return;
            }
            ShopItem item = *maybe_item;
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user || item.price <= 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("unable to calculate max amount")).set_flags(dpp::m_ephemeral));
                return;
            }
            amount = user->wallet / item.price;
        } else {
            try { amount = std::stoll(amtstr); } catch(...) { }
        }
        
        if (amount <= 0) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("amount must be positive")).set_flags(dpp::m_ephemeral));
            return;
        }
        const int64_t MAX_Q = 100000;
        if (amount > MAX_Q) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("amount too large")).set_flags(dpp::m_ephemeral));
            return;
        }
        auto maybe_item = db->get_shop_item(item_id);
        if (!maybe_item) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("item not found")).set_flags(dpp::m_ephemeral));
            return;
        }
        ShopItem item = *maybe_item;
        int64_t cost = item.price * amount;
        uint64_t uid = event.command.get_issuing_user().id;
        auto user = db->get_user(uid);
        if (!user || user->wallet < cost) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("you can't afford this item")).set_flags(dpp::m_ephemeral));
            return;
        }
        // Check prestige requirement from metadata
        int req_prestige = get_required_prestige(item.metadata);
        if (req_prestige > 0) {
            int user_prestige = db->get_prestige(uid);
            if (user_prestige < req_prestige) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("you need prestige **" + std::to_string(req_prestige) + "** to purchase this item")).set_flags(dpp::m_ephemeral));
                return;
            }
        }
        if (db->update_wallet(uid, -cost)) {
            int success_count = 0;
            for (int64_t i = 0; i < amount; ++i) {
                if (db->add_item(uid, item.item_id, "bait", 1, item.metadata, item.level)) {
                    success_count++;
                } else {
                    break;
                }
            }
            if (success_count == amount) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success("purchased **" + item.name + "** x" + std::to_string(amount) + " for $" + format_number(cost)))
                        .set_flags(dpp::m_ephemeral));
            } else {
                int64_t refund = (amount - success_count) * item.price;
                if (refund > 0) db->update_wallet(uid, refund);
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("only " + std::to_string(success_count) + " of " + std::to_string(amount) + " were added to inventory"))
                        .set_flags(dpp::m_ephemeral));
            }
        } else {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("failed to complete purchase")).set_flags(dpp::m_ephemeral));
        }
    });
}

} // namespace commands
