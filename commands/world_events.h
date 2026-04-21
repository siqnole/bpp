#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "economy_core.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iostream>

using namespace bronx::db;

namespace commands {
namespace world_events {

// ============================================================
// World Event Definitions
// ============================================================
// Each event has a type, name, description, emoji, bonus_type, bonus_value,
// and duration range. The engine picks one at random and starts it.
// ============================================================

struct EventTemplate {
    std::string event_type;
    std::string event_name;
    std::string description;
    std::string emoji;
    std::string bonus_type;    // used by game code to check for active bonuses
    double bonus_value;        // multiplier (e.g. 0.25 = +25%)
    int min_duration_minutes;
    int max_duration_minutes;
};

// All possible random events
inline const std::vector<EventTemplate>& get_event_templates() {
    static const std::vector<EventTemplate> templates = {
        // Fishing events
        {
            "fishing", "Rare Fish Spawning!",
            "A school of rare fish has appeared! Rare+ fish chance increased for a limited time.",
            "🐟", "fishing_rare", 0.25,
            20, 45
        },
        {
            "fishing", "Golden Tide!",
            "The seas are shimmering gold! All fish sell for bonus value.",
            "✨", "fishing_value", 0.30,
            15, 30
        },
        {
            "fishing", "Bait Frenzy!",
            "Fish are extra hungry! Bait is twice as effective — double catch chance.",
            "🪱", "fishing_catch", 0.50,
            15, 30
        },

        // Mining events
        {
            "mining", "Gold Rush!",
            "A gold vein has erupted! Gold ore drops are significantly boosted.",
            "⛏️", "mining_gold", 0.50,
            20, 60
        },
        {
            "mining", "Meteor Shower!",
            "Meteors are raining down! Chance to find rare Meteor Fragments while mining.",
            "☄️", "mining_meteor", 1.0,
            30, 60
        },
        {
            "mining", "Crystal Cavern Discovered!",
            "Miners have discovered a crystal cavern! All ore values increased.",
            "💎", "mining_value", 0.40,
            20, 45
        },

        // Gambling events
        {
            "gambling", "Casino Night!",
            "The house is feeling generous! All gambling payouts get a bonus.",
            "🎰", "gambling_payout", 0.10,
            30, 60
        },
        {
            "gambling", "Lucky Hour!",
            "Lady luck smiles upon you! Win rates are slightly improved.",
            "🍀", "gambling_luck", 0.05,
            15, 30
        },
        {
            "gambling", "Jackpot Fever!",
            "The progressive jackpot trigger chance is doubled!",
            "💰", "jackpot_boost", 2.0,
            20, 40
        },

        // Economy events
        {
            "economy", "Tax Holiday!",
            "The government declared a tax holiday! Rob protection for everyone. No one can be robbed.",
            "🏛️", "no_rob", 1.0,
            30, 60
        },
        {
            "economy", "Double Daily!",
            "It's a generous day! Daily reward payouts are doubled.",
            "💵", "daily_bonus", 1.0,
            60, 120
        },
        {
            "economy", "Market Boom!",
            "The economy is booming! All shop items are 20% cheaper.",
            "📈", "shop_discount", 0.20,
            30, 60
        },

        // XP events
        {
            "xp", "Double XP!",
            "A server-wide XP boost is active! All XP gains are doubled.",
            "⭐", "xp_boost", 1.0,
            30, 60
        },
        {
            "xp", "Knowledge Rush!",
            "The library is open! 50% bonus XP from all messages.",
            "📚", "xp_boost", 0.50,
            20, 45
        },
    };
    return templates;
}

// ============================================================
// Random Event Engine — called by a timer in main.cpp
// ============================================================
// Checks if there's currently an active event. If not, rolls a chance
// to start a new one. Returns true if a new event was started.
// ============================================================

// Chance per tick (every 5 minutes) to spawn a new event when none is active.
// ~12 ticks per hour → approximately 1 event every 3-4 hours on average.
constexpr double EVENT_SPAWN_CHANCE = 0.04; // 4% per tick

inline std::optional<WorldEventData> try_spawn_random_event(Database* db, bool force = false) {
    // Expire any past-due events
    db->expire_world_events();

    // Check if there's already an active event
    auto active = db->get_active_world_event();
    if (active && !force) return std::nullopt; // already an event running

    // If forced and there's an active one, end it first? 
    // For now, let's just allow multiple if forced, but the DB schema might only support one active at a time 
    // due to how get_active_world_event works (it likely picks the first active one).
    // Let's stick to ending the current one if forced.
    if (force && active) {
        db->end_active_world_event();
    }

    // Roll the dice (if not forced)
    if (!force) {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> chance(0.0, 1.0);
        if (chance(rng) > EVENT_SPAWN_CHANCE) return std::nullopt; // no event this tick
    }

    // Pick a random event template
    static thread_local std::mt19937 rng(std::random_device{}());
    const auto& templates = get_event_templates();
    std::uniform_int_distribution<size_t> idx(0, templates.size() - 1);
    const auto& tmpl = templates[idx(rng)];

    // Pick a random duration within the range
    std::uniform_int_distribution<int> dur(tmpl.min_duration_minutes, tmpl.max_duration_minutes);
    int duration = dur(rng);

    // Start the event
    bool ok = db->start_world_event(tmpl.event_type, tmpl.event_name, tmpl.description,
                                     tmpl.emoji, tmpl.bonus_type, tmpl.bonus_value, duration);

    if (ok) {
        std::cout << "\033[35m[world event]\033[0m " << tmpl.emoji << " " << tmpl.event_name
                  << " started (bonus: " << tmpl.bonus_type << " +" << (tmpl.bonus_value * 100) << "%, "
                  << duration << " min)\n";
        
        // Return the newly created event data
        return db->get_active_world_event();
    }

    return std::nullopt;
}

// ============================================================
// /event command — view current world event and recent history
// ============================================================

inline Command* get_event_command(Database* db) {
    static Command* cmd = new Command("event", "view the current world event", "economy", {"events", "worldevent", "we"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            auto active = db->get_active_world_event();

            std::string desc;
            desc += "# 🌍 World Events\n\n";

            if (active) {
                // Time remaining
                auto now = std::chrono::system_clock::now();
                auto ends = std::chrono::system_clock::from_time_t(active->ends_at_timestamp);
                auto remaining = std::chrono::duration_cast<std::chrono::minutes>(ends - now);
                int mins_left = std::max(0, (int)remaining.count());

                desc += "## " + active->emoji + " " + active->event_name + " (ACTIVE)\n\n";
                desc += active->description + "\n\n";
                desc += "⏰ **time remaining:** " + std::to_string(mins_left) + " minutes\n";
                desc += "📊 **bonus:** " + active->bonus_type + " +" +
                        std::to_string((int)(active->bonus_value * 100)) + "%\n";
            } else {
                desc += "*no event is currently active*\n\n";
                desc += "events spawn randomly every few hours. keep playing and one will appear!\n";
            }

            // Recent history
            auto history = db->get_world_event_history(5);
            if (!history.empty()) {
                desc += "\n📜 **recent events**\n";
                for (const auto& ev : history) {
                    std::string status = ev.active ? "🟢 active" : "⚫ ended";
                    desc += ev.emoji + " **" + ev.event_name + "** — " + status + "\n";
                }
            }

            auto embed = bronx::create_embed(desc, 0x9B59B6); // purple
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::maybe_add_support_link(embed);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto active = db->get_active_world_event();

            std::string desc;
            desc += "# 🌍 World Events\n\n";

            if (active) {
                auto now = std::chrono::system_clock::now();
                auto ends = std::chrono::system_clock::from_time_t(active->ends_at_timestamp);
                auto remaining = std::chrono::duration_cast<std::chrono::minutes>(ends - now);
                int mins_left = std::max(0, (int)remaining.count());

                desc += "## " + active->emoji + " " + active->event_name + " (ACTIVE)\n\n";
                desc += active->description + "\n\n";
                desc += "⏰ **time remaining:** " + std::to_string(mins_left) + " minutes\n";
                desc += "📊 **bonus:** " + active->bonus_type + " +" +
                        std::to_string((int)(active->bonus_value * 100)) + "%\n";
            } else {
                desc += "*no event is currently active*\n\n";
                desc += "events spawn randomly every few hours. keep playing and one will appear!\n";
            }

            auto history = db->get_world_event_history(5);
            if (!history.empty()) {
                desc += "\n📜 **recent events**\n";
                for (const auto& ev : history) {
                    std::string status = ev.active ? "🟢 active" : "⚫ ended";
                    desc += ev.emoji + " **" + ev.event_name + "** — " + status + "\n";
                }
            }

            auto embed = bronx::create_embed(desc, 0x9B59B6);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            bronx::maybe_add_support_link(embed);
            event.reply(dpp::message().add_embed(embed));
        }
    );
    return cmd;
}

// Build an announcement embed for when a new event starts (sent to guilds)
inline dpp::embed build_event_start_embed(const WorldEventData& ev) {
    std::string desc = "## " + ev.emoji + " " + ev.event_name + "\n\n";
    desc += ev.description + "\n\n";

    auto now = std::chrono::system_clock::now();
    auto ends = std::chrono::system_clock::from_time_t(ev.ends_at_timestamp);
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(ends - now);
    desc += "⏰ **duration:** " + std::to_string(duration.count()) + " minutes\n";
    desc += "📊 **bonus:** +" + std::to_string((int)(ev.bonus_value * 100)) + "% " + ev.bonus_type;

    auto embed = bronx::create_embed(desc, 0x9B59B6);
    embed.set_title("🌍 World Event Started!");
    embed.set_footer(dpp::embed_footer().set_text("use b.event to check the current event"));
    return embed;
}

} // namespace world_events
} // namespace commands
