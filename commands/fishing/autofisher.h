#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "autofish_runner.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

using namespace bronx::db;

namespace commands {

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static std::string af_trigger_label(const AutofisherConfig& cfg) {
    if (!cfg.auto_sell) return "off";
    if (cfg.as_trigger == "bag")     return "bag limit (" + std::to_string(cfg.bag_limit) + ")";
    if (cfg.as_trigger == "count")   return std::to_string(cfg.as_threshold) + " fish";
    if (cfg.as_trigger == "balance") return "$" + format_number(cfg.as_threshold) + " stored";
    return cfg.as_trigger;
}

// Build the full status embed description for one user's autofisher
static std::string build_af_status(Database* db, uint64_t uid,
                                   const AutofisherConfig& cfg,
                                   std::optional<std::chrono::system_clock::time_point> last_run,
                                   int interval_min, bool slash_mode) {
    std::string pfx = slash_mode ? "/" : "";
    std::string s = "**autofisher status**\n\n";

    s += "**tier:** " + std::to_string(cfg.tier) + "\n";
    s += "**active:** " + std::string(cfg.active ? "yes" : "no") + "\n";
    s += "**interval:** " + std::to_string(interval_min) + " minutes\n";

    if (last_run) {
        auto next = *last_run + std::chrono::minutes(interval_min);
        int64_t ts = std::chrono::duration_cast<std::chrono::seconds>(next.time_since_epoch()).count();
        s += "**next cycle:** <t:" + std::to_string(ts) + ":R>\n";
    } else {
        s += "**next cycle:** not started yet\n";
    }

    s += "\n**gear**\n";
    s += "rod: " + (cfg.af_rod_id.empty() ? "*none*" : "`" + cfg.af_rod_id + "`") + "\n";
    s += "bait: " + (cfg.af_bait_id.empty() ? "*none*" : "`" + cfg.af_bait_id + "`") + "\n";
    s += "bait in hopper: **" + std::to_string(cfg.af_bait_qty) + "**\n";

    s += "\n**economy**\n";
    s += "bank draw limit: " + (cfg.max_bank_draw > 0 ? "$" + format_number(cfg.max_bank_draw) + " per top-up" : "off") + "\n";

    int fish_count = db->autofisher_fish_count(uid);
    s += "\n**storage** (" + std::to_string(fish_count) + "/" + std::to_string(cfg.bag_limit) + " fish)\n";
    s += "auto-sell: **" + af_trigger_label(cfg) + "** (80% payout)\n";

    s += "\n**commands**\n";
    s += "`" + pfx + "autofisher equip rod <id>` – set autofisher rod\n";
    s += "`" + pfx + "autofisher equip bait <id>` – set autofisher bait type\n";
    s += "`" + pfx + "autofisher deposit [amount]` – add bait to hopper\n";
    s += "`" + pfx + "autofisher withdraw [amount]` – take bait back\n";
    s += "`" + pfx + "autofisher balance <amount|off>` – bank draw limit\n";
    s += "`" + pfx + "autofisher autosell <on|off> [bag|count|balance] [threshold]`\n";
    s += "`" + pfx + "autofisher fish` – view stored fish\n";
    s += "`" + pfx + "autofisher sell` – manually sell all fish (100%)\n";
    s += "`" + pfx + "autofisher collect` – move fish to your inventory\n";
    return s;
}

// ──────────────────────────────────────────────────────────────────────────────
// Main handler (message command)
// ──────────────────────────────────────────────────────────────────────────────

static void handle_autofisher_msg(Database* db,
                                   dpp::cluster& bot,
                                   const dpp::message_create_t& event,
                                   const std::vector<std::string>& args) {
    uint64_t uid = event.msg.author.id;

    // ── guard: must own an autofisher ──
    auto require_af = [&]() -> bool {
        if (!db->has_autofisher(uid)) db->create_autofisher(uid);
        int tier = db->get_autofisher_tier(uid);
        if (tier == 0) {
            bronx::send_message(bot, event, bronx::error("you don't have an autofisher yet – buy one from the shop"));
            return false;
        }
        return true;
    };

    std::string sub = args.empty() ? "status" : args[0];
    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

    // ── status ───────────────────────────────────────────────────────────────
    if (sub == "status" || sub == "info") {
        if (!require_af()) return;
        auto cfg = db->get_autofisher_config(uid);
        if (!cfg) { bronx::send_message(bot, event, bronx::error("config unavailable")); return; }
        int tier     = cfg->tier;
        int interval = (tier == 2) ? 20 : 30;
        auto last    = db->get_autofisher_last_run(uid);
        auto embed   = bronx::create_embed(build_af_status(db, uid, *cfg, last, interval, false));
        bronx::add_invoker_footer(embed, event.msg.author);
        bronx::send_message(bot, event, embed);
        return;
    }

    // ── start / stop ─────────────────────────────────────────────────────────
    if (sub == "start" || sub == "activate" || sub == "on") {
        if (!require_af()) return;
        if (db->activate_autofisher(uid)) {
            int tier = db->get_autofisher_tier(uid);
            int iv   = (tier == 2) ? 20 : 30;
            bronx::send_message(bot, event, bronx::success(
                "autofisher activated – fishing every " + std::to_string(iv) + " minutes"));
        } else {
            bronx::send_message(bot, event, bronx::error("failed to activate"));
        }
        return;
    }
    if (sub == "stop" || sub == "deactivate" || sub == "off") {
        if (!require_af()) return;
        if (db->deactivate_autofisher(uid))
            bronx::send_message(bot, event, bronx::success("autofisher deactivated"));
        else
            bronx::send_message(bot, event, bronx::error("failed to deactivate"));
        return;
    }

    // ── equip ─────────────────────────────────────────────────────────────────
    if (sub == "equip") {
        if (!require_af()) return;
        if (args.size() < 3) {
            bronx::send_message(bot, event, bronx::error("usage: `autofisher equip rod|bait <item_id>`"));
            return;
        }
        std::string slot  = args[1]; std::transform(slot.begin(),  slot.end(),  slot.begin(),  ::tolower);
        std::string item  = args[2]; std::transform(item.begin(),  item.end(),  item.begin(),  ::tolower);

        // Fetch user's active gear to prevent sharing
        auto user_gear = db->get_active_fishing_gear(uid);

        if (slot == "rod") {
            if (!db->has_item(uid, item, 1)) {
                bronx::send_message(bot, event, bronx::error("you don't own that rod")); return;
            }
            if (item == user_gear.first) {
                bronx::send_message(bot, event, bronx::error(
                    "your autofisher can't use the same rod as you – equip a different rod to yourself first"));
                return;
            }
            if (db->autofisher_set_rod(uid, item))
                bronx::send_message(bot, event, bronx::success("autofisher rod set to `" + item + "`"));
            else
                bronx::send_message(bot, event, bronx::error("failed to set rod"));

        } else if (slot == "bait") {
            if (!db->has_item(uid, item, 1)) {
                bronx::send_message(bot, event, bronx::error("you don't own any of that bait")); return;
            }
            if (item == user_gear.second) {
                bronx::send_message(bot, event, bronx::error(
                    "your autofisher can't use the same bait as you – equip a different bait to yourself first"));
                return;
            }
            // Fetch meta/level for this bait from inventory
            int blvl = 1; std::string bmeta;
            for (auto& it : db->get_inventory(uid))
                if (it.item_id == item) { blvl = it.level; bmeta = it.metadata; break; }
            if (db->autofisher_set_bait(uid, item, blvl, bmeta))
                bronx::send_message(bot, event, bronx::success("autofisher bait set to `" + item + "`"));
            else
                bronx::send_message(bot, event, bronx::error("failed to set bait"));
        } else {
            bronx::send_message(bot, event, bronx::error("unknown slot – use `rod` or `bait`"));
        }
        return;
    }

    // ── deposit bait ─────────────────────────────────────────────────────────
    if (sub == "deposit") {
        if (!require_af()) return;
        auto cfg = db->get_autofisher_config(uid);
        if (!cfg || cfg->af_bait_id.empty()) {
            bronx::send_message(bot, event, bronx::error("set a bait type first: `autofisher equip bait <id>`"));
            return;
        }
        int in_inv = db->get_item_quantity(uid, cfg->af_bait_id);
        if (in_inv <= 0) {
            bronx::send_message(bot, event, bronx::error("you have none of `" + cfg->af_bait_id + "` to deposit"));
            return;
        }
        int amount = in_inv; // default: all
        if (args.size() >= 2) {
            std::string a = args[1]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a != "max" && a != "all") {
                try { amount = std::stoi(a); } catch (...) {}
            }
        }
        amount = std::min(amount, in_inv);
        if (amount <= 0) { bronx::send_message(bot, event, bronx::error("invalid amount")); return; }
        if (db->remove_item(uid, cfg->af_bait_id, amount) && db->autofisher_deposit_bait(uid, amount))
            bronx::send_message(bot, event, bronx::success(
                "deposited **" + std::to_string(amount) + "x " + cfg->af_bait_id + "** into autofisher hopper"));
        else
            bronx::send_message(bot, event, bronx::error("failed to deposit bait"));
        return;
    }

