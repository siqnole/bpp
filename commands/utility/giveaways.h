#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/server_economy_operations.h"
#include "../../performance/cache_manager.h"
#include "../economy/helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <chrono>
#include <regex>
#include <set>

using namespace bronx::db;
using namespace commands::economy;

namespace commands {
namespace utility {

// Parse duration string (e.g., "1h", "30m", "1d", "24h30m")
static int parse_duration_seconds(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    int total = 0;
    std::regex pattern("(\\d+)([smhd])");
    auto begin = std::sregex_iterator(lower.begin(), lower.end(), pattern);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it) {
        int value = std::stoi((*it)[1].str());
        char unit = (*it)[2].str()[0];
        switch (unit) {
            case 's': total += value; break;
            case 'm': total += value * 60; break;
            case 'h': total += value * 3600; break;
            case 'd': total += value * 86400; break;
        }
    }
    
    // If no matches, try parsing as plain number (assume minutes)
    if (total == 0) {
        try {
            total = std::stoi(lower) * 60;
        } catch (...) {
            return 0;
        }
    }
    
    return total;
}

// Format seconds into human-readable duration
static std::string format_duration(int seconds) {
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m";
    if (seconds < 86400) {
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        return m > 0 ? std::to_string(h) + "h " + std::to_string(m) + "m" 
                     : std::to_string(h) + "h";
    }
    int d = seconds / 86400;
    int h = (seconds % 86400) / 3600;
    return h > 0 ? std::to_string(d) + "d " + std::to_string(h) + "h" 
                 : std::to_string(d) + "d";
}

// Structure to track active giveaways
struct ActiveGiveaway {
    uint64_t id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id;
    uint64_t created_by;
    int64_t prize;
    int max_winners;
    std::chrono::system_clock::time_point ends_at;
    bool ended;
};

// ─── Cache-backed giveaway storage ──────────────────────────────────────────
// Uses TTLCache for per-giveaway lookups with auto-expiry, plus lightweight
// index sets for iteration (timer + list command).
// Replaces the old std::map + std::mutex with a pattern consistent with
// the rest of the codebase (performance/cache_manager.h).
// ────────────────────────────────────────────────────────────────────────────
struct GiveawayCache {
    // Per-giveaway TTL cache — entries auto-expire after the giveaway duration + 1h buffer
    bronx::cache::TTLCache<uint64_t, ActiveGiveaway> entries{std::chrono::hours(8)};

    // Index structures for iteration (TTLCache doesn't expose iterators)
    std::mutex index_mutex;
    std::set<uint64_t> active_ids;                                    // global set of active IDs
    std::unordered_map<uint64_t, std::set<uint64_t>> guild_ids;       // guild_id -> giveaway IDs

    void store(const ActiveGiveaway& ga) {
        // TTL = time remaining + 1 hour buffer for post-end cleanup
        auto time_left = std::chrono::duration_cast<std::chrono::milliseconds>(
            ga.ends_at - std::chrono::system_clock::now());
        auto ttl = std::max(std::chrono::milliseconds(3600000), time_left + std::chrono::hours(1));

        entries.set(ga.id, ga, ttl);

        std::lock_guard<std::mutex> lock(index_mutex);
        active_ids.insert(ga.id);
        guild_ids[ga.guild_id].insert(ga.id);
    }

    std::optional<ActiveGiveaway> get(uint64_t giveaway_id) {
        return entries.get(giveaway_id);
    }

    void remove(uint64_t giveaway_id, uint64_t guild_id) {
        entries.invalidate(giveaway_id);

        std::lock_guard<std::mutex> lock(index_mutex);
        active_ids.erase(giveaway_id);
        auto it = guild_ids.find(guild_id);
        if (it != guild_ids.end()) {
            it->second.erase(giveaway_id);
            if (it->second.empty()) guild_ids.erase(it);
        }
    }

