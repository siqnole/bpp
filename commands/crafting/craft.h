#pragma once
#include "crafting_helpers.h"
#include "../economy/helpers.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <ctime>

using namespace bronx::db;

namespace commands {
namespace crafting {

// ============================================================================
// INGREDIENT CHECKING — resolves collectibles by metadata name
// ============================================================================

struct IngredientCheck {
    bool has_enough;
    int available;
    int required;
    std::string display;
    // For collectible ingredients, store the matching item_ids so we can remove them
    std::vector<std::string> matching_item_ids;
};

// Check if user has all ingredients for a recipe
inline std::vector<IngredientCheck> check_ingredients(Database* db, uint64_t user_id, const Recipe& recipe) {
    std::vector<IngredientCheck> checks;
    auto inventory = db->get_inventory(user_id);
    
    for (const auto& ingredient : recipe.ingredients) {
        IngredientCheck check;
        check.required = ingredient.quantity;
        check.display = ingredient.emoji + " " + ingredient.display_name;
        
        if (ingredient.item_type == "coins") {
            // Special case: check wallet balance
            int64_t wallet = db->get_wallet(user_id);
            check.available = static_cast<int>(wallet);
            if (check.available > ingredient.quantity) check.available = ingredient.quantity + 1; // cap display
            check.has_enough = (wallet >= ingredient.quantity);
        } else if (ingredient.item_type == "collectible") {
            // Collectibles (fish/ore) are stored with unique IDs and metadata containing name
            // We need to find items matching the ingredient name
            int count = 0;
            for (const auto& item : inventory) {
                if (item.item_type == "collectible" && !item.metadata.empty()) {
                    if (matches_ingredient_by_name(item.metadata, ingredient.display_name)) {
                        // Check if the item is locked
                        if (item.metadata.find("\"locked\":true") != std::string::npos) continue;
                        count += item.quantity;
                        check.matching_item_ids.push_back(item.item_id);
                        if (count >= ingredient.quantity) break;
                    }
                }
            }
            check.available = count;
            check.has_enough = (count >= ingredient.quantity);
        } else {
            // Direct item_id match (rods, bait, tools, lootboxes, etc.)
            check.available = db->get_item_quantity(user_id, ingredient.item_id);
            check.has_enough = (check.available >= ingredient.quantity);
            if (check.has_enough) {
                check.matching_item_ids.push_back(ingredient.item_id);
            }
        }
        
        checks.push_back(check);
    }
    
    return checks;
}

// Consume ingredients for a recipe
inline bool consume_ingredients(Database* db, uint64_t user_id, const Recipe& recipe, const std::vector<IngredientCheck>& checks) {
    for (size_t i = 0; i < recipe.ingredients.size(); i++) {
        const auto& ingredient = recipe.ingredients[i];
        const auto& check = checks[i];
        
        if (ingredient.item_type == "coins") {
            // Deduct from wallet
            db->update_wallet(user_id, -ingredient.quantity);
        } else if (ingredient.item_type == "collectible") {
            // Remove collectible items by their unique IDs
            int remaining = ingredient.quantity;
            for (const auto& item_id : check.matching_item_ids) {
                if (remaining <= 0) break;
                int qty = db->get_item_quantity(user_id, item_id);
                int to_remove = std::min(qty, remaining);
                db->remove_item(user_id, item_id, to_remove);
                remaining -= to_remove;
            }
        } else {
            // Remove by item_id directly
            db->remove_item(user_id, ingredient.item_id, ingredient.quantity);
        }
    }
    return true;
}

// ============================================================================
// CRAFT COMMAND — /craft or b.craft
// ============================================================================

// Interactive state for recipe browsing
struct CraftSession {
    uint64_t user_id;
    std::string current_category;
    int page;
    std::chrono::steady_clock::time_point created;
};

static std::unordered_map<uint64_t, CraftSession> active_sessions;
static std::mutex session_mutex;

// Build the recipe list embed for a category
inline dpp::embed build_recipe_list_embed(Database* db, uint64_t user_id, const std::string& category, int page) {
    auto recipes = get_recipes_by_category(category);
    int prestige = db->get_prestige(user_id);
    
    const int per_page = 5;
    int total_pages = std::max(1, (int)((recipes.size() + per_page - 1) / per_page));
    page = std::max(0, std::min(page, total_pages - 1));
    
    std::string title = get_category_display(category) + " Recipes";
    auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
    embed.set_title(title);
    
    int start = page * per_page;
    int end = std::min(start + per_page, (int)recipes.size());
    
    for (int i = start; i < end; i++) {
        const auto& recipe = *recipes[i];
        bool can_craft = (prestige >= recipe.prestige_required);
        
        std::string field_name = recipe.emoji + " " + recipe.name;
        if (!can_craft) field_name += " \xF0\x9F\x94\x92"; // locked emoji
        
        std::string field_value = recipe.description + "\n";
        
        // Show ingredients
        field_value += "**Ingredients:** ";
        for (size_t j = 0; j < recipe.ingredients.size(); j++) {
            const auto& ing = recipe.ingredients[j];
            if (j > 0) field_value += ", ";
            if (ing.item_type == "coins") {
                field_value += ing.emoji + " $" + economy::format_number(ing.quantity);
            } else {
                field_value += ing.emoji + " " + std::to_string(ing.quantity) + "x " + ing.display_name;
            }
        }
        field_value += "\n**Output:** " + recipe.output.emoji + " " + 
                       std::to_string(recipe.output.quantity) + "x " + recipe.output.name;
        
        if (recipe.prestige_required > 0) {
            field_value += "\n*Requires Prestige " + std::to_string(recipe.prestige_required) + "+*";
        }
        
        field_value += "\n`craft " + recipe.id + "`";
        
        embed.add_field(field_name, field_value, false);
    }
    
    if (recipes.empty()) {
        embed.set_description("No recipes found in this category.");
    }
    
    embed.set_footer(dpp::embed_footer().set_text(
        "Page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + 
        " | Use craft <recipe_id> to craft"
    ));
    
    return embed;
}

// Build the recipe detail/preview embed
inline dpp::embed build_recipe_detail_embed(Database* db, uint64_t user_id, const Recipe& recipe) {
    auto checks = check_ingredients(db, user_id, recipe);
    int prestige = db->get_prestige(user_id);
    bool meets_prestige = (prestige >= recipe.prestige_required);
    
    bool all_met = meets_prestige;
    for (const auto& check : checks) {
        if (!check.has_enough) all_met = false;
    }
    
    uint32_t color = all_met ? bronx::COLOR_SUCCESS : bronx::COLOR_WARNING;
    auto embed = bronx::create_embed("", color);
    embed.set_title(recipe.emoji + " " + recipe.name);
    embed.set_description(recipe.description);
    
    // Ingredients with availability status
    std::string ingredients_text;
    for (size_t i = 0; i < recipe.ingredients.size(); i++) {
        const auto& ing = recipe.ingredients[i];
        const auto& check = checks[i];
        
        std::string status = check.has_enough ? "\xE2\x9C\x85" : "\xE2\x9D\x8C"; // checkmark or X
        
        if (ing.item_type == "coins") {
            ingredients_text += status + " $" + economy::format_number(ing.quantity) + 
                              " (" + economy::format_number(check.available) + " available)\n";
        } else {
            ingredients_text += status + " " + std::to_string(ing.quantity) + "x " + 
                              ing.emoji + " " + ing.display_name + " (" + 
                              std::to_string(check.available) + " available)\n";
        }
    }
    embed.add_field("Ingredients", ingredients_text, false);
    
    // Output
    std::string output_text = recipe.output.emoji + " " + 
                              std::to_string(recipe.output.quantity) + "x **" + 
                              recipe.output.name + "**\n" + recipe.output.description;
    embed.add_field("Creates", output_text, false);
    
    // Prestige requirement
    if (recipe.prestige_required > 0) {
        std::string ptext = meets_prestige ? "\xE2\x9C\x85" : "\xE2\x9D\x8C";
        ptext += " Prestige " + std::to_string(recipe.prestige_required) + "+" +
                 " (you: P" + std::to_string(prestige) + ")";
        embed.add_field("Requirement", ptext, false);
    }
    
    // Status
    if (all_met) {
        embed.add_field("", "\xF0\x9F\x94\xA8 **Ready to craft!** Use `craft " + recipe.id + " confirm` to craft.", false);
    } else {
        embed.add_field("", "\xE2\x9D\x8C Missing ingredients — gather what you need and try again!", false);
    }
    
    return embed;
}

// Build the success embed after crafting
inline dpp::embed build_craft_success_embed(const Recipe& recipe) {
    auto embed = bronx::create_embed("", bronx::COLOR_SUCCESS);
    embed.set_title("\xF0\x9F\x94\xA8 Crafting Complete!");
    embed.set_description("You crafted " + recipe.output.emoji + " **" + 
                         std::to_string(recipe.output.quantity) + "x " + recipe.output.name + "**!\n\n" +
                         recipe.output.description);
    return embed;
}

// Build the categories overview embed
inline dpp::embed build_categories_embed(Database* db, uint64_t user_id) {
    auto embed = bronx::create_embed("", bronx::COLOR_INFO);
    embed.set_title("\xF0\x9F\x94\xA8 Crafting Workbench");
    embed.set_description("Combine materials from fishing, mining, and more to create powerful items!\n\n"
                         "Use `craft list <category>` to browse recipes, or `craft <recipe_id>` to view a recipe.");
    
    auto categories = get_recipe_categories();
    for (const auto& cat : categories) {
        auto recipes = get_recipes_by_category(cat);
        std::string value = std::to_string(recipes.size()) + " recipe" + (recipes.size() != 1 ? "s" : "");
        embed.add_field(get_category_display(cat), value + "\n`craft list " + cat + "`", true);
    }
    
    // Quick stats
    int total_recipes = get_recipes().size();
    embed.add_field("", "\xF0\x9F\x93\x8A **" + std::to_string(total_recipes) + " recipes** available", false);
    
    bronx::add_invoker_footer(embed, dpp::user()); // placeholder
    return embed;
}

// ============================================================================
// COMMAND CREATION
// ============================================================================

inline Command* create_craft_command(Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;
    
    cmd = new Command(
        "craft", "Craft items from collected materials", "economy",
        {"crafting", "recipe", "recipes", "workbench"}, true,
        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            if (args.empty()) {
                // Show categories overview
                auto embed = build_categories_embed(db, user_id);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string subcommand = args[0];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "list") {
                // List recipes in a category
                std::string category = "fishing"; // default
                if (args.size() > 1) {
                    category = args[1];
                    std::transform(category.begin(), category.end(), category.begin(), ::tolower);
                }
                
                int page = 0;
                if (args.size() > 2) {
                    try { page = std::stoi(args[2]) - 1; } catch (...) {}
                }
                
                auto embed = build_recipe_list_embed(db, user_id, category, page);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (subcommand == "search") {
                // Search recipes
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("Usage: `craft search <query>`"));
                    return;
                }
                std::string query;
                for (size_t i = 1; i < args.size(); i++) {
                    if (i > 1) query += " ";
                    query += args[i];
                }
                
                auto results = search_recipes(query);
                if (results.empty()) {
                    bronx::send_message(bot, event, bronx::error("No recipes found matching \"" + query + "\""));
                    return;
                }
                
                auto embed = bronx::create_embed("", bronx::COLOR_INFO);
                embed.set_title("\xF0\x9F\x94\x8D Recipe Search: " + query);
                
                for (size_t i = 0; i < std::min(results.size(), (size_t)10); i++) {
                    const auto& recipe = *results[i];
                    embed.add_field(
                        recipe.emoji + " " + recipe.name,
                        recipe.description + "\n`craft " + recipe.id + "`",
                        false
                    );
                }
                
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Try to find recipe by ID
            std::string recipe_id = subcommand;
            const Recipe* recipe = find_recipe(recipe_id);
            
            if (!recipe) {
                // Try searching
                auto results = search_recipes(recipe_id);
                if (results.size() == 1) {
                    recipe = results[0];
                } else if (results.size() > 1) {
                    auto embed = bronx::create_embed("", bronx::COLOR_WARNING);
                    embed.set_title("Multiple recipes found");
                    for (const auto& r : results) {
                        embed.add_field(r->emoji + " " + r->name, "`craft " + r->id + "`", true);
                    }
                    bronx::send_message(bot, event, embed);
                    return;
                } else {
                    bronx::send_message(bot, event, bronx::error(
                        "Recipe \"" + recipe_id + "\" not found. Use `craft` to see available categories."));
                    return;
                }
            }
            
            // Check if "confirm" is the second arg
            bool confirm = (args.size() > 1 && args[1] == "confirm");
            
            if (!confirm) {
                // Show recipe detail / preview
                auto embed = build_recipe_detail_embed(db, user_id, *recipe);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // === EXECUTE CRAFT ===
            
            // Check prestige requirement
            int prestige = db->get_prestige(user_id);
            if (prestige < recipe->prestige_required) {
                bronx::send_message(bot, event, bronx::error(
                    "This recipe requires **Prestige " + std::to_string(recipe->prestige_required) + 
                    "+**. You are P" + std::to_string(prestige) + "."));
                return;
            }
            
            // Check ingredients
            auto checks = check_ingredients(db, user_id, *recipe);
            bool all_met = true;
            for (const auto& check : checks) {
                if (!check.has_enough) { all_met = false; break; }
            }
            
            if (!all_met) {
                auto embed = build_recipe_detail_embed(db, user_id, *recipe);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Consume ingredients
            consume_ingredients(db, user_id, *recipe, checks);
            
            // Grant crafted item
            db->add_item(user_id, recipe->output.item_id, recipe->output.item_type,
                        recipe->output.quantity, recipe->output.metadata, recipe->output.level);
            
            // Track crafting stats
            db->increment_stat(user_id, "items_crafted", recipe->output.quantity);
            db->increment_stat(user_id, "recipes_used", 1);
            
            auto embed = build_craft_success_embed(*recipe);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.usr.id;
            db->ensure_user_exists(user_id);
            
            std::string subcommand = "list";
            std::string recipe_arg;
            bool confirm = false;
            
            try {
                subcommand = std::get<std::string>(event.get_parameter("action"));
            } catch (...) {}
            try {
                recipe_arg = std::get<std::string>(event.get_parameter("recipe"));
            } catch (...) {}
            try {
                confirm = std::get<bool>(event.get_parameter("confirm"));
            } catch (...) {}
            
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "list" && recipe_arg.empty()) {
                auto embed = build_categories_embed(db, user_id);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // If recipe specified, look it up
            std::string lookup = recipe_arg.empty() ? subcommand : recipe_arg;
            const Recipe* recipe = find_recipe(lookup);
            if (!recipe) {
                auto results = search_recipes(lookup);
                if (results.size() == 1) recipe = results[0];
                else if (results.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error(
                        "Recipe \"" + lookup + "\" not found.")));
                    return;
                } else {
                    auto embed = bronx::create_embed("", bronx::COLOR_WARNING);
                    embed.set_title("Multiple recipes found");
                    for (const auto& r : results) {
                        embed.add_field(r->emoji + " " + r->name, "`/craft recipe:" + r->id + "`", true);
                    }
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }
            }
            
            if (!confirm) {
                auto embed = build_recipe_detail_embed(db, user_id, *recipe);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // === EXECUTE CRAFT ===
            int prestige = db->get_prestige(user_id);
            if (prestige < recipe->prestige_required) {
                event.reply(dpp::message().add_embed(bronx::error(
                    "This recipe requires **Prestige " + std::to_string(recipe->prestige_required) + 
                    "+**. You are P" + std::to_string(prestige) + ".")));
                return;
            }
            
            auto checks = check_ingredients(db, user_id, *recipe);
            bool all_met = true;
            for (const auto& check : checks) {
                if (!check.has_enough) { all_met = false; break; }
            }
            
            if (!all_met) {
                auto embed = build_recipe_detail_embed(db, user_id, *recipe);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            consume_ingredients(db, user_id, *recipe, checks);
            db->add_item(user_id, recipe->output.item_id, recipe->output.item_type,
                        recipe->output.quantity, recipe->output.metadata, recipe->output.level);
            db->increment_stat(user_id, "items_crafted", recipe->output.quantity);
            db->increment_stat(user_id, "recipes_used", 1);
            
            auto embed = build_craft_success_embed(*recipe);
            bronx::add_invoker_footer(embed, event.command.usr);
            event.reply(dpp::message().add_embed(embed));
        },
        // SLASH OPTIONS
        {
            dpp::command_option(dpp::co_string, "action", "list, search, or a recipe ID", false)
                .add_choice(dpp::command_option_choice("List Recipes", std::string("list")))
                .add_choice(dpp::command_option_choice("Search", std::string("search"))),
            dpp::command_option(dpp::co_string, "recipe", "Recipe ID or search query", false),
            dpp::command_option(dpp::co_boolean, "confirm", "Confirm crafting", false)
        }
    );
    
    cmd->extended_description = "Combine materials from fishing, mining, and more into powerful items!";
    cmd->detailed_usage = "craft [list <category>|search <query>|<recipe_id> [confirm]]";
    cmd->subcommands = {
        {"craft", "Show all crafting categories"},
        {"craft list <category>", "Browse recipes (fishing, mining, utility, prestige)"},
        {"craft search <query>", "Search for a recipe by name"},
        {"craft <recipe_id>", "View recipe details and check ingredients"},
        {"craft <recipe_id> confirm", "Craft the item (consumes ingredients)"}
    };
    cmd->examples = {"craft", "craft list fishing", "craft bait_refinery", "craft lucky_charm confirm"};
    
    return cmd;
}

} // namespace crafting
} // namespace commands
