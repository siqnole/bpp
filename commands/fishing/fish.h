#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../../database/operations/economy/server_fishing_operations.h"
#include "../economy_core.h"
#include "../milestones.h"
#include "../achievements.h"
#include "fishing_helpers.h"
#include "simple_commands.h"  // for friendly_item_name, format_number
#include "../global_boss.h"
#include "../pets/pets.h"
#include "crews.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include "../skill_tree/skill_tree.h"
#include <dpp/dpp.h>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>

using namespace bronx::db;

namespace commands {
namespace fishing {

// how long the fishing command remains on cooldown (seconds)
static const int FISH_COOLDOWN_SECONDS = 30;

// Get cooldown adjusted by Quick Hands skill (fish_cooldown_reduction)
static int get_adjusted_fish_cooldown(Database* db, uint64_t uid, int base_cd) {
    double reduction = commands::skill_tree::get_skill_bonus(db, uid, "fish_cooldown_reduction");
    if (reduction > 0.0) {
        int adjusted = static_cast<int>(base_cd * (1.0 - reduction / 100.0));
        return std::max(adjusted, 5); // minimum 5s cooldown
    }
    return base_cd;
}

// ============================================================
// ANTI-MACRO / CAPTCHA SYSTEM
// ============================================================

// range of fish commands between captcha prompts
static const int CAPTCHA_MIN_INTERVAL = 8;
static const int CAPTCHA_MAX_INTERVAL = 20;
// seconds the user has to answer before it counts as a failure
static const int CAPTCHA_TIMEOUT_SECONDS = 120;

struct AntiMacroState {
    int fish_count = 0;              // fish commands since last captcha
    int next_captcha_at = 0;         // randomly chosen threshold
    bool captcha_pending = false;    // waiting for user to answer
    std::string captcha_answer;      // expected answer string
    std::string captcha_question;    // the display question
    int strikes = 0;                 // escalating punishment counter (persists)
    std::chrono::steady_clock::time_point captcha_issued_at;
};
static std::unordered_map<uint64_t, AntiMacroState> anti_macro_states;
static std::mutex anti_macro_mutex;

static int random_captcha_interval() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(CAPTCHA_MIN_INTERVAL, CAPTCHA_MAX_INTERVAL);
    return dis(gen);
}

// generate a simple math captcha and return {question, answer}
static std::pair<std::string, std::string> generate_captcha() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> type_dis(0, 2);
    int type = type_dis(gen);
    std::string question, answer;
    if (type == 0) {
        std::uniform_int_distribution<int> n(1, 50);
        int a = n(gen), b = n(gen);
        question = "What is **" + std::to_string(a) + " + " + std::to_string(b) + "**?";
        answer = std::to_string(a + b);
    } else if (type == 1) {
        std::uniform_int_distribution<int> n(1, 50);
        int a = n(gen), b = n(gen);
        if (a < b) std::swap(a, b);
        question = "What is **" + std::to_string(a) + " - " + std::to_string(b) + "**?";
        answer = std::to_string(a - b);
    } else {
        std::uniform_int_distribution<int> n(2, 12);
        int a = n(gen), b = n(gen);
        question = "What is **" + std::to_string(a) + " x " + std::to_string(b) + "**?";
        answer = std::to_string(a * b);
    }
    return {question, answer};
}

// apply escalating penalty and return a message to send
static dpp::message apply_anti_macro_penalty(Database* db, uint64_t uid, AntiMacroState& state, const std::string& reason) {
    if (state.strikes >= 3) {
        // BLACKLIST
        db->add_global_blacklist(uid, "(anti-macro) failed 3 captcha verifications");
        state = AntiMacroState{};
        return dpp::message().add_embed(bronx::error(
            "\xf0\x9f\x9a\xab **You have been blacklisted.**\n\n"
            "Reason: failed anti-macro verification 3 times (" + reason + ")\n\n"
            "If this was a mistake, appeal in the support server."
        ));
    } else if (state.strikes == 2) {
        int cooldown = 30 * 60;
        db->set_cooldown(uid, "fish", cooldown);
        state.fish_count = 0;
        state.next_captcha_at = random_captcha_interval();
        auto ts = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() + std::chrono::seconds(cooldown));
        return dpp::message().add_embed(bronx::error(
            "\xf0\x9f\x94\x92 **Anti-macro verification failed** (" + reason + ")\n\n"
            "**Strike 2/3** \xe2\x80\x94 you are on a **30 minute** fishing cooldown.\n"
            "You can fish again <t:" + std::to_string(ts) + ":R>\n\n"
            "\xe2\x9a\xa0\xef\xb8\x8f **Next failure will result in a blacklist!**"
        ));
    } else {
        int cooldown = 5 * 60;
        db->set_cooldown(uid, "fish", cooldown);
        state.fish_count = 0;
        state.next_captcha_at = random_captcha_interval();
        auto ts = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now() + std::chrono::seconds(cooldown));
        return dpp::message().add_embed(bronx::error(
            "\xf0\x9f\x94\x92 **Anti-macro verification failed** (" + reason + ")\n\n"
            "**Strike " + std::to_string(state.strikes) + "/3** \xe2\x80\x94 you are on a **5 minute** fishing cooldown.\n"
            "You can fish again <t:" + std::to_string(ts) + ":R>\n\n"
            "\xe2\x9a\xa0\xef\xb8\x8f Further failures will increase penalties."
        ));
    }
}

