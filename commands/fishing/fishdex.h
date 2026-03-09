#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "../achievements.h"
#include "fishing_helpers.h"
#include "simple_commands.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <sstream>

using namespace bronx::db;

namespace commands {
namespace fishing {

// ── category definitions ─────────────────────────────────────────────────────

static const std::vector<std::string> FISHDEX_CATEGORY_ORDER = {
    "Common", "Uncommon", "Rare", "Epic", "Legendary",
    "🌸 Spring", "☀️ Summer", "🍂 Fall", "❄️ Winter",
    "Special (Math/Dev/Shrek)",
    "Low Prestige (P1-P4)", "Mid Prestige (P5-P9)",
    "High Prestige (P10-P19)", "Ultimate (P20)"
};

// Map a FishType to its category string
inline std::string fish_category(const FishType& fish) {
    if (fish.season != Season::AllYear) {
        switch (fish.season) {
            case Season::Spring: return "🌸 Spring";
            case Season::Summer: return "☀️ Summer";
            case Season::Fall:   return "🍂 Fall";
            case Season::Winter: return "❄️ Winter";
            default: break;
        }
    }
    if (fish.min_gear_level >= 20) return "Ultimate (P20)";
    if (fish.min_gear_level >= 15) return "High Prestige (P10-P19)";
    if (fish.min_gear_level >= 10) return "Mid Prestige (P5-P9)";
    if (fish.min_gear_level >= 7)  return "Low Prestige (P1-P4)";
    if (fish.min_gear_level >= 6)  return "Special (Math/Dev/Shrek)";
    if (fish.min_gear_level >= 4)  return "Legendary";
    if (fish.min_gear_level >= 3)  return "Epic";
    if (fish.min_gear_level >= 2)  return "Rare";
    if (fish.min_gear_level >= 1)  return "Uncommon";
    return "Common";
}

// Case-insensitive category name → index, -1 if not found.
// Accepts plain names without emoji prefix (e.g. "spring" → slot for "🌸 Spring")
// and without parenthetical suffixes (e.g. "low prestige" → "Low Prestige (P1-P4)").
inline int resolve_category_slot(const std::string& input) {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    auto strip_prefix = [](const std::string& cat) -> std::string {
        size_t pos = 0;
        while (pos < cat.size() && (unsigned char)cat[pos] > 127) ++pos;
        while (pos < cat.size() && cat[pos] == ' ') ++pos;
        return cat.substr(pos);
    };
    auto strip_paren = [](const std::string& s) -> std::string {
        size_t p = s.rfind('(');
        if (p != std::string::npos && p > 0) {
            size_t trim = p;
            while (trim > 0 && s[trim - 1] == ' ') --trim;
            return s.substr(0, trim);
        }
        return s;
    };

    std::string in = lower(input);
    for (int i = 0; i < (int)FISHDEX_CATEGORY_ORDER.size(); ++i) {
        const std::string& cat = FISHDEX_CATEGORY_ORDER[i];
        if (lower(cat) == in) return i;
        std::string stripped = strip_prefix(cat);
        if (lower(stripped) == in) return i;
        if (lower(strip_paren(stripped)) == in) return i;
        if (lower(strip_paren(cat)) == in) return i;
    }
    return -1;
}

// ── inventory helpers ─────────────────────────────────────────────────────────

// Returns map<fish_name, total_times_caught> from fish_catches history (survives prestige)
inline std::map<std::string, int> get_caught_fish_counts(Database* db, uint64_t user_id) {
    std::map<std::string, int> counts;
    for (const auto& kv : db->get_fish_catch_counts_by_species(user_id))
        counts[kv.first] = (int)kv.second;
    return counts;
}

// Backward-compatible unique-set variant used by achievement check
inline std::set<std::string> get_caught_fish(Database* db, uint64_t user_id) {
    std::set<std::string> out;
    for (const auto& kv : get_caught_fish_counts(db, user_id)) out.insert(kv.first);
    return out;
}

// ── progress helpers ──────────────────────────────────────────────────────────

inline int calc_pct(int got, int total) {
    return total > 0 ? (got * 100) / total : 0;
}

inline std::string progress_bar(int pct, int len = 18) {
    int filled = (pct * len) / 100;
    std::string bar = "[";
    for (int i = 0; i < len; ++i) bar += (i < filled ? "█" : "░");
    return bar + "]";
}

// ── achievements & title award ────────────────────────────────────────────────

inline void check_fishdex_achievements(dpp::cluster& bot, Database* db,
                                        const dpp::snowflake& channel_id,
                                        uint64_t user_id, int pct) {
    if (pct >= 10) achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "fishdex_10");
    if (pct >= 25) achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "fishdex_25");
    if (pct >= 50) achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "fishdex_50");
    if (pct >= 75) achievements::check_achievements_for_stat(bot, db, channel_id, user_id, "fishdex_75");
    if (pct >= 100 && !db->has_item(user_id, "title_fishforever")) {
        db->add_item(user_id, "title_fishforever", "title", 1, "{}", 1);
        bot.message_create(dpp::message(channel_id,
            "<@" + std::to_string(user_id) + "> **LEGENDARY ACHIEVEMENT UNLOCKED!**\n"
            "🎣 **FISH FOREVER** 🎣\n"
            "*You've caught every single fish in existence! The title 'Fish Forever' is now yours!*"
        ));
    }
}

