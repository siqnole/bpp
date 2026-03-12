#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "mining_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <mutex>

using namespace bronx::db;

namespace commands {
namespace mining {

// ============================================================================
// /minv  – Mining ore inventory viewer
// ============================================================================

static const int ORES_PER_PAGE = 12;

struct MineInvState {
    uint64_t user_id;
    std::vector<InventoryItem> ores;
    int current_page = 0;
};
static std::unordered_map<uint64_t, MineInvState> minv_states;
static std::mutex minv_mutex;

static dpp::message build_minv_message(uint64_t uid, Database* db) {
    std::lock_guard<std::mutex> lk(minv_mutex);
    auto it = minv_states.find(uid);
    if (it == minv_states.end()) {
        return dpp::message().add_embed(bronx::error("no ore inventory available"));
    }
    MineInvState& st = it->second;
    int total = st.ores.size();
    int pages = (total + ORES_PER_PAGE - 1) / ORES_PER_PAGE;
    if (pages == 0) pages = 1;
    int p = st.current_page;
    if (p < 0) p = 0;
    if (p >= pages) p = pages - 1;

    std::string desc = "⛏️ **ore inventory** (" + std::to_string(total) + " ores)\n\n";
    int start = p * ORES_PER_PAGE;
    int end = std::min(total, start + ORES_PER_PAGE);

    int64_t page_value = 0;
    for (int i = start; i < end; i++) {
        auto& item = st.ores[i];
        // Parse ore name and value from metadata
        std::string name = "unknown ore";
        int64_t val = 0;
        bool locked = false;

        size_t npos = item.metadata.find("\"name\":\"");
        if (npos != std::string::npos) {
            npos += 8;
            size_t nend = item.metadata.find('"', npos);
            if (nend != std::string::npos) name = item.metadata.substr(npos, nend - npos);
        }
        val = parse_mine_meta_int64(item.metadata, "value", 0);
        size_t lpos = item.metadata.find("\"locked\":true");
        locked = (lpos != std::string::npos);

        // Find emoji
        std::string emoji = "🪨";
        for (auto& ot : ore_types) {
            if (ot.name == name) { emoji = ot.emoji; break; }
        }

        desc += emoji + " **" + name + "** `[" + item.item_id + "]` — $" + format_number(val);
        if (locked) desc += " 🔒";
        desc += "\n";
        page_value += val;
    }

    desc += "\n**page value:** $" + format_number(page_value);

    auto embed = bronx::create_embed(desc);
    embed.set_footer(dpp::embed_footer().set_text("page " + std::to_string(p + 1) + "/" + std::to_string(pages)));

    dpp::message msg;
    msg.add_embed(embed);

    if (pages > 1) {
        dpp::component row;
        row.add_component(dpp::component().set_type(dpp::cot_button).set_label("◀").set_style(dpp::cos_secondary).set_id("minv_prev_" + std::to_string(uid)));
        row.add_component(dpp::component().set_type(dpp::cot_button).set_label(std::to_string(p + 1) + "/" + std::to_string(pages)).set_style(dpp::cos_secondary).set_disabled(true).set_id("minv_page_" + std::to_string(uid)));
        row.add_component(dpp::component().set_type(dpp::cot_button).set_label("▶").set_style(dpp::cos_secondary).set_id("minv_next_" + std::to_string(uid)));
        msg.add_component(row);
    }

    return msg;
}

inline Command* get_minv_command(Database* db) {
    static Command* minv = new Command("minv", "view your mined ore inventory", "mining", {"oreinv", "ores"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            auto inv = db->get_inventory(uid);
            std::vector<InventoryItem> ores;
            for (auto& item : inv) {
                if (item.item_type == "collectible" && item.item_id.rfind("M", 0) == 0) {
                    // Check it's an ore (has "type":"ore" in metadata)
                    if (item.metadata.find("\"type\":\"ore\"") != std::string::npos ||
                        item.metadata.find("\"name\":\"") != std::string::npos) {
                        // check if it looks like an ore by checking if name matches any ore_type
                        bool is_ore = false;
                        size_t npos = item.metadata.find("\"name\":\"");
                        if (npos != std::string::npos) {
                            npos += 8;
                            size_t nend = item.metadata.find('"', npos);
                            if (nend != std::string::npos) {
                                std::string name = item.metadata.substr(npos, nend - npos);
                                for (auto& ot : ore_types) {
                                    if (ot.name == name) { is_ore = true; break; }
                                }
                            }
                        }
                        if (is_ore) ores.push_back(item);
                    }
                }
            }
            if (ores.empty()) {
                bronx::send_message(bot, event, bronx::error("you have no ores. use `mine` to start mining!"));
                return;
            }
            {
                std::lock_guard<std::mutex> lk(minv_mutex);
                minv_states[uid] = {uid, ores, 0};
            }
            dpp::message msg = build_minv_message(uid, db);
            if (!msg.embeds.empty()) bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
            bronx::send_message(bot, event, msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            auto inv = db->get_inventory(uid);
            std::vector<InventoryItem> ores;
            for (auto& item : inv) {
                if (item.item_type == "collectible" && item.item_id.rfind("M", 0) == 0) {
                    bool is_ore = false;
                    size_t npos = item.metadata.find("\"name\":\"");
                    if (npos != std::string::npos) {
                        npos += 8;
                        size_t nend = item.metadata.find('"', npos);
                        if (nend != std::string::npos) {
                            std::string name = item.metadata.substr(npos, nend - npos);
                            for (auto& ot : ore_types) {
                                if (ot.name == name) { is_ore = true; break; }
                            }
                        }
                    }
                    if (is_ore) ores.push_back(item);
                }
            }
            if (ores.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("you have no ores. use `/mine` to start mining!")));
                return;
            }
            {
                std::lock_guard<std::mutex> lk(minv_mutex);
                minv_states[uid] = {uid, ores, 0};
            }
            dpp::message msg = build_minv_message(uid, db);
            event.reply(msg);
        });
    return minv;
}

// ============================================================================
// /sellore  – Sell individual ore by ID or 'all'
// ============================================================================

inline Command* get_sellore_command(Database* db) {
    static Command* sellore = new Command("sellore", "sell a mined ore by ID or 'all'", "mining", {"sore", "sellorall"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t uid = event.msg.author.id;
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `sellore <ore_id>` or `sellore all`"));
                return;
            }
            std::string target = args[0];
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);

            if (target == "all") {
                // Sell all unlocked ores
                auto inv = db->get_inventory(uid);
                int64_t total = 0;
                int count = 0;
                for (auto& item : inv) {
                    if (item.item_type != "collectible" || item.item_id.rfind("M", 0) != 0) continue;
                    if (item.metadata.find("\"locked\":true") != std::string::npos) continue;
                    // Check it's an ore
                    bool is_ore = false;
                    size_t npos = item.metadata.find("\"name\":\"");
                    if (npos != std::string::npos) {
                        npos += 8;
                        size_t nend = item.metadata.find('"', npos);
                        if (nend != std::string::npos) {
                            std::string name = item.metadata.substr(npos, nend - npos);
                            for (auto& ot : ore_types) {
                                if (ot.name == name) { is_ore = true; break; }
                            }
                        }
                    }
                    if (!is_ore) continue;
                    int64_t val = parse_mine_meta_int64(item.metadata, "value", 0);
                    if (db->remove_item(uid, item.item_id, 1)) {
                        total += val;
                        count++;
                        db->increment_stat(uid, "ores_sold", 1);
                    }
                }
                if (count == 0) {
                    bronx::send_message(bot, event, bronx::error("no sellable ores found"));
                    return;
                }
                db->update_wallet(uid, total);
                db->increment_stat(uid, "ore_profit", total);
                bronx::send_message(bot, event, bronx::create_embed("⛏️ sold **" + std::to_string(count) + "** ores for **$" + format_number(total) + "**", bronx::COLOR_SUCCESS));
            } else {
                // Sell specific ore
                std::string ore_id = args[0]; // use original case for ID lookup
                if (!db->has_item(uid, ore_id, 1)) {
                    bronx::send_message(bot, event, bronx::error("you don't have an ore with ID `" + ore_id + "`"));
                    return;
                }
                auto inv = db->get_inventory(uid);
                for (auto& item : inv) {
                    if (item.item_id == ore_id) {
                        if (item.metadata.find("\"locked\":true") != std::string::npos) {
                            bronx::send_message(bot, event, bronx::error("that ore is locked! unlock it first"));
                            return;
                        }
                        int64_t val = parse_mine_meta_int64(item.metadata, "value", 0);
                        if (db->remove_item(uid, ore_id, 1)) {
                            db->update_wallet(uid, val);
                            db->increment_stat(uid, "ores_sold", 1);
                            db->increment_stat(uid, "ore_profit", val);

                            std::string name = "ore";
                            size_t npos = item.metadata.find("\"name\":\"");
                            if (npos != std::string::npos) {
                                npos += 8;
                                size_t nend = item.metadata.find('"', npos);
                                if (nend != std::string::npos) name = item.metadata.substr(npos, nend - npos);
                            }
                            bronx::send_message(bot, event, bronx::create_embed("⛏️ sold **" + name + "** for **$" + format_number(val) + "**", bronx::COLOR_SUCCESS));
                        }
                        return;
                    }
                }
                bronx::send_message(bot, event, bronx::error("ore not found"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            std::string target = "all";
            if (event.get_parameter("ore_id").index() != 0) {
                target = std::get<std::string>(event.get_parameter("ore_id"));
            }

            std::string lower = target;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower == "all") {
                auto inv = db->get_inventory(uid);
                int64_t total = 0;
                int count = 0;
                for (auto& item : inv) {
                    if (item.item_type != "collectible" || item.item_id.rfind("M", 0) != 0) continue;
                    if (item.metadata.find("\"locked\":true") != std::string::npos) continue;
                    bool is_ore = false;
                    size_t npos = item.metadata.find("\"name\":\"");
                    if (npos != std::string::npos) {
                        npos += 8;
                        size_t nend = item.metadata.find('"', npos);
                        if (nend != std::string::npos) {
                            std::string name = item.metadata.substr(npos, nend - npos);
                            for (auto& ot : ore_types) {
                                if (ot.name == name) { is_ore = true; break; }
                            }
                        }
                    }
                    if (!is_ore) continue;
                    int64_t val = parse_mine_meta_int64(item.metadata, "value", 0);
                    if (db->remove_item(uid, item.item_id, 1)) {
                        total += val;
                        count++;
                        db->increment_stat(uid, "ores_sold", 1);
                    }
                }
                if (count == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("no sellable ores found")));
                    return;
                }
                db->update_wallet(uid, total);
                db->increment_stat(uid, "ore_profit", total);
                event.reply(dpp::message().add_embed(bronx::create_embed("⛏️ sold **" + std::to_string(count) + "** ores for **$" + format_number(total) + "**", bronx::COLOR_SUCCESS)));
            } else {
                if (!db->has_item(uid, target, 1)) {
                    event.reply(dpp::message().add_embed(bronx::error("you don't have an ore with ID `" + target + "`")));
                    return;
                }
                auto inv = db->get_inventory(uid);
                for (auto& item : inv) {
                    if (item.item_id == target) {
                        if (item.metadata.find("\"locked\":true") != std::string::npos) {
                            event.reply(dpp::message().add_embed(bronx::error("that ore is locked!")));
                            return;
                        }
                        int64_t val = parse_mine_meta_int64(item.metadata, "value", 0);
                        if (db->remove_item(uid, target, 1)) {
                            db->update_wallet(uid, val);
                            db->increment_stat(uid, "ores_sold", 1);
                            db->increment_stat(uid, "ore_profit", val);
                            std::string name = "ore";
                            size_t npos = item.metadata.find("\"name\":\"");
                            if (npos != std::string::npos) {
                                npos += 8;
                                size_t nend = item.metadata.find('"', npos);
                                if (nend != std::string::npos) name = item.metadata.substr(npos, nend - npos);
                            }
                            event.reply(dpp::message().add_embed(bronx::create_embed("⛏️ sold **" + name + "** for **$" + format_number(val) + "**", bronx::COLOR_SUCCESS)));
                        }
                        return;
                    }
                }
                event.reply(dpp::message().add_embed(bronx::error("ore not found")));
            }
        });
    sellore->options.push_back(dpp::command_option(dpp::co_string, "ore_id", "ore ID to sell or 'all'", false));
    return sellore;
}