// Check anti-macro state before fishing.
// Returns an empty message (no embeds) if OK to proceed, otherwise a response to send.
static dpp::message check_anti_macro(Database* db, uint64_t uid, const std::string& captcha_input) {
    std::lock_guard<std::mutex> lock(anti_macro_mutex);
    auto& state = anti_macro_states[uid];

    // initialise threshold on first encounter
    if (state.next_captcha_at == 0) {
        state.next_captcha_at = random_captcha_interval();
    }

    // ---- captcha is already pending ----
    if (state.captcha_pending) {
        // check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - state.captcha_issued_at).count();
        if (elapsed > CAPTCHA_TIMEOUT_SECONDS) {
            state.captcha_pending = false;
            state.strikes++;
            return apply_anti_macro_penalty(db, uid, state, "captcha timed out");
        }
        // no answer provided — remind them
        if (captcha_input.empty()) {
            return dpp::message().add_embed(bronx::error(
                "\xf0\x9f\x94\x92 **Anti-macro verification pending!**\n\n"
                + state.captcha_question + "\n\n"
                "Reply with `fish <answer>` or `/fish answer:<answer>` to continue fishing.\n"
                "\xe2\x9a\xa0\xef\xb8\x8f Strikes: " + std::to_string(state.strikes) + "/3"
            ));
        }
        // check answer
        if (captcha_input == state.captcha_answer) {
            state.captcha_pending = false;
            state.fish_count = 0;
            state.next_captcha_at = random_captcha_interval();
            // correct — return empty message so fishing proceeds
            return dpp::message();
        } else {
            state.captcha_pending = false;
            state.strikes++;
            return apply_anti_macro_penalty(db, uid, state,
                "incorrect answer (you said: " + captcha_input + ", expected: " + state.captcha_answer + ")");
        }
    }

    // ---- normal fishing flow — increment counter ----
    state.fish_count++;

    if (state.fish_count >= state.next_captcha_at) {
        // issue a captcha
        auto [question, answer] = generate_captcha();
        state.captcha_pending = true;
        state.captcha_question = question;
        state.captcha_answer = answer;
        state.captcha_issued_at = std::chrono::steady_clock::now();
        return dpp::message().add_embed(bronx::create_embed(
            "\xf0\x9f\x94\x92 **Anti-macro verification**\n\n"
            "To continue fishing, solve this:\n\n"
            + question + "\n\n"
            "Reply with `fish <answer>` or `/fish answer:<answer>`.\n"
            "You have **" + std::to_string(CAPTCHA_TIMEOUT_SECONDS) + " seconds** to answer.\n\n"
            "\xe2\x9a\xa0\xef\xb8\x8f Strikes: " + std::to_string(state.strikes) + "/3"
        ));
    }

    // no captcha needed — proceed
    return dpp::message();
}

// ============================================================
// FISHING MINIGAME SYSTEM
// ============================================================

// Timing constants for the minigame
static const int MINIGAME_CAST_MIN_MS = 3000;   // min wait before bite (3s)
static const int MINIGAME_CAST_MAX_MS = 8000;   // max wait before bite (8s)
static const int MINIGAME_BITE_WINDOW_MS = 10000; // time to reel in (10s)

// Speed bonus thresholds (reaction time in ms)
static const int SPEED_PERFECT_MS = 1500;    // < 1.5s = perfect
static const int SPEED_GREAT_MS = 3000;      // < 3s = great
static const int SPEED_GOOD_MS = 6000;       // < 6s = good
// > 6s = slow

// Speed multipliers
static const double SPEED_PERFECT_MULT = 1.5;
static const double SPEED_GREAT_MULT = 1.25;
static const double SPEED_GOOD_MULT = 1.0;
static const double SPEED_SLOW_MULT = 0.8;

struct FishMinigameState {
    uint64_t uid;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id = 0;
    bool is_boosting;
    bool bite_active = false;     // fish is biting, waiting for reel
    bool completed = false;       // minigame finished (success or timeout)
    std::string interaction_token; // non-empty for slash commands (use webhook edit)
    std::chrono::steady_clock::time_point bite_time;  // when bite appeared

    // pre-validated gear info
    std::string rod_id, bait_id;
    int rod_lvl = 1, bait_lvl = 1;
    std::string rod_meta, bait_meta;
    int used_bait = 0;
    int starting_bait = 0;
    int luck = 0;
    int prestige_level = 0;
    int prestige_bonus_pct = 0;
    int gear_lvl = 0;
    int bait_bonus = 0;
    int synergy = 0;
    int extra_fish = 0;
    bool has_explicit_unlocks = false;
    std::vector<FishType> pool;
    std::vector<int> weights;
    int total_weight = 0;
};
static std::unordered_map<uint64_t, FishMinigameState> fish_minigames;
static std::mutex fish_minigame_mutex;

// Build the "casting" phase message
static dpp::message build_casting_message(uint64_t uid) {
    std::string desc = "🎣 **Casting your line...**\n\n";
    desc += "*Wait for a fish to bite...*\n";
    desc += "```\n";
    desc += "  ~~ ~~~~~~ ~~~~ ~~~~~ ~~\n";
    desc += "       🎣\n";
    desc += "        |\n";
    desc += "  ~~ ~~~~~~ ~~~~ ~~~~~ ~~\n";
    desc += "```";

    dpp::message msg;
    msg.add_embed(bronx::create_embed(desc, bronx::COLOR_INFO));

    dpp::component row;
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("Waiting...")
        .set_style(dpp::cos_secondary)
        .set_id("fish_reel_" + std::to_string(uid))
        .set_disabled(true));
    msg.add_component(row);
    return msg;
}

// Build the "bite" phase message
static dpp::message build_bite_message(uint64_t uid) {
    std::string desc = "🐟 **Something's biting!**\n\n";
    desc += "**Quick, reel it in!**\n";
    desc += "```\n";
    desc += "  ~~ ~~~~~~ ~~~~ ~~~~~ ~~\n";
    desc += "       🎣  💥  🐟\n";
    desc += "        |\n";
    desc += "  ~~ ~~~~~~ ~~~~ ~~~~~ ~~\n";
    desc += "```";

    dpp::message msg;
    msg.add_embed(bronx::create_embed(desc, 0xFFD700));  // gold/yellow for urgency

    dpp::component row;
    row.add_component(dpp::component()
        .set_type(dpp::cot_button)
        .set_label("🎣 Reel In!")
        .set_style(dpp::cos_success)
        .set_id("fish_reel_" + std::to_string(uid))
        .set_disabled(false));
    msg.add_component(row);
    return msg;
}

// Build the "escaped" message (timeout)
static dpp::message build_escaped_message(int used_bait) {
    std::string desc = "💨 **The fish got away!**\n\n";
    desc += "You were too slow to reel it in.\n";
    desc += "*" + std::to_string(used_bait) + " bait was consumed.*\n\n";
    desc += "Better luck next time! Use `fish` to try again.";

    dpp::message msg;
    msg.add_embed(bronx::create_embed(desc, bronx::COLOR_ERROR));
    return msg;
}

// Get speed rating string and multiplier from reaction time
static std::pair<std::string, double> get_speed_rating(int reaction_ms) {
    if (reaction_ms < SPEED_PERFECT_MS) {
        return {"⚡ **PERFECT CATCH!** (+" + std::to_string((int)((SPEED_PERFECT_MULT - 1.0) * 100)) + "% value bonus)", SPEED_PERFECT_MULT};
    } else if (reaction_ms < SPEED_GREAT_MS) {
        return {"🌟 **Great catch!** (+" + std::to_string((int)((SPEED_GREAT_MULT - 1.0) * 100)) + "% value bonus)", SPEED_GREAT_MULT};
    } else if (reaction_ms < SPEED_GOOD_MS) {
        return {"✅ **Good catch!**", SPEED_GOOD_MULT};
    } else {
        return {"🐌 **Slow catch...** (" + std::to_string((int)((1.0 - SPEED_SLOW_MULT) * 100)) + "% value penalty)", SPEED_SLOW_MULT};
    }
}

