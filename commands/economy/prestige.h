#pragma once
#include "helpers.h"

namespace commands {
namespace economy {

// Base prestige requirements (for prestige 1)
constexpr int64_t BASE_NETWORTH_REQUIREMENT = 500000000;      // 500 million
constexpr int64_t BASE_COMMON_FISH_REQUIREMENT = 1000;        // 1000 common fish
constexpr int64_t BASE_RARE_FISH_REQUIREMENT = 200;           // 200 rare fish
constexpr int64_t BASE_EPIC_FISH_REQUIREMENT = 50;            // 50 epic fish
constexpr int64_t BASE_LEGENDARY_FISH_REQUIREMENT = 10;       // 10 legendary fish
constexpr int64_t BASE_PRESTIGE_FISH_REQUIREMENT = 1;         // 1 prestige fish (starts at P5)
constexpr int PRESTIGE_FISH_START_LEVEL = 5;                  // Prestige fish required starting at P5

// Scaling multipliers per prestige level
constexpr double NETWORTH_MULTIPLIER = 2.0;       // Each prestige needs 2x more money
constexpr double COMMON_MULTIPLIER = 1.1;         // Each prestige needs 1.1x more common (scales slowly)
constexpr double RARE_MULTIPLIER = 1.15;          // Each prestige needs 1.15x more rare
constexpr double EPIC_MULTIPLIER = 1.25;          // Each prestige needs 1.25x more epic
constexpr double LEGENDARY_MULTIPLIER = 1.4;      // Each prestige needs 1.4x more legendary (scales fastest)
constexpr double PRESTIGE_FISH_MULTIPLIER = 1.3;  // Each prestige needs 1.3x more prestige fish

// Calculate scaled requirement
inline int64_t scale_requirement(int64_t base, double multiplier, int prestige_level) {
    if (prestige_level <= 0) return base;
    double scaled = base;
    for (int i = 0; i < prestige_level; i++) {
        scaled *= multiplier;
    }
    return static_cast<int64_t>(scaled);
}

// Helper to check prestige requirements
struct PrestigeRequirements {
    bool networth_met = false;
    bool common_met = false;
    bool rare_met = false;
    bool epic_met = false;
    bool legendary_met = false;
    bool prestige_fish_met = false;
    
    int64_t current_networth = 0;
    int64_t common_count = 0;
    int64_t rare_count = 0;
    int64_t epic_count = 0;
    int64_t legendary_count = 0;
    int64_t prestige_fish_count = 0;
    
    int64_t required_networth = 0;
    int64_t required_common = 0;
    int64_t required_rare = 0;
    int64_t required_epic = 0;
    int64_t required_legendary = 0;
    int64_t required_prestige_fish = 0;
    
    bool prestige_fish_required = false;  // Only true at P5+
    