    // Return all active (non-expired) giveaways, cleaning stale index entries
    std::vector<ActiveGiveaway> get_all_active() {
        std::vector<ActiveGiveaway> result;
        std::lock_guard<std::mutex> lock(index_mutex);
        for (auto it = active_ids.begin(); it != active_ids.end(); ) {
            auto ga = entries.get(*it);
            if (ga.has_value() && !ga->ended) {
                result.push_back(ga.value());
                ++it;
            } else {
                it = active_ids.erase(it); // stale — TTL expired or ended
            }
        }
        return result;
    }

    // Return active giveaways for a specific guild
    std::vector<ActiveGiveaway> get_guild_active(uint64_t guild_id) {
        std::vector<ActiveGiveaway> result;
        std::lock_guard<std::mutex> lock(index_mutex);
        auto git = guild_ids.find(guild_id);
        if (git == guild_ids.end()) return result;
        for (auto it = git->second.begin(); it != git->second.end(); ) {
            auto ga = entries.get(*it);
            if (ga.has_value() && !ga->ended) {
                result.push_back(ga.value());
                ++it;
            } else {
                it = git->second.erase(it);
            }
        }
        if (git->second.empty()) guild_ids.erase(git);
        return result;
    }
};

static GiveawayCache giveaway_cache;

// Reload active giveaways from the database into the cache (restart recovery)
static void reload_giveaways_from_db(Database* db) {
    auto rows = db->get_active_giveaways();
    for (auto& row : rows) {
        ActiveGiveaway ga;
        ga.id = row.id;
        ga.guild_id = row.guild_id;
        ga.channel_id = row.channel_id;
        ga.message_id = row.message_id;
        ga.created_by = row.created_by;
        ga.prize = row.prize;
        ga.max_winners = row.max_winners;
        ga.ends_at = row.ends_at;
        ga.ended = false;
        giveaway_cache.store(ga);
    }
    if (!rows.empty()) {
        std::cout << "[giveaway] recovered " << rows.size() << " active giveaways from database" << std::endl;
    }
}

// Helper to check if member has Manage Guild permission
static bool member_can_manage_guild(const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g && g->owner_id == member.user_id) return true;
    
    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (!r) continue;
        uint64_t perms = static_cast<uint64_t>(r->permissions);
        if (perms & static_cast<uint64_t>(dpp::p_administrator)) return true;
        if (perms & static_cast<uint64_t>(dpp::p_manage_guild)) return true;
    }
    return false;
}

// Build giveaway embed
static dpp::embed build_giveaway_embed(const ActiveGiveaway& ga, int entry_count = 0) {
    auto now = std::chrono::system_clock::now();
    auto time_left = std::chrono::duration_cast<std::chrono::seconds>(ga.ends_at - now).count();
    
    std::string desc = "🎉 **GIVEAWAY** 🎉\n\n";
    desc += "**Prize:** $" + format_number(ga.prize) + "\n";
    desc += "**Winners:** " + std::to_string(ga.max_winners) + "\n";
    desc += "**Entries:** " + std::to_string(entry_count) + "\n\n";
    
    if (ga.ended) {
        desc += "⏰ **Ended**";
    } else if (time_left > 0) {
        desc += "⏰ Ends in **" + format_duration(time_left) + "**";
    } else {
        desc += "⏰ **Ending soon...**";
    }
    
    return bronx::create_embed(desc);
}

// Build giveaway message with button
static dpp::message build_giveaway_message(const ActiveGiveaway& ga, int entry_count = 0) {
    dpp::message msg;
    msg.add_embed(build_giveaway_embed(ga, entry_count));
    
    if (!ga.ended) {
        dpp::component btn;
        btn.set_type(dpp::cot_button)
           .set_label("🎉 Enter Giveaway")
           .set_style(dpp::cos_success)
           .set_id("giveaway_enter_" + std::to_string(ga.id));
        msg.add_component(dpp::component().add_component(btn));
    }
    
    return msg;
}

