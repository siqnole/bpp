#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../../database/operations/economy/skill_operations.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <mutex>
#include <cmath>
#include <algorithm>

namespace commands {
namespace skill_tree {

using namespace ::bronx::db;

// ============================================================================
// SKILL TREES — Post-prestige progression with 3 branches
// ============================================================================
// Unlocked at Prestige 1. Players earn Prestige Points (PP) for each prestige
// level. Points are spent on nodes in 3 skill branches:
//
//   🎣 Angler     — fishing bonuses (catch rate, value, rare chance)
//   ⛏️ Prospector — mining bonuses (ore yield, value, rare ore chance)
//   🎰 Gambler    — gambling bonuses (win rate, payout, jackpot chance)
//
// Each branch has 5 tiers of skills, each with up to 5 ranks.
// Respeccing costs coins (10% of networth, min $500K).
//
// Subcommands:
//   /skills           — view your skill tree overview
//   /skills invest <branch> <skill>  — invest a point
//   /skills respec    — reset all skill points (costs coins)
//   /skills info      — detailed info about all skills
// ============================================================================

// --- Skill definitions ---
struct SkillNode {
    std::string id;             // unique identifier
    std::string name;           // display name
    std::string description;    // what it does (use {value} for current bonus)
    std::string branch;         // "angler", "prospector", "gambler"
    int tier;                   // 1-5 (higher = deeper in tree)
    int max_rank;               // max points investable
    double bonus_per_rank;      // % bonus per rank
    std::string bonus_type;     // what stat it modifies
    std::string emoji;
    int prerequisite_tier;      // must have >= 1 point in any skill of this tier-1
};

// Angler branch skills
static const std::vector<SkillNode> ANGLER_SKILLS = {
    {"angler_catch_speed",   "Quick Hands",     "+{value}% faster fishing cooldown",     "angler", 1, 5, 3.0,  "fish_cooldown_reduction",  "\xF0\x9F\x8E\xA3", 0},
    {"angler_value_boost",   "Appraiser's Eye",  "+{value}% fish sell value",            "angler", 1, 5, 2.0,  "fish_value_bonus",         "\xF0\x9F\x92\xB0", 0},
    {"angler_rare_chance",   "Lucky Lure",       "+{value}% rare fish chance",           "angler", 2, 5, 1.5,  "rare_fish_bonus",          "\xE2\x9C\xA8", 1},
    {"angler_double_catch",  "Double Hook",      "+{value}% chance to catch 2 fish",     "angler", 3, 5, 2.0,  "double_catch_chance",      "\xF0\x9F\xAA\x9D", 2},
    {"angler_epic_chance",   "Master Angler",    "+{value}% epic+ fish chance",          "angler", 4, 3, 2.0,  "epic_fish_bonus",          "\xF0\x9F\x8C\x9F", 3},
    {"angler_legendary",     "Legendary Lure",   "+{value}% legendary fish chance",      "angler", 5, 2, 1.0,  "legendary_fish_bonus",     "\xF0\x9F\x91\x91", 4},
};

// Prospector branch skills
static const std::vector<SkillNode> PROSPECTOR_SKILLS = {
    {"prospector_yield",       "Deep Veins",       "+{value}% ore yield",                "prospector", 1, 5, 3.0,  "ore_yield_bonus",        "\xE2\x9B\x8F\xEF\xB8\x8F", 0},
    {"prospector_value",       "Gem Cutter",       "+{value}% ore sell value",            "prospector", 1, 5, 2.0,  "ore_value_bonus",        "\xF0\x9F\x92\x8E", 0},
    {"prospector_rare",        "Keen Nose",        "+{value}% rare ore chance",           "prospector", 2, 5, 1.5,  "rare_ore_bonus",         "\xF0\x9F\x91\x83", 1},
    {"prospector_double",      "Twin Pickaxe",     "+{value}% chance to double ore",      "prospector", 3, 5, 2.0,  "double_ore_chance",      "\xE2\x9A\x92\xEF\xB8\x8F", 2},
    {"prospector_celestial",   "Celestial Sense",  "+{value}% celestial ore chance",      "prospector", 4, 3, 1.5,  "celestial_ore_bonus",    "\xF0\x9F\x8C\xA0", 3},
    {"prospector_void",        "Void Walker",      "+{value}% void crystal chance",       "prospector", 5, 2, 1.0,  "void_crystal_bonus",     "\xF0\x9F\x95\xB3\xEF\xB8\x8F", 4},
};

// Gambler branch skills
static const std::vector<SkillNode> GAMBLER_SKILLS = {
    {"gambler_luck",         "Beginner's Luck",  "+{value}% gambling win chance",       "gambler", 1, 5, 1.0,  "gambling_luck_bonus",    "\xF0\x9F\x8D\x80", 0},
    {"gambler_payout",       "High Roller",      "+{value}% gambling payout",           "gambler", 1, 5, 2.0,  "gambling_payout_bonus",  "\xF0\x9F\x92\xB8", 0},
    {"gambler_streak",       "Hot Hand",         "+{value}% win streak bonus",          "gambler", 2, 5, 3.0,  "win_streak_bonus",       "\xF0\x9F\x94\xA5", 1},
    {"gambler_insurance",    "Safety Net",       "+{value}% loss reduction",            "gambler", 3, 5, 2.0,  "loss_reduction",         "\xF0\x9F\x9B\xA1\xEF\xB8\x8F", 2},
    {"gambler_jackpot",      "Jackpot Hunter",   "+{value}% jackpot chance",            "gambler", 4, 3, 0.5,  "jackpot_chance_bonus",   "\xF0\x9F\x8E\xB0", 3},
    {"gambler_crit",         "Critical Hit",     "+{value}% chance for 2x payout",      "gambler", 5, 2, 1.0,  "critical_payout_chance", "\xF0\x9F\x92\xA5", 4},
};

// All skills combined for lookup
static const std::vector<SkillNode>& get_all_skills() {
    static std::vector<SkillNode> all;
    if (all.empty()) {
        all.insert(all.end(), ANGLER_SKILLS.begin(), ANGLER_SKILLS.end());
        all.insert(all.end(), PROSPECTOR_SKILLS.begin(), PROSPECTOR_SKILLS.end());
        all.insert(all.end(), GAMBLER_SKILLS.begin(), GAMBLER_SKILLS.end());
    }
    return all;
}

static const SkillNode* find_skill(const std::string& skill_id) {
    const auto& all = get_all_skills();
    for (const auto& s : all) {
        if (s.id == skill_id) return &s;
    }
    return nullptr;
}

static const std::vector<SkillNode>& get_branch_skills(const std::string& branch) {
    if (branch == "angler") return ANGLER_SKILLS;
    if (branch == "prospector") return PROSPECTOR_SKILLS;
    return GAMBLER_SKILLS;
}

static std::string branch_emoji(const std::string& branch) {
    if (branch == "angler") return "\xF0\x9F\x8E\xA3";
    if (branch == "prospector") return "\xE2\x9B\x8F\xEF\xB8\x8F";
    return "\xF0\x9F\x8E\xB0";
}

static std::string branch_display(const std::string& branch) {
    if (branch == "angler") return "Angler";
    if (branch == "prospector") return "Prospector";
    return "Gambler";
}

static int get_points_per_prestige(int prestige_level) {
    if (prestige_level <= 5) return 2;
    if (prestige_level <= 10) return 3;
    return 4;
}

static int get_total_prestige_points(int prestige_level) {
    int total = 0;
    for (int p = 1; p <= prestige_level; p++) {
        total += get_points_per_prestige(p);
    }
    return total;
}

struct UserSkillState {
    std::map<std::string, int> skill_ranks;  // skill_id -> current rank
    int total_points_spent;
    int total_points_available;
    int prestige_level;
};

static UserSkillState get_user_skills(Database* db, uint64_t user_id) {
    UserSkillState state;
    state.total_points_spent = 0;
    state.prestige_level = db->get_prestige(user_id);
    state.total_points_available = get_total_prestige_points(state.prestige_level);
    
    state.skill_ranks = bronx::db::skill_operations::get_user_skills(db, user_id);
    for (auto const& [id, rank] : state.skill_ranks) {
        state.total_points_spent += rank;
    }
    
    return state;
}

static int get_skill_rank(const UserSkillState& state, const std::string& skill_id) {
    auto it = state.skill_ranks.find(skill_id);
    return (it != state.skill_ranks.end()) ? it->second : 0;
}

static double get_skill_bonus(Database* db, uint64_t user_id, const std::string& bonus_type) {
    auto state = get_user_skills(db, user_id);
    double total = 0.0;
    auto all = get_all_skills();
    for (const auto& skill : all) {
        if (skill.bonus_type == bonus_type) {
            int rank = get_skill_rank(state, skill.id);
            total += rank * skill.bonus_per_rank;
        }
    }
    return total;
}

static std::string format_bonus_name(const std::string& bonus) {
    std::string result;
    std::string word;
    for (size_t i = 0; i <= bonus.size(); i++) {
        if (i == bonus.size() || bonus[i] == '_') {
            if (!word.empty()) {
                std::string uw = word;
                std::transform(uw.begin(), uw.end(), uw.begin(), ::toupper);
                if (uw == "XP") {
                    result += "XP";
                } else {
                    word[0] = static_cast<char>(toupper(static_cast<unsigned char>(word[0])));
                    result += word;
                }
                result += ' ';
                word.clear();
            }
        } else {
            word += bonus[i];
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

static bool meets_prerequisite(const UserSkillState& state, const SkillNode& skill) {
    if (skill.prerequisite_tier == 0) return true;
    const auto& branch_skills_vec = get_branch_skills(skill.branch);
    for (const auto& s : branch_skills_vec) {
        if (s.tier == skill.prerequisite_tier) {
            if (get_skill_rank(state, s.id) > 0) return true;
        }
    }
    return false;
}

static bool invest_skill_point(Database* db, uint64_t user_id, const std::string& skill_id) {
    return bronx::db::skill_operations::invest_skill_point(db, user_id, skill_id);
}

static void reset_all_skills(Database* db, uint64_t user_id) {
    bronx::db::skill_operations::reset_all_skills(db, user_id);
}

static std::string format_skill_display(const SkillNode& skill, int current_rank) {
    std::string line = skill.emoji + " **" + skill.name + "** ";
    for (int i = 0; i < skill.max_rank; i++) {
        line += (i < current_rank) ? "\xE2\x97\x8F" : "\xE2\x97\x8B"; 
    }
    double value = current_rank * skill.bonus_per_rank;
    std::string desc = skill.description;
    size_t pos = desc.find("{value}");
    if (pos != std::string::npos) {
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(1);
        oss << value;
        desc.replace(pos, 7, oss.str());
    }
    line += "\n   " + desc;
    return line;
}

static std::string build_branch_display(const std::string& branch, const UserSkillState& state) {
    const auto& skills = get_branch_skills(branch);
    std::string display = branch_emoji(branch) + " **" + branch_display(branch) + " Branch**\n";
    int branch_points = 0;
    int branch_max = 0;
    for (const auto& s : skills) {
        int rank = get_skill_rank(state, s.id);
        branch_points += rank;
        branch_max += s.max_rank;
    }
    display += "*Points invested: " + std::to_string(branch_points) + "/" + std::to_string(branch_max) + "*\n\n";
    int current_tier = 0;
    for (const auto& s : skills) {
        if (s.tier != current_tier) {
            current_tier = s.tier;
            display += "**Tier " + std::to_string(current_tier) + "**\n";
        }
        int rank = get_skill_rank(state, s.id);
        display += format_skill_display(s, rank) + "\n\n";
    }
    return display;
}

inline void handle_skill_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db) {
    std::string custom_id = event.custom_id;
    if (custom_id.find("skill_invest_") != 0) return;
    std::string remainder = custom_id.substr(13);
    size_t last_underscore = remainder.rfind('_');
    if (last_underscore == std::string::npos) return;
    std::string skill_id = remainder.substr(0, last_underscore);
    uint64_t target_user_id = std::stoull(remainder.substr(last_underscore + 1));
    uint64_t clicker_id = event.command.get_issuing_user().id;
    if (clicker_id != target_user_id) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("that's not your skill tree!")).set_flags(dpp::m_ephemeral));
        return;
    }
    db->ensure_user_exists(clicker_id);
    auto state = get_user_skills(db, clicker_id);
    if (state.prestige_level < 1) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("you need prestige 1+ to use skill trees!")).set_flags(dpp::m_ephemeral));
        return;
    }
    int free_points = state.total_points_available - state.total_points_spent;
    if (free_points <= 0) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("no skill points available!")).set_flags(dpp::m_ephemeral));
        return;
    }
    const SkillNode* skill = find_skill(skill_id);
    if (!skill) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("unknown skill!")).set_flags(dpp::m_ephemeral));
        return;
    }
    int current_rank = get_skill_rank(state, skill_id);
    if (current_rank >= skill->max_rank) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("that skill is already maxed!")).set_flags(dpp::m_ephemeral));
        return;
    }
    if (!meets_prerequisite(state, *skill)) {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("prerequisite not met!")) .set_flags(dpp::m_ephemeral));
        return;
    }
    if (invest_skill_point(db, clicker_id, skill_id)) {
        double new_bonus = (current_rank + 1) * skill->bonus_per_rank;
        std::ostringstream oss; oss << std::fixed; oss.precision(1); oss << new_bonus;
        std::string desc = ::bronx::EMOJI_CHECK + " Invested 1 point in **" + skill->name + "**\nBonus: **+" + oss.str() + "%** " + format_bonus_name(skill->bonus_type);
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::create_embed(desc, ::bronx::COLOR_SUCCESS)).set_flags(dpp::m_ephemeral));
    } else {
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(::bronx::error("failed to invest skill point!")).set_flags(dpp::m_ephemeral));
    }
}

