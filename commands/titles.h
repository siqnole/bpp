#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "economy_core.h"
#include "title_utils.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <optional>
#include <algorithm>
#include <chrono>

// ============================================================
//  TITLE CATALOG
//  Add new titles here.  Each entry needs:
//    item_id       – unique key stored in the DB inventory
//    display       – shown next to the user on the leaderboard
//                    (supports plain text or Discord markdown,
//                     e.g. "**[Legend]**" or "🎣 Angler")
//    shop_desc     – one-liner shown in the shop
//    price         – cost in bronx dollars
//    rotation_slot – 0 = always in shop
//                    1, 2, 3 … = rotates weekly;
//                    slot (UTC_week % NUM_ROTATION_SLOTS + 1) is live
// ============================================================

namespace commands {

static constexpr int TITLE_ROTATION_SLOTS = 4; // how many rotation groups

struct TitleDef {
    std::string item_id;
    std::string display;    // leaderboard / equip display string
    std::string shop_desc;
    int64_t     price;
    int         rotation_slot; // 0 = permanent, 1-N = rotating
    int         purchase_limit; // 0 = unlimited, N = only N copies ever sold server-wide
};

// ----------------------------------------------------------------
//  ★  ADD TITLES HERE  ★
// ----------------------------------------------------------------
inline const std::vector<TitleDef>& title_catalog() {
    static const std::vector<TitleDef> catalog = {
        // rotation_slot 0 = always listed, purchase_limit 0 = unlimited
        { "title_billionaire", "💎 Billionaire",          "baller status, flex with pride",       1000000000, 0, 0 },
        { "title_goat",        "🐐 GOAT",                "greatest of all time",                  50000000000, 0, 0 },
        { "title_angler",      "🎣 Angler",             "for those who live by the rod",         50000000,  0, 0 },
        { "title_hustler",     "💰 Hustler",             "always on the grind",                   800000,  0, 0 },
        { "title_fisherking",   "👑 Fisher King",        "reigns over the fishing leaderboard, carol baskinK",   20000000, 0, 0 },
        { "title_legendaryrod","legendary rod owner",    "owns a legendary rod, lets get flexy",     100000000, 0, 0 },
        { "title_og",          "OG",                    "early supporter and all-around legend", 2500000000, 0, 100 },
        { "title_veteran",      "Veteran",              "been around the block a few times",      100000000, 0, 0 },
        { "title_rich",        "Rich",                  "rich enough to not care about titles",  500000000, 0, 0 },
        { "title_fisherman",   "Fisherman",             "lives for the thrill of the catch",     10000000, 0, 0 },
        { "title_kingpin",     "Kingpin",              "rules the gambling scene with an iron fist",    150000000, 0, 0 },
        { "title_entrepreneur","Entrepreneur",          "always looking for the next big opportunity",   75000000, 0, 0 },
        { "title_bigspender",   "Big Spender",          "spends money like it's going out of style",     300000000, 0, 0 },
        { "title_sharkfin",     "Shark Fin",            "owns a shark fin, flexes on everyone at the beach",     25000000, 0, 0 },

        // automatically awarded titles (not purchasable)
        { "title_global_top10", "🌍 Top 10 Global",      "granted daily to the top-10 global networth users", 0, 0, 0 },        
        { "title_earlygrinder", "🏋️ Early Grinder",     "awarded to early prestige players", 0, 0, 0 },
        { "title_betatester",     "🧪 Beta Tester",       "awarded to users who helped test and improve the bot", 0, 0, 0 },        { "title_fishforever",    "🎣 Fish Forever",      "awarded for catching every single fish in existence", 0, 0, 0 },        // rebirth titles (awarded automatically on rebirth)
        { "title_reborn",          "🔄 Reborn",            "completed rebirth I",       0, 0, 0 },
        { "title_twice_reborn",    "🔄 Twice Reborn",      "completed rebirth II",      0, 0, 0 },
        { "title_thrice_reborn",   "🔄 Thrice Reborn",     "completed rebirth III",     0, 0, 0 },
        { "title_ascended",        "🔄 Ascended",          "completed rebirth IV",      0, 0, 0 },
        { "title_transcendent",    "🔄 Transcendent",      "completed rebirth V",       0, 0, 0 },        // LIMITED titles – purchase_limit > 0, gone forever once all copies are claimed
        { "title_first",       "🥇 First",               "first in line, first in life — only 1 ever",      999999999,  0, 1  },
        { "title_femboy",     "*femboy :3*",                "got my programmer socks on rn",       50000000,   0, 10 },
        { "title_chad",       "***chad***",                     "stare directly into the sun with these shades on, 10 copies only",       50000000,   0, 10 },
        { "title_chud",      "*chud*",                     "lives in the sewers, only 10 copies",       50000000,   0, 10 },
        { "title_shy", "||*im shy*||",               "too shy to show their title off, only 10 copies", 50000000, 0, 10 },
        { "title_rarest",      "💠 Ultra Rare",          "extremely limited edition, only 3 copies",        500000000,  0, 3  },
        { "title_chosen",      "⚡ The Chosen",          "not everyone can have this — only 5 exist",       250000000,  0, 5  },
        { "title_certified",   "✅ Certified",           "certified bronx member, limited to 10",           75000000,   0, 10 },
        { "title_early",       "🌅 Early Bird",          "got here before everyone else — 15 copies only",  30000000,   0, 15 },

        // rotation_slot 1 – week group A
        { "title_legend",      "**[Legend]**",           "a true bronx legend",                   250000000, 1, 0 },
        { "title_shark",       "🦈 Shark",               "the loan shark of the server",          2000000,   1, 0 },
        { "title_hateks",      "i hate ks",              "self-explanatory",                      750000,    1, 50 },
        { "title_loveks",     "i love ks",              "also self-explanatory",                      750000,    1, 50 },

        // rotation_slot 2 – week group B
        { "title_gambler",     "🎲 High Roller",         "wins big, loses bigger",                1500000,   2, 0 },
        { "title_whale",       "🐋 Whale",               "drops stacks without blinking",         3000000,   2, 0 },
        { "title_infinityrod", "i have infinity rod",    "flex responsibly",                      2500000,   2, 0 },

        // rotation_slot 3 – week group C
        { "title_peasant",     "🪣 Peasant",             "broke but proud",                       500000,    3, 0 },
        { "title_ghost",       "👻 Ghost",               "never seen, always here",               1200000,   3, 0 },
        { "title_lv5gang",     "lv5 gang",               "maxed out and proud of it",             1000000,   3, 0 },

        // rotation_slot 4 – week group D
        { "title_divine",      "✨ **Divine**",          "touched by something greater",          5000000,   4, 0 },
        { "title_suspect",     "🕵️ Suspect",             "questionable activity detected",        18000,     4, 0 },
        { "title_commonpi",    "common > pi",            "controversial opinion, correct take",   600000,    4, 0 },
    };
    return catalog;
}
// ----------------------------------------------------------------

// Look up a title definition by item_id using only the static catalog.
// Accepts both the canonical form ("title_angler") and the short form ("angler").
inline std::optional<TitleDef> find_title_static(const std::string& item_id) {
    for (const auto& t : title_catalog()) {
        if (t.item_id == item_id) return t;
    }
    // Try prepending "title_" to support short-form IDs
    std::string full_id = "title_" + item_id;
    for (const auto& t : title_catalog()) {
        if (t.item_id == full_id) return t;
    }
    return {};
}

// helper to load any dynamic titles stored in the database (category='title').
inline std::vector<TitleDef> load_dynamic_titles(bronx::db::Database* db) {
    if (!db) return {};
    return db->get_dynamic_titles();
}

// Same as above, but also searches dynamic titles stored in the database.
inline std::optional<TitleDef> find_title(bronx::db::Database* db, const std::string& item_id) {
    // first check static list
    auto st = find_title_static(item_id);
    if (st) return st;
    if (db) {
        for (auto& t : load_dynamic_titles(db)) {
            if (t.item_id == item_id) return t;
            if (t.item_id == "title_" + item_id) return t;
        }
    }
    return {};
}

// backward-compat wrapper that only checks static titles (preserves old
// calls that don't know about the database)
inline std::optional<TitleDef> find_title(const std::string& item_id) {
    return find_title_static(item_id);
}

// Return the current UTC week number (used for rotation)
inline int current_utc_week() {
    using namespace std::chrono;
    auto now = system_clock::now();
    time_t t  = system_clock::to_time_t(now);
    // ISO week: days since epoch / 7
    return static_cast<int>(t / (60 * 60 * 24 * 7));
}

// Titles available in the shop right now (permanent + this week's slot)
inline std::vector<TitleDef> get_available_titles() {
    int live_slot = (current_utc_week() % TITLE_ROTATION_SLOTS) + 1;
    std::vector<TitleDef> out;
    for (const auto& t : title_catalog()) {
        if (t.rotation_slot == 0 || t.rotation_slot == live_slot) {
            out.push_back(t);
        }
    }
    return out;
}

// Returns true if a limited title has sold out (purchase_limit > 0 and all copies taken).
// Pass nullptr for db to skip the check (always returns false).
inline bool is_title_sold_out(bronx::db::Database* db, const TitleDef& def) {
    if (def.purchase_limit <= 0) return false;
    if (!db) return false;
    return db->count_item_owners(def.item_id) >= def.purchase_limit;
}


// Titles available right now with sold-out limited titles removed.
// Requires a live db pointer to check purchase counts.
inline std::vector<TitleDef> get_available_titles(bronx::db::Database* db) {
    int live_slot = (current_utc_week() % TITLE_ROTATION_SLOTS) + 1;
    std::vector<TitleDef> out;
    // static catalog
    for (const auto& t : title_catalog()) {
        if (t.rotation_slot != 0 && t.rotation_slot != live_slot) continue;
        if (is_title_sold_out(db, t)) continue;
        out.push_back(t);
    }
    // dynamic entries from DB
    for (auto& t : load_dynamic_titles(db)) {
        if (t.rotation_slot != 0 && t.rotation_slot != live_slot) continue;
        if (is_title_sold_out(db, t)) continue;
        out.push_back(t);
    }
    return out;
}

// title_display_to_json is now in title_utils.h (included above)

// Get the display string for a user's equipped title (empty if none).
// Metadata is stored as {"display":"VALUE"} (valid JSON).  Pre-existing rows
// that still hold the raw display string are returned as-is (legacy fallback).
inline std::string get_equipped_title_display(bronx::db::Database* db, uint64_t user_id) {
    auto inv = db->get_inventory(user_id);
    for (const auto& it : inv) {
        if (it.item_id == "active_title" && !it.metadata.empty()) {
            // New format: {"display":"VALUE"}
            static const std::string kPrefix = "{\"display\":\"";
            static const std::string kSuffix = "\"}";
            if (it.metadata.size() > kPrefix.size() + kSuffix.size() &&
                it.metadata.rfind(kPrefix, 0) == 0 &&
                it.metadata.compare(it.metadata.size() - kSuffix.size(),
                                    kSuffix.size(), kSuffix) == 0) {
                std::string val = it.metadata.substr(
                    kPrefix.size(),
                    it.metadata.size() - kPrefix.size() - kSuffix.size());
                return val; // may be empty string = no title equipped
            }
            // Legacy fallback: metadata is the raw display string
            return it.metadata;
        }
    }
    return "";
}

// Equip a title the user owns.  Returns error string or empty on success.
// Accepts both the canonical form ("title_angler") and the short form ("angler").
inline std::string equip_title(bronx::db::Database* db, uint64_t user_id, const std::string& item_id) {
    // Resolve canonical definition first so we always use the full item_id
    auto def = find_title(db, item_id);
    if (!def) return "unknown title";
    if (!db->has_item(user_id, def->item_id)) {
        return "you don't own that title";
    }

    // Upsert active_title slot — metadata must be valid JSON for MariaDB's
    // JSON column constraint, so encode the display string as {"display":"VALUE"}.
    std::string meta = title_display_to_json(def->display);
    if (db->has_item(user_id, "active_title")) {
        db->update_item_metadata(user_id, "active_title", meta);
    } else {
        db->add_item(user_id, "active_title", "title_slot", 1, meta, 1);
        // Always update metadata after add_item in case row existed with qty=0
        // (add_item's UPDATE path doesn't update metadata on existing rows)
        db->update_item_metadata(user_id, "active_title", meta);
    }
    return "";
}

// ----------------------------------------------------------------
//  Command handler
// ----------------------------------------------------------------
inline std::vector<Command*> get_title_commands(bronx::db::Database* db) {
    static std::vector<Command*> cmds;

    // title / titles – view owned titles + equip
    static Command* titles_cmd = new Command(
        "title",
        "view and equip your titles",
        "cosmetics",
        {"titles", "mytitles"},
        true,
        // ── text handler ──────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;

            // title equip <item_id>
            if (!args.empty()) {
                std::string sub = args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                if (sub == "equip" || sub == "set" || sub == "use") {
                    if (args.size() < 2) {
                        bronx::send_message(bot, event, bronx::error("usage: `title equip <item_id>`"));
                        return;
                    }
                    std::string err = equip_title(db, uid, args[1]);
                    if (!err.empty()) {
                        bronx::send_message(bot, event, bronx::error(err));
                        return;
                    }
                    auto def = find_title(db, args[1]);
                    bronx::send_message(bot, event,
                        bronx::success("title equipped: " + def->display));
                    return;
                }

                if (sub == "remove" || sub == "clear" || sub == "unequip") {
                    if (db->has_item(uid, "active_title")) {
                        // simply remove the inventory entry entirely; the metadata
                        // is no longer needed and this keeps inventories clean.
                        db->remove_item(uid, "active_title", 1);
                    }
                    bronx::send_message(bot, event, bronx::success("title removed"));
                    return;
                }
            }

            // Default: list owned titles
            auto inv = db->get_inventory(uid);
            std::string equipped = get_equipped_title_display(db, uid);

            std::string description = "**your titles**\n\n";
            bool found = false;
            for (const auto& inv_item : inv) {
                // Match by item_type OR by item_id prefix (handles legacy rows
                // that were stored with a non-"title" type).
                bool is_title_item = (inv_item.item_type == "title") ||
                                     (inv_item.item_id.rfind("title_", 0) == 0);
                if (!is_title_item) continue;
                if (inv_item.item_id == "active_title") continue;
                found = true;
                auto def = find_title(db, inv_item.item_id);
                std::string disp = def ? def->display : inv_item.item_id;
                bool is_active = !equipped.empty() && def && def->display == equipped;
                description += (is_active ? "▶ " : "  ") + disp;
                description += "  `" + inv_item.item_id + "`";
                if (is_active) description += " *(equipped)*";
                description += "\n";
            }
            if (!found) {
                description += "*you don't own any titles yet – buy one in the shop!*\n";
            }
            description += "\n`title equip <item_id>` to equip   `title remove` to unequip";

            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        // ── slash handler ─────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;

            std::string action;
            try {
                auto p = event.get_parameter("action");
                if (std::holds_alternative<std::string>(p)) action = std::get<std::string>(p);
            } catch (...) {}

            std::string item_id;
            try {
                auto p = event.get_parameter("item_id");
                if (std::holds_alternative<std::string>(p)) item_id = std::get<std::string>(p);
            } catch (...) {}

            if (action == "equip" && !item_id.empty()) {
                std::string err = equip_title(db, uid, item_id);
                if (!err.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error(err)));
                    return;
                }
                auto def = find_title(db, item_id);
                event.reply(dpp::message().add_embed(
                    bronx::success("title equipped: " + (def ? def->display : item_id))));
                return;
            }

