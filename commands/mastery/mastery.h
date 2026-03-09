#pragma once
#include "mastery_helpers.h"
#include "../economy/helpers.h"
#include "../fishing/fishing_helpers.h"
#include "../mining/mining_helpers.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>

using namespace bronx::db;

namespace commands {
namespace mastery {

// ============================================================================
// ORE MASTERY TRACKING
// ============================================================================

// Count ores by species from inventory (items with metadata containing type:ore)
inline std::map<std::string, int64_t> count_ores_from_inventory(Database* db, uint64_t user_id) {
    std::map<std::string, int64_t> ore_counts;
    auto inventory = db->get_inventory(user_id);
    
    for (const auto& item : inventory) {
        if (item.item_type != "collectible") continue;
        if (item.metadata.find("\"type\":\"ore\"") == std::string::npos) continue;
        
        // Extract ore name from metadata
        auto pos = item.metadata.find("\"name\"");
        if (pos == std::string::npos) continue;
        pos = item.metadata.find("\"", pos + 6);
        if (pos == std::string::npos) continue;
        pos++;
        auto end = item.metadata.find("\"", pos);
        if (end == std::string::npos) continue;
        
        std::string ore_name = item.metadata.substr(pos, end - pos);
        ore_counts[ore_name] += item.quantity;
    }
    
    // Also add per-species stats (tracked from mines going forward)
    // Format: ore_mastery_<species_name>
    for (const auto& ore : mining::ore_types) {
        std::string stat_key = "ore_mastery_" + ore.name;
        int64_t stat_val = db->get_stat(user_id, stat_key);
        if (stat_val > 0) {
            // Use the higher of inventory count or stat count (stat is cumulative)
            ore_counts[ore.name] = std::max(ore_counts[ore.name], stat_val);
        }
    }
    
    return ore_counts;
}

// ============================================================================
// EMBED BUILDERS
// ============================================================================

// Build mastery overview embed (summary of all mastery)
inline dpp::embed build_mastery_overview(Database* db, uint64_t user_id) {
    auto fish_counts = db->get_fish_catch_counts_by_species(user_id);
    auto ore_counts = count_ores_from_inventory(db, user_id);
    
    auto fish_summary = calculate_summary(fish_counts);
    auto ore_summary = calculate_summary(ore_counts);
    
    auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
    embed.set_title("\xF0\x9F\x8F\x86 Mastery Overview");
    embed.set_description("Catch the same species repeatedly to earn permanent value bonuses!\n"
                         "Each mastery tier increases the sell value of that species.");
    
    // Fish mastery section
    std::string fish_text;
    fish_text += "\xF0\x9F\x93\x8A **" + std::to_string(fish_summary.total_species) + "** species discovered\n";
    fish_text += "\xF0\x9F\x9F\xA1 **" + std::to_string(fish_summary.mastered_species) + "** mastered (100+ catches)\n";
    fish_text += "\xE2\xAD\x90 **" + std::to_string(fish_summary.mythic_species) + "** mythic (1000+ catches)\n";
    if (!fish_summary.best_species.empty()) {
        auto prog = get_mastery_progress(fish_summary.best_catches);
        fish_text += "\xF0\x9F\x91\x91 Best: **" + fish_summary.best_species + "** (" + 
                    std::to_string(fish_summary.best_catches) + " catches, " + 
                    prog.current_tier->emoji + " " + prog.current_tier->name + ")\n";
    }
    embed.add_field("\xF0\x9F\x8E\xA3 Fish Mastery", fish_text, false);
    
    // Ore mastery section
    std::string ore_text;
    ore_text += "\xF0\x9F\x93\x8A **" + std::to_string(ore_summary.total_species) + "** ores discovered\n";
    ore_text += "\xF0\x9F\x9F\xA1 **" + std::to_string(ore_summary.mastered_species) + "** mastered (100+ mined)\n";
    ore_text += "\xE2\xAD\x90 **" + std::to_string(ore_summary.mythic_species) + "** mythic (1000+ mined)\n";
    if (!ore_summary.best_species.empty()) {
        auto prog = get_mastery_progress(ore_summary.best_catches);
        ore_text += "\xF0\x9F\x91\x91 Best: **" + ore_summary.best_species + "** (" + 
                   std::to_string(ore_summary.best_catches) + " mined, " + 
                   prog.current_tier->emoji + " " + prog.current_tier->name + ")\n";
    }
    embed.add_field("\xE2\x9B\x8F\xEF\xB8\x8F Ore Mastery", ore_text, false);
    
    // Tier legend
    std::string tier_text;
    for (const auto& tier : get_mastery_tiers()) {
        tier_text += tier.emoji + " " + tier.name + " (" + std::to_string(tier.catches_required) + "+) ";
        if (tier.value_bonus > 0) {
            tier_text += "+" + std::to_string((int)(tier.value_bonus * 100)) + "% value";
        }
        tier_text += "\n";
    }
    embed.add_field("\xF0\x9F\x93\x96 Mastery Tiers", tier_text, false);
    
    embed.set_footer(dpp::embed_footer().set_text("Use mastery fish or mastery ore to see details"));
    
    return embed;
}

// Build detailed species mastery list
inline dpp::embed build_mastery_detail(Database* db, uint64_t user_id, MasteryType type, int page, const std::string& sort_by = "catches") {
    std::map<std::string, int64_t> counts;
    std::string title_prefix;
    
    if (type == MasteryType::Fish) {
        counts = db->get_fish_catch_counts_by_species(user_id);
        title_prefix = "\xF0\x9F\x8E\xA3";
    } else {
        counts = count_ores_from_inventory(db, user_id);
        title_prefix = "\xE2\x9B\x8F\xEF\xB8\x8F";
    }
    
    // Convert to sortable vector
    struct SpeciesEntry {
        std::string name;
        int64_t catches;
        MasteryProgress progress;
    };
    
    std::vector<SpeciesEntry> entries;
    for (const auto& [species, count] : counts) {
        SpeciesEntry e;
        e.name = species;
        e.catches = count;
        e.progress = get_mastery_progress(static_cast<int>(count));
        entries.push_back(e);
    }
    
    // Sort
    if (sort_by == "name") {
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    } else if (sort_by == "bonus") {
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.progress.total_bonus > b.progress.total_bonus; });
    } else {
        // Default: sort by catches (descending)
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.catches > b.catches; });
    }
    
    const int per_page = 10;
    int total_pages = std::max(1, (int)((entries.size() + per_page - 1) / per_page));
    page = std::max(0, std::min(page, total_pages - 1));
    
    auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
    embed.set_title(title_prefix + " " + mastery_type_name(type) + " Mastery");
    
    if (entries.empty()) {
        embed.set_description("No " + mastery_type_name(type) + " mastery data yet. Start fishing or mining!");
        return embed;
    }
    
    int start = page * per_page;
    int end_idx = std::min(start + per_page, (int)entries.size());
    
    std::string desc;
    for (int i = start; i < end_idx; i++) {
        const auto& e = entries[i];
        auto& prog = e.progress;
        
        // Species line: emoji tier_name | species | catches | progress bar | bonus
        desc += prog.current_tier->emoji + " **" + e.name + "** — " + 
               std::to_string(e.catches) + " caught\n";
        desc += "  " + build_progress_bar(prog.progress_percent, 8);
        
        if (prog.next_tier) {
            desc += " " + std::to_string(prog.catches_to_next) + " to " + prog.next_tier->name;
        } else {
            desc += " **MAX**";
        }
        
        if (prog.total_bonus > 0) {
            desc += " | +" + std::to_string((int)(prog.total_bonus * 100)) + "% value";
        }
        desc += "\n";
    }
    
    embed.set_description(desc);
    
    // Summary at bottom
    auto summary = calculate_summary(counts);
    embed.set_footer(dpp::embed_footer().set_text(
        "Page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + 
        " | " + std::to_string(summary.mastered_species) + "/" + std::to_string(summary.total_species) + " mastered" +
        " | Sort: " + sort_by
    ));
    
    return embed;
}

