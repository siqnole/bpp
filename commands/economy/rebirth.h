#pragma once
#include "helpers.h"
#include "../../database/operations/economy/history_operations.h"
#include "../title_utils.h"

using namespace bronx::db::history_operations;

namespace commands {
namespace economy {

// ============================================================================
// REBIRTH SYSTEM — Ultimate prestige beyond P20
// ============================================================================
// At Prestige 20, players can "Rebirth" which resets prestige back to 0
// but grants a permanent global earnings multiplier.
//
// Each rebirth grants a 1.1x multiplier (stacks multiplicatively).
// Max 5 rebirths → 1.1^5 = ~1.61x permanent boost to ALL earnings.
//
// Requirements scale with each rebirth level:
//   Rebirth 1: P20 + $50B networth
//   Rebirth 2: P20 + $100B networth + 10K fish caught + 5K ores mined
//   Rebirth 3: P20 + $250B networth + 25K fish + 15K ores + 5K gambles won
//   Rebirth 4: P20 + $500B networth + 50K fish + 30K ores + 15K gambles
//   Rebirth 5: P20 + $1T networth + 100K fish + 50K ores + 25K gambles
//
// Subcommands:
//   /rebirth            — view rebirth info + requirements
//   /rebirth confirm    — execute rebirth
// ============================================================================

// Progressive rebirth emojis based on level
static inline std::string get_rebirth_emoji(int level) {
    switch (level) {
        case 1: return "<:rebirth:1481426459200327720>";
        case 2: return "<:rebirth2:1481426460517601340>";
        case 3: return "<:rebirth3:1481427415195451452>";
        case 4: return "<:rebirth4:1481427416038510622>";
        case 5: return "<:rebirth5:1481427838400856197>";
        default: return "<:rebirth:1481426459200327720>"; // fallback to level 1
    }
}

// Requirements per rebirth level
struct RebirthRequirement {
    int level;                  // rebirth # (1-5)
    int required_prestige;      // must be at this prestige level
    int64_t required_networth;
    int64_t required_fish;      // total fish caught (stat)
    int64_t required_ores;      // total ores mined (stat)
    int64_t required_gambles;   // total gambling wins (stat)
    double multiplier_bonus;    // permanent multiplier granted
    std::string title;          // title display name
    std::string title_item_id;  // inventory item_id for the title
};

// Note: titles use get_rebirth_emoji(level) + " Title" format dynamically
static const std::vector<RebirthRequirement> REBIRTH_REQUIREMENTS = {
    {1, 20, 50000000000LL,    0,      0,     0,     1.1,  "<:rebirth:1481426459200327720> Reborn",        "title_reborn"},
    {2, 20, 100000000000LL,   10000,  5000,  0,     1.1,  "<:rebirth2:1481426460517601340> Twice Reborn",  "title_twice_reborn"},
    {3, 20, 250000000000LL,   25000,  15000, 5000,  1.1,  "<:rebirth3:1481427415195451452> Thrice Reborn", "title_thrice_reborn"},
    {4, 20, 500000000000LL,   50000,  30000, 15000, 1.1,  "<:rebirth4:1481427416038510622> Ascended",      "title_ascended"},
    {5, 20, 1000000000000LL,  100000, 50000, 25000, 1.1,  "<:rebirth5:1481427838400856197> Transcendent",  "title_transcendent"},
};

static const int MAX_REBIRTHS = 5;

struct RebirthState {
    int current_rebirths;
    double total_multiplier;    // 1.1^rebirths
    bool can_rebirth;
    
    // Current values
    int current_prestige;
    int64_t current_networth;
    int64_t current_fish;
    int64_t current_ores;
    int64_t current_gambles;
    