// ============================================================

// pagination state stored per-user
struct FishReceiptState {
    std::string header;
    std::string footer;
    std::vector<CatchInfo> log;
    int current_page = 0;
};
static std::unordered_map<uint64_t, FishReceiptState> fish_states;
static std::mutex fish_states_mutex;

// build a message containing one page of the receipt for user uid
static dpp::message build_fish_message(uint64_t uid) {
    std::lock_guard<std::mutex> lock(fish_states_mutex);
    auto it = fish_states.find(uid);
    if (it == fish_states.end()) {
        return dpp::message().add_embed(bronx::error("no receipt available"));
    }
    FishReceiptState &st = it->second;
    int per_page = 5;
    int total = st.log.size();
    if (total <= 0) {
        return dpp::message().add_embed(bronx::error("no fish in receipt"));
    }
    int pages = (total + per_page - 1) / per_page;
    if (pages == 0) pages = 1;
    int p = st.current_page;
    if (p < 0) p = 0;
    if (p >= pages) p = pages - 1;
    std::string desc = st.header;
    int start = p * per_page;
    int end = std::min(total, start + per_page);
    if (start >= total) start = total - 1;
    if (end > total) end = total;
    if (start < 0) start = 0;
    
    for (int i = start; i < end && i < (int)st.log.size(); ++i) {
        auto &entry = st.log[i];
        if (entry.isBonus) desc += "[BONUS] ";
        desc += entry.fish.emoji + " ***" + entry.fish.name + "*** `[" + entry.item_id + "]`";
        // always show probability odds like finfo command would
        char bufp[32];
        snprintf(bufp, sizeof(bufp), " *(**%.2f%%**)*", entry.probability);
        desc += bufp;
        if (!entry.fish.description.empty()) {
            desc += " – *" + entry.fish.description + "*";
        }
        desc += " – __$" + format_number(entry.value) + "__";
        if (entry.hadEffect) {
            if (entry.effect_mult != 1.0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "**x%.2f**", entry.effect_mult);
                desc += buf;
            }
            if (entry.effect_delta != 0) {
                if (entry.effect_delta > 0)
                    desc += " *+$" + format_number(entry.effect_delta) + "*";
                else
                    desc += " -$" + format_number(-entry.effect_delta);
            }
        }
        desc += "\n";
    }
    desc += st.footer;
    auto embed = bronx::create_embed(desc);
    dpp::message msg;
    msg.add_embed(embed);

    // button row containing navigation controls (if there are multiple pages) and a quick-sell-all button
    if (total > 0) {
        dpp::component row;
        if (pages > 1) {
            dpp::component prev;
            prev.set_type(dpp::cot_button)
                .set_label("◀")
                .set_style(dpp::cos_secondary)
                .set_id("fish_nav_prev_" + std::to_string(uid));
            dpp::component page_count;
            page_count.set_type(dpp::cot_button)
                .set_label(std::to_string(p+1) + "/" + std::to_string(pages))
                .set_style(dpp::cos_secondary)
                .set_disabled(true)
                .set_id("fish_page_count_" + std::to_string(uid));
            dpp::component next;
            next.set_type(dpp::cot_button)
                .set_label("▶")
                .set_style(dpp::cos_secondary)
                .set_id("fish_nav_next_" + std::to_string(uid));
            row.add_component(prev);
            row.add_component(page_count);
            row.add_component(next);
        }
        // quick sell all button (sells every catch shown on the current page)
        dpp::component sellall;
        sellall.set_type(dpp::cot_button)
            .set_label("sell all")
            .set_style(dpp::cos_danger)
            .set_id("fish_sellall_" + std::to_string(uid) + "_" + std::to_string(p));
        row.add_component(sellall);
        msg.add_component(row);
    }

    // quicksell select menu for fish on current page
    if (total > 0) {
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("quick sell fish on this page")
            .set_id("fish_sell_" + std::to_string(uid) + "_" + std::to_string(p));
        bool any_option = false;
        for (int i = start; i < end && i < (int)st.log.size(); ++i) {
            auto &entry = st.log[i];
            if (entry.sold) continue;
            std::string label = entry.fish.emoji + " " + entry.fish.name + " (" + entry.item_id + ")";
            select_menu.add_select_option(dpp::select_option(label, entry.item_id));
            any_option = true;
        }
        if (any_option) {
            msg.add_component(dpp::component().add_component(select_menu));
        }
    }

    return msg;
}

// Support server ID for home court bonus
static const uint64_t SUPPORT_SERVER_ID = 1259717095382319215ULL;
// Owner ID for owner bonus
static const uint64_t OWNER_ID = 814226043924643880ULL;