// Build detail for a single species
inline dpp::embed build_species_detail(Database* db, uint64_t user_id, const std::string& species_name, MasteryType type) {
    std::map<std::string, int64_t> counts;
    if (type == MasteryType::Fish) {
        counts = db->get_fish_catch_counts_by_species(user_id);
    } else {
        counts = count_ores_from_inventory(db, user_id);
    }
    
    auto it = counts.find(species_name);
    int64_t catch_count = (it != counts.end()) ? it->second : 0;
    
    auto prog = get_mastery_progress(static_cast<int>(catch_count));
    
    auto embed = bronx::create_embed("", bronx::COLOR_DEFAULT);
    embed.set_title(mastery_type_emoji(type) + " " + species_name + " Mastery");
    
    std::string desc;
    desc += prog.current_tier->emoji + " **" + prog.current_tier->name + "**\n\n";
    desc += "\xF0\x9F\x93\x8A Total caught: **" + std::to_string(catch_count) + "**\n";
    desc += "\xF0\x9F\x92\xB0 Value bonus: **+" + std::to_string((int)(prog.total_bonus * 100)) + "%**\n\n";
    
    // Progress bar to next tier
    if (prog.next_tier) {
        desc += "**Progress to " + prog.next_tier->emoji + " " + prog.next_tier->name + ":**\n";
        desc += build_progress_bar(prog.progress_percent, 15) + " " + 
               std::to_string((int)prog.progress_percent) + "% (" + 
               std::to_string(prog.catches_to_next) + " more needed)\n\n";
    } else {
        desc += "\xE2\xAD\x90 **Maximum mastery achieved!**\n\n";
    }
    
    // Show all tiers with current progress
    desc += "**All Tiers:**\n";
    for (const auto& tier : get_mastery_tiers()) {
        bool reached = (catch_count >= tier.catches_required);
        std::string indicator = reached ? "\xE2\x9C\x85" : "\xE2\xAC\x9C";
        desc += indicator + " " + tier.emoji + " " + tier.name + " — " + 
               std::to_string(tier.catches_required) + " catches";
        if (tier.value_bonus > 0) {
            desc += " (+" + std::to_string((int)(tier.value_bonus * 100)) + "% value)";
        }
        desc += "\n";
    }
    
    embed.set_description(desc);
    return embed;
}