    // ── withdraw bait ────────────────────────────────────────────────────────
    if (sub == "withdraw") {
        if (!require_af()) return;
        auto cfg = db->get_autofisher_config(uid);
        if (!cfg || cfg->af_bait_id.empty() || cfg->af_bait_qty <= 0) {
            bronx::send_message(bot, event, bronx::error("no bait in hopper to withdraw"));
            return;
        }
        int amount = cfg->af_bait_qty;
        if (args.size() >= 2) {
            std::string a = args[1]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a != "max" && a != "all") {
                try { amount = std::stoi(a); } catch (...) {}
            }
        }
        amount = std::min(amount, cfg->af_bait_qty);
        if (amount <= 0) { bronx::send_message(bot, event, bronx::error("invalid amount")); return; }
        // return bait to user inventory
        if (db->autofisher_consume_bait(uid, amount) &&
            db->add_item(uid, cfg->af_bait_id, "bait", amount, cfg->af_bait_meta, cfg->af_bait_level))
            bronx::send_message(bot, event, bronx::success(
                "returned **" + std::to_string(amount) + "x " + cfg->af_bait_id + "** to your inventory"));
        else
            bronx::send_message(bot, event, bronx::error("failed to withdraw bait"));
        return;
    }

    // ── bank draw limit ───────────────────────────────────────────────────────
    if (sub == "balance") {
        if (!require_af()) return;
        if (args.size() < 2) {
            bronx::send_message(bot, event, bronx::error("usage: `autofisher balance <amount|off>`")); return;
        }
        std::string a = args[1]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        int64_t amt = 0;
        if (a != "off" && a != "0") {
            try { amt = std::stoll(a); } catch (...) {
                bronx::send_message(bot, event, bronx::error("invalid amount")); return;
            }
        }
        if (amt < 0) amt = 0;
        if (db->autofisher_set_max_bank_draw(uid, amt)) {
            if (amt == 0)
                bronx::send_message(bot, event, bronx::success("bank draw disabled – autofisher won't buy bait automatically"));
            else
                bronx::send_message(bot, event, bronx::success(
                    "autofisher will draw up to **$" + format_number(amt) + "** from your bank per bait top-up"));
        } else {
            bronx::send_message(bot, event, bronx::error("failed to update balance setting"));
        }
        return;
    }

    // ── auto-sell ─────────────────────────────────────────────────────────────
    if (sub == "autosell") {
        if (!require_af()) return;
        if (args.size() < 2) {
            bronx::send_message(bot, event, bronx::error(
                "usage: `autofisher autosell <on|off> [bag|count|balance] [threshold]`")); return;
        }
        std::string toggle = args[1]; std::transform(toggle.begin(), toggle.end(), toggle.begin(), ::tolower);
        if (toggle == "off" || toggle == "0") {
            if (db->autofisher_set_autosell(uid, false, "bag", 0))
                bronx::send_message(bot, event, bronx::success("auto-sell disabled"));
            else
                bronx::send_message(bot, event, bronx::error("failed to update"));
            return;
        }
        // on [trigger] [threshold]
        auto cfg = db->get_autofisher_config(uid);
        std::string trigger   = (cfg ? cfg->as_trigger : "bag");
        int64_t    threshold  = (cfg ? cfg->as_threshold : 0);
        if (args.size() >= 3) {
            trigger = args[2]; std::transform(trigger.begin(), trigger.end(), trigger.begin(), ::tolower);
        }
        if (trigger != "bag" && trigger != "count" && trigger != "balance") {
            bronx::send_message(bot, event, bronx::error("trigger must be `bag`, `count`, or `balance`")); return;
        }
        if (args.size() >= 4) {
            try { threshold = std::stoll(args[3]); } catch (...) {}
        }
        if (trigger == "count" && threshold <= 0) threshold = 10;
        if (trigger == "balance" && threshold <= 0) threshold = 1000;
        if (db->autofisher_set_autosell(uid, true, trigger, threshold)) {
            std::string desc = "auto-sell enabled – trigger: **" + trigger + "**";
            if (trigger == "count")   desc += " (threshold: " + std::to_string(threshold) + " fish)";
            if (trigger == "balance") desc += " (threshold: $" + format_number(threshold) + ")";
            if (trigger == "bag")     desc += " (when bag is full)";
            desc += "\n80% payout – 20% convenience fee";
            bronx::send_message(bot, event, bronx::success(desc));
        } else {
            bronx::send_message(bot, event, bronx::error("failed to update"));
        }
        return;
    }

    // ── view stored fish ──────────────────────────────────────────────────────
    if (sub == "fish" || sub == "storage" || sub == "inventory") {
        if (!require_af()) return;
        auto fish_list = db->autofisher_get_fish(uid);
        if (fish_list.empty()) {
            bronx::send_message(bot, event, bronx::info("no fish in autofisher storage"));
            return;
        }
        // Summarise by name
        std::map<std::string, std::pair<int,int64_t>> summary; // name → {count, total_value}
        int64_t grand_total = 0;
        for (auto& f : fish_list) {
            summary[f.fish_name].first++;
            summary[f.fish_name].second += f.value;
            grand_total += f.value;
        }
        std::string desc = "**autofisher storage** (" + std::to_string(fish_list.size()) + " fish)\n\n";
        for (auto& [name, cv] : summary)
            desc += "**" + name + "** x" + std::to_string(cv.first) +
                    " – $" + format_number(cv.second) + "\n";
        desc += "\n**total value:** $" + format_number(grand_total);
        desc += "\n**auto-sell payout (80%):** $" + format_number((int64_t)(grand_total * 0.8));
        desc += "\nuse `sell` (100%) or `autosell` to configure auto-sell";
        auto embed = bronx::create_embed(desc);
        bronx::add_invoker_footer(embed, event.msg.author);
        bronx::send_message(bot, event, embed);
        return;
    }

    // ── manual sell (100%) ───────────────────────────────────────────────────
    if (sub == "sell") {
        if (!require_af()) return;
        int count = db->autofisher_fish_count(uid);
        if (count == 0) { bronx::send_message(bot, event, bronx::info("no fish to sell")); return; }
        int64_t total = db->autofisher_clear_fish(uid);
        if (total > 0) {
            db->update_wallet(uid, total);
            bronx::send_message(bot, event, bronx::success(
                "sold **" + std::to_string(count) + " fish** for **$" + format_number(total) + "** (100% – manual sell)"));
        } else {
            bronx::send_message(bot, event, bronx::error("failed to sell fish"));
        }
        return;
    }

    // ── collect into inventory ───────────────────────────────────────────────
    if (sub == "collect") {
        if (!require_af()) return;
        auto fish_list = db->autofisher_get_fish(uid);
        if (fish_list.empty()) { bronx::send_message(bot, event, bronx::info("no fish to collect")); return; }
        db->autofisher_clear_fish(uid);
        int added = 0;
        for (auto& f : fish_list) {
            std::string fid = "af_" + std::to_string(f.id);
            if (db->add_item(uid, fid, "collectible", 1, f.metadata)) added++;
        }
        bronx::send_message(bot, event, bronx::success(
            "moved **" + std::to_string(added) + " fish** from autofisher storage to your inventory"));
        return;
    }

    bronx::send_message(bot, event, bronx::error(
        "unknown subcommand – use `autofisher status` to see all options"));
}