// ── dropdown helper ───────────────────────────────────────────────────────────

// Build a category select-menu component row.
// current_slot = -1 for overview (no default selected).
inline dpp::component build_fishdex_dropdown(uint64_t uid, int current_slot) {
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("jump to a category…")
        .set_id("fdex_cat_" + std::to_string(uid));

    for (int i = 0; i < (int)FISHDEX_CATEGORY_ORDER.size(); ++i) {
        dpp::select_option opt(FISHDEX_CATEGORY_ORDER[i], std::to_string(i));
        if (i == current_slot) opt.set_default(true);
        select_menu.add_select_option(opt);
    }

    dpp::component row;
    row.add_component(select_menu);
    return row;
}

// ── message builders ──────────────────────────────────────────────────────────

// Overview: overall progress + per-category summary + category dropdown
inline dpp::message build_fishdex_overview(Database* db, uint64_t uid) {
    auto counts = get_caught_fish_counts(db, uid);

    std::set<std::string> all_names;
    for (const auto& f : fish_types) all_names.insert(f.name);
    int total  = all_names.size();
    int got    = 0;
    for (const auto& n : all_names) if (counts.count(n)) got++;
    int pct = calc_pct(got, total);

    std::string desc = "🎣 **FISHDEX**\n\n";
    desc += "**Collection:** " + std::to_string(got) + "/" + std::to_string(total) +
            " species (" + std::to_string(pct) + "%)\n";
    desc += progress_bar(pct) + "\n\n";

    desc += "**Milestones:**\n";
    for (int thr : {10, 25, 50, 75, 100}) {
        bool done = pct >= thr;
        desc += (done ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY);
        desc += " " + std::to_string(thr) + "%";
        if (thr == 100) desc += " — **FISH FOREVER**";
        if (!done) {
            int need = (total * thr) / 100 - got;
            if (need > 0) desc += " (" + std::to_string(need) + " more)";
        }
        desc += "\n";
    }
    desc += "\n";

    desc += "**Categories** — select below or use `fishdex <category>`:\n";
    std::map<std::string, std::pair<int,int>> cat_stats;
    for (const auto& f : fish_types) {
        std::string cat = fish_category(f);
        cat_stats[cat].second++;
        if (counts.count(f.name)) cat_stats[cat].first++;
    }
    for (const auto& cat : FISHDEX_CATEGORY_ORDER) {
        auto it = cat_stats.find(cat);
        if (it == cat_stats.end()) continue;
        int c = it->second.first, t = it->second.second;
        desc += "**" + cat + "** — " + std::to_string(c) + "/" + std::to_string(t) +
                " (" + std::to_string(calc_pct(c, t)) + "%)\n";
    }

    dpp::message msg;
    msg.add_embed(bronx::create_embed(desc));
    msg.add_component(build_fishdex_dropdown(uid, -1));
    return msg;
}

