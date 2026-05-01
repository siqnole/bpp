// ============================================================================
// owner.cpp — All owner command implementations.
// Declarations are in owner.h (kept minimal for fast incremental builds).
// ============================================================================
#include "owner.h"

// Additional project headers needed only by implementations
#include "../database/operations/community/suggestion_operations.h"
#include "../database/operations/economy/history_operations.h"
#include "../database/operations/economy/gambling_verification.h"
#include "../performance/cache_manager.h"
#include "../performance/chart_renderer.h"
#include "../security/input_validation.h"
#include "../commands/market_state.h"
#include "economy_core.h"
#include "titles.h"
#include "owner/gambling_audit.h"
#include "owner/telemetry.h"
#include "owner/audit.h"
#include "world_events.h"
#include "../utils/logger.h"

#include <dpp/version.h>
#include <fstream>
#include <dirent.h>
#include <sstream>
#include <iomanip>
#include <shared_mutex>
#include <sys/stat.h>
#include <unistd.h>

// Anonymous namespace for file-local helpers (replaces 'static' in headers)
namespace {

// helper to compute next weekly rotation boundary (UTC)
time_t next_rotation_time() {
    using namespace std::chrono;
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const int64_t week = 7 * 24 * 60 * 60;
    int64_t next = ((secs / week) + 1) * week;
    return static_cast<time_t>(next);
}

} // anonymous namespace

// ─── Modularized owner command logic moved to owner/*.cpp ───

namespace commands {

// get_owner_ids() and is_owner() are defined inline in owner.h

// parse a mention or raw numeric string into a snowflake ID
// parse_snowflake() and parse_scope_args() are defined inline in owner.h


std::vector<Command*> get_owner_commands(CommandHandler* handler, bronx::db::Database* db) {
    static ::std::vector<Command*> cmds;
    
    // Owner Stats command (bot statistics) — paginated
    static Command stats("ostats", "view detailed bot statistics (owner only)", "owner", {"ownerstatistics", "botinfo"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("this command is restricted to the bot owner only.")));
                return;
            }