// ──────────────────────────────────────────────────────────────────────────────
// Command registration
// ──────────────────────────────────────────────────────────────────────────────

::std::vector<Command*> get_autofisher_commands(Database* db) {
    static ::std::vector<Command*> cmds;

    static Command* autofisher = new Command("autofisher", "manage your autofisher", "automation",
        {"autofish", "af"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            handle_autofisher_msg(db, bot, event, args);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            std::string action = "status";
            auto ap = event.get_parameter("action");
            if (std::holds_alternative<std::string>(ap)) action = std::get<std::string>(ap);

            if (!db->has_autofisher(uid)) db->create_autofisher(uid);
            int tier = db->get_autofisher_tier(uid);
            if (tier == 0) {
                event.reply(dpp::message().add_embed(bronx::error("you don't have an autofisher yet – buy one from the shop")));
                return;
            }

            if (action == "status") {
                auto cfg = db->get_autofisher_config(uid);
                if (!cfg) { event.reply(dpp::message().add_embed(bronx::error("config unavailable"))); return; }
                int iv   = (cfg->tier == 2) ? 20 : 30;
                auto last = db->get_autofisher_last_run(uid);
                auto embed = bronx::create_embed(build_af_status(db, uid, *cfg, last, iv, true));
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
            } else if (action == "start") {
                if (db->activate_autofisher(uid)) {
                    int iv = (tier == 2) ? 20 : 30;
                    event.reply(dpp::message().add_embed(bronx::success(
                        "autofisher activated – fishing every " + std::to_string(iv) + " minutes")));
                } else { event.reply(dpp::message().add_embed(bronx::error("failed to activate"))); }
            } else if (action == "stop") {
                if (db->deactivate_autofisher(uid))
                    event.reply(dpp::message().add_embed(bronx::success("autofisher deactivated")));
                else event.reply(dpp::message().add_embed(bronx::error("failed to deactivate")));
            } else if (action == "sell") {
                int count = db->autofisher_fish_count(uid);
                if (count == 0) { event.reply(dpp::message().add_embed(bronx::info("no fish to sell"))); return; }
                int64_t total = db->autofisher_clear_fish(uid);
                db->update_wallet(uid, total);
                event.reply(dpp::message().add_embed(bronx::success(
                    "sold **" + std::to_string(count) + " fish** for **$" + format_number(total) + "** (100%)")));
            } else {
                event.reply(dpp::message().add_embed(bronx::info("use message commands for `autofisher " + action + "`")));
            }
        },
        {
            dpp::command_option(dpp::co_string, "action", "action to perform", false)
                .add_choice(dpp::command_option_choice("status", "status"))
                .add_choice(dpp::command_option_choice("start", "start"))
                .add_choice(dpp::command_option_choice("stop", "stop"))
                .add_choice(dpp::command_option_choice("sell fish (100%)", "sell"))
        });
    cmds.push_back(autofisher);

    return cmds;
}

} // namespace commands