// Paginated category detail — 12 fish per page, each with catch count.
// Button ID format: fdex_pg_{page}_{uid}_{catslot}_{dir}
// Nav buttons always present; next on last page wraps to next category, prev on
// first page wraps to last page of the previous category.
inline dpp::message build_fishdex_category_page(Database* db, uint64_t uid,
                                                  int cat_slot, int page) {
    const int PER_PAGE = 12;
    const int NUM_CATS = (int)FISHDEX_CATEGORY_ORDER.size();

    if (cat_slot < 0 || cat_slot >= NUM_CATS)
        return dpp::message().add_embed(bronx::error("unknown category"));

    const std::string& cat_name = FISHDEX_CATEGORY_ORDER[cat_slot];

    std::vector<const FishType*> cat_fish;
    for (const auto& f : fish_types)
        if (fish_category(f) == cat_name) cat_fish.push_back(&f);

    if (cat_fish.empty())
        return dpp::message().add_embed(bronx::error("no fish in that category"));

    auto counts = get_caught_fish_counts(db, uid);

    int total_in_cat = cat_fish.size();
    int caught_in_cat = 0;
    for (const auto* f : cat_fish) if (counts.count(f->name)) caught_in_cat++;
    int cat_pct = calc_pct(caught_in_cat, total_in_cat);

    int total_pages = (total_in_cat + PER_PAGE - 1) / PER_PAGE;
    if (page < 0) page = 0;
    if (page >= total_pages) page = total_pages - 1;

    int start = page * PER_PAGE;
    int end   = std::min(start + PER_PAGE, total_in_cat);

    // Determine adjacent categories for wrap label hints
    int prev_cat = (cat_slot - 1 + NUM_CATS) % NUM_CATS;
    int next_cat = (cat_slot + 1) % NUM_CATS;

    std::string desc = "🎣 **FISHDEX — " + cat_name + "**\n";
    desc += std::to_string(caught_in_cat) + "/" + std::to_string(total_in_cat) +
            " caught (" + std::to_string(cat_pct) + "%)  " +
            progress_bar(cat_pct, 14) + "\n";
    desc += "*page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + "*\n\n";

    for (int i = start; i < end; ++i) {
        const FishType& f = *cat_fish[i];
        int cnt = 0;
        auto it = counts.find(f.name);
        if (it != counts.end()) cnt = it->second;

        bool is_caught = cnt > 0;
        desc += (is_caught ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + std::string(" ");
        desc += f.emoji + " ";
        if (is_caught) {
            desc += "**" + f.name + "**";
        } else {
            desc += "||" + f.name + "||";
        }
        desc += "  `×" + std::to_string(cnt) + "`\n";
    }

    dpp::message msg;
    msg.add_embed(bronx::create_embed(desc));

    // Navigation row — always shown; wraps to prev/next category at boundaries
    {
        dpp::component row;

        bool at_first_page = (page <= 0);
        bool at_last_page  = (page >= total_pages - 1);

        // Prev button: goes to previous page, or last page of previous category
        dpp::component prev_btn;
        prev_btn.set_type(dpp::cot_button)
            .set_style(at_first_page ? dpp::cos_secondary : dpp::cos_primary)
            .set_id("fdex_pg_" + std::to_string(page) + "_" + std::to_string(uid) +
                    "_" + std::to_string(cat_slot) + "_prev");
        if (at_first_page)
            prev_btn.set_label("◀ " + FISHDEX_CATEGORY_ORDER[prev_cat].substr(
                // strip leading emoji bytes for a clean label
                [&]{ size_t p=0; const auto& s=FISHDEX_CATEGORY_ORDER[prev_cat];
                     while(p<s.size()&&(unsigned char)s[p]>127)++p;
                     while(p<s.size()&&s[p]==' ')++p; return p; }()
            ));
        else
            prev_btn.set_emoji("◀️");
        row.add_component(prev_btn);

        // Page counter (disabled)
        dpp::component page_btn;
        page_btn.set_type(dpp::cot_button)
            .set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages))
            .set_style(dpp::cos_secondary)
            .set_id("fdex_pginfo_" + std::to_string(uid))
            .set_disabled(true);
        row.add_component(page_btn);

        // Next button: goes to next page, or first page of next category
        dpp::component next_btn;
        next_btn.set_type(dpp::cot_button)
            .set_style(at_last_page ? dpp::cos_secondary : dpp::cos_primary)
            .set_id("fdex_pg_" + std::to_string(page) + "_" + std::to_string(uid) +
                    "_" + std::to_string(cat_slot) + "_next");
        if (at_last_page)
            next_btn.set_label(FISHDEX_CATEGORY_ORDER[next_cat].substr(
                [&]{ size_t p=0; const auto& s=FISHDEX_CATEGORY_ORDER[next_cat];
                     while(p<s.size()&&(unsigned char)s[p]>127)++p;
                     while(p<s.size()&&s[p]==' ')++p; return p; }()
            ) + " ▶");
        else
            next_btn.set_emoji("▶️");
        row.add_component(next_btn);

        msg.add_component(row);
    }

    // Category dropdown row
    msg.add_component(build_fishdex_dropdown(uid, cat_slot));

    return msg;
}