// ============================================================
// PHASE 1: Validate gear, consume bait, build fish pool
// Returns an error message (with embeds) on failure, or empty message on success.
// On success, populates the FishMinigameState struct.
// ============================================================
static dpp::message prepare_fish(Database* db, uint64_t uid, uint64_t guild_id, bool is_boosting, FishMinigameState& state) {
    try {
        state.uid = uid;
        state.guild_id = guild_id;
        state.is_boosting = is_boosting;

        auto gear = db->get_active_fishing_gear(uid);
        if (gear.first.empty()) {
            return dpp::message().add_embed(bronx::error("you need to equip a fishing rod before fishing"));
        }
        if (gear.second.empty()) {
            return dpp::message().add_embed(bronx::error("you need to equip bait before fishing"));
        }
        if (!db->has_item(uid, gear.first, 1)) {
            return dpp::message().add_embed(bronx::error("you don't actually have your equipped rod"));
        }
        if (!db->has_item(uid, gear.second, 1)) {
            return dpp::message().add_embed(bronx::error("you don't actually have your equipped bait"));
        }

        state.rod_id = gear.first;
        state.bait_id = gear.second;

        // compute rod/bait levels
        state.rod_lvl = 1; state.bait_lvl = 1;
        for (auto &it : db->get_inventory(uid)) {
            if (it.item_id == gear.first) { state.rod_lvl = it.level; state.rod_meta = it.metadata; }
            if (it.item_id == gear.second) { state.bait_lvl = it.level; state.bait_meta = it.metadata; }
        }

        if (state.bait_lvl > 3 && abs(state.rod_lvl - state.bait_lvl) > 2) {
            return dpp::message().add_embed(bronx::error("rod and bait levels are incompatible"));
        }

        state.prestige_level = db->get_prestige(uid);
        state.prestige_bonus_pct = state.prestige_level * 5;

        int n_bait = db->get_item_quantity(uid, gear.second);
        int capacity = parse_meta_int(state.rod_meta, "capacity", 1);
        state.used_bait = std::min(n_bait, capacity);
        state.starting_bait = n_bait;
        if (state.used_bait <= 0) {
            return dpp::message().add_embed(bronx::error("you have no bait to use"));
        }
        if (!db->remove_item(uid, gear.second, state.used_bait)) {
            return dpp::message().add_embed(bronx::error("failed to consume bait"));
        }

        // build pool
        auto unlocks = parse_meta_array(state.bait_meta, "unlocks");
        if (!unlocks.empty()) {
            for (auto &f : fish_types) {
                if (std::find(unlocks.begin(), unlocks.end(), f.name) != unlocks.end()) state.pool.push_back(f);
            }
        }
        state.has_explicit_unlocks = !unlocks.empty() && !state.pool.empty();
        if (state.pool.empty()) state.pool = fish_types;
        if (gear.second == "bait_rare") {
            state.pool.erase(std::remove_if(state.pool.begin(), state.pool.end(), [](const FishType &f){
                std::string rarity = get_fish_rarity(f.name);
                return rarity == "normal";
            }), state.pool.end());
        } else if (state.bait_lvl > 2) {
            state.pool.erase(std::remove_if(state.pool.begin(), state.pool.end(), [](const FishType &f){ return f.name=="common fish"; }), state.pool.end());
        }

        state.gear_lvl = std::min(state.rod_lvl, state.bait_lvl);
        int unlock_gear_lvl = state.has_explicit_unlocks ? state.bait_lvl : state.gear_lvl;
        state.pool.erase(std::remove_if(state.pool.begin(), state.pool.end(), [&](const FishType &f){
            if (f.max_gear_level > 0 && state.gear_lvl >= f.max_gear_level) return true;
            if (f.min_gear_level > 0 && unlock_gear_lvl < f.min_gear_level) return true;
            return false;
        }), state.pool.end());
        if (state.pool.empty()) {
            return dpp::message().add_embed(bronx::error("no fish available for your rod/bait combination"));
        }

        // weights & luck
        state.luck = parse_meta_int(state.rod_meta, "luck", 0);
        int max_w = 0; for (auto &f : state.pool) max_w = std::max(max_w, f.weight);
        for (auto &f : state.pool) {
            int w = f.weight;
            if (w <= 0) w = 1;
            if (state.luck != 0 && max_w > 0) w += (int)((max_w - w) * (state.luck / 100.0));
            state.weights.push_back(w);
        }
        state.total_weight = 0; for (int w : state.weights) state.total_weight += w;
        if (state.total_weight <= 0) {
            return dpp::message().add_embed(bronx::error("invalid fishing weight distribution"));
        }

        state.bait_bonus = parse_meta_int(state.bait_meta, "bonus", 0);
        state.synergy = (state.rod_lvl == state.bait_lvl ? state.rod_lvl : 1);
        state.extra_fish = (state.used_bait * state.bait_bonus * state.synergy) / 100;

        // success — return empty message
        return dpp::message();
    } catch (const std::exception &e) {
        return dpp::message().add_embed(bronx::error("an internal error occurred while preparing to fish"));
    }
}