    bool all_met() const { 
        return networth_met && common_met && rare_met && epic_met && legendary_met && 
               (!prestige_fish_required || prestige_fish_met); 
    }
};

inline PrestigeRequirements check_prestige_requirements(Database* db, uint64_t user_id, int current_prestige) {
    PrestigeRequirements req;
    
    // Calculate scaled requirements based on current prestige
    req.required_networth = scale_requirement(BASE_NETWORTH_REQUIREMENT, NETWORTH_MULTIPLIER, current_prestige);
    req.required_common = scale_requirement(BASE_COMMON_FISH_REQUIREMENT, COMMON_MULTIPLIER, current_prestige);
    req.required_rare = scale_requirement(BASE_RARE_FISH_REQUIREMENT, RARE_MULTIPLIER, current_prestige);
    req.required_epic = scale_requirement(BASE_EPIC_FISH_REQUIREMENT, EPIC_MULTIPLIER, current_prestige);
    req.required_legendary = scale_requirement(BASE_LEGENDARY_FISH_REQUIREMENT, LEGENDARY_MULTIPLIER, current_prestige);
    
    // Prestige fish only required at P5+
    req.prestige_fish_required = (current_prestige >= PRESTIGE_FISH_START_LEVEL);
    if (req.prestige_fish_required) {
        int levels_past_threshold = current_prestige - PRESTIGE_FISH_START_LEVEL;
        req.required_prestige_fish = scale_requirement(BASE_PRESTIGE_FISH_REQUIREMENT, PRESTIGE_FISH_MULTIPLIER, levels_past_threshold);
    }
    
    // Check networth (wallet + bank)
    req.current_networth = db->get_networth(user_id);
    req.networth_met = (req.current_networth >= req.required_networth);
    
    // Check fish counts (total caught, including sold)
    req.common_count = db->count_fish_caught_by_rarity(user_id, "normal");
    req.common_met = (req.common_count >= req.required_common);
    
    req.rare_count = db->count_fish_caught_by_rarity(user_id, "rare");
    req.rare_met = (req.rare_count >= req.required_rare);
    
    req.epic_count = db->count_fish_caught_by_rarity(user_id, "epic");
    req.epic_met = (req.epic_count >= req.required_epic);
    
    req.legendary_count = db->count_fish_caught_by_rarity(user_id, "legendary");
    req.legendary_met = (req.legendary_count >= req.required_legendary);
    
    // Check prestige fish (only if required)
    if (req.prestige_fish_required) {
        req.prestige_fish_count = db->count_prestige_fish_caught(user_id);
        req.prestige_fish_met = (req.prestige_fish_count >= req.required_prestige_fish);
    } else {
        req.prestige_fish_met = true;
    }
    
    return req;
}

// format_requirement is now in helpers.h

inline Command* create_prestige_command(Database* db) {
    static Command* prestige_cmd = new Command("prestige", "prestige to reset your progress and gain a prestige rank", "economy", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            auto user = db->get_user(user_id);
            if (!user) {
                bronx::send_message(bot, event, bronx::error("user not found"));
                return;
            }
            
            // Check if user provided "confirm" argument
            bool confirmed = !args.empty() && (args[0] == "confirm" || args[0] == "yes");
            
            int current_prestige = user->prestige;
            
            // Check prestige requirements
            auto req = check_prestige_requirements(db, user_id, current_prestige);
            
            if (!confirmed) {
                // Show prestige info and warning
                int next_prestige = current_prestige + 1;
                
                ::std::string description = "**" + bronx::EMOJI_STAR + " prestige system**\n\n";
                description += "your current prestige: **" + ::std::to_string(current_prestige) + "**\n\n";
                
                // Show requirements (scaled based on current prestige)
                description += "**requirements for prestige " + ::std::to_string(next_prestige) + ":**\n";
                description += format_requirement(req.networth_met, 
                    "$" + format_number(req.required_networth) + " networth (" + 
                    format_number(req.current_networth) + "/" + format_number(req.required_networth) + ")") + "\n";
                description += format_requirement(req.common_met,
                    ::std::to_string(req.required_common) + " common fish caught (" +
                    ::std::to_string(req.common_count) + "/" + ::std::to_string(req.required_common) + ")") + "\n";
                description += format_requirement(req.rare_met,
                    ::std::to_string(req.required_rare) + " rare fish caught (" +
                    ::std::to_string(req.rare_count) + "/" + ::std::to_string(req.required_rare) + ")") + "\n";
                description += format_requirement(req.epic_met,
                    ::std::to_string(req.required_epic) + " epic fish caught (" +
                    ::std::to_string(req.epic_count) + "/" + ::std::to_string(req.required_epic) + ")") + "\n";
                description += format_requirement(req.legendary_met,
                    ::std::to_string(req.required_legendary) + " legendary fish caught (" +
                    ::std::to_string(req.legendary_count) + "/" + ::std::to_string(req.required_legendary) + ")") + "\n";
                if (req.prestige_fish_required) {
                    description += format_requirement(req.prestige_fish_met,
                        ::std::to_string(req.required_prestige_fish) + " prestige fish caught (" +
                        ::std::to_string(req.prestige_fish_count) + "/" + ::std::to_string(req.required_prestige_fish) + ")") + "\n";
                }
                description += "\n";
                
                description += "**prestiging will reset:**\n";
                description += "• your wallet and bank balance\n";
                description += "• all fish in your inventory\n";
                description += "• all bait and fishing rods\n";
                description += "• all upgrades and potions\n";
                description += "• your autofisher\n\n";
                description += "**what you keep:**\n";
                description += "• titles\n";
                description += "• your gambling stats\n";
                description += "• your commands used count\n";
                description += "• your fish caught stats\n\n";
                description += "**what you gain:**\n";
                description += "• prestige rank **" + ::std::to_string(next_prestige) + "**\n";
                description += "• roman numeral prefix on leaderboards\n";
                description += "• +5% fishing value bonus per prestige\n\n";
                
                if (req.all_met()) {
                    description += "type `prestige confirm` to prestige";
                } else {
                    description += bronx::EMOJI_WARNING + " **you do not meet all requirements to prestige**";
                }
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Check requirements before confirming
            if (!req.all_met()) {
                bronx::send_message(bot, event, bronx::error("you do not meet all prestige requirements"));
                return;
            }
            
            // Perform prestige
            if (db->perform_prestige(user_id)) {
                int new_prestige = db->get_prestige(user_id);
                ::std::string description = "**" + bronx::EMOJI_STAR + " prestige successful!**\n\n";
                description += "you are now prestige **" + ::std::to_string(new_prestige) + "**\n\n";
                description += "your progress has been reset. good luck on your journey!";
                
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("failed to prestige. please try again"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            auto user = db->get_user(user_id);
            if (!user) {
                event.reply(dpp::message().add_embed(bronx::error("user not found")));
                return;
            }
            
            // Check for confirm parameter
            bool confirmed = false;
            try {
                auto confirm_param = event.get_parameter("confirm");
                if (::std::holds_alternative<bool>(confirm_param)) {
                    confirmed = ::std::get<bool>(confirm_param);
                }
            } catch (...) {}
            
            int current_prestige = user->prestige;
            
            // Check prestige requirements
            auto req = check_prestige_requirements(db, user_id, current_prestige);
            
            if (!confirmed) {
                // Show prestige info and warning
                int next_prestige = current_prestige + 1;
                
                ::std::string description = "**" + bronx::EMOJI_STAR + " prestige system**\n\n";
                description += "your current prestige: **" + ::std::to_string(current_prestige) + "**\n\n";
                
                // Show requirements (scaled based on current prestige)
                description += "**requirements for prestige " + ::std::to_string(next_prestige) + ":**\n";
                description += format_requirement(req.networth_met, 
                    "$" + format_number(req.required_networth) + " networth (" + 
                    format_number(req.current_networth) + "/" + format_number(req.required_networth) + ")") + "\n";
                description += format_requirement(req.common_met,
                    ::std::to_string(req.required_common) + " common fish caught (" +
                    ::std::to_string(req.common_count) + "/" + ::std::to_string(req.required_common) + ")") + "\n";
                description += format_requirement(req.rare_met,
                    ::std::to_string(req.required_rare) + " rare fish caught (" +
                    ::std::to_string(req.rare_count) + "/" + ::std::to_string(req.required_rare) + ")") + "\n";
                description += format_requirement(req.epic_met,
                    ::std::to_string(req.required_epic) + " epic fish caught (" +
                    ::std::to_string(req.epic_count) + "/" + ::std::to_string(req.required_epic) + ")") + "\n";
                description += format_requirement(req.legendary_met,
                    ::std::to_string(req.required_legendary) + " legendary fish caught (" +
                    ::std::to_string(req.legendary_count) + "/" + ::std::to_string(req.required_legendary) + ")") + "\n";
                if (req.prestige_fish_required) {
                    description += format_requirement(req.prestige_fish_met,
                        ::std::to_string(req.required_prestige_fish) + " prestige fish caught (" +
                        ::std::to_string(req.prestige_fish_count) + "/" + ::std::to_string(req.required_prestige_fish) + ")") + "\n";
                }
                description += "\n";
                
                description += "**prestiging will reset:**\n";
                description += "• your wallet and bank balance\n";
                description += "• all fish in your inventory\n";
                description += "• all bait and fishing rods\n";
                description += "• all upgrades and potions\n";
                description += "• your autofisher\n\n";
                description += "**what you keep:**\n";
                description += "• titles\n";
                description += "• your gambling stats\n";
                description += "• your commands used count\n";
                description += "• your fish caught stats\n\n";
                description += "**what you gain:**\n";
                description += "• prestige rank **" + ::std::to_string(next_prestige) + "**\n";
                description += "• roman numeral prefix on leaderboards\n";
                description += "• +5% fishing value bonus per prestige\n\n";
                
                if (req.all_met()) {
                    description += "use `/prestige confirm:true` to prestige";
                } else {
                    description += bronx::EMOJI_WARNING + " **you do not meet all requirements to prestige**";
                }
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // Check requirements before confirming
            if (!req.all_met()) {
                event.reply(dpp::message().add_embed(bronx::error("you do not meet all prestige requirements")));
                return;
            }
            
            // Perform prestige
            if (db->perform_prestige(user_id)) {
                int new_prestige = db->get_prestige(user_id);
                ::std::string description = "**" + bronx::EMOJI_STAR + " prestige successful!**\n\n";
                description += "you are now prestige **" + ::std::to_string(new_prestige) + "**\n\n";
                description += "your progress has been reset. good luck on your journey!";
                
                auto embed = bronx::success(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to prestige. please try again")));
            }
        },
        {
            dpp::command_option(dpp::co_boolean, "confirm", "confirm prestige (this will reset your progress)", false)
        });
    return prestige_cmd;
}

} // namespace economy
} // namespace commands