// Pick random winners from entries
static std::vector<uint64_t> pick_winners(const std::vector<uint64_t>& entries, int max_winners) {
    std::vector<uint64_t> winners;
    if (entries.empty()) return winners;
    
    std::vector<uint64_t> pool = entries;
    std::random_device rd;
    std::mt19937 gen(rd());
    
    int to_pick = std::min(max_winners, static_cast<int>(pool.size()));
    for (int i = 0; i < to_pick; ++i) {
        std::uniform_int_distribution<> dist(0, pool.size() - 1);
        int idx = dist(gen);
        winners.push_back(pool[idx]);
        pool.erase(pool.begin() + idx);
    }
    
    return winners;
}

// End a giveaway and distribute prizes
static void end_giveaway(dpp::cluster& bot, Database* db, uint64_t giveaway_id) {
    auto ga_opt = giveaway_cache.get(giveaway_id);
    if (!ga_opt.has_value() || ga_opt->ended) return;
    
    ActiveGiveaway ga = ga_opt.value();
    ga.ended = true;
    
    // Update cache to mark as ended before processing (prevent double-end)
    giveaway_cache.entries.set(ga.id, ga, std::chrono::minutes(5));
    
    // Get entries from database
    auto entries = db->get_giveaway_entries(giveaway_id);
    auto winners = pick_winners(entries, ga.max_winners);
    
    // Mark giveaway as ended in database
    db->end_giveaway(giveaway_id, winners);
    
    // Calculate prize per winner
    int64_t prize_each = winners.empty() ? 0 : ga.prize / winners.size();
    
    // Build winner announcement
    std::string desc;
    if (winners.empty()) {
        desc = "🎉 **GIVEAWAY ENDED** 🎉\n\n";
        desc += "**Prize:** $" + format_number(ga.prize) + "\n\n";
        desc += "😢 No one entered the giveaway!";
    } else {
        desc = "🎉 **GIVEAWAY ENDED** 🎉\n\n";
        desc += "**Prize:** $" + format_number(ga.prize) + "\n";
        desc += "**Winners:**\n";
        for (uint64_t winner_id : winners) {
            desc += "🏆 <@" + std::to_string(winner_id) + "> — $" + format_number(prize_each) + "\n";
            // Add prize to winner's wallet
            db->update_wallet(winner_id, prize_each);
        }
    }
    
    auto embed = bronx::success(desc);
    dpp::message msg;
    msg.add_embed(embed);
    msg.channel_id = ga.channel_id;
    
    // Edit original message to show ended state
    if (ga.message_id != 0) {
        dpp::message edit_msg(ga.channel_id, build_giveaway_embed(ga, entries.size()));
        edit_msg.id = ga.message_id;
        bot.message_edit(edit_msg);
    }
    
    // Send winner announcement
    bot.message_create(msg);
    
    // Remove from cache
    giveaway_cache.remove(ga.id, ga.guild_id);
}