// ============================================================
// PHASE 2: Roll fish and build receipt (called after successful reel)
// speed_mult is applied as a bonus/penalty to all fish values.
// ============================================================
static dpp::message complete_fish(Database* db, const FishMinigameState& state, double speed_mult, const std::string& speed_label) {
    try {
        uint64_t uid = state.uid;
        std::random_device rd; std::mt19937 gen(rd());
        std::discrete_distribution<> dis;
        try {
            dis = std::discrete_distribution<>(state.weights.begin(), state.weights.end());
        } catch (const std::invalid_argument &ie) {
            return dpp::message().add_embed(bronx::error("an internal error occurred during fishing"));
        }

        // Pre-compute crew bonus (1.0, 1.15, or 1.25)
        double crew_mult = ::commands::fishing::crews::get_crew_bonus(db, state.uid);

        // Pre-fetch DB stats used by fish effects to avoid per-fish DB calls
        int64_t prefetch_wallet = db->get_wallet(uid);
        int64_t prefetch_bank = db->get_bank(uid);
        int64_t prefetch_fish_caught = db->get_stat(uid, "fish_caught");
        int64_t prefetch_fish_sold = db->get_stat(uid, "fish_sold");
        int64_t prefetch_gambling_wins = db->get_stat(uid, "gambling_wins");
        int64_t prefetch_gambling_losses = db->get_stat(uid, "gambling_losses");
        // Pre-fetch inventory for Collector effect
        auto prefetch_inv = db->get_inventory(uid);
        std::set<std::string> prefetch_unique_collectibles;
        for (auto& i : prefetch_inv) if (i.item_type == "collectible") prefetch_unique_collectibles.insert(i.item_id);

        // Accumulator for batch fish_catches INSERT (must be declared before lambda)
        std::vector<Database::FishCatchRow> pending_fish_catches;
        pending_fish_catches.reserve(state.used_bait + state.extra_fish);

        auto roll_and_store = [&](bool is_bonus) -> CatchInfo {
            int idx; const FishType *fishptr;
            bool avoid_common = (state.bait_id == "bait_rare" || state.bait_id == "bait_epic" || state.bait_id == "bait_legendary");
            do {
                try {
                    idx = dis(gen);
                    if (idx < 0 || idx >= (int)state.pool.size()) idx = 0;
                } catch (...) { idx = 0; }
                fishptr = &state.pool[idx];
            } while (avoid_common && fishptr->name == "common fish");
            const auto &fish = *fishptr;
            bool triggered = false; FishEffect trigType = FishEffect::None;

            double probability = 0.0;
            if (state.total_weight > 0) {
                if (avoid_common) {
                    double reduced = 0.0;
                    for (size_t j = 0; j < state.pool.size(); ++j) {
                        if (state.pool[j].name != "common fish") reduced += state.weights[j];
                    }
                    if (reduced > 0.0) {
                        if (state.pool[idx].name != "common fish") probability = (state.weights[idx] / reduced) * 100.0;
                        else probability = 0.01;
                    }
                } else {
                    probability = (state.weights[idx] / (double)state.total_weight) * 100.0;
                }
            }
            if (probability <= 0.0) probability = 0.01;

            int64_t base;
            try {
                std::uniform_int_distribution<int64_t> valdis(fish.min_value, fish.max_value);
                base = valdis(gen);
            } catch (...) { base = fish.min_value; }
            if (state.luck != 0) base = base + (base * state.luck / 100);
            if (state.bait_lvl > 1) base += state.bait_lvl * 5;
            int bait_mult = parse_meta_int(state.bait_meta, "multiplier", 0);
            if (bait_mult != 0) base += (base * bait_mult / 100);
            if (state.prestige_bonus_pct > 0) base += (base * state.prestige_bonus_pct / 100);
            int64_t val = base;
            double roll = (double)rand() / RAND_MAX;
            if (roll < fish.effect_chance) {
                triggered = true; trigType = fish.effect;
                switch(fish.effect) {
                    case FishEffect::Flat: val += state.luck + state.bait_lvl; break;
                    case FishEffect::Exponential: val = (int64_t)(val * pow(1.0 + state.luck / 100.0, 2)); break;
                    case FishEffect::Logarithmic: val = (int64_t)(val * log2(state.luck + 2)); break;
                    case FishEffect::NLogN: { double n = state.luck + state.bait_lvl; val = (int64_t)(val * (n * log2(n + 2))); break; }
                    case FishEffect::Wacky: val *= (rand() % 5 + 1); break;
                    case FishEffect::Jackpot: val = (rand() % 2 == 0) ? (int64_t)(val * 0.2) : (int64_t)(val * 8); break;
                    case FishEffect::Critical: val = (rand() % 2 == 0) ? val * 2 : val; break;
                    case FishEffect::Volatile: val = (int64_t)(val * (0.3 + (rand() % 38) / 10.0)); break;
                    case FishEffect::Surge: val += state.gear_lvl * state.gear_lvl * 50; break;
                    case FishEffect::Diminishing: { double m = 3.0 - state.luck / 50.0; val = (int64_t)(val * std::max(0.5, m)); break; }
                    case FishEffect::Cascading: { int rolls = 1 + rand() % 6; val = (int64_t)(val * pow(1.15, rolls)); break; }
                    case FishEffect::Wealthy: { val = (int64_t)(val * (1.0 + sqrt((double)prefetch_wallet) / 1000.0)); break; }
                    case FishEffect::Banker: { val = (int64_t)(val * (1.0 + log10((double)prefetch_bank + 1) / 5.0)); break; }
                    case FishEffect::Fisher: { val = (int64_t)(val * (1.0 + (double)prefetch_fish_caught / 50000.0)); break; }
                    case FishEffect::Merchant: { val += prefetch_fish_sold / 100; break; }
                    case FishEffect::Gambler: { val = (prefetch_gambling_wins > prefetch_gambling_losses) ? val * 2 : (int64_t)(val * 0.5); break; }
                    case FishEffect::Ascended: { double mult = std::min(10.0, pow(1.5, state.prestige_level)); val = (int64_t)(val * mult); break; }
                    case FishEffect::Underdog: { double m2 = 2.0 - (double)prefetch_wallet / 10000000.0; val = (int64_t)(val * std::max(0.5, m2)); break; }
                    case FishEffect::HotStreak: { val = (int64_t)(val * (1.0 + (double)prefetch_gambling_wins / ((double)prefetch_gambling_wins + (double)prefetch_gambling_losses + 1.0))); break; }
                    case FishEffect::Collector: { val += prefetch_unique_collectibles.size() * 100; break; }
                    case FishEffect::Persistent: { val = (int64_t)(val * log2((double)(prefetch_fish_caught + prefetch_fish_sold) + 2.0)); break; }
                    default: break;
                }
            }

            // Apply home court bonus (5%)
            if (state.guild_id == SUPPORT_SERVER_ID) {
                val += (val * 5) / 100;
            }
            // Apply supporter boost (10%)
            if (state.is_boosting) {
                val += (val * 10) / 100;
            }
            // Apply owner bonus (10%)
            if (uid == OWNER_ID && state.guild_id != 0) {
                val += (val * 10) / 100;
            }

            // Apply minigame speed multiplier
            val = (int64_t)(val * speed_mult);
            if (val < 1) val = 1;

            // Apply crew bonus
            if (crew_mult > 1.0) {
                val = (int64_t)(val * crew_mult);
                if (val < 1) val = 1;
            }

            int64_t delta = val - base;
            double mult = base > 0 ? (double)val / base : 1.0;
            std::string fid = generate_fish_id();
            std::string metadata = "{\"name\":\"" + fish.name + "\",\"value\":" + std::to_string(val) + ",\"locked\":false}";
            db->add_item(uid, fid, "collectible", 1, metadata);
            // fish_catch logging is deferred to a batch INSERT after the loop
            std::string rarity = get_fish_rarity(fish.name);
            pending_fish_catches.push_back({rarity, fish.name, 1.0, val, state.rod_id, state.bait_id});
            return {fish, val, triggered, trigType, is_bonus, delta, mult, probability, fid, false};
        };

        std::vector<CatchInfo> caught_log;
        for (int i = 0; i < state.used_bait; ++i) caught_log.push_back(roll_and_store(false));
        for (int i = 0; i < state.extra_fish; ++i) caught_log.push_back(roll_and_store(true));

        // Flush all fish catches in one round-trip instead of N
        if (!pending_fish_catches.empty()) {
            db->add_fish_catches_batch(uid, pending_fish_catches);
            // Also record per-guild fish catches for dashboard stats
            if (state.guild_id != 0) {
                bronx::db::server_economy_operations::create_guild_economy(db, state.guild_id);
                for (const auto& fc : pending_fish_catches) {
                    bronx::db::server_fishing_operations::create_server_fish_catch(
                        db, state.guild_id, uid, fc.rarity, fc.fish_name, fc.weight, fc.value, fc.rod_id, fc.bait_id);
                }
            }
        }

        // build header/footer
        std::string header;
        header += speed_label + "\n\n";
        header += "**Fish catch receipt**\n";
        header += "Rod: " + friendly_item_name(db, state.rod_id) + " (lvl " + std::to_string(state.rod_lvl) + ", luck " + std::to_string(state.luck) + "% )\n";
        header += "Bait: " + friendly_item_name(db, state.bait_id) + " (lvl " + std::to_string(state.bait_lvl) + ")";
        if (state.bait_bonus > 0) header += "  [bonus " + std::to_string(state.bait_bonus) + "%]";
        int bait_mv = parse_meta_int(state.bait_meta, "multiplier", 0);
        if (bait_mv > 0) header += "  [value x" + std::to_string(100 + bait_mv) + "%]";
        header += "\n";
        if (state.prestige_level > 0) {
            header += bronx::EMOJI_STAR + " Prestige " + std::to_string(state.prestige_level) + " [+" + std::to_string(state.prestige_bonus_pct) + "% value]\n";
        }
        if (state.guild_id == SUPPORT_SERVER_ID) header += "🏠 Home Court Bonus [+5% fish value]\n";
        if (state.is_boosting) header += "💎 Supporter Boost [+10% fish value]\n";
        if (state.uid == OWNER_ID && state.guild_id != 0) header += "👑 Owner Bonus [+10% fish value]\n";
        if (crew_mult > 1.0) header += "🫂 Crew Bonus [+" + std::to_string((int)((crew_mult - 1.0) * 100)) + "% fish value]\n";
        header += "Bait before: " + std::to_string(state.starting_bait) + ", used: " + std::to_string(state.used_bait) + "\n";
        header += "\n";

        int64_t total_value = 0; int bonus_cnt = 0;
        int64_t bait_price = 0;
        if (auto bi = db->get_shop_item(state.bait_id)) bait_price = bi->price;
        int64_t bait_cost = bait_price * state.used_bait;
        for (auto &e : caught_log) { total_value += e.value; if (e.isBonus) bonus_cnt++; }

        // Update crew activity stats
        ::commands::fishing::crews::update_crew_activity(db, state.uid, (int)caught_log.size(), total_value);

        std::string footer;
        footer += "\n**total fish:** " + std::to_string(caught_log.size());
        if (bonus_cnt > 0) footer += " (" + std::to_string(bonus_cnt) + " bonus)";
        footer += "\n**total value:** $" + format_number(total_value);
        int64_t profit = total_value - bait_cost;
        if (profit < 0 && state.bait_lvl <= 3) {
            int64_t deficit = -profit;
            int64_t minv = fish_types.empty() ? 0 : fish_types.front().min_value;
            if (minv > 0) {
                int64_t extra = ((deficit + minv - 1) / minv) * minv;
                if (extra > 5000) extra = 5000;
                profit += extra;
                footer += "\n*compensation bonus: +$" + format_number(extra) + "*";
            }
        }
        db->record_fishing_log(state.rod_lvl, state.bait_lvl, profit);
        if (profit < 0) footer += "\n**net profit:** -$" + format_number(-profit);
        else footer += "\n**net profit:** $" + format_number(profit);
        footer += "\n\nuse `sellfish <id>` to sell individual fish or `finv` to view your catches";
        footer += "\nClick the buttons below to navigate pages or quick-sell fish.";

        FishReceiptState st{header, footer, caught_log, 0};
        {
            std::lock_guard<std::mutex> lock(fish_states_mutex);
            fish_states[uid] = st;
        }
        dpp::message msg = build_fish_message(uid);
        global_boss::on_fish_command(db, uid, (int64_t)caught_log.size());
        ::commands::pets::pet_hooks::on_fish(db, uid, (int)caught_log.size());

        // Track daily challenge stats
        ::commands::daily_challenges::track_daily_stat(db, uid, "fish_caught", (int64_t)caught_log.size());
        // Also increment fish_caught in user_stats (was missing)
        db->increment_stat(uid, "fish_caught", (int64_t)caught_log.size());
        // Count rare+ catches for daily challenge
        int rare_count = 0;
        for (const auto& c : caught_log) {
            std::string rarity = get_fish_rarity(c.fish.name);
            if (rarity == "rare" || rarity == "epic" || rarity == "legendary" || rarity == "prestige") rare_count++;
        }
        if (rare_count > 0) ::commands::daily_challenges::track_daily_stat(db, uid, "rare_fish_caught", rare_count);

        return msg;
    } catch (const std::exception &e) {
        return dpp::message().add_embed(bronx::error("an internal error occurred during fishing"));
    }
}