    // Required values (for next rebirth)
    const RebirthRequirement* next_req;
};

// ============================================================================
// Database — lazy table creation
// ============================================================================
static bool g_rebirth_tables_created = false;
static std::mutex g_rebirth_mutex;

static void ensure_rebirth_tables(Database* db) {
    if (g_rebirth_tables_created) return;
    std::lock_guard<std::mutex> lock(g_rebirth_mutex);
    if (g_rebirth_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS user_rebirths ("
        "  user_id BIGINT UNSIGNED PRIMARY KEY,"
        "  rebirth_level INT NOT NULL DEFAULT 0,"
        "  total_multiplier DOUBLE NOT NULL DEFAULT 1.0,"
        "  last_rebirth_at TIMESTAMP NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_rebirth_tables_created = true;
}

// ============================================================================
// DB helpers
// ============================================================================

static int get_rebirth_level(Database* db, uint64_t user_id) {
    ensure_rebirth_tables(db);
    std::string sql = "SELECT rebirth_level FROM user_rebirths WHERE user_id = " + std::to_string(user_id);
    MYSQL_RES* res = db_select(db, sql);
    int level = 0;
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) level = std::stoi(row[0]);
        mysql_free_result(res);
    }
    return level;
}

static double get_rebirth_multiplier(Database* db, uint64_t user_id) {
    ensure_rebirth_tables(db);
    std::string sql = "SELECT total_multiplier FROM user_rebirths WHERE user_id = " + std::to_string(user_id);
    MYSQL_RES* res = db_select(db, sql);
    double mult = 1.0;
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) mult = std::stod(row[0]);
        mysql_free_result(res);
    }
    return mult;
}

static RebirthState check_rebirth_state(Database* db, uint64_t user_id) {
    ensure_rebirth_tables(db);
    RebirthState state;
    
    state.current_rebirths = get_rebirth_level(db, user_id);
    state.total_multiplier = std::pow(1.1, state.current_rebirths);
    state.current_prestige = db->get_prestige(user_id);
    state.current_networth = db->get_networth(user_id);
    state.current_fish = db->get_stat(user_id, "fish_caught");
    state.current_ores = db->get_stat(user_id, "ores_mined");
    state.current_gambles = db->get_stat(user_id, "gambling_wins");
    
    state.next_req = nullptr;
    state.can_rebirth = false;
    
    if (state.current_rebirths < MAX_REBIRTHS) {
        state.next_req = &REBIRTH_REQUIREMENTS[state.current_rebirths];
        
        state.can_rebirth = (
            state.current_prestige >= state.next_req->required_prestige &&
            state.current_networth >= state.next_req->required_networth &&
            state.current_fish >= state.next_req->required_fish &&
            state.current_ores >= state.next_req->required_ores &&
            state.current_gambles >= state.next_req->required_gambles
        );
    }
    
    return state;
}