inline Command* create_skill_tree_command(Database* db) {
    static Command* cmd = new Command("skills", "view & manage your skill tree", "economy", {"skill", "tree", "skilltree"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            auto state = get_user_skills(db, user_id);
            if (state.prestige_level < 1) {
                ::bronx::send_message(bot, event, ::bronx::error("skill trees unlock at **Prestige 1**!"));
                return;
            }
            std::string action = args.empty() ? "" : args[0];
            if (action == "respec" || action == "reset") {
                if (state.total_points_spent == 0) { ::bronx::send_message(bot, event, ::bronx::error("no points spent!")); return; }
                int64_t networth = db->get_networth(user_id);
                int64_t respec_cost = std::max((int64_t)500000, (int64_t)(networth * 0.10));
                if (db->get_wallet(user_id) < respec_cost) { ::bronx::send_message(bot, event, ::bronx::error("not enough money!")); return; }
                if (args.size() >= 2 && args[1] == "confirm") {
                    db->update_wallet(user_id, -respec_cost);
                    reset_all_skills(db, user_id);
                    ::bronx::send_message(bot, event, ::bronx::create_embed("Skill tree reset!", ::bronx::COLOR_SUCCESS));
                } else {
                    ::bronx::send_message(bot, event, ::bronx::create_embed("Respec costs $" + std::to_string(respec_cost) + ". Use `confirm` to proceed.", ::bronx::COLOR_WARNING));
                }
                return;
            }
            if (action == "invest" || action == "add") {
                if (args.size() < 2) { ::bronx::send_message(bot, event, ::bronx::error("usage: `b.skills invest <skill>`")); return; }
                std::string skill_query = args[1];
                const SkillNode* skill = find_skill(skill_query);
                if (!skill) { ::bronx::send_message(bot, event, ::bronx::error("unknown skill!")); return; }
                if (invest_skill_point(db, user_id, skill->id)) { ::bronx::send_message(bot, event, ::bronx::create_embed("Point invested!", ::bronx::COLOR_SUCCESS)); }
                return;
            }
            if (action == "angler" || action == "prospector" || action == "gambler") {
                ::bronx::send_message(bot, event, ::bronx::create_embed(build_branch_display(action, state)));
                return;
            }
            int free_points = state.total_points_available - state.total_points_spent;
            std::string desc = "**Skill Tree Overview**\nPoints: " + std::to_string(free_points) + " available";
            auto embed = ::bronx::create_embed(desc);
            ::bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);
            auto state = get_user_skills(db, user_id);
            if (state.prestige_level < 1) { event.reply(dpp::message().add_embed(::bronx::error("prestige 1 required!"))); return; }
            std::string action = event.get_parameter("action").index() != 0 ? std::get<std::string>(event.get_parameter("action")) : "";
            if (action == "respec") {
                reset_all_skills(db, user_id);
                event.reply(dpp::message().add_embed(::bronx::create_embed("Reset!", ::bronx::COLOR_SUCCESS)));
                return;
            }
            std::string branch = event.get_parameter("branch").index() != 0 ? std::get<std::string>(event.get_parameter("branch")) : "";
            if (!branch.empty()) {
                event.reply(dpp::message().add_embed(::bronx::create_embed(build_branch_display(branch, state))));
                return;
            }
            event.reply(dpp::message().add_embed(::bronx::create_embed("Skill tree overview")));
        },
        {
            dpp::command_option(dpp::co_string, "action", "what to do", false)
                .add_choice(dpp::command_option_choice("view", std::string("view")))
                .add_choice(dpp::command_option_choice("invest", std::string("invest")))
                .add_choice(dpp::command_option_choice("respec", std::string("respec"))),
            dpp::command_option(dpp::co_string, "branch", "skill branch to view", false)
                .add_choice(dpp::command_option_choice("angler", std::string("angler")))
                .add_choice(dpp::command_option_choice("prospector", std::string("prospector")))
                .add_choice(dpp::command_option_choice("gambler", std::string("gambler"))),
            dpp::command_option(dpp::co_string, "skill", "skill ID to invest in", false)
        }
    );
    cmd->extended_description = "Unlock at Prestige 1. Spend Prestige Points on skills across 3 branches.";
    return cmd;
}

} // namespace skill_tree
} // namespace commands