// ============================================================
// Start the minigame: send casting message, schedule bite, handle timeout
// ============================================================
static void start_minigame(dpp::cluster& bot, Database* db, FishMinigameState state) {
    // Schedule the bite after a random delay
    std::thread([&bot, db, uid = state.uid]() {
        // Random cast delay
        static std::mt19937 rng(static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<int> delay_dist(MINIGAME_CAST_MIN_MS, MINIGAME_CAST_MAX_MS);
        int delay_ms = delay_dist(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

        uint64_t channel_id, message_id;
        std::string interaction_token;
        {
            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
            auto it = fish_minigames.find(uid);
            if (it == fish_minigames.end() || it->second.completed) return;
            it->second.bite_active = true;
            it->second.bite_time = std::chrono::steady_clock::now();
            channel_id = it->second.channel_id;
            message_id = it->second.message_id;
            interaction_token = it->second.interaction_token;
        }

        if (message_id == 0) {
            // message_create/edit_response failed (e.g. Missing Permissions) — clean up and refund bait
            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
            auto it = fish_minigames.find(uid);
            if (it != fish_minigames.end() && !it->second.completed) {
                it->second.completed = true;
                if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                    try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                }
                fish_minigames.erase(uid);
            }
            return;
        }

        // Update message to show bite (use interaction webhook for slash commands)
        dpp::message bite_msg = build_bite_message(uid);
        bite_msg.id = message_id;
        bite_msg.channel_id = channel_id;

        // Error handler for bite edit failure — clean up minigame and refund bait
        auto on_bite_edit = [&bot, db, uid, channel_id](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                auto it = fish_minigames.find(uid);
                if (it != fish_minigames.end() && !it->second.completed) {
                    it->second.completed = true;
                    if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                        try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                    }
                    fish_minigames.erase(uid);
                }
                // Try to send a fallback error message
                try {
                    bot.message_create(dpp::message(channel_id, "").add_embed(
                        bronx::error("fishing failed \xe2\x80\x94 missing permissions to edit messages in this channel. your bait has been refunded.")));
                } catch (...) {}
            }
        };

        if (!interaction_token.empty()) {
            bot.interaction_response_edit(interaction_token, bite_msg, on_bite_edit);
        } else {
            bot.message_edit(bite_msg, on_bite_edit);
        }

        // Schedule timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(MINIGAME_BITE_WINDOW_MS));

        {
            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
            auto it = fish_minigames.find(uid);
            if (it == fish_minigames.end() || it->second.completed) return;
            // Timeout — fish escaped
            it->second.completed = true;
            int used_bait = it->second.used_bait;

            dpp::message escaped_msg = build_escaped_message(used_bait);
            escaped_msg.id = message_id;
            escaped_msg.channel_id = channel_id;

            // Error handler for escaped edit failure — send fallback message
            auto on_escape_edit = [&bot, channel_id, used_bait](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    try {
                        bot.message_create(dpp::message(channel_id, "").add_embed(
                            bronx::error("the fish got away! (" + std::to_string(used_bait) + " bait consumed). use `fish` to try again.")));
                    } catch (...) {}
                }
            };

            if (!interaction_token.empty()) {
                bot.interaction_response_edit(interaction_token, escaped_msg, on_escape_edit);
            } else {
                bot.message_edit(escaped_msg, on_escape_edit);
            }

            // Set a shorter cooldown on timeout (15s instead of 30s), adjusted by Quick Hands skill
            db->set_cooldown(uid, "fish", get_adjusted_fish_cooldown(db, uid, FISH_COOLDOWN_SECONDS / 2));
            fish_minigames.erase(uid);
        }
    }).detach();
}