static bool perform_rebirth(Database* db, uint64_t user_id) {
    ensure_rebirth_tables(db);
    
    auto state = check_rebirth_state(db, user_id);
    if (!state.can_rebirth || !state.next_req) return false;
    
    int new_level = state.current_rebirths + 1;
    double new_mult = std::pow(1.1, new_level);
    std::string uid = std::to_string(user_id);
    
    // Use a single connection for all resets (transaction)
    auto conn = db->get_pool()->acquire();
    bool ok = true;
    
    mysql_query(conn->get(), "START TRANSACTION");
    
    // Reset balance & prestige
    ok = ok && (mysql_query(conn->get(), ("UPDATE users SET wallet = 0, bank = 0, prestige = 0 WHERE user_id = " + uid).c_str()) == 0);
    
    // Clear inventory — but preserve titles and active_title so they survive rebirth
    ok = ok && (mysql_query(conn->get(), ("DELETE FROM inventory WHERE user_id = " + uid + " AND item_id NOT LIKE 'title\\_%' AND item_id != 'active_title'").c_str()) == 0);
    
    // Clear unsold fish
    ok = ok && (mysql_query(conn->get(), ("DELETE FROM fish_catches WHERE user_id = " + uid + " AND sold = FALSE").c_str()) == 0);
    
    // Clear mining claims
    ok = ok && (mysql_query(conn->get(), ("DELETE FROM mining_claims WHERE user_id = " + uid).c_str()) == 0);
    
    // Clear fish ponds
    mysql_query(conn->get(), ("DELETE FROM pond_fish WHERE pond_id IN (SELECT id FROM fish_ponds WHERE user_id = " + uid + ")").c_str());
    mysql_query(conn->get(), ("DELETE FROM fish_ponds WHERE user_id = " + uid).c_str());
    
    // Reset skill tree
    mysql_query(conn->get(), ("DELETE FROM user_skill_points WHERE user_id = " + uid).c_str());
    
    // Clear cooldowns
    mysql_query(conn->get(), ("DELETE FROM cooldowns WHERE user_id = " + uid).c_str());
    
    // Update/insert rebirth record
    std::ostringstream mult_ss;
    mult_ss << std::fixed;
    mult_ss.precision(6);
    mult_ss << new_mult;
    
    std::string rebirth_sql = "INSERT INTO user_rebirths (user_id, rebirth_level, total_multiplier, last_rebirth_at) "
                              "VALUES (" + uid + ", " + std::to_string(new_level) + ", " + mult_ss.str() + ", NOW()) "
                              "ON DUPLICATE KEY UPDATE rebirth_level = " + std::to_string(new_level) + 
                              ", total_multiplier = " + mult_ss.str() + ", last_rebirth_at = NOW()";
    ok = ok && (mysql_query(conn->get(), rebirth_sql.c_str()) == 0);
    
    // Grant the rebirth title to the user's inventory
    if (ok) {
        const auto* req = &REBIRTH_REQUIREMENTS[new_level - 1];
        // Store proper JSON metadata so the title displays correctly
        // Inline the JSON encoding to avoid include-order issues with titles.h
        std::string meta = "{\"display\":\"";
        for (char c : req->title) {
            if      (c == '"')  meta += "\\\"";
            else if (c == '\\') meta += "\\\\";
            else                meta += c;
        }
        meta += "\"}";
        // Escape single quotes in meta for SQL
        std::string escaped_meta;
        for (char c : meta) {
            if (c == '\'') escaped_meta += "\\'";
            else escaped_meta += c;
        }
        std::string grant_sql = "INSERT INTO inventory (user_id, item_id, item_type, quantity, metadata, level) "
                                "VALUES (" + uid + ", '" + req->title_item_id + "', 'title', 1, '" + escaped_meta + "', 1) "
                                "ON DUPLICATE KEY UPDATE quantity = 1";
        mysql_query(conn->get(), grant_sql.c_str()); // best-effort inside the transaction
        
        // Auto-equip the rebirth title
        std::string equip_meta = escaped_meta;
        std::string equip_sql = "INSERT INTO inventory (user_id, item_id, item_type, quantity, metadata, level) "
                                "VALUES (" + uid + ", 'active_title', 'title_slot', 1, '" + equip_meta + "', 1) "
                                "ON DUPLICATE KEY UPDATE metadata = '" + equip_meta + "'";
        mysql_query(conn->get(), equip_sql.c_str()); // best-effort
    }

    if (ok) {
        mysql_query(conn->get(), "COMMIT");
    } else {
        mysql_query(conn->get(), "ROLLBACK");
    }
    
    db->get_pool()->release(conn);
    return ok;
}

// Roman numeral for rebirth level
static std::string rebirth_numeral(int level) {
    switch (level) {
        case 0: return "0";
        case 1: return "I";
        case 2: return "II";
        case 3: return "III";
        case 4: return "IV";
        case 5: return "V";
        default: return std::to_string(level);
    }
}

static std::string rebirth_star_visual(int level) {
    std::string stars;
    for (int i = 0; i < level; i++) stars += "\xE2\xAD\x90"; // ⭐
    for (int i = level; i < MAX_REBIRTHS; i++) stars += "\xE2\x98\x86"; // ☆
    return stars;
}