// ── interaction handlers ──────────────────────────────────────────────────────

inline void register_fishdex_interactions(dpp::cluster& bot, Database* db) {
    // Button clicks: ◀/▶ pagination + category wrap
    bot.on_button_click([db](const dpp::button_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("fdex_pg_", 0) != 0) return;

        // ID: fdex_pg_{page}_{uid}_{catslot}_{dir}
        std::vector<std::string> parts;
        std::stringstream ss(cid);
        std::string part;
        while (std::getline(ss, part, '_')) parts.push_back(part);
        if (parts.size() != 6) return;

        int page; uint64_t uid; int cat_slot;
        try {
            page     = std::stoi(parts[2]);
            uid      = std::stoull(parts[3]);
            cat_slot = std::stoi(parts[4]);
        } catch (...) { return; }
        const std::string& dir = parts[5];

        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("these buttons aren't for you"))
                    .set_flags(dpp::m_ephemeral));
            return;
        }

        const int NUM_CATS = (int)FISHDEX_CATEGORY_ORDER.size();
        if (cat_slot < 0 || cat_slot >= NUM_CATS) return;

        const std::string& cat_name = FISHDEX_CATEGORY_ORDER[cat_slot];
        int total_in_cat = 0;
        for (const auto& f : fish_types) if (fish_category(f) == cat_name) total_in_cat++;
        const int PER_PAGE = 12;
        int total_pages = std::max(1, (total_in_cat + PER_PAGE - 1) / PER_PAGE);

        if (dir == "prev") {
            if (page > 0) {
                page--;
            } else {
                // Wrap to previous category, last page
                cat_slot = (cat_slot - 1 + NUM_CATS) % NUM_CATS;
                int new_total = 0;
                for (const auto& f : fish_types)
                    if (fish_category(f) == FISHDEX_CATEGORY_ORDER[cat_slot]) new_total++;
                int new_pages = std::max(1, (new_total + PER_PAGE - 1) / PER_PAGE);
                page = new_pages - 1;
            }
        } else if (dir == "next") {
            if (page < total_pages - 1) {
                page++;
            } else {
                // Wrap to next category, first page
                cat_slot = (cat_slot + 1) % NUM_CATS;
                page = 0;
            }
        }

        dpp::message msg = build_fishdex_category_page(db, uid, cat_slot, page);
        event.reply(dpp::ir_update_message, msg);
    });

    // Select menu: jump directly to a category
    bot.on_select_click([db](const dpp::select_click_t& event) {
        const std::string& cid = event.custom_id;
        if (cid.rfind("fdex_cat_", 0) != 0) return;

        uint64_t uid;
        try { uid = std::stoull(cid.substr(9)); } catch (...) { return; }

        if (event.command.get_issuing_user().id != uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("this menu isn't for you"))
                    .set_flags(dpp::m_ephemeral));
            return;
        }

        if (event.values.empty()) return;
        int cat_slot;
        try { cat_slot = std::stoi(event.values[0]); } catch (...) { return; }

        dpp::message msg = build_fishdex_category_page(db, uid, cat_slot, 0);
        event.reply(dpp::ir_update_message, msg);
    });
}

