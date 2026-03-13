#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "fishing_helpers.h"
#include <dpp/dpp.h>
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace bronx::db;

namespace commands {
namespace fishing {

// Helper to convert string to FishEffect enum
inline std::optional<FishEffect> parse_fish_effect(std::string effect_str) {
    std::transform(effect_str.begin(), effect_str.end(), effect_str.begin(), ::tolower);
    
    if (effect_str == "none") return FishEffect::None;
    if (effect_str == "flat") return FishEffect::Flat;
    if (effect_str == "exponential") return FishEffect::Exponential;
    if (effect_str == "logarithmic") return FishEffect::Logarithmic;
    if (effect_str == "nlogn") return FishEffect::NLogN;
    if (effect_str == "wacky") return FishEffect::Wacky;
    if (effect_str == "jackpot") return FishEffect::Jackpot;
    if (effect_str == "critical") return FishEffect::Critical;
    if (effect_str == "volatile") return FishEffect::Volatile;
    if (effect_str == "surge") return FishEffect::Surge;
    if (effect_str == "diminishing") return FishEffect::Diminishing;
    if (effect_str == "cascading") return FishEffect::Cascading;
    if (effect_str == "wealthy") return FishEffect::Wealthy;
    if (effect_str == "banker") return FishEffect::Banker;
    if (effect_str == "fisher") return FishEffect::Fisher;
    if (effect_str == "merchant") return FishEffect::Merchant;
    if (effect_str == "gambler") return FishEffect::Gambler;
    if (effect_str == "ascended") return FishEffect::Ascended;
    if (effect_str == "underdog") return FishEffect::Underdog;
    if (effect_str == "hotstreak") return FishEffect::HotStreak;
    if (effect_str == "collector") return FishEffect::Collector;
    if (effect_str == "persistent") return FishEffect::Persistent;
    
    return std::nullopt;
}

// Helper to get FishEffect name as string
inline std::string fish_effect_to_string(FishEffect effect) {
    switch (effect) {
        case FishEffect::None: return "None";
        case FishEffect::Flat: return "Flat";
        case FishEffect::Exponential: return "Exponential";
        case FishEffect::Logarithmic: return "Logarithmic";
        case FishEffect::NLogN: return "NLogN";
        case FishEffect::Wacky: return "Wacky";
        case FishEffect::Jackpot: return "Jackpot";
        case FishEffect::Critical: return "Critical";
        case FishEffect::Volatile: return "Volatile";
        case FishEffect::Surge: return "Surge";
        case FishEffect::Diminishing: return "Diminishing";
        case FishEffect::Cascading: return "Cascading";
        case FishEffect::Wealthy: return "Wealthy";
        case FishEffect::Banker: return "Banker";
        case FishEffect::Fisher: return "Fisher";
        case FishEffect::Merchant: return "Merchant";
        case FishEffect::Gambler: return "Gambler";
        case FishEffect::Ascended: return "Ascended";
        case FishEffect::Underdog: return "Underdog";
        case FishEffect::HotStreak: return "HotStreak";
        case FishEffect::Collector: return "Collector";
        case FishEffect::Persistent: return "Persistent";
        default: return "None";
    }
}

// Suggest fish command - allows users to suggest new fish with all parameters
inline Command* get_suggestfish_command(Database* db) {
    static Command* suggestfish = new Command(
        "suggestfish", 
        "Suggest a new fish to be added to the game", 
        "fishing", 
        {"fishsuggestion", "proposefish"}, 
        true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Show usage if no args
            if (args.empty()) {
                std::stringstream ss;
                ss << "**Suggest a new fish!**\n\n";
                ss << "**Usage:** `suggestfish <name> <emoji> <weight> <min_value> <max_value> <effect> <effect_chance> <min_gear> <max_gear> <description...>`\n\n";
                ss << "**Parameters:**\n";
                ss << "• `name` - Fish name (use quotes if multiple words)\n";
                ss << "• `emoji` - Fish emoji (single emoji)\n";
                ss << "• `weight` - Rarity weight (1-250, higher = more common)\n";
                ss << "• `min_value` - Minimum value in coins\n";
                ss << "• `max_value` - Maximum value in coins\n";
                ss << "• `effect` - Effect type (None, Flat, Exponential, etc.)\n";
                ss << "• `effect_chance` - Effect trigger chance (0.0-1.0)\n";
                ss << "• `min_gear` - Minimum rod level required (0-26)\n";
                ss << "• `max_gear` - Max gear level or 0 for no cap\n";
                ss << "• `description` - Brief description (rest of args)\n\n";
                ss << "**Available Effects:**\n";
                ss << "`None`, `Flat`, `Exponential`, `Logarithmic`, `NLogN`, `Wacky`, `Jackpot`, ";
                ss << "`Critical`, `Volatile`, `Surge`, `Diminishing`, `Cascading`, `Wealthy`, ";
                ss << "`Banker`, `Fisher`, `Merchant`, `Gambler`, `Ascended`, `Underdog`, ";
                ss << "`HotStreak`, `Collector`, `Persistent`\n\n";
                ss << "**Example:**\n";
                ss << "`suggestfish \"cosmic carp\" 🌌 50 5000 15000 Wacky 0.25 5 0 A mystical fish from another dimension`";
                
                auto embed = dpp::embed()
                    .set_color(0x3498db)
                    .set_title("Fish Suggestion System")
                    .set_description(ss.str());
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Parse arguments - need at least 10 arguments
            if (args.size() < 10) {
                bronx::send_message(bot, event, bronx::error("Not enough arguments. Use `suggestfish` with no args to see usage."));
                return;
            }
            
            std::string fish_name = args[0];
            std::string emoji = args[1];
            int weight;
            int64_t min_value;
            int64_t max_value;
            double effect_chance;
            int min_gear_level;
            int max_gear_level;
            
            // Parse numeric values
            try {
                weight = std::stoi(args[2]);
                min_value = std::stoll(args[3]);
                max_value = std::stoll(args[4]);
                effect_chance = std::stod(args[6]);
                min_gear_level = std::stoi(args[7]);
                max_gear_level = std::stoi(args[8]);
            } catch (const std::exception& e) {
                bronx::send_message(bot, event, bronx::error("Invalid numeric value. Please check your parameters."));
                return;
            }
            
            // Validate ranges
            if (weight < 1 || weight > 250) {
                bronx::send_message(bot, event, bronx::error("Weight must be between 1 and 250"));
                return;
            }
            if (min_value < 1 || max_value < min_value) {
                bronx::send_message(bot, event, bronx::error("Invalid value range. Max must be >= min, and both must be positive."));
                return;
            }
            if (effect_chance < 0.0 || effect_chance > 1.0) {
                bronx::send_message(bot, event, bronx::error("Effect chance must be between 0.0 and 1.0"));
                return;
            }
            if (min_gear_level < 0 || min_gear_level > 26) {
                bronx::send_message(bot, event, bronx::error("Min gear level must be between 0 and 26"));
                return;
            }
            if (max_gear_level < 0 || (max_gear_level > 0 && max_gear_level < min_gear_level)) {
                bronx::send_message(bot, event, bronx::error("Invalid gear level range. Use 0 for no max cap."));
                return;
            }
            
            // Parse effect
            auto effect_opt = parse_fish_effect(args[5]);
            if (!effect_opt.has_value()) {
                bronx::send_message(bot, event, bronx::error("Invalid effect type. Use `suggestfish` with no args to see valid effects."));
                return;
            }
            FishEffect effect = effect_opt.value();
            
            // Collect description from remaining args
            std::stringstream desc_ss;
            for (size_t i = 9; i < args.size(); i++) {
                if (i > 9) desc_ss << " ";
                desc_ss << args[i];
            }
            std::string description = desc_ss.str();
            
            if (description.empty()) {
                bronx::send_message(bot, event, bronx::error("Description cannot be empty"));
                return;
            }
            
            // Escape quotes in strings for JSON
            auto escape_json = [](const std::string& str) {
                std::string result;
                for (char c : str) {
                    if (c == '"') result += "\\\"";
                    else if (c == '\\') result += "\\\\";
                    else if (c == '\n') result += "\\n";
                    else if (c == '\r') result += "\\r";
                    else if (c == '\t') result += "\\t";
                    else result += c;
                }
                return result;
            };
            
            // Create JSON suggestion
            std::stringstream json_ss;
            json_ss << "{\n";
            json_ss << "  \"type\": \"fish_suggestion\",\n";
            json_ss << "  \"name\": \"" << escape_json(fish_name) << "\",\n";
            json_ss << "  \"emoji\": \"" << escape_json(emoji) << "\",\n";
            json_ss << "  \"weight\": " << weight << ",\n";
            json_ss << "  \"min_value\": " << min_value << ",\n";
            json_ss << "  \"max_value\": " << max_value << ",\n";
            json_ss << "  \"effect\": \"" << fish_effect_to_string(effect) << "\",\n";
            json_ss << "  \"effect_chance\": " << effect_chance << ",\n";
            json_ss << "  \"min_gear_level\": " << min_gear_level << ",\n";
            json_ss << "  \"max_gear_level\": " << max_gear_level << ",\n";
            json_ss << "  \"description\": \"" << escape_json(description) << "\",\n";
            json_ss << "  \"season\": \"AllYear\"\n";
            json_ss << "}";
            
            std::string suggestion_json = json_ss.str();
            
            // Get user's networth for the suggestion system
            int64_t networth = db->get_networth(event.msg.author.id);
            
            // Insert into suggestions table
            if (!db->add_suggestion(event.msg.author.id, suggestion_json, networth)) {
                bronx::send_message(bot, event, bronx::error("Failed to submit suggestion"));
                return;
            }
            
            // Create confirmation embed
            std::stringstream confirm_ss;
            confirm_ss << "**Fish Suggestion Submitted!**\n\n";
            confirm_ss << "**Name:** " << fish_name << " " << emoji << "\n";
            confirm_ss << "**Weight:** " << weight << " (rarity)\n";
            confirm_ss << "**Value Range:** " << min_value << " - " << max_value << " coins\n";
            confirm_ss << "**Effect:** " << fish_effect_to_string(effect) << " (" << (effect_chance * 100) << "% chance)\n";
            confirm_ss << "**Gear Requirements:** Level " << min_gear_level;
            if (max_gear_level > 0) {
                confirm_ss << " to " << max_gear_level;
            } else {
                confirm_ss << "+";
            }
            confirm_ss << "\n**Description:** " << description << "\n\n";
            confirm_ss << "Your suggestion has been submitted and will be reviewed. Thank you for contributing!";
            
            auto embed = dpp::embed()
                .set_color(0x2ecc71)
                .set_title(bronx::EMOJI_CHECK + " Suggestion Submitted")
                .set_description(confirm_ss.str());
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        }
    );
    
    return suggestfish;
}

} // namespace fishing
} // namespace commands