// ============================================================================
// /rebirth command
// ============================================================================
inline Command* create_rebirth_command(Database* db) {
    static Command* cmd = new Command("rebirth", "transcend beyond prestige for permanent multipliers", "economy", {"rb", "transcend"}, true,
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_rebirth_tables(db);
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            auto state = check_rebirth_state(db, user_id);
            bool confirmed = !args.empty() && (args[0] == "confirm" || args[0] == "yes");
            
            if (!confirmed) {
                // Show rebirth info - use dynamic emoji based on next rebirth level
                int display_level = (state.current_rebirths < MAX_REBIRTHS) ? state.current_rebirths + 1 : MAX_REBIRTHS;
                std::string desc = get_rebirth_emoji(display_level) + " **Rebirth System**\n\n";
                
                // Current status
                desc += "\xE2\xAD\x90 **Current Rebirth:** " + rebirth_numeral(state.current_rebirths) + " / " + rebirth_numeral(MAX_REBIRTHS) + "\n";
                desc += rebirth_star_visual(state.current_rebirths) + "\n";
                
                std::ostringstream mult_oss;
                mult_oss << std::fixed;
                mult_oss.precision(2);
                mult_oss << state.total_multiplier;
                desc += "\xF0\x9F\x93\x88 **Earnings Multiplier:** " + mult_oss.str() + "x\n\n";
                
                if (state.current_rebirths >= MAX_REBIRTHS) {
                    desc += "\xF0\x9F\x91\x91 **You have reached maximum rebirth!**\n";
                    desc += "Your permanent multiplier: **" + mult_oss.str() + "x** to all earnings.";
                    
                    auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                    embed.set_title(get_rebirth_emoji(MAX_REBIRTHS) + " Rebirth");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
                
                // Requirements for next rebirth
                const auto* req = state.next_req;
                int next_level = state.current_rebirths + 1;
                
                desc += "**Requirements for Rebirth " + rebirth_numeral(next_level) + ":**\n";
                
                desc += format_requirement(state.current_prestige >= req->required_prestige,
                    "Prestige " + std::to_string(req->required_prestige) + " (" + std::to_string(state.current_prestige) + "/" + std::to_string(req->required_prestige) + ")") + "\n";
                
                desc += format_requirement(state.current_networth >= req->required_networth,
                    "$" + format_number(req->required_networth) + " networth ($" + format_number(state.current_networth) + ")") + "\n";
                
                if (req->required_fish > 0) {
                    desc += format_requirement(state.current_fish >= req->required_fish,
                        format_number(req->required_fish) + " fish caught (" + format_number(state.current_fish) + ")") + "\n";
                }
                if (req->required_ores > 0) {
                    desc += format_requirement(state.current_ores >= req->required_ores,
                        format_number(req->required_ores) + " ores mined (" + format_number(state.current_ores) + ")") + "\n";
                }
                if (req->required_gambles > 0) {
                    desc += format_requirement(state.current_gambles >= req->required_gambles,
                        format_number(req->required_gambles) + " gambling wins (" + format_number(state.current_gambles) + ")") + "\n";
                }
                
                desc += "\n**Rebirth will reset:**\n";
                desc += "\xE2\x80\xA2 Your prestige level (back to 0)\n";
                desc += "\xE2\x80\xA2 All wallet & bank balance\n";
                desc += "\xE2\x80\xA2 All inventory, fish, rods, bait\n";
                desc += "\xE2\x80\xA2 Mining claims & fish ponds\n";
                desc += "\xE2\x80\xA2 Skill tree points\n";
                desc += "\xE2\x80\xA2 All cooldowns\n\n";
                
                desc += "**You gain:**\n";
                desc += "\xE2\x80\xA2 Permanent **1.1x** earnings multiplier\n";
                desc += "\xE2\x80\xA2 New multiplier: **" + std::to_string(static_cast<int>((std::pow(1.1, next_level)) * 100)) + "%** (" + std::to_string(static_cast<int>(std::pow(1.1, next_level) * 100) / 100) + "." + std::to_string(static_cast<int>(std::pow(1.1, next_level) * 100) % 100)  + "x)\n";
                desc += "\xE2\x80\xA2 Title: **\"" + req->title + "\"**\n\n";
                
                if (state.can_rebirth) {
                    desc += bronx::EMOJI_CHECK + " **All requirements met!** Use `b.rebirth confirm` to proceed.";
                } else {
                    desc += bronx::EMOJI_DENY + " **Requirements not yet met.**";
                }
                
                auto embed = bronx::create_embed(desc);
                embed.set_title(get_rebirth_emoji(next_level) + " Rebirth " + rebirth_numeral(next_level));
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // Confirmed — attempt rebirth
            if (state.current_rebirths >= MAX_REBIRTHS) {
                bronx::send_message(bot, event, bronx::error("you've already reached max rebirth!"));
                return;
            }
            
            if (!state.can_rebirth) {
                bronx::send_message(bot, event, bronx::error("you don't meet all rebirth requirements!"));
                return;
            }
            
            int new_level = state.current_rebirths + 1;
            const auto* req = state.next_req;
            
            if (perform_rebirth(db, user_id)) {
                double new_mult = std::pow(1.1, new_level);
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(2);
                oss << new_mult;
                
                log_balance_change(db, user_id, "REBIRTH " + rebirth_numeral(new_level) + " — multiplier now " + oss.str() + "x");
                
                std::string desc = get_rebirth_emoji(new_level) + " **REBIRTH COMPLETE!**\n\n";
                desc += rebirth_star_visual(new_level) + "\n\n";
                desc += "You are now **Rebirth " + rebirth_numeral(new_level) + "**!\n";
                desc += "Title earned: **\"" + req->title + "\"**\n";
                desc += "Permanent multiplier: **" + oss.str() + "x** to all earnings\n\n";
                desc += "Your progress has been reset. The journey begins anew... but stronger.";
                
                auto embed = bronx::create_embed(desc, 0xFFD700); // Gold color
                embed.set_title(get_rebirth_emoji(new_level) + " REBIRTH " + rebirth_numeral(new_level));
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
            } else {
                bronx::send_message(bot, event, bronx::error("rebirth failed! please try again."));
            }
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_rebirth_tables(db);
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);
            
            auto state = check_rebirth_state(db, user_id);
            
            bool confirmed = false;
            try {
                auto p = event.get_parameter("confirm");
                if (std::holds_alternative<bool>(p)) confirmed = std::get<bool>(p);
            } catch (...) {}
            
            if (!confirmed) {
                // Use dynamic emoji based on next rebirth level
                int display_level = (state.current_rebirths < MAX_REBIRTHS) ? state.current_rebirths + 1 : MAX_REBIRTHS;
                std::string desc = get_rebirth_emoji(display_level) + " **Rebirth System**\n\n";
                
                desc += "\xE2\xAD\x90 **Current Rebirth:** " + rebirth_numeral(state.current_rebirths) + " / " + rebirth_numeral(MAX_REBIRTHS) + "\n";
                desc += rebirth_star_visual(state.current_rebirths) + "\n";
                
                std::ostringstream mult_oss;
                mult_oss << std::fixed;
                mult_oss.precision(2);
                mult_oss << state.total_multiplier;
                desc += "\xF0\x9F\x93\x88 **Earnings Multiplier:** " + mult_oss.str() + "x\n\n";
                
                if (state.current_rebirths >= MAX_REBIRTHS) {
                    desc += "\xF0\x9F\x91\x91 **Maximum rebirth reached!** Multiplier: **" + mult_oss.str() + "x**";
                    auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                    embed.set_title(get_rebirth_emoji(MAX_REBIRTHS) + " Rebirth");
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }
                
                const auto* req = state.next_req;
                int next_level = state.current_rebirths + 1;
                
                desc += "**Requirements for Rebirth " + rebirth_numeral(next_level) + ":**\n";
                desc += format_requirement(state.current_prestige >= req->required_prestige,
                    "Prestige " + std::to_string(req->required_prestige) + " (" + std::to_string(state.current_prestige) + "/" + std::to_string(req->required_prestige) + ")") + "\n";
                desc += format_requirement(state.current_networth >= req->required_networth,
                    "$" + format_number(req->required_networth) + " networth ($" + format_number(state.current_networth) + ")") + "\n";
                if (req->required_fish > 0)
                    desc += format_requirement(state.current_fish >= req->required_fish,
                        format_number(req->required_fish) + " fish caught (" + format_number(state.current_fish) + ")") + "\n";
                if (req->required_ores > 0)
                    desc += format_requirement(state.current_ores >= req->required_ores,
                        format_number(req->required_ores) + " ores mined (" + format_number(state.current_ores) + ")") + "\n";
                if (req->required_gambles > 0)
                    desc += format_requirement(state.current_gambles >= req->required_gambles,
                        format_number(req->required_gambles) + " gambling wins (" + format_number(state.current_gambles) + ")") + "\n";
                
                desc += "\n**Resets everything** (prestige, balance, inventory, skills)\n";
                desc += "**Grants:** 1.1x permanent multiplier + \"" + req->title + "\" title\n\n";
                
                if (state.can_rebirth)
                    desc += bronx::EMOJI_CHECK + " Use `/rebirth confirm:true` to proceed.";
                else
                    desc += bronx::EMOJI_DENY + " **Requirements not yet met.**";
                
                auto embed = bronx::create_embed(desc);
                embed.set_title(get_rebirth_emoji(next_level) + " Rebirth " + rebirth_numeral(next_level));
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            // Confirmed
            if (state.current_rebirths >= MAX_REBIRTHS) {
                event.reply(dpp::message().add_embed(bronx::error("max rebirth reached!")));
                return;
            }
            if (!state.can_rebirth) {
                event.reply(dpp::message().add_embed(bronx::error("requirements not met!")));
                return;
            }
            
            int new_level = state.current_rebirths + 1;
            const auto* req = state.next_req;
            
            if (perform_rebirth(db, user_id)) {
                double new_mult = std::pow(1.1, new_level);
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(2);
                oss << new_mult;
                
                log_balance_change(db, user_id, "REBIRTH " + rebirth_numeral(new_level) + " — multiplier now " + oss.str() + "x");
                
                std::string desc = get_rebirth_emoji(new_level) + " **REBIRTH COMPLETE!**\n\n";
                desc += rebirth_star_visual(new_level) + "\n\n";
                desc += "**Rebirth " + rebirth_numeral(new_level) + "** achieved!\n";
                desc += "Title: **\"" + req->title + "\"**\n";
                desc += "Multiplier: **" + oss.str() + "x** permanent\n\n";
                desc += "The journey begins anew...";
                
                auto embed = bronx::create_embed(desc, 0xFFD700);
                embed.set_title(get_rebirth_emoji(new_level) + " REBIRTH " + rebirth_numeral(new_level));
                event.reply(dpp::message().add_embed(embed));
            } else {
                event.reply(dpp::message().add_embed(bronx::error("rebirth failed!")));
            }
        },
        // Slash options
        {
            dpp::command_option(dpp::co_boolean, "confirm", "confirm rebirth (this is permanent!)", false)
        }
    );
    
    cmd->extended_description = "The ultimate endgame progression. At Prestige 20, Rebirth resets your prestige to 0 "
                                "but grants a permanent 1.1x multiplier to ALL earnings. Stack up to 5 rebirths "
                                "for a 1.61x permanent boost.";
    cmd->examples = {"b.rebirth", "b.rebirth confirm"};
    
    return cmd;
}

} // namespace economy
} // namespace commands