// ============================================================
// Handle the reel-in button click (called from interaction handler)
// ============================================================
static void handle_reel_in(dpp::cluster& bot, Database* db, uint64_t uid, uint64_t channel_id,
                            const dpp::button_click_t& event) {
    FishMinigameState state;
    int reaction_ms = 0;
    {
        std::lock_guard<std::mutex> lock(fish_minigame_mutex);
        auto it = fish_minigames.find(uid);
        if (it == fish_minigames.end()) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("no active fishing session found")).set_flags(dpp::m_ephemeral));
            return;
        }
        if (it->second.completed) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this fishing session has already ended")).set_flags(dpp::m_ephemeral));
            return;
        }
        if (!it->second.bite_active) {
            // Clicked too early — fish escapes!
            it->second.completed = true;
            int used_bait = it->second.used_bait;
            fish_minigames.erase(uid);

            std::string desc = "😤 **You reeled in too early!**\n\n";
            desc += "You need to wait for a fish to bite before reeling in!\n";
            desc += "*" + std::to_string(used_bait) + " bait was consumed.*\n\n";
            desc += "Use `fish` to try again.";
            event.reply(dpp::ir_update_message, dpp::message().add_embed(bronx::create_embed(desc, bronx::COLOR_ERROR)));
            db->set_cooldown(uid, "fish", get_adjusted_fish_cooldown(db, uid, FISH_COOLDOWN_SECONDS / 2));
            return;
        }

        // Calculate reaction time
        auto now = std::chrono::steady_clock::now();
        reaction_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.bite_time).count();
        state = it->second;
        it->second.completed = true;
        fish_minigames.erase(uid);
    }

    // Get speed rating and apply
    auto [speed_label, speed_mult] = get_speed_rating(reaction_ms);

    // Acknowledge with deferred update
    event.reply(dpp::ir_deferred_update_message, dpp::message());

    // Roll fish with speed bonus
    dpp::message result = complete_fish(db, state, speed_mult, speed_label);

    // Add reaction time to footer
    if (!result.embeds.empty()) {
        auto& embed = result.embeds[0];
        std::string time_str = std::to_string(reaction_ms / 1000) + "." + std::to_string((reaction_ms % 1000) / 100) + "s";
        embed.set_footer(dpp::embed_footer().set_text("reaction time: " + time_str));
    }

    // Edit the original message with the receipt (use interaction webhook for button clicks)
    event.edit_response(result);

    // Set cooldown (adjusted by Quick Hands skill)
    db->set_cooldown(uid, "fish", get_adjusted_fish_cooldown(db, uid, FISH_COOLDOWN_SECONDS));

    // Track milestones
    {
        std::lock_guard<std::mutex> lock(fish_states_mutex);
        auto it = fish_states.find(uid);
        if (it != fish_states.end() && !it->second.log.empty()) {
            int fish_count = it->second.log.size();
            milestones::track_milestone(bot, db, channel_id, uid,
                                       milestones::MilestoneType::FISH_CAUGHT, fish_count);
            achievements::check_achievements_for_stat(bot, db, channel_id, uid, "fish_caught");
        }
    }
}