// ── command handler ───────────────────────────────────────────────────────────

inline std::vector<Command*> get_fishdex_commands(Database* db) {
    static std::vector<Command*> cmds;

    static Command* fishdex_cmd = new Command(
        "fishdex",
        "view your fish collection — optionally filter by category",
        "fishing",
        {"pokedex", "fishcollection"},
        true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event,
             const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;

            // last token = optional page number; everything before = category
            int req_page = 0;
            std::vector<std::string> cat_parts;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i == args.size() - 1) {
                    try { req_page = std::max(0, std::stoi(args[i]) - 1); continue; }
                    catch (...) {}
                }
                cat_parts.push_back(args[i]);
            }
            std::string cat_input;
            for (size_t i = 0; i < cat_parts.size(); ++i) {
                cat_input += cat_parts[i];
                if (i + 1 < cat_parts.size()) cat_input += " ";
            }

            dpp::message msg;
            if (cat_input.empty()) {
                msg = build_fishdex_overview(db, uid);
            } else {
                int slot = resolve_category_slot(cat_input);
                if (slot < 0) {
                    bronx::send_message(bot, event,
                        bronx::error("unknown category **" + cat_input + "**\n"
                            "Valid: Common, Uncommon, Rare, Epic, Legendary, "
                            "Spring, Summer, Fall, Winter, "
                            "Special, Low Prestige, Mid Prestige, "
                            "High Prestige, Ultimate"));
                    return;
                }
                msg = build_fishdex_category_page(db, uid, slot, req_page);
            }

            if (!msg.embeds.empty())
                bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            bronx::send_message(bot, event, msg);

            // achievement check
            std::set<std::string> all_names;
            for (const auto& f : fish_types) all_names.insert(f.name);
            auto c = get_caught_fish_counts(db, uid);
            int g = 0;
            for (const auto& n : all_names) if (c.count(n)) g++;
            check_fishdex_achievements(bot, db, event.msg.channel_id, uid,
                                        calc_pct(g, all_names.size()));
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;

            std::string cat_input;
            int req_page = 0;
            try {
                auto p = event.get_parameter("category");
                if (std::holds_alternative<std::string>(p))
                    cat_input = std::get<std::string>(p);
            } catch (...) {}
            try {
                auto p = event.get_parameter("page");
                if (std::holds_alternative<int64_t>(p))
                    req_page = (int)std::max((int64_t)0, std::get<int64_t>(p) - 1);
            } catch (...) {}

            dpp::message msg;
            if (cat_input.empty()) {
                msg = build_fishdex_overview(db, uid);
            } else {
                int slot = resolve_category_slot(cat_input);
                if (slot < 0) {
                    event.reply(dpp::message().add_embed(
                        bronx::error("unknown category **" + cat_input + "**")));
                    return;
                }
                msg = build_fishdex_category_page(db, uid, slot, req_page);
            }

            if (!msg.embeds.empty())
                bronx::add_invoker_footer(msg.embeds[0], event.command.get_issuing_user());
            event.reply(msg);

            // achievement check
            std::set<std::string> all_names;
            for (const auto& f : fish_types) all_names.insert(f.name);
            auto c = get_caught_fish_counts(db, uid);
            int g = 0;
            for (const auto& n : all_names) if (c.count(n)) g++;
            check_fishdex_achievements(bot, db, event.command.channel_id, uid,
                                        calc_pct(g, all_names.size()));
        },
        {
            dpp::command_option(dpp::co_string, "category",
                "category to browse (e.g. Common, Rare, Legendary, Spring…)", false),
            dpp::command_option(dpp::co_integer, "page", "page number", false)
        }
    );

    cmds.push_back(fishdex_cmd);
    return cmds;
}

} // namespace fishing
} // namespace commands