// ============================================================================
// Register minv interactions (pagination buttons)
// ============================================================================
inline void register_minv_interactions(dpp::cluster& bot, Database* db) {
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        try {
            if (event.custom_id.rfind("minv_prev_", 0) != 0 &&
                event.custom_id.rfind("minv_next_", 0) != 0) return;

            size_t pos = event.custom_id.rfind('_');
            uint64_t uid = std::stoull(event.custom_id.substr(pos + 1));
            if (event.command.get_issuing_user().id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("not your inventory")).set_flags(dpp::m_ephemeral));
                return;
            }

            {
                std::lock_guard<std::mutex> lk(minv_mutex);
                auto it = minv_states.find(uid);
                if (it == minv_states.end()) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("inventory expired")).set_flags(dpp::m_ephemeral));
                    return;
                }
                int total = it->second.ores.size();
                int pages = (total + ORES_PER_PAGE - 1) / ORES_PER_PAGE;
                if (pages == 0) pages = 1;

                if (event.custom_id.find("_prev_") != std::string::npos) {
                    it->second.current_page--;
                    if (it->second.current_page < 0) it->second.current_page = pages - 1;
                } else {
                    it->second.current_page++;
                    if (it->second.current_page >= pages) it->second.current_page = 0;
                }
            }

            dpp::message msg = build_minv_message(uid, db);
            event.reply(dpp::ir_update_message, msg);
        } catch (const std::exception& e) {
            std::cerr << "minv interaction error: " << e.what() << "\n";
        }
    });
}

} // namespace mining
} // namespace commands