            if (action == "remove") {
                if (db->has_item(uid, "active_title"))
                    db->remove_item(uid, "active_title", 1);
                event.reply(dpp::message().add_embed(bronx::success("title removed")));
                return;
            }

            // List owned titles
            auto inv = db->get_inventory(uid);
            std::string equipped = get_equipped_title_display(db, uid);
            std::string description = "**your titles**\n\n";
            bool found = false;
            for (const auto& inv_item : inv) {
                bool is_title_item = (inv_item.item_type == "title") ||
                                     (inv_item.item_id.rfind("title_", 0) == 0);
                if (!is_title_item) continue;
                if (inv_item.item_id == "active_title") continue;
                found = true;
                auto def = find_title(db, inv_item.item_id);
                std::string disp = def ? def->display : inv_item.item_id;
                bool is_active = !equipped.empty() && def && def->display == equipped;
                description += (is_active ? "▶ " : "  ") + disp + "  `" + inv_item.item_id + "`";
                if (is_active) description += " *(equipped)*";
                description += "\n";
            }
            if (!found) description += "*no titles yet – buy one in the shop!*\n";
            description += "\n`/title action:equip item_id:<id>` to equip";

            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "action", "equip or remove", false)
                .add_choice(dpp::command_option_choice("equip", "equip"))
                .add_choice(dpp::command_option_choice("remove", "remove")),
            dpp::command_option(dpp::co_string, "item_id", "title item id to equip", false)
        }
    );
    cmds.push_back(titles_cmd);
    return cmds;
}

} // namespace commands