            uint64_t uid = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(commands::owner::ostats_mutex);
                auto& state = commands::owner::ostats_states[uid];
                if (!args.empty()) {
                    try {
                        int p = std::stoi(args[0]) - 1;
                        if (p >= 0 && p < commands::owner::OSTATS_TOTAL_PAGES) state.current_page = p;
                    } catch (...) {}
                } else {
                    state.current_page = 0;
                }
                msg = commands::owner::build_ostats_message(bot, db, uid);
            }
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&stats);

    // cleanup command to remove obsolete active_title entries from every user's inventory
    static Command cleantitles("cleantitles", "purge legacy active_title items from all inventories", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner")));
                return;
            }
            int removed = db->delete_inventory_item_for_all_users("active_title");
            bot.message_create(dpp::message(event.msg.channel_id,
                bronx::success("removed " + std::to_string(removed) + " active_title entries")));
        },
        nullptr, {});
    cmds.push_back(&cleantitles);

    // title database management (dynamic titles that can be added/edited at runtime)
    static Command titledb("titledb", "manage dynamic titles (add/edit/remove/list)", "owner", {"edittitles"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("usage: titledb <list|add|edit|remove> [...]")));
                return;
            }
            std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            if (action == "list") {
                auto dyn = db->get_dynamic_titles();
                std::string desc = "**dynamic titles**\n\n";
                if (dyn.empty()) {
                    desc += "(none)";
                } else {
                    time_t expiry = next_rotation_time();
                    std::string expstr = expiry ? " (expires <t:" + std::to_string(expiry) + ":R>)" : "";
                    for (auto &t : dyn) {
                        desc += t.item_id + " : " + t.display + " ($" + format_number(t.price) + ")";
                        if (t.rotation_slot) desc += " slot=" + std::to_string(t.rotation_slot);
                        if (t.purchase_limit) desc += " limit=" + std::to_string(t.purchase_limit);
                        if (t.rotation_slot) desc += expstr;
                        desc += "\n";
                    }
                }
                bot.message_create(dpp::message(event.msg.channel_id, bronx::create_embed(desc)));
                return;
            }
            if (action == "remove") {
                if (args.size() < 2) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("usage: titledb remove <item_id>")));
                    return;
                }
                if (db->delete_dynamic_title(args[1])) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("removed title " + args[1])));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("failed to remove title (check ID?)")));
                }
                return;
            }
            if (action == "add" || action == "edit") {
                // syntax: add <item_id> <display> <price> <shop_desc> [slot] [limit]
                if (args.size() < 5) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("usage: titledb " + action + " <item_id> <display> <price> <shop_desc> [rotation_slot] [purchase_limit]")));
                    return;
                }
                commands::TitleDef t;
                t.item_id = args[1];
                t.display = args[2];
                try {
                    t.price = parse_amount(args[3], INT64_MAX);
                } catch (...) { t.price = 0; }
                t.shop_desc = args[4];
                t.rotation_slot = 0;
                t.purchase_limit = 0;
                if (args.size() >= 6) t.rotation_slot = std::stoi(args[5]);
                if (args.size() >= 7) t.purchase_limit = std::stoi(args[6]);
                bool ok = (action == "add") ? db->create_dynamic_title(t)
                                              : db->update_dynamic_title(t);
                if (ok) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::success("title " + t.item_id + " " + (action=="add"?"added":"updated"))));
                } else {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("database operation failed")));
                }
                return;
            }
            bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("unknown action for titledb")));
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            if (!is_owner(uid)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto get_str = [&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            std::string action = get_str("action");
            if (action.empty() || action == "list") {
                auto dyn = db->get_dynamic_titles();
                std::string desc = "**dynamic titles**\n\n";
                if (dyn.empty()) desc += "(none)";
                else {
                    for (auto &t : dyn) {
                        desc += t.item_id + " : " + t.display + " ($" + format_number(t.price) + ")";
                        if (t.rotation_slot) desc += " slot=" + std::to_string(t.rotation_slot);
                        if (t.purchase_limit) desc += " limit=" + std::to_string(t.purchase_limit);
                        desc += "\n";
                    }
                }
                event.reply(dpp::message().add_embed(bronx::create_embed(desc)));
                return;
            }
            if (action == "remove") {
                std::string id = get_str("item_id");
                if (id.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("need item_id")));
                    return;
                }
                if (db->delete_dynamic_title(id))
                    event.reply(dpp::message().add_embed(bronx::success("removed " + id)));
                else
                    event.reply(dpp::message().add_embed(bronx::error("failed to remove")));
                return;
            }
            if (action == "add" || action == "edit") {
                std::string id = get_str("item_id");
                std::string disp = get_str("display");
                std::string price_str = get_str("price");
                std::string desc = get_str("shop_desc");
                int slot = 0; try { slot = std::stoi(get_str("slot")); } catch(...){}
                int limit = 0; try { limit = std::stoi(get_str("limit")); } catch(...){}
                if (id.empty() || disp.empty() || price_str.empty() || desc.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("missing required fields")));
                    return;
                }
                commands::TitleDef t;
                t.item_id = id;
                t.display = disp;
                try { t.price = parse_amount(price_str, INT64_MAX); } catch(...) { t.price = 0; }
                t.shop_desc = desc;
                t.rotation_slot = slot;
                t.purchase_limit = limit;
                bool ok = (action == "add") ? db->create_dynamic_title(t) : db->update_dynamic_title(t);
                if (ok)
                    event.reply(dpp::message().add_embed(bronx::success("title " + id + " " + (action=="add"?"added":"updated"))));
                else
                    event.reply(dpp::message().add_embed(bronx::error("database error")));
                return;
            }
            event.reply(dpp::message().add_embed(bronx::error("unknown action")));
        }, { dpp::command_option(dpp::co_string,"action","list|add|edit|remove",false)
            .add_choice(dpp::command_option_choice("list","list"))
            .add_choice(dpp::command_option_choice("add","add"))
            .add_choice(dpp::command_option_choice("edit","edit"))
            .add_choice(dpp::command_option_choice("remove","remove")),
           dpp::command_option(dpp::co_string,"item_id","id of title",false),
           dpp::command_option(dpp::co_string,"display","display text",false),
           dpp::command_option(dpp::co_string,"price","price",false),
           dpp::command_option(dpp::co_string,"shop_desc","shop description",false),
           dpp::command_option(dpp::co_integer,"slot","rotation slot",false),
           dpp::command_option(dpp::co_integer,"limit","purchase limit",false) });
    cmds.push_back(&titledb);
    
    // Servers list command (paginated with leave buttons)
    static Command servers_cmd("servers", "view and manage all servers the bot is in (owner only)", "owner", {"serverlist", "guilds"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("this command is restricted to the bot owner only.")));
                return;
            }
            
            uint64_t owner_id = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(commands::owner::server_list_mutex);
                msg = commands::owner::build_servers_message(bot, owner_id);
            }
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&servers_cmd);

    // World Event Spawn Command (Owner utility)
    static Command spawnevent("spawnevent", "force a world event to spawn (owner only)", "owner", {"forceevent", "startevent"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner.")));
                return;
            }

            auto result = commands::world_events::try_spawn_random_event(db, true); // true = force spawn

            if (result) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("🌍 **world event triggered!**\nstarted: **" + result->event_name + "** (" + result->emoji + ")")));
                
                // Optional: trigger the announcement broadcast normally sent by the bot
                // For now, let's assume the user wants to see the announcement 
                // However, try_spawn_random_event doesn't broadcast internally (it only updates DB and logs to console).
                // I should add a broadcast call here if I want the announcement to appear.
                auto embed = commands::world_events::build_event_start_embed(*result);
                bot.message_create(dpp::message(event.msg.channel_id, "").add_embed(embed));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("failed to spawn world event. check console for details.")));
            }
        });
    cmds.push_back(&spawnevent);
    
    // mysql eval command
    static Command mysql_cmd("mysql", "execute arbitrary mysql statement (owner only)", "owner", {"sql"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("this command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                std::string help = "usage: mysql <SQL statement>\n";
                help += "\ncommon examples:\n";
                help += "```sql\n";
                help += "SELECT * FROM tablename;          -- grab info initially\n";
                help += "SHOW TABLES;\n";
                help += "SHOW COLUMNS FROM tablename;\n";
                help += "ALTER TABLE tablename ADD COLUMN newcol VARCHAR(255);\n";
                help += "```";
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error(help)));
                return;
            }
            std::string sql;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) sql += " ";
                sql += args[i];
            }

            // SECURITY FIX: Use direct MySQL C API instead of popen() shell injection vector.
            // This eliminates shell metacharacter injection and avoids exposing
            // credentials on the command line (visible in /proc/*/cmdline).
            std::string result;
            auto conn = db->get_pool()->acquire();
            if (!conn) {
                result = "failed to acquire database connection";
            } else {
                if (mysql_query(conn->get(), sql.c_str()) == 0) {
                    MYSQL_RES* res = mysql_store_result(conn->get());
                    if (res) {
                        unsigned int num_fields = mysql_num_fields(res);
                        MYSQL_FIELD* fields = mysql_fetch_fields(res);
                        // Header row
                        for (unsigned int i = 0; i < num_fields; i++) {
                            if (i) result += "\t";
                            result += fields[i].name;
                        }
                        result += "\n";
                        // Data rows (limit to 50 rows to avoid message overflow)
                        int row_count = 0;
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res)) && row_count < 50) {
                            for (unsigned int i = 0; i < num_fields; i++) {
                                if (i) result += "\t";
                                result += row[i] ? row[i] : "NULL";
                            }
                            result += "\n";
                            row_count++;
                        }
                        uint64_t total_rows = mysql_num_rows(res);
                        if (total_rows > 50) {
                            result += "... (" + std::to_string(total_rows - 50) + " more rows)\n";
                        }
                        mysql_free_result(res);
                    } else {
                        // Non-SELECT statement (INSERT/UPDATE/DELETE/ALTER/etc.)
                        uint64_t affected = mysql_affected_rows(conn->get());
                        result = "ok, " + std::to_string(affected) + " rows affected";
                    }
                } else {
                    result = "mysql error: ";
                    result += mysql_error(conn->get());
                }
                db->get_pool()->release(conn);
            }
            if (result.empty()) result = "(no output)";
            // Truncate to fit Discord message limit
            if (result.size() > 1900) result = result.substr(0, 1900) + "\n... (truncated)";
            bot.message_create(dpp::message(event.msg.channel_id, "```" + result + "```"));
        }
    );
    cmds.push_back(&mysql_cmd);
    
    // machine learning settings commands
    static Command mlstatus("mlstatus", "show ML configuration settings (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
 
            // ── Market regime block ──
            std::string market_block = db->get_market_state_report();
            if (market_block.empty()) {
                market_block = "⚫ **market regime:** not yet classified\n"
                               "  run `mlclassify` to perform the first classification\n";
            }
 
            // ── ML settings block (unchanged) ──
            auto list = db->list_ml_settings();
            std::string settings_block;
            if (list.empty()) {
                settings_block = "*(no ML settings stored)*\n";
            } else {
                for (auto& p : list) {
                    settings_block += p.first + " = " + p.second + "\n";
                }
            }
 
            // ── Price effect report block ──
            int hours = 24;
            if (!args.empty()) {
                try { hours = std::stoi(args[0]); } catch(...) {}
            }
            std::string effect = db->get_ml_effect_report(hours);
 
            std::string msgtxt =
                "**— market state —**\n" + market_block +
                "\n**— ml settings —**\n" + settings_block +
                "\n**— price adjustments (last " + std::to_string(hours) + "h) —**\n" + effect;
 
            bot.message_create(dpp::message(event.msg.channel_id, bronx::info(msgtxt)));
        });
    cmds.push_back(&mlstatus);

    // list of known ML configuration keys with descriptions; update as features are added
    static Command mlclassify("mlclassify",
        "run market state classifier and update regime (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
 
            // optional min_samples argument (default 50)
            int min_samples = 50;
            if (!args.empty()) {
                try { min_samples = std::stoi(args[0]); } catch(...) {}
                if (min_samples < 1) min_samples = 1;
            }
 
            std::string result = db->classify_market_state(min_samples);
            bot.message_create(dpp::message(event.msg.channel_id, bronx::info(
                "**market classifier result** (min_samples=" +
                std::to_string(min_samples) + "):\n" + result +
                "\nrun `mlstatus` for full regime report."
            )));
        });
    cmds.push_back(&mlclassify);

    static const std::vector<std::pair<std::string,std::string>> ml_keys = {
        {"tune_scale",             "price tuning scale factor (float); overwritten by classifier unless tune_scale_override is set"},
        {"catch_win_prob",         "(example) chance of winning fishing; not actively used"},
        {"profit_floor",           "minimum profit threshold for ML adjustments (supports k/m suffix)"},
        {"bait_delta_cap",         "maximum per-run bait price change (int; use k/m as needed)"},
        {"bait_price_min",         "minimum allowable bait price (int, clamped; suffix _<level> for per-rarity)"},
        {"bait_price_max",         "maximum allowable bait price (int, clamped; suffix _<level> for per-rarity)"},
        // ── new market-state keys ──
        {"market_stable_band",     "fraction of profit_target within which market is STABLE (default 0.15 = ±15%)"},
        {"market_inflation_band",  "fraction above target at which market enters INFLATION (default 0.60 = 60%)"},
        {"market_critical_band",   "fraction deviation at which market enters CRITICAL state (default 1.50 = 150%)"},
        {"tune_scale_override",    "set to any value to prevent classifier from overwriting tune_scale (manual lock)"},
        {"tune_decay_override",    "set to any value to prevent classifier from overwriting tune_decay (manual lock)"},
    };

    auto normalize_ml_value = [](const std::string &raw)->std::string {
        // support optional < or > prefix, and k/m/b suffixes for thousands/millions/billions
        if (raw.empty()) return raw;
        size_t idx = 0;
        char op = '\0';
        if (raw[0] == '<' || raw[0] == '>') {
            op = raw[0];
            idx = 1;
        }
        std::string core = raw.substr(idx);
        // trim spaces
        while (!core.empty() && isspace((unsigned char)core.front())) core.erase(core.begin());
        while (!core.empty() && isspace((unsigned char)core.back())) core.pop_back();
        if (core.empty()) return raw;
        // detect suffix
        char suf = '\0';
        if (core.size() > 1) {
            char last = tolower(core.back());
            if (last=='k' || last=='m' || last=='b') {
                suf = last;
                core.pop_back();
            }
        }
        // attempt numeric parse
        try {
            if (core.find('.') != std::string::npos) {
                double f = std::stod(core);
                if (suf == 'k') f *= 1e3;
                else if (suf == 'm') f *= 1e6;
                else if (suf == 'b') f *= 1e9;
                std::string out = std::to_string(f);
                if (op) out = std::string(1, op) + out;
                return out;
            } else {
                long long n = std::stoll(core);
                if (suf == 'k') n *= 1000;
                else if (suf == 'm') n *= 1000000;
                else if (suf == 'b') n *= 1000000000;
                std::string out = std::to_string(n);
                if (op) out = std::string(1, op) + out;
                return out;
            }
        } catch(...) {
            return raw; // fallback to unmodified
        }
    };

    static Command mlset("mlset", "set an ML configuration key/value (owner only)", "owner", {}, false,
        [db,normalize_ml_value](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.size() < 2) {
                // build usage/help embed including list of known keys
                std::string help = "usage: mlset <key> <value>\n\n*available keys:*\n";
                for (auto &p : ml_keys) {
                    help += "• `" + p.first + "` - " + p.second + "\n";
                }
                help += "\nuse `mlstatus` to view current values. \n";
                help += "(you can also specify `bait_price_min_3` etc. to limit a particular bait level)";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error(help)));
                return;
            }
            std::string key = args[0];
            std::string val;
            for (size_t i=1;i<args.size();++i){ val += args[i]; if(i+1<args.size()) val += " "; }
            std::string norm = normalize_ml_value(val);
            bool stored = db->set_ml_setting(key,norm);
            if (stored) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("setting stored: " + norm)));
                // if this is a price bound, immediately clamp existing shop prices
                std::string lk = key;
                std::string prefix;
                int level = 0;
                // detect optional level suffix
                size_t pos = lk.find_last_of('_');
                if (pos != std::string::npos && pos + 1 < lk.size()) {
                    std::string suffix = lk.substr(pos+1);
                    bool all_digits = std::all_of(suffix.begin(), suffix.end(), ::isdigit);
                    if (all_digits) {
                        level = std::stoi(suffix);
                        prefix = lk.substr(0,pos);
                    }
                }
                if (prefix.empty()) prefix = lk; // no suffix
                if (prefix == "bait_price_max" || prefix == "bait_price_min") {
                    // build SQL clamp depending on min/max
                    std::string op = (prefix == "bait_price_max") ? "LEAST" : "GREATEST";
                    std::string value = norm;
                    std::string q;
                    if (level <= 0) {
                        q = "UPDATE shop_items SET price = " + op + "(price, " + value + ") WHERE category='bait'";
                    } else {
                        q = "UPDATE shop_items SET price = " + op + "(price, " + value + ") WHERE category='bait' AND level=" + std::to_string(level);
                    }
                    if (!db->execute(q)) {
                        bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to clamp existing prices")));
                    }
                }
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to store setting")));
            }
        });
    cmds.push_back(&mlset);

    static Command mldelete("mldelete", "remove an ML configuration key (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.size() != 1) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: mldelete <key>")));
                return;
            }
            if (db->delete_ml_setting(args[0])) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("setting removed")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("failed to remove setting")));
            }
        });
    cmds.push_back(&mldelete);

    static Command invdbg("invdebug", "enable/disable inventory debug logging (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.empty()) {
                // no arguments: toggle the current state
                bool cur = db->get_inventory_debug();
                db->set_inventory_debug(!cur);
                bool now = db->get_inventory_debug();
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(std::string("inventory debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = args[0]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_inventory_debug(true);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("inventory debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_inventory_debug(false);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("inventory debug disabled")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: invdebug [on|off]")));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto param = event.get_parameter("mode");
            if (!std::holds_alternative<std::string>(param) || std::get<std::string>(param).empty()) {
                // toggle current state when no explicit mode provided
                bool cur = db->get_inventory_debug();
                db->set_inventory_debug(!cur);
                bool now = db->get_inventory_debug();
                event.reply(dpp::message().add_embed(bronx::success(std::string("inventory debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = std::get<std::string>(param);
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_inventory_debug(true);
                event.reply(dpp::message().add_embed(bronx::success("inventory debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_inventory_debug(false);
                event.reply(dpp::message().add_embed(bronx::success("inventory debug disabled")));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("usage: /invdebug [mode:on|off]")));
            }
        }, { dpp::command_option(dpp::co_string, "mode", "on or off (omit to toggle)", false) });
    cmds.push_back(&invdbg);

    static Command dbdebug("dbdebug", "enable/disable verbose database connection logging (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args){
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("owner only")));
                return;
            }
            if (args.empty()) {
                bool cur = db->get_connection_debug();
                db->set_connection_debug(!cur);
                bool now = db->get_connection_debug();
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(std::string("connection debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = args[0]; std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_connection_debug(true);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("connection debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_connection_debug(false);
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success("connection debug disabled")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::error("usage: dbdebug [on|off]")));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("owner only")));
                return;
            }
            auto param = event.get_parameter("mode");
            if (!std::holds_alternative<std::string>(param) || std::get<std::string>(param).empty()) {
                bool cur = db->get_connection_debug();
                db->set_connection_debug(!cur);
                bool now = db->get_connection_debug();
                event.reply(dpp::message().add_embed(bronx::success(std::string("connection debug ") + (now?"enabled":"disabled"))));
                return;
            }
            std::string a = std::get<std::string>(param);
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            if (a == "on" || a == "enable") {
                db->set_connection_debug(true);
                event.reply(dpp::message().add_embed(bronx::success("connection debug enabled")));
            } else if (a == "off" || a == "disable") {
                db->set_connection_debug(false);
                event.reply(dpp::message().add_embed(bronx::success("connection debug disabled")));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("usage: /dbdebug [mode:on|off]")));
            }
        }, { dpp::command_option(dpp::co_string, "mode", "on or off (omit to toggle)", false) });
    cmds.push_back(&dbdebug);

    // Presence command
    static Command presence("presence", "change bot presence/status (owner only)", "owner", {"status", "activity"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            
            if (args.empty()) {
                ::std::string description = "**presence guide**\n\n";
                description += "**types:**\n";
                description += "• `online` - Online (green)\n";
                description += "• `idle` - Idle (yellow)\n";
                description += "• `dnd` - Do Not Disturb (red)\n";
                description += "• `invisible` - Invisible/Offline\n\n";
                description += "**activity types:**\n";
                description += "• `playing` - Playing <text>\n";
                description += "• `listening` - Listening to <text>\n";
                description += "• `watching` - Watching <text>\n";
                description += "• `streaming` - Streaming <text> <url>\n";
                description += "• `competing` - Competing in <text>\n\n";
                description += "**examples:**\n";
                description += "```\n";
                description += "b.presence online playing with commands\n";
                description += "b.presence dnd listening to music\n";
                description += "b.presence idle watching you\n";
                description += "b.presence online streaming https://twitch.tv/example Live!\n";
                description += "```";
                
                auto embed = bronx::create_embed(description);
                embed.set_color(0x7289DA);
                bronx::add_invoker_footer(embed, event.msg.author);
                
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Usage: `b.presence <status> <activity_type> <text> [url]`")));
                return;
            }
            
            ::std::string status_str = args[0];
            ::std::string activity_type = args[1];
            
            // Parse status
            dpp::presence_status status = dpp::ps_online;
            if (status_str == "idle") status = dpp::ps_idle;
            else if (status_str == "dnd") status = dpp::ps_dnd;
            else if (status_str == "invisible" || status_str == "offline") status = dpp::ps_invisible;
            
            // Parse activity type
            dpp::activity_type type = dpp::at_game;
            if (activity_type == "listening") type = dpp::at_listening;
            else if (activity_type == "watching") type = dpp::at_watching;
            else if (activity_type == "streaming") type = dpp::at_streaming;
            else if (activity_type == "competing") type = dpp::at_competing;
            
            // Get activity text (remaining args)
            ::std::string text;
            ::std::string url;
            
            if (type == dpp::at_streaming && args.size() >= 4) {
                // For streaming: status type url text...
                url = args[2];
                for (size_t i = 3; i < args.size(); i++) {
                    text += args[i];
                    if (i < args.size() - 1) text += " ";
                }
            } else {
                // For other types: status type text...
                for (size_t i = 2; i < args.size(); i++) {
                    text += args[i];
                    if (i < args.size() - 1) text += " ";
                }
            }
            
            if (text.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Activity text cannot be empty!")));
                return;
            }
            
            // Set presence
            bot.set_presence(dpp::presence(status, type, text));
            
            // Confirm
            ::std::string status_name = status_str;
            ::std::string type_name = activity_type;
            
            ::std::string description = "**presence updated**\n\n";
            description += "**status:** " + status_name + "\n";
            description += "**activity:** " + type_name + " " + text;
            if (!url.empty()) {
                description += "\n**url:** " + url;
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_color(0x7289DA);
            bronx::add_invoker_footer(embed, event.msg.author);
            bot.message_create(dpp::message(event.msg.channel_id, embed));
            return;
        });
    cmds.push_back(&presence);

    // Suggestions management (owner only)
    static Command suggestions_cmd("suggestions", "view and manage user suggestions (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }

            uint64_t owner_id = event.msg.author.id;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(commands::owner::suggest_mutex);
                msg = commands::owner::build_suggestions_message(db, owner_id);
            }
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        });
    cmds.push_back(&suggestions_cmd);

    // Command history viewer (owner only)
    static Command cmdhistory_cmd("cmdh", "view command history and balance changes for a user (owner only)", "owner", {"cmdhistory", "history"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            
            uint64_t owner_id = event.msg.author.id;
            bool target_invalid = false;
            dpp::message msg;
            {
                std::lock_guard<std::recursive_mutex> lock(commands::owner::cmdhistory_mutex);
                auto& state = commands::owner::cmdhistory_states[owner_id];
                
                if (!args.empty()) {
                    uint64_t target_id = 0;
                    if (!event.msg.mentions.empty()) {
                        target_id = event.msg.mentions[0].first.id;
                    } else {
                        target_id = parse_snowflake(args[0]);
                    }
                    
                    if (target_id == 0) target_invalid = true;
                    else {
                        state.target_user = target_id;
                        state.current_page = 0;
                    }
                }
                
                if (!target_invalid) {
                    msg = commands::owner::build_cmdhistory_message(db, owner_id);
                }
            }
            
            if (target_invalid) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("could not parse a valid user ID from `" + args[0] + "`")));
            } else {
                msg.set_channel_id(event.msg.channel_id);
                bot.message_create(msg);
            }
        });
    cmds.push_back(&cmdhistory_cmd);

    // Give money command (owner only) with payout syntax and alias
    static Command* givemoney = new Command("givemoney", "add or remove money from users (owner only)", "owner", {"payout"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Usage: `givemoney <user(s)/all/everyone> <amount>` (you may also prefix with add/remove as before)")));
                return;
            }

            // optionally consume a leading add/remove token for backwards compatibility
            size_t arg_index = 0;
            ::std::string action = "";
            if (args.size() >= 2) {
                ::std::string first = args[0];
                std::transform(first.begin(), first.end(), first.begin(), ::tolower);
                if (first == "add" || first == "remove") {
                    action = first;
                    arg_index = 1;
                    if (arg_index >= args.size() - 1) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Usage: `givemoney <user(s)/all/everyone> <amount>`")));
                        return;
                    }
                }
            }

            // Amount is always the last argument
            ::std::string amount_str = args.back();
            bool neg_from_string = false;
            if (!amount_str.empty() && amount_str[0] == '-') {
                neg_from_string = true;
                amount_str = amount_str.substr(1);
            }
            int64_t amount;
            try {
                amount = parse_amount(amount_str, INT64_MAX);
            } catch (const ::std::exception& e) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error(::std::string("Invalid amount: ") + e.what())));
                return;
            }
            if (neg_from_string) {
                amount = -amount;
            }
            if (action == "remove") {
                amount = -::std::abs(amount);
            }

            // Check for forbidden payout targets (roles, servers)
            // Check for role mentions
            if (!event.msg.mention_roles.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Payouts to roles are not permitted for security reasons.")));
                return;
            }
            
            // Check for forbidden keywords
            for (size_t i = arg_index; i + 1 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "server" || lower == "guild" || lower == "role") {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::error("Payouts to entire servers or roles are not permitted for security reasons.")));
                    return;
                }
            }

            // determine if applying to everyone
            bool apply_to_all = false;
            for (size_t i = arg_index; i + 1 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "all" || lower == "everyone") {
                    apply_to_all = true;
                    break;
                }
            }

            if (apply_to_all) {
                auto user_ids = db->get_all_user_ids();
                if (user_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id, 
                        bronx::error("No users found in database.")));
                    return;
                }
                int success_count = 0;
                for (uint64_t user_id : user_ids) {
                    if (db->update_wallet(user_id, amount).has_value()) {
                        success_count++;
                    }
                }
                ::std::string description;
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to **" + ::std::to_string(success_count) + "** users!";
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from **" + ::std::to_string(success_count) + "** users!";
                }
                auto embed = bronx::create_embed(description);
                embed.set_color(0x43B581);
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }
            
            // Handle multiple users - collect all user IDs from args (except the last one which is amount)
            ::std::vector<uint64_t> target_ids;
            
            // First, add all mentioned users
            for (const auto& mention : event.msg.mentions) {
                target_ids.push_back(mention.first.id);
            }
            
            // Then parse any user IDs from args (all args except the last one).
            // Only treat an argument as an ID if it is entirely numeric; plaintext
            // names (e.g. "123abc") will be ignored here and must be mentioned or
            // resolved by Discord so that event.msg.mentions catches them.
            for (size_t i = arg_index; i + 1 < args.size(); i++) {
                const std::string& tok = args[i];
                if (tok.empty())
                    continue;
                bool all_digits = std::all_of(tok.begin(), tok.end(), ::isdigit);
                if (!all_digits)
                    continue; // not a pure number, probably a username starting with digit
                try {
                    uint64_t user_id = ::std::stoull(tok);
                    // Check if not already added from mentions
                    if (::std::find(target_ids.begin(), target_ids.end(), user_id) == target_ids.end()) {
                        target_ids.push_back(user_id);
                    }
                } catch (...) {
                    // Shouldn't happen since we checked digits, but just in case
                }
            }
            
            if (target_ids.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("No valid users found. Please mention users or provide valid user IDs.")));
                return;
            }
            
            // Apply to all target users
            int success_count = 0;
            ::std::string users_list = "";
            
            for (uint64_t target_id : target_ids) {
                auto result = db->update_wallet(target_id, amount);
                if (result.has_value()) {
                    success_count++;
                    users_list += "<@" + ::std::to_string(target_id) + "> ";
                }
            }
            
            if (success_count == 0) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("Failed to update any user wallets.")));
                return;
            }
            
            ::std::string description;
            if (target_ids.size() == 1) {
                // Single user - show new balance
                auto new_balance = db->get_wallet(target_ids[0]);
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to <@" + ::std::to_string(target_ids[0]) + ">!\nnew balance: $" + format_number(new_balance);
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from <@" + ::std::to_string(target_ids[0]) + ">!\nnew balance: $" + format_number(new_balance);
                }
            } else {
                // Multiple users
                if (amount >= 0) {
                    description = "gave **$" + format_number(amount) + "** to **" + ::std::to_string(success_count) + "** user(s):\n" + users_list;
                } else {
                    description = "took **$" + format_number(::std::abs(amount)) + "** from **" + ::std::to_string(success_count) + "** user(s):\n" + users_list;
                }
            }
            
            auto embed = bronx::create_embed(description);
            embed.set_color(0x43B581);
            bronx::add_invoker_footer(embed, event.msg.author);
            
            bot.message_create(dpp::message(event.msg.channel_id, embed));
        });
    cmds.push_back(givemoney);

    // Give item command (owner only) with similar syntax to givemoney
    static Command* giveitem = new Command("giveitem", "add or remove items from users (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.size() < 3) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `giveitem <user(s)/all/everyone> <item_id> <quantity>` (optionally prefix add/remove)")));
                return;
            }

            size_t arg_index = 0;
            ::std::string action = "";
            if (args.size() >= 3) {
                ::std::string first = args[0];
                std::transform(first.begin(), first.end(), first.begin(), ::tolower);
                if (first == "add" || first == "remove") {
                    action = first;
                    arg_index = 1;
                    if (arg_index >= args.size() - 2) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Usage: `giveitem <user(s)/all/everyone> <item_id> <quantity>`")));
                        return;
                    }
                }
            }

            ::std::string item_id = args[args.size() - 2];
            ::std::string qty_str = args.back();
            int quantity = 0;
            try {
                quantity = std::stoi(qty_str);
            } catch (...) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid quantity.")));
                return;
            }
            if (action == "remove") quantity = -std::abs(quantity);

            bool apply_to_all = false;
            for (size_t i = arg_index; i + 2 < args.size(); ++i) {
                ::std::string tok = args[i];
                ::std::string lower = tok;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "all" || lower == "everyone") {
                    apply_to_all = true;
                    break;
                }
            }

            ::std::vector<uint64_t> target_ids;
            if (!apply_to_all) {
                for (const auto& mention : event.msg.mentions) {
                    target_ids.push_back(mention.first.id);
                }
                for (size_t i = arg_index; i + 2 < args.size(); ++i) {
                    const std::string& tok = args[i];
                    if (tok.empty()) continue;
                    bool all_digits = std::all_of(tok.begin(), tok.end(), ::isdigit);
                    if (!all_digits) continue;
                    try {
                        uint64_t user_id = ::std::stoull(tok);
                        if (::std::find(target_ids.begin(), target_ids.end(), user_id) == target_ids.end()) {
                            target_ids.push_back(user_id);
                        }
                    } catch (...) {}
                }
                if (target_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("No valid users found. Mention users or provide numeric IDs.")));
                    return;
                }
            }

            if (apply_to_all) {
                target_ids = db->get_all_user_ids();
                if (target_ids.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::error("No users found in database to give items to.")));
                    return;
                }
            }

            int success_count = 0;
            for (uint64_t uid : target_ids) {
                bool ok;
                if (quantity >= 0) {
                    ok = db->add_item(uid, item_id, "other", quantity, "", 1);
                } else {
                    ok = db->remove_item(uid, item_id, -quantity);
                }
                if (ok) success_count++;
            }

            ::std::string description;
            if (apply_to_all) {
                if (target_ids.empty()) {
                    description = "no users available";
                } else if (success_count == 0) {
                    description = "attempted to give items but none of the users could be updated (check DB logs)";
                } else if (quantity >= 0) {
                    description = "gave **" + ::std::to_string(quantity) + "x " + item_id + "** to **" + ::std::to_string(success_count) + "** users!";
                } else {
                    description = "removed **" + ::std::to_string(-quantity) + "x " + item_id + "** from **" + ::std::to_string(success_count) + "** users!";
                }
            } else if (target_ids.size() == 1) {
                if (quantity >= 0)
                    description = "gave <@" + ::std::to_string(target_ids[0]) + "> **" + ::std::to_string(quantity) + "x " + item_id + "**";
                else
                    description = "removed <@" + ::std::to_string(target_ids[0]) + "> **" + ::std::to_string(-quantity) + "x " + item_id + "**";
            } else {
                if (quantity >= 0)
                    description = "gave **" + ::std::to_string(quantity) + "x " + item_id + "** to **" + ::std::to_string(success_count) + "** users";
                else
                    description = "removed **" + ::std::to_string(-quantity) + "x " + item_id + "** from **" + ::std::to_string(success_count) + "** users";
            }
            auto embed = bronx::create_embed(description);
            embed.set_color(0x43B581);
            bronx::add_invoker_footer(embed, event.msg.author);
            bot.message_create(dpp::message(event.msg.channel_id, embed));
        });
    cmds.push_back(giveitem);

    // BAC (Bronx AntiCheat) — global ban management
    static Command* blacklist_cmd = new Command("bac", "manage BAC global bans (owner only)", "owner", {"blacklist"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.bac <add|remove|list> -u <user> [-r <reason>] [-s]`")));
                return;
            }
            ::std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);

            auto parse_id = [&](const ::std::string& str) -> uint64_t {
                ::std::string idstr = str;
                if (idstr.find("<@") == 0 || idstr.find("<@!") == 0) {
                    size_t start = idstr.find_first_of("0123456789");
                    size_t end = idstr.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        idstr = idstr.substr(start, end - start + 1);
                    }
                }
                return ::std::stoull(idstr);
            };

            if (action == "list") {
                auto list = db->get_global_blacklist();
                if (list.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::info("\U0001f6e1\ufe0f BAC ban list is empty.")));
                } else {
                    ::std::string out = "";
                    for (const auto& entry : list) {
                        out += "<@" + ::std::to_string(entry.user_id) + ">";
                        if (!entry.reason.empty()) {
                            out += " — " + entry.reason;
                        }
                        out += "\n";
                    }
                    dpp::embed eb = bronx::info("\U0001f6e1\ufe0f BAC — Banned Users");
                    eb.set_description(out);
                    bot.message_create(dpp::message(event.msg.channel_id, eb));
                }
                return;
            }

            if (action != "add" && action != "remove") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid action. Use add, remove or list.")));
                return;
            }

            // find -u flag, -r flag, and -s (silent) flag
            uint64_t target_id = 0;
            ::std::string reason = "";
            bool silent = false;
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "-s") {
                    silent = true;
                } else if (args[i] == "-u" && i + 1 < args.size()) {
                    try {
                        target_id = parse_id(args[i+1]);
                    } catch (...) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Invalid user identifier.")));
                        return;
                    }
                    ++i; // skip the value
                } else if (args[i] == "-r" && i + 1 < args.size()) {
                    // Collect all remaining args after -r as reason
                    for (size_t j = i + 1; j < args.size(); ++j) {
                        if (args[j] == "-u" || args[j] == "-s") break; // stop if we hit another flag
                        if (!reason.empty()) reason += " ";
                        reason += args[j];
                    }
                    break; // done parsing
                }
            }
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You must specify a user with `-u <user>`.")));
                return;
            }

            bool ok = false;
            if (action == "add") {
                ok = db->add_global_blacklist(target_id, reason.empty() ? "(BAC) manual ban" : "(BAC) " + reason);
                if (ok) {
                    if (!silent) {
                        // Send DM to banned user about appeal with embed and button
                        auto ban_embed = bronx::create_embed(
                            "\U0001f6e1\ufe0f **Bronx AntiCheat (BAC)**\n\n"
                            "you have been **banned** from using this bot.\n\n"
                            "if you believe this was a mistake or would like to appeal, please join our support server.",
                            bronx::COLOR_ERROR);
                        ban_embed.set_title("BAC — Banned");
                        dpp::message dm_msg;
                        dm_msg.add_embed(ban_embed);
                        dm_msg.add_component(
                            dpp::component()
                                .set_type(dpp::cot_action_row)
                                .add_component(
                                    dpp::component()
                                        .set_type(dpp::cot_button)
                                        .set_label("appeal in support server")
                                        .set_style(dpp::cos_link)
                                        .set_url(bronx::SUPPORT_SERVER_URL)
                                )
                        );
                        bot.direct_message_create(target_id, dm_msg);
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::success("\U0001f6e1\ufe0f BAC — user banned. they have been sent a DM with appeal information.")));
                    } else {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::success("\U0001f6e1\ufe0f BAC — user silently banned. no DM was sent.")));
                    }
                }
            } else if (action == "remove") {
                ok = db->remove_global_blacklist(target_id);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("\U0001f6e1\ufe0f BAC — user unbanned.")));
            }
            if (!ok) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed. Check logs.")));
            }
        });
    cmds.push_back(blacklist_cmd);

    // Global whitelist management
    static Command* whitelist_cmd = new Command("whitelist", "manage global command whitelist (owner only)", "owner", {}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.whitelist <add|remove|list> -u <user> [-r <reason>]`")));
                return;
            }
            ::std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);

            auto parse_id = [&](const ::std::string& str) -> uint64_t {
                ::std::string idstr = str;
                if (idstr.find("<@") == 0 || idstr.find("<@!") == 0) {
                    size_t start = idstr.find_first_of("0123456789");
                    size_t end = idstr.find_last_of("0123456789");
                    if (start != ::std::string::npos && end != ::std::string::npos) {
                        idstr = idstr.substr(start, end - start + 1);
                    }
                }
                return ::std::stoull(idstr);
            };

            if (action == "list") {
                auto list = db->get_global_whitelist();
                if (list.empty()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::info("Global whitelist is empty.")));
                } else {
                    ::std::string out = "";
                    for (const auto& entry : list) {
                        out += "<@" + ::std::to_string(entry.user_id) + ">";
                        if (!entry.reason.empty()) {
                            out += " - " + entry.reason;
                        }
                        out += "\n";
                    }
                    dpp::embed eb = bronx::info("Whitelisted users");
                    eb.set_description(out);
                    bot.message_create(dpp::message(event.msg.channel_id, eb));
                }
                return;
            }

            if (action != "add" && action != "remove") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid action. Use add, remove or list.")));
                return;
            }

            // find -u flag and -r flag
            uint64_t target_id = 0;
            ::std::string reason = "";
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "-u" && i + 1 < args.size()) {
                    try {
                        target_id = parse_id(args[i+1]);
                    } catch (...) {
                        bot.message_create(dpp::message(event.msg.channel_id,
                            bronx::error("Invalid user identifier.")));
                        return;
                    }
                    ++i; // skip the value
                } else if (args[i] == "-r" && i + 1 < args.size()) {
                    // Collect all remaining args after -r as reason
                    for (size_t j = i + 1; j < args.size(); ++j) {
                        if (args[j] == "-u") break; // stop if we hit another flag
                        if (!reason.empty()) reason += " ";
                        reason += args[j];
                    }
                    break; // done parsing
                }
            }
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("You must specify a user with `-u <user>`.")));
                return;
            }

            bool ok = false;
            if (action == "add") {
                ok = db->add_global_whitelist(target_id, reason);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("User added to global whitelist.")));
            } else if (action == "remove") {
                ok = db->remove_global_whitelist(target_id);
                if (ok) bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("User removed from global whitelist.")));
            }
            if (!ok) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed. Check logs.")));
            }
        });
    cmds.push_back(whitelist_cmd);

    // per-guild module toggle (enable/disable entire category)
    static Command* module_cmd = new Command("module", "enable or disable a module. scope: -u <user> -r <role> -c <channel> -e (exclusive)", "utility", {}, true,
        [db, handler](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.module <name> <enable|disable> [-c <channel>] [-u <user>] [-r <role>] [-e]`")));
                return;
            }
            std::string mod = args[0];
            std::string action = args[1];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            bool enable;
            if (action == "enable") enable = true;
            else if (action == "disable") enable = false;
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Action must be 'enable' or 'disable'.")));
                return;
            }
            std::string scope_type;
            uint64_t scope_id;
            bool exclusive;
            if (!parse_scope_args(args, 2, scope_type, scope_id, exclusive)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid scope. Use `-c <channel>`, `-u <user>`, `-r <role>`, `-e` (exclusive, only with channel)")));
                return;
            }
            // Prevent disabling the owner or utility modules to avoid lockout
            if (!enable && (mod == "owner" || mod == "utility") && scope_type == "guild") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Cannot disable the '" + mod + "' module - this would prevent you from managing permissions!")));
                return;
            }
            bool ok = db->set_guild_module_enabled(event.msg.guild_id, mod, enable, scope_type, scope_id, exclusive);
            if (ok) {
                if (handler) handler->notify_module_state_changed(event.msg.guild_id, mod, enable);
                std::string msg = std::string("Module '") + mod + " " + (enable ? "enabled" : "disabled");
                if (scope_type != "guild") {
                    msg += " for ";
                    msg += scope_type + " ";
                    msg += "`" + std::to_string(scope_id) + "`";
                    if (exclusive) msg += " (exclusive)";
                }
                msg += ".";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(msg)));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed.")));
            }
        },
        // slash handler
        [db, handler](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }
            auto get_str=[&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            auto get_bool=[&](const std::string &name)->bool {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<bool>(p)) return std::get<bool>(p);
                return false;
            };
            std::string mod = get_str("module");
            std::string action = get_str("action");
            std::string scope = get_str("scope");
            std::string target = get_str("target");
            bool exclusive = get_bool("exclusive");
            if (!mod.empty() && !action.empty()) {
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                bool enable;
                if (action == "enable") enable = true;
                else if (action == "disable") enable = false;
                else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Action must be 'enable' or 'disable'.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string scope_type;
                uint64_t scope_id;
                if (scope.empty()) {
                    scope_type = "guild";
                    scope_id = 0;
                } else {
                    std::vector<std::string> tmp = {scope, target};
                    bool excl_dummy;
                    if (!parse_scope_args(tmp, 0, scope_type, scope_id, excl_dummy)) {
                        event.reply(dpp::ir_channel_message_with_source,
                            dpp::message().add_embed(bronx::error("Invalid scope/type or target.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                // Validate exclusive mode
                if (exclusive && scope_type != "channel") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Exclusive mode (-e) can only be used with channel scope.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                // Prevent disabling the owner or utility modules to avoid lockout
                if (!enable && (mod == "owner" || mod == "utility") && scope_type == "guild") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + mod + "' module - this would prevent you from managing permissions!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                bool ok = db->set_guild_module_enabled(gid, mod, enable, scope_type, scope_id, exclusive);
                if (ok) {
                    if (handler) handler->notify_module_state_changed(gid, mod, enable);
                    std::string msg = std::string("Module '") + mod + " " + (enable ? "enabled" : "disabled");
                    if (scope_type != "guild") {
                        msg += " for ";
                        msg += scope_type + " ";
                        msg += "`" + std::to_string(scope_id) + "`";
                        if (exclusive) msg += " (exclusive)";
                    }
                    msg += ".";
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success(msg)).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Database operation failed.")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            auto categories = handler->get_commands_by_category();
            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder("select a module")
                  .set_id("owner_mod_select_" + std::to_string(uid));
            for (const auto& [cat, cmds] : categories) {
                select.add_select_option(dpp::select_option(cat, cat));
            }
            dpp::message msg;
            msg.add_component(dpp::component().add_component(select));
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
        }, std::vector<dpp::command_option>{
            dpp::command_option(dpp::co_string, "module", "module name (leave blank for interactive)", false),
            dpp::command_option(dpp::co_string, "action", "enable or disable", false),
            dpp::command_option(dpp::co_string, "scope", "guild/channel/role/user", false),
            dpp::command_option(dpp::co_string, "target", "ID or mention of channel/role/user", false),
            dpp::command_option(dpp::co_boolean, "exclusive", "exclusive mode (only for channels)", false)
        });
    cmds.push_back(module_cmd);

    // per-guild command toggle
    static Command* toggle_cmd = new Command("command", "enable or disable a command. scope: -u <user> -r <role> -c <channel> -e (exclusive)", "utility", {}, true,
        [db, handler](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            if (args.size() < 2) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Usage: `b.command <name> <enable|disable> [-c <channel>] [-u <user>] [-r <role>] [-e]`")));
                return;
            }
            std::string cmd = args[0];
            std::string action = args[1];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            bool enable;
            if (action == "enable") enable = true;
            else if (action == "disable") enable = false;
            else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Action must be 'enable' or 'disable'.")));
                return;
            }
            std::string scope_type;
            uint64_t scope_id;
            bool exclusive;
            if (!parse_scope_args(args, 2, scope_type, scope_id, exclusive)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Invalid scope. Use `-c <channel>`, `-u <user>`, `-r <role>`, `-e` (exclusive, only with channel)")));
                return;
            }
            // Prevent disabling critical permission commands to avoid lockout
            if (!enable && (cmd == "module" || cmd == "command") && scope_type == "guild") {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Cannot disable the '" + cmd + "' command - this would prevent you from re-enabling it!")));
                return;
            }
            bool ok = db->set_guild_command_enabled(event.msg.guild_id, cmd, enable, scope_type, scope_id, exclusive);
            if (ok) {
                // Invalidate cache so the change takes effect immediately
                handler->notify_command_state_changed(event.msg.guild_id, cmd, enable);
                
                std::string msg = std::string("Command '") + cmd + " " + (enable ? "enabled" : "disabled");
                if (scope_type != "guild") {
                    msg += " for ";
                    msg += scope_type + " ";
                    msg += "`" + std::to_string(scope_id) + "`";
                    if (exclusive) msg += " (exclusive)";
                }
                msg += ".";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::success(msg)));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("Database operation failed.")));
            }
        },
        // slash handler
        [db, handler](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }
            auto get_str=[&](const std::string &name)->std::string {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<std::string>(p)) return std::get<std::string>(p);
                return "";
            };
            auto get_bool=[&](const std::string &name)->bool {
                auto p = event.get_parameter(name);
                if (std::holds_alternative<bool>(p)) return std::get<bool>(p);
                return false;
            };
            std::string cmd = get_str("command");
            std::string action = get_str("action");
            std::string scope = get_str("scope");
            std::string target = get_str("target");
            bool exclusive = get_bool("exclusive");
            if (!cmd.empty() && !action.empty()) {
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                bool enable;
                if (action == "enable") enable = true;
                else if (action == "disable") enable = false;
                else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Action must be 'enable' or 'disable'.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                std::string scope_type;
                uint64_t scope_id;
                if (scope.empty()) {
                    scope_type = "guild";
                    scope_id = 0;
                } else {
                    std::vector<std::string> tmp = {scope, target};
                    bool excl_dummy;
                    if (!parse_scope_args(tmp, 0, scope_type, scope_id, excl_dummy)) {
                        event.reply(dpp::ir_channel_message_with_source,
                            dpp::message().add_embed(bronx::error("Invalid scope/type or target.")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                }
                // Validate exclusive mode
                if (exclusive && scope_type != "channel") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Exclusive mode (-e) can only be used with channel scope.")).set_flags(dpp::m_ephemeral));
                    return;
                }
                // Prevent disabling critical permission commands to avoid lockout
                if (!enable && (cmd == "module" || cmd == "command") && scope_type == "guild") {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Cannot disable the '" + cmd + "' command - this would prevent you from re-enabling it!")).set_flags(dpp::m_ephemeral));
                    return;
                }
                bool ok = db->set_guild_command_enabled(gid, cmd, enable, scope_type, scope_id, exclusive);
                if (ok) {
                    // Invalidate cache so the change takes effect immediately
                    handler->notify_command_state_changed(gid, cmd, enable);
                    
                    std::string msg = std::string("Command '") + cmd + " " + (enable ? "enabled" : "disabled");
                    if (scope_type != "guild") {
                        msg += " for ";
                        msg += scope_type + " ";
                        msg += "`" + std::to_string(scope_id) + "`";
                        if (exclusive) msg += " (exclusive)";
                    }
                    msg += ".";
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::success(msg)).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Database operation failed.")).set_flags(dpp::m_ephemeral));
                }
                return;
            }
            auto categories = handler->get_commands_by_category();
            // collect unique command names
            std::set<std::string> seen;
            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder("select a command")
                  .set_id("owner_cmd_select_" + std::to_string(uid));
            for (auto& [cat, cmds] : categories) {
                for (auto* cmd : cmds) {
                    if (seen.insert(cmd->name).second) {
                        select.add_select_option(dpp::select_option(cmd->name, cmd->name));
                    }
                }
            }
            dpp::message msg;
            msg.add_component(dpp::component().add_component(select));
            msg.set_flags(dpp::m_ephemeral);
            event.reply(dpp::ir_channel_message_with_source, msg);
        }, std::vector<dpp::command_option>{
            dpp::command_option(dpp::co_string, "command", "command name (leave blank for interactive)", false),
            dpp::command_option(dpp::co_string, "action", "enable or disable", false),
            dpp::command_option(dpp::co_string, "scope", "guild/channel/role/user", false),
            dpp::command_option(dpp::co_string, "target", "ID or mention of channel/role/user", false),
            dpp::command_option(dpp::co_boolean, "exclusive", "exclusive mode (only for channels)", false)
        });
    cmds.push_back(toggle_cmd);

    // show current module/command permission settings for this guild
    static Command* permissions_cmd = new Command("permissions", "show all module and command permission settings for this guild", "utility", {}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& /*args*/) {
            bool allowed = is_owner(event.msg.author.id);
            if (!allowed) {
                for (const auto& rid : event.msg.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to administrators or the bot owner.")));
                return;
            }
            if (event.msg.guild_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command can only be used in a guild.")));
                return;
            }
            uint64_t gid = event.msg.guild_id;

            // helper to format a scope mention
            auto fmt_scope = [](const std::string& scope_type, uint64_t scope_id) -> std::string {
                if (scope_type == "channel") return "<#" + std::to_string(scope_id) + ">";
                if (scope_type == "role")    return "<@&" + std::to_string(scope_id) + ">";
                if (scope_type == "user")    return "<@" + std::to_string(scope_id) + ">";
                return std::to_string(scope_id);
            };

            // Gather data
            auto mod_settings = db->get_all_module_settings(gid);
            auto cmd_settings = db->get_all_command_settings(gid);
            auto mod_scopes   = db->get_all_module_scope_settings(gid);
            auto cmd_scopes   = db->get_all_command_scope_settings(gid);

            bool has_any = !mod_settings.empty() || !cmd_settings.empty() || !mod_scopes.empty() || !cmd_scopes.empty();
            if (!has_any) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::info("No custom permission settings configured for this server. All modules and commands are at their defaults.")));
                return;
            }

            std::string desc;

            // Module guild-wide settings
            if (!mod_settings.empty()) {
                desc += "**__Module Settings (guild-wide)__**\n";
                for (const auto& ms : mod_settings) {
                    desc += (ms.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + ms.module + "** — " + (ms.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            // Module scoped overrides
            if (!mod_scopes.empty()) {
                desc += "**__Module Scope Overrides__**\n";
                for (const auto& s : mod_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + s.name + "** → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
                desc += "\n";
            }

            // Command guild-wide settings
            if (!cmd_settings.empty()) {
                desc += "**__Command Settings (guild-wide)__**\n";
                for (const auto& cs : cmd_settings) {
                    desc += (cs.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + cs.command + "` — " + (cs.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            // Command scoped overrides
            if (!cmd_scopes.empty()) {
                desc += "**__Command Scope Overrides__**\n";
                for (const auto& s : cmd_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + s.name + "` → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
            }

            // Truncate if too long for a single embed (Discord limit ~4096)
            if (desc.size() > 4000) {
                desc = desc.substr(0, 3990) + "\n*...truncated*";
            }

            auto embed = bronx::create_embed(desc);
            embed.set_title("⚙️ Permission Settings");
            embed.set_color(0x5865F2);
            embed.set_footer(dpp::embed_footer().set_text("Use 'module' and 'command' to modify these settings"));
            embed.set_timestamp(time(0));
            bronx::add_invoker_footer(embed, event.msg.author);

            dpp::message msg(event.msg.channel_id, "");
            msg.add_embed(embed);
            msg.set_reference(event.msg.id);
            bot.message_create(msg);
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t uid = event.command.get_issuing_user().id;
            bool allowed = is_owner(uid);
            if (!allowed) {
                for (const auto& rid : event.command.member.get_roles()) {
                    dpp::role* r = dpp::find_role(rid);
                    if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                        allowed = true;
                        break;
                    }
                }
            }
            if (!allowed) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command is restricted to administrators or the bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t gid = event.command.guild_id;
            if (gid == 0) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("This command can only be used in a guild.")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto fmt_scope = [](const std::string& scope_type, uint64_t scope_id) -> std::string {
                if (scope_type == "channel") return "<#" + std::to_string(scope_id) + ">";
                if (scope_type == "role")    return "<@&" + std::to_string(scope_id) + ">";
                if (scope_type == "user")    return "<@" + std::to_string(scope_id) + ">";
                return std::to_string(scope_id);
            };

            auto mod_settings = db->get_all_module_settings(gid);
            auto cmd_settings = db->get_all_command_settings(gid);
            auto mod_scopes   = db->get_all_module_scope_settings(gid);
            auto cmd_scopes   = db->get_all_command_scope_settings(gid);

            bool has_any = !mod_settings.empty() || !cmd_settings.empty() || !mod_scopes.empty() || !cmd_scopes.empty();
            if (!has_any) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::info("No custom permission settings configured for this server. All modules and commands are at their defaults.")).set_flags(dpp::m_ephemeral));
                return;
            }

            std::string desc;

            if (!mod_settings.empty()) {
                desc += "**__Module Settings (guild-wide)__**\n";
                for (const auto& ms : mod_settings) {
                    desc += (ms.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + ms.module + "** — " + (ms.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            if (!mod_scopes.empty()) {
                desc += "**__Module Scope Overrides__**\n";
                for (const auto& s : mod_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " **" + s.name + "** → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
                desc += "\n";
            }

            if (!cmd_settings.empty()) {
                desc += "**__Command Settings (guild-wide)__**\n";
                for (const auto& cs : cmd_settings) {
                    desc += (cs.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + cs.command + "` — " + (cs.enabled ? "enabled" : "disabled") + "\n";
                }
                desc += "\n";
            }

            if (!cmd_scopes.empty()) {
                desc += "**__Command Scope Overrides__**\n";
                for (const auto& s : cmd_scopes) {
                    desc += (s.enabled ? bronx::EMOJI_CHECK : bronx::EMOJI_DENY) + " `" + s.name + "` → " + s.scope_type + " " + fmt_scope(s.scope_type, s.scope_id);
                    desc += " — " + std::string(s.enabled ? "enabled" : "disabled");
                    if (s.exclusive) desc += " *(exclusive)*";
                    desc += "\n";
                }
            }

            if (desc.size() > 4000) {
                desc = desc.substr(0, 3990) + "\n*...truncated*";
            }

            auto embed = bronx::create_embed(desc);
            embed.set_title("⚙️ Permission Settings");
            embed.set_color(0x5865F2);
            embed.set_footer(dpp::embed_footer().set_text("Use /module and /command to modify these settings"));
            embed.set_timestamp(time(0));

            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
        });
    cmds.push_back(permissions_cmd);

    // Purge all data for a user (wipe from every table, cascading from users row)
    static Command purgeuser_cmd("purgeuser", "completely wipe a user's data from the database (owner only)", "owner", {"wipeuser", "clearuser", "resetuser"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("This command is restricted to the bot owner only.")));
                return;
            }
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("usage: purgeuser <user_id or @mention> [confirm]\n"
                                 "pass `confirm` to skip the warning and delete immediately.")));
                return;
            }

            uint64_t target_id = parse_snowflake(args[0]);
            if (target_id == 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("could not parse a valid user ID from `" + args[0] + "`")));
                return;
            }

            // Safety: require explicit "confirm" flag
            bool confirmed = (args.size() >= 2 && args[1] == "confirm");
            if (!confirmed) {
                std::string warn = bronx::EMOJI_WARNING + " **this will permanently delete ALL data** for <@" + std::to_string(target_id) + "> (`" + std::to_string(target_id) + "`):\n\n"
                    "economy, inventory, fish catches, autofisher, bazaar, XP, gambling stats, "
                    "command history, cooldowns, wishlists, trades, suggestions, bug reports, "
                    "reminders, and every other record tied to this user.\n\n"
                    "**this action cannot be undone.**\n\n"
                    "to proceed, run:\n```\npurgeuser " + std::to_string(target_id) + " confirm\n```";
                bot.message_create(dpp::message(event.msg.channel_id, bronx::create_embed(warn)));
                return;
            }

            // Build ordered delete queries.  Most child tables cascade from users,
            // but we explicitly delete from tables that might lack a FK or where
            // cascading order matters, then finish with the users row itself.
            std::string uid = std::to_string(target_id);
            std::vector<std::string> queries = {
                // Tables that may lack ON DELETE CASCADE or have unusual FK chains
                "DELETE FROM user_autofish_storage WHERE user_id = " + uid,
                "DELETE FROM user_autofishers WHERE user_id = " + uid,
                // The main delete — cascades to most child tables
                "DELETE FROM users WHERE user_id = " + uid,
            };

            int ok = db->execute_batch(queries);
            int total = static_cast<int>(queries.size());

            if (ok == total) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::success("purged all data for `" + uid + "` (" + std::to_string(ok) + "/" + std::to_string(total) + " statements succeeded)")));
            } else if (ok > 0) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::create_embed(bronx::EMOJI_WARNING + " partial purge: " + std::to_string(ok) + "/" + std::to_string(total) + " statements succeeded for `" + uid + "`. check logs for errors.")));
            } else {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::error("purge failed for `" + uid + "`: " + db->get_last_error())));
            }
        });
    cmds.push_back(&purgeuser_cmd);

    // Gambling audit command
    auto* gambling_audit = commands::owner::get_gambling_audit_owner_command(db);
    if (gambling_audit) {
        cmds.push_back(gambling_audit);
    }

    // Health command - shortcut to ostats page 9
    static Command health("health", "view real-time bot health & infrastructure (owner only)", "owner", {"h", "diag"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (!is_owner(event.msg.author.id)) {
                bot.message_create(dpp::message(event.msg.channel_id, 
                    bronx::error("restricted to bot owner.")));
                return;
            }
            dpp::message msg = commands::owner::build_ostats_message(bot, db, event.msg.author.id);
            msg.set_channel_id(event.msg.channel_id);
            bot.message_create(msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!is_owner(event.command.get_issuing_user().id)) {
                event.reply(dpp::message().add_embed(bronx::error("restricted to bot owner.")).set_flags(dpp::m_ephemeral));
                return;
            }
            dpp::message msg = commands::owner::build_ostats_message(bot, db, event.command.get_issuing_user().id);
            event.reply(msg);
        });
    cmds.push_back(&health);

    return cmds;
}

// Owner-specific interaction handlers (currently used by suggestions paginator)
void register_owner_interactions(dpp::cluster& bot, bronx::db::Database* db, CommandHandler* handler) {
    bot.on_interaction_create([&bot, db](const dpp::interaction_create_t& event) {
        // ── Modularized Owner Interactions ──
        if (commands::owner::handle_ostats_interaction(event, bot, db)) return;
        if (commands::owner::handle_audit_interaction(event, bot, db)) return;
    });
}

} // namespace commands