// ============================================================================
// COMMAND CREATION
// ============================================================================

inline Command* create_mastery_command(Database* db) {
    static Command* cmd = nullptr;
    static bool initialized = false;
    if (initialized) return cmd;
    initialized = true;
    
    cmd = new Command(
        "mastery", "View your species mastery progress and value bonuses", "economy",
        {"mast", "mastered", "masteries"}, true,
        // TEXT HANDLER
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            if (args.empty()) {
                auto embed = build_mastery_overview(db, user_id);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            
            if (sub == "fish" || sub == "fishing") {
                int page = 0;
                std::string sort_by = "catches";
                if (args.size() > 1) {
                    std::string arg1 = args[1];
                    std::transform(arg1.begin(), arg1.end(), arg1.begin(), ::tolower);
                    if (arg1 == "name" || arg1 == "bonus" || arg1 == "catches") {
                        sort_by = arg1;
                        if (args.size() > 2) {
                            try { page = std::stoi(args[2]) - 1; } catch (...) {}
                        }
                    } else {
                        try { page = std::stoi(arg1) - 1; } catch (...) {
                            // Might be a species name — look it up
                            std::string species;
                            for (size_t i = 1; i < args.size(); i++) {
                                if (i > 1) species += " ";
                                species += args[i];
                            }
                            auto embed = build_species_detail(db, user_id, species, MasteryType::Fish);
                            bronx::add_invoker_footer(embed, event.msg.author);
                            bronx::send_message(bot, event, embed);
                            return;
                        }
                    }
                }
                auto embed = build_mastery_detail(db, user_id, MasteryType::Fish, page, sort_by);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (sub == "ore" || sub == "mining" || sub == "ores") {
                int page = 0;
                std::string sort_by = "catches";
                if (args.size() > 1) {
                    std::string arg1 = args[1];
                    std::transform(arg1.begin(), arg1.end(), arg1.begin(), ::tolower);
                    if (arg1 == "name" || arg1 == "bonus" || arg1 == "catches") {
                        sort_by = arg1;
                        if (args.size() > 2) {
                            try { page = std::stoi(args[2]) - 1; } catch (...) {}
                        }
                    } else {
                        try { page = std::stoi(arg1) - 1; } catch (...) {
                            std::string species;
                            for (size_t i = 1; i < args.size(); i++) {
                                if (i > 1) species += " ";
                                species += args[i];
                            }
                            auto embed = build_species_detail(db, user_id, species, MasteryType::Ore);
                            bronx::add_invoker_footer(embed, event.msg.author);
                            bronx::send_message(bot, event, embed);
                            return;
                        }
                    }
                }
                auto embed = build_mastery_detail(db, user_id, MasteryType::Ore, page, sort_by);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Check if it's a specific species lookup
            std::string species;
            for (size_t i = 0; i < args.size(); i++) {
                if (i > 0) species += " ";
                species += args[i];
            }
            
            // Try fish first, then ore
            auto fish_counts = db->get_fish_catch_counts_by_species(user_id);
            std::string lower_species = species;
            std::transform(lower_species.begin(), lower_species.end(), lower_species.begin(), ::tolower);
            
            for (const auto& [name, count] : fish_counts) {
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name == lower_species) {
                    auto embed = build_species_detail(db, user_id, name, MasteryType::Fish);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
            }
            
            auto ore_counts = count_ores_from_inventory(db, user_id);
            for (const auto& [name, count] : ore_counts) {
                std::string lower_name = name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name == lower_species) {
                    auto embed = build_species_detail(db, user_id, name, MasteryType::Ore);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
            }
            
            // Not found — show overview
            bronx::send_message(bot, event, bronx::error(
                "Species \"" + species + "\" not found in your mastery. Try `mastery fish` or `mastery ore`."));
        },
        // SLASH HANDLER
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.usr.id;
            db->ensure_user_exists(user_id);
            
            std::string type = "overview";
            std::string species;
            int64_t page_num = 1;
            
            try { type = std::get<std::string>(event.get_parameter("type")); } catch (...) {}
            try { species = std::get<std::string>(event.get_parameter("species")); } catch (...) {}
            try { page_num = std::get<int64_t>(event.get_parameter("page")); } catch (...) {}
            
            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            
            if (!species.empty()) {
                // Look up specific species
                auto fish_counts = db->get_fish_catch_counts_by_species(user_id);
                std::string lower_species = species;
                std::transform(lower_species.begin(), lower_species.end(), lower_species.begin(), ::tolower);
                
                for (const auto& [name, count] : fish_counts) {
                    std::string lower_name = name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    if (lower_name == lower_species) {
                        auto embed = build_species_detail(db, user_id, name, MasteryType::Fish);
                        bronx::add_invoker_footer(embed, event.command.usr);
                        event.reply(dpp::message().add_embed(embed));
                        return;
                    }
                }
                
                auto ore_counts = count_ores_from_inventory(db, user_id);
                for (const auto& [name, count] : ore_counts) {
                    std::string lower_name = name;
                    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                    if (lower_name == lower_species) {
                        auto embed = build_species_detail(db, user_id, name, MasteryType::Ore);
                        bronx::add_invoker_footer(embed, event.command.usr);
                        event.reply(dpp::message().add_embed(embed));
                        return;
                    }
                }
                
                event.reply(dpp::message().add_embed(bronx::error(
                    "Species \"" + species + "\" not found.")));
                return;
            }
            
            if (type == "fish") {
                auto embed = build_mastery_detail(db, user_id, MasteryType::Fish, (int)page_num - 1);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
            } else if (type == "ore") {
                auto embed = build_mastery_detail(db, user_id, MasteryType::Ore, (int)page_num - 1);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
            } else {
                auto embed = build_mastery_overview(db, user_id);
                bronx::add_invoker_footer(embed, event.command.usr);
                event.reply(dpp::message().add_embed(embed));
            }
        },
        // SLASH OPTIONS
        {
            dpp::command_option(dpp::co_string, "type", "What to view", false)
                .add_choice(dpp::command_option_choice("Overview", std::string("overview")))
                .add_choice(dpp::command_option_choice("Fish Mastery", std::string("fish")))
                .add_choice(dpp::command_option_choice("Ore Mastery", std::string("ore"))),
            dpp::command_option(dpp::co_string, "species", "Look up a specific species by name", false),
            dpp::command_option(dpp::co_integer, "page", "Page number", false)
        }
    );
    
    cmd->extended_description = "Track your mastery progress for each fish and ore species. "
                               "Higher mastery tiers grant permanent value bonuses when selling.";
    cmd->detailed_usage = "mastery [fish|ore [sort] [page]|<species name>]";
    cmd->subcommands = {
        {"mastery", "Show mastery overview for fish and ore"},
        {"mastery fish", "List all fish species mastery progress"},
        {"mastery ore", "List all ore species mastery progress"},
        {"mastery fish name", "Sort fish mastery by name"},
        {"mastery <species>", "View detailed mastery for a specific species"}
    };
    cmd->examples = {"mastery", "mastery fish", "mastery ore bonus", "mastery common fish"};
    
    return cmd;
}

} // namespace mastery
} // namespace commands