// Register giveaway button handler
inline void register_giveaway_interactions(dpp::cluster& bot, Database* db) {
    // Reload active giveaways from DB on startup (restart recovery)
    reload_giveaways_from_db(db);
    
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        std::string cid = event.custom_id;
        if (cid.rfind("giveaway_enter_", 0) != 0) return;
        
        uint64_t giveaway_id = 0;
        try {
            giveaway_id = std::stoull(cid.substr(15));
        } catch (...) {
            event.reply(dpp::message().add_embed(bronx::error("invalid giveaway")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        uint64_t user_id = static_cast<uint64_t>(event.command.get_issuing_user().id);
        
        // Check if giveaway is still active via cache
        auto ga_opt = giveaway_cache.get(giveaway_id);
        if (!ga_opt.has_value() || ga_opt->ended) {
            event.reply(dpp::message().add_embed(bronx::error("this giveaway has ended")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        // Try to enter
        bool entered = db->enter_giveaway(giveaway_id, user_id);
        if (entered) {
            event.reply(dpp::message().add_embed(bronx::success("you've entered the giveaway! 🎉")).set_flags(dpp::m_ephemeral));
        } else {
            event.reply(dpp::message().add_embed(bronx::info("you've already entered this giveaway")).set_flags(dpp::m_ephemeral));
        }
    });
    
    // Timer to check and end giveaways
    bot.start_timer([&bot, db](dpp::timer t) {
        auto now = std::chrono::system_clock::now();
        auto all = giveaway_cache.get_all_active();
        
        for (const auto& ga : all) {
            if (!ga.ended && now >= ga.ends_at) {
                end_giveaway(bot, db, ga.id);
            }
        }
    }, 10); // Check every 10 seconds
}

inline std::vector<Command*> get_giveaway_commands(Database* db) {
    static std::vector<Command*> cmds;
    static bool initialized = false;
    
    if (!initialized) {
        // Giveaway command with subcommands
        static Command* giveaway = new Command("giveaway", "manage server giveaways", "utility", {"ga"}, true,
            [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
                if (!event.msg.guild_id) {
                    bronx::send_message(bot, event, bronx::error("this command must be used in a server"));
                    return;
                }
                
                if (args.empty()) {
                    std::string help = "**giveaway commands:**\n";
                    help += "`giveaway create <prize> <duration> [winners]` — create a giveaway\n";
                    help += "`giveaway list` — view active giveaways\n";
                    help += "`giveaway end <id>` — end a giveaway early\n";
                    help += "`giveaway reroll <id>` — reroll winners\n";
                    bronx::send_message(bot, event, bronx::info(help));
                    return;
                }
                
                std::string sub = args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                
                // ── giveaway create <prize> <duration> [winners] ──
                if (sub == "create" || sub == "start" || sub == "new") {
                    if (!member_can_manage_guild(event.msg.guild_id, event.msg.member)) {
                        bronx::send_message(bot, event, bronx::error("you need Manage Server permission to create giveaways"));
                        return;
                    }
                    
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: giveaway create <prize> <duration> [winners]\ne.g. `giveaway create 10000 1h 3`"));
                        return;
                    }
                    
                    // Parse prize
                    int64_t prize = 0;
                    try {
                        prize = parse_amount(args[1], INT64_MAX);
                    } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid prize amount"));
                        return;
                    }
                    
                    if (prize <= 0) {
                        bronx::send_message(bot, event, bronx::error("prize must be greater than 0"));
                        return;
                    }
                    
                    // Parse duration
                    int duration = parse_duration_seconds(args[2]);
                    if (duration <= 0) {
                        bronx::send_message(bot, event, bronx::error("invalid duration. use: 1h, 30m, 1d, etc."));
                        return;
                    }
                    if (duration > 604800) { // 7 days max
                        bronx::send_message(bot, event, bronx::error("maximum duration is 7 days"));
                        return;
                    }
                    
                    // Parse max winners
                    int max_winners = 1;
                    if (args.size() >= 4) {
                        try {
                            max_winners = std::stoi(args[3]);
                        } catch (...) {
                            bronx::send_message(bot, event, bronx::error("invalid winner count"));
                            return;
                        }
                    }
                    if (max_winners < 1 || max_winners > 20) {
                        bronx::send_message(bot, event, bronx::error("winner count must be between 1 and 20"));
                        return;
                    }
                    
                    // Check guild treasury balance
                    int64_t treasury = db->get_guild_balance(event.msg.guild_id);
                    if (treasury < prize) {
                        bronx::send_message(bot, event, bronx::error(
                            "insufficient treasury balance\n"
                            "treasury: $" + format_number(treasury) + "\n"
                            "required: $" + format_number(prize)));
                        return;
                    }
                    
                    // Deduct from treasury
                    server_economy_operations::add_to_guild_balance(db, event.msg.guild_id, -prize);
                    
                    // Create giveaway in database
                    uint64_t giveaway_id = db->create_giveaway(
                        event.msg.guild_id,
                        event.msg.channel_id,
                        event.msg.author.id,
                        prize,
                        max_winners,
                        duration
                    );
                    
                    if (giveaway_id == 0) {
                        // Refund treasury
                        server_economy_operations::add_to_guild_balance(db, event.msg.guild_id, prize);
                        bronx::send_message(bot, event, bronx::error("failed to create giveaway"));
                        return;
                    }
                    
                    // Create cached tracking
                    ActiveGiveaway ga;
                    ga.id = giveaway_id;
                    ga.guild_id = event.msg.guild_id;
                    ga.channel_id = event.msg.channel_id;
                    ga.message_id = 0;
                    ga.created_by = event.msg.author.id;
                    ga.prize = prize;
                    ga.max_winners = max_winners;
                    ga.ends_at = std::chrono::system_clock::now() + std::chrono::seconds(duration);
                    ga.ended = false;
                    
                    // Send giveaway message
                    auto msg = build_giveaway_message(ga);
                    msg.channel_id = event.msg.channel_id;
                    
                    bot.message_create(msg, [ga](const dpp::confirmation_callback_t& cb) mutable {
                        if (!cb.is_error()) {
                            auto created_msg = std::get<dpp::message>(cb.value);
                            ga.message_id = created_msg.id;
                            giveaway_cache.store(ga);
                        }
                    });
                    return;
                }
                
                // ── giveaway list ──
                if (sub == "list" || sub == "active") {
                    auto guild_gas = giveaway_cache.get_guild_active(event.msg.guild_id);
                    std::string desc = "**active giveaways:**\n\n";
                    bool found = false;
                    
                    for (const auto& ga : guild_gas) {
                        found = true;
                        auto time_left = std::chrono::duration_cast<std::chrono::seconds>(
                            ga.ends_at - std::chrono::system_clock::now()).count();
                        desc += "**ID " + std::to_string(ga.id) + "** — $" + format_number(ga.prize);
                        desc += " — " + std::to_string(ga.max_winners) + " winner(s)";
                        desc += " — " + format_duration(std::max(0L, time_left)) + " left\n";
                    }
                    
                    if (!found) {
                        desc = "no active giveaways in this server";
                    }
                    
                    bronx::send_message(bot, event, bronx::info(desc));
                    return;
                }
                
                // ── giveaway end <id> ──
                if (sub == "end" || sub == "stop") {
                    if (!member_can_manage_guild(event.msg.guild_id, event.msg.member)) {
                        bronx::send_message(bot, event, bronx::error("you need Manage Server permission to end giveaways"));
                        return;
                    }
                    
                    if (args.size() < 2) {
                        bronx::send_message(bot, event, bronx::error("usage: giveaway end <id>"));
                        return;
                    }
                    
                    uint64_t giveaway_id = 0;
                    try {
                        giveaway_id = std::stoull(args[1]);
                    } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid giveaway id"));
                        return;
                    }
                    
                    {
                        auto ga_opt = giveaway_cache.get(giveaway_id);
                        if (!ga_opt.has_value() || ga_opt->guild_id != static_cast<uint64_t>(event.msg.guild_id)) {
                            bronx::send_message(bot, event, bronx::error("giveaway not found"));
                            return;
                        }
                    }
                    
                    end_giveaway(bot, db, giveaway_id);
                    bronx::send_message(bot, event, bronx::success("giveaway ended"));
                    return;
                }
                
                // ── giveaway reroll <id> ──
                if (sub == "reroll") {
                    if (!member_can_manage_guild(event.msg.guild_id, event.msg.member)) {
                        bronx::send_message(bot, event, bronx::error("you need Manage Server permission to reroll giveaways"));
                        return;
                    }
                    
                    if (args.size() < 2) {
                        bronx::send_message(bot, event, bronx::error("usage: giveaway reroll <id>"));
                        return;
                    }
                    
                    uint64_t giveaway_id = 0;
                    try {
                        giveaway_id = std::stoull(args[1]);
                    } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid giveaway id"));
                        return;
                    }
                    
                    // Get entries and pick new winners
                    auto entries = db->get_giveaway_entries(giveaway_id);
                    if (entries.empty()) {
                        bronx::send_message(bot, event, bronx::error("no entries found for this giveaway"));
                        return;
                    }
                    
                    auto winners = pick_winners(entries, 1); // Reroll picks 1 new winner
                    if (winners.empty()) {
                        bronx::send_message(bot, event, bronx::error("could not pick a new winner"));
                        return;
                    }
                    
                    std::string desc = "🎲 **rerolled winner:**\n";
                    desc += "🏆 <@" + std::to_string(winners[0]) + ">";
                    bronx::send_message(bot, event, bronx::success(desc));
                    return;
                }
                
                bronx::send_message(bot, event, bronx::error("unknown subcommand: " + sub));
            },
            [](dpp::cluster&, const dpp::slashcommand_t&) {},
            {}
        );
        cmds.push_back(giveaway);
        
        // Treasury command
        static Command* treasury_cmd = new Command("treasury", "view server giveaway treasury balance", "utility", {"guildbalance", "serverbalance"}, true,
            [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
                if (!event.msg.guild_id) {
                    bronx::send_message(bot, event, bronx::error("this command must be used in a server"));
                    return;
                }
                
                int64_t balance = db->get_guild_balance(event.msg.guild_id);
                
                std::string desc = "🏦 **server treasury**\n\n";
                desc += "**balance:** $" + format_number(balance) + "\n\n";
                desc += "*treasury is funded by transaction taxes and donations*";
                
                bronx::send_message(bot, event, bronx::info(desc));
            },
            [](dpp::cluster&, const dpp::slashcommand_t&) {},
            {}
        );
        cmds.push_back(treasury_cmd);
        
        // Payout command
        static Command* payout_cmd = new Command("payout", "pay a user from the server treasury", "utility", {"serverpay"}, true,
            [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
                if (!event.msg.guild_id) {
                    bronx::send_message(bot, event, bronx::error("this command must be used in a server"));
                    return;
                }
                
                if (!member_can_manage_guild(event.msg.guild_id, event.msg.member)) {
                    bronx::send_message(bot, event, bronx::error("you need Manage Server permission to use treasury payouts"));
                    return;
                }
                
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: payout <@user> <amount>"));
                    return;
                }
                
                // Parse user mention
                uint64_t target_id = 0;
                std::regex mention_re("<@!?(\\d+)>");
                std::smatch m;
                if (std::regex_search(args[0], m, mention_re)) {
                    target_id = std::stoull(m[1].str());
                } else {
                    try {
                        target_id = std::stoull(args[0]);
                    } catch (...) {
                        bronx::send_message(bot, event, bronx::error("invalid user mention or id"));
                        return;
                    }
                }
                
                // Parse amount
                int64_t amount = 0;
                try {
                    amount = parse_amount(args[1], INT64_MAX);
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid amount"));
                    return;
                }
                
                if (amount <= 0) {
                    bronx::send_message(bot, event, bronx::error("amount must be greater than 0"));
                    return;
                }
                
                // Check treasury balance
                int64_t treasury = db->get_guild_balance(event.msg.guild_id);
                if (treasury < amount) {
                    bronx::send_message(bot, event, bronx::error(
                        "insufficient treasury balance\n"
                        "treasury: $" + format_number(treasury) + "\n"
                        "requested: $" + format_number(amount)));
                    return;
                }
                
                // Deduct from treasury and add to user
                server_economy_operations::add_to_guild_balance(db, event.msg.guild_id, -amount);
                db->update_wallet(target_id, amount);
                
                std::string desc = "💸 **treasury payout**\n\n";
                desc += "<@" + std::to_string(target_id) + "> received $" + format_number(amount) + " from the server treasury";
                bronx::send_message(bot, event, bronx::success(desc));
            },
            [](dpp::cluster&, const dpp::slashcommand_t&) {},
            {}
        );
        cmds.push_back(payout_cmd);
        
        initialized = true;
    }
    
    return cmds;
}

} // namespace utility
} // namespace commands