inline Command* get_fish_command(Database* db) {
    // Helper to start the minigame after validation
    auto launch_minigame = [db](dpp::cluster& bot, uint64_t uid, uint64_t guild_id, uint64_t channel_id,
                                bool is_boosting, const std::string& captcha_input,
                                const std::string& interaction_token,
                                std::function<void(dpp::message)> send_initial,
                                std::function<void(dpp::message)> send_error) {
        // --- anti-macro captcha check ---
        dpp::message captcha_msg = check_anti_macro(db, uid, captcha_input);
        if (!captcha_msg.embeds.empty()) {
            send_error(captcha_msg);
            return;
        }

        // Check for existing active minigame
        {
            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
            if (fish_minigames.count(uid)) {
                send_error(dpp::message().add_embed(bronx::error("you already have an active fishing session! reel it in or wait for it to expire")));
                return;
            }
        }

        // Phase 1: validate and consume bait
        FishMinigameState state;
        dpp::message err = prepare_fish(db, uid, guild_id, is_boosting, state);
        if (!err.embeds.empty()) {
            send_error(err);
            return;
        }

        state.channel_id = channel_id;
        state.interaction_token = interaction_token;

        // Build and send casting message
        dpp::message cast_msg = build_casting_message(uid);

        // Store state and send message
        // We need the message ID from the callback to edit it later
        {
            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
            fish_minigames[uid] = state;
        }

        send_initial(cast_msg);
    };

    static Command* fish = new Command("fish", "cast your line and catch fish", "fishing", {"cast", "fih"}, true,
        [db, launch_minigame](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            uint64_t guild_id = event.msg.guild_id;
            uint64_t channel_id = event.msg.channel_id;

            // cooldown check
            if (db->is_on_cooldown(uid, "fish")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "fish")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event, bronx::error("your fishing rod needs rest! try again <t:" + std::to_string(timestamp) + ":R>"));
                }
                return;
            }

            std::string captcha_input = args.empty() ? "" : args[0];

            // Determine boosting status, then launch minigame
            if (guild_id != 0) {
                bot.guild_get_member(guild_id, uid, [db, launch_minigame, &bot, event, uid, guild_id, channel_id, captcha_input](const dpp::confirmation_callback_t& callback) {
                    bool is_boosting = false;
                    if (!callback.is_error()) {
                        try {
                            auto member = std::get<dpp::guild_member>(callback.value);
                            is_boosting = (member.premium_since > 0);
                        } catch (const std::bad_variant_access&) {
                            // Failed to get member, just use is_boosting = false
                        }
                    }

                    launch_minigame(bot, uid, guild_id, channel_id, is_boosting, captcha_input, "",
                        // send_initial: send casting message and get message ID
                        [&bot, event, db, uid](dpp::message cast_msg) {
                            cast_msg.set_reference(event.msg.id);
                            cast_msg.channel_id = event.msg.channel_id;
                            bot.message_create(cast_msg, [&bot, db, uid](const dpp::confirmation_callback_t& cb) {
                                if (!cb.is_error()) {
                                    try {
                                        auto sent = std::get<dpp::message>(cb.value);
                                        std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                        auto it = fish_minigames.find(uid);
                                        if (it != fish_minigames.end()) {
                                            it->second.message_id = sent.id;
                                            // Now start the minigame timer
                                            FishMinigameState state_copy = it->second;
                                            start_minigame(bot, db, state_copy);
                                        }
                                    } catch (const std::bad_variant_access&) {
                                        // Failed to get message from callback — clean up and refund bait
                                        std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                        auto it = fish_minigames.find(uid);
                                        if (it != fish_minigames.end()) {
                                            if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                                try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                            }
                                            fish_minigames.erase(uid);
                                        }
                                    }
                                } else {
                                    // message_create failed (e.g. Missing Permissions) — clean up and refund bait
                                    std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                    auto it = fish_minigames.find(uid);
                                    if (it != fish_minigames.end()) {
                                        if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                            try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                        }
                                        fish_minigames.erase(uid);
                                    }
                                }
                            });
                        },
                        // send_error
                        [&bot, event](dpp::message err) {
                            bronx::send_message(bot, event, err);
                        });
                });
                return;
            }

            // DMs or no guild context
            launch_minigame(bot, uid, 0, channel_id, false, captcha_input, "",
                [&bot, event, db, uid](dpp::message cast_msg) {
                    cast_msg.set_reference(event.msg.id);
                    cast_msg.channel_id = event.msg.channel_id;
                    bot.message_create(cast_msg, [&bot, db, uid](const dpp::confirmation_callback_t& cb) {
                        if (!cb.is_error()) {
                            try {
                                auto sent = std::get<dpp::message>(cb.value);
                                std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                auto it = fish_minigames.find(uid);
                                if (it != fish_minigames.end()) {
                                    it->second.message_id = sent.id;
                                    FishMinigameState state_copy = it->second;
                                    start_minigame(bot, db, state_copy);
                                }
                            } catch (const std::bad_variant_access&) {
                                // Failed to get message from callback — clean up and refund bait
                                std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                auto it = fish_minigames.find(uid);
                                if (it != fish_minigames.end()) {
                                    if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                        try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                    }
                                    fish_minigames.erase(uid);
                                }
                            }
                        } else {
                            // message_create failed (e.g. Missing Permissions) — clean up and refund bait
                            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                            auto it = fish_minigames.find(uid);
                            if (it != fish_minigames.end()) {
                                if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                    try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                }
                                fish_minigames.erase(uid);
                            }
                        }
                    });
                },
                [&bot, event](dpp::message err) {
                    bronx::send_message(bot, event, err);
                });
        },
        [db, launch_minigame](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            uint64_t guild_id = event.command.guild_id;
            uint64_t channel_id = event.command.channel_id;

            if (db->is_on_cooldown(uid, "fish")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "fish")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(bronx::error("your fishing rod needs rest! try again <t:" + std::to_string(timestamp) + ":R>")));
                }
                return;
            }

            std::string captcha_input;
            try {
                auto p = event.get_parameter("answer");
                if (std::holds_alternative<std::string>(p)) {
                    captcha_input = std::get<std::string>(p);
                }
            } catch (...) {}

            // Defer the interaction immediately to avoid the 3-second token expiry
            event.thinking(false, [db, launch_minigame, &bot, event, uid, guild_id, channel_id, captcha_input](const dpp::confirmation_callback_t&) {
                // Helper to edit the deferred response and start minigame
                auto edit_and_start = [&bot, event, db, uid](dpp::message cast_msg) {
                    event.edit_response(cast_msg, [&bot, event, db, uid](const dpp::confirmation_callback_t& cb) {
                        if (!cb.is_error()) {
                            event.get_original_response([&bot, db, uid](const dpp::confirmation_callback_t& cb2) {
                                if (!cb2.is_error()) {
                                    try {
                                        auto msg = std::get<dpp::message>(cb2.value);
                                        std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                        auto it = fish_minigames.find(uid);
                                        if (it != fish_minigames.end()) {
                                            it->second.message_id = msg.id;
                                            FishMinigameState state_copy = it->second;
                                            start_minigame(bot, db, state_copy);
                                        }
                                    } catch (const std::bad_variant_access&) {
                                        // Failed to get message — clean up and refund bait
                                        std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                        auto it = fish_minigames.find(uid);
                                        if (it != fish_minigames.end()) {
                                            if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                                try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                            }
                                            fish_minigames.erase(uid);
                                        }
                                    }
                                } else {
                                    // get_original_response failed — clean up and refund bait
                                    std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                                    auto it = fish_minigames.find(uid);
                                    if (it != fish_minigames.end()) {
                                        if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                            try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                        }
                                        fish_minigames.erase(uid);
                                    }
                                }
                            });
                        } else {
                            // edit_response failed (e.g. Missing Permissions) — clean up and refund bait
                            std::lock_guard<std::mutex> lock(fish_minigame_mutex);
                            auto it = fish_minigames.find(uid);
                            if (it != fish_minigames.end()) {
                                if (it->second.used_bait > 0 && !it->second.bait_id.empty()) {
                                    try { db->add_item(uid, it->second.bait_id, "bait", it->second.used_bait, it->second.bait_meta, it->second.bait_lvl); } catch (...) {}
                                }
                                fish_minigames.erase(uid);
                            }
                        }
                    });
                };
                auto edit_error = [event](dpp::message err) {
                    event.edit_response(err);
                };

                if (guild_id != 0) {
                    bot.guild_get_member(guild_id, uid, [db, launch_minigame, &bot, event, uid, guild_id, channel_id, captcha_input, edit_and_start, edit_error](const dpp::confirmation_callback_t& callback) {
                        bool is_boosting = false;
                        if (!callback.is_error()) {
                            try {
                                auto member = std::get<dpp::guild_member>(callback.value);
                                is_boosting = (member.premium_since > 0);
                            } catch (const std::bad_variant_access&) {}
                        }
                        launch_minigame(bot, uid, guild_id, channel_id, is_boosting, captcha_input, event.command.token,
                            edit_and_start, edit_error);
                    });
                } else {
                    // DMs or no guild context
                    launch_minigame(bot, uid, 0, channel_id, false, captcha_input, event.command.token,
                        edit_and_start, edit_error);
                }
            });
        },
        // slash command options: optional "answer" for captcha verification
        {dpp::command_option(dpp::co_string, "answer", "captcha answer for anti-macro verification", false)});
    return fish;
}

} // namespace fishing
} // namespace commands
