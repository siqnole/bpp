#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/core/connection_pool.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <mariadb/mysql.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

using namespace bronx::db;

namespace commands {
namespace fishing {
namespace crews {

// ============================================================================
// FISHING CREWS
// ============================================================================
// Players form crews of up to 5 members. When 2+ crew members have fished
// in the last hour, everyone in the crew gets a bonus fish value multiplier:
//   2+ active  → +15%
//   all active → +25%
//
// Tables:
//   crews          — crew metadata, one per crew
//   crew_members   — per-member rows (includes last_fished_at)
//   crew_invites   — pending invite rows (expire after 24h)
// ============================================================================

static bool crews_tables_created = false;

inline void ensure_crews_tables(Database* db) {
    if (crews_tables_created) return;
    db->execute(
        "CREATE TABLE IF NOT EXISTS crews ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  name VARCHAR(50) UNIQUE NOT NULL,"
        "  owner_id BIGINT UNSIGNED NOT NULL,"
        "  open_join TINYINT NOT NULL DEFAULT 0,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  total_fish_caught BIGINT NOT NULL DEFAULT 0,"
        "  total_fish_value BIGINT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    db->execute(
        "CREATE TABLE IF NOT EXISTS crew_members ("
        "  crew_id BIGINT NOT NULL,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  last_fished_at TIMESTAMP NULL DEFAULT NULL,"
        "  contribution_fish INT NOT NULL DEFAULT 0,"
        "  contribution_value BIGINT NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (crew_id, user_id),"
        "  INDEX idx_user (user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    db->execute(
        "CREATE TABLE IF NOT EXISTS crew_invites ("
        "  crew_id BIGINT NOT NULL,"
        "  invited_user_id BIGINT UNSIGNED NOT NULL,"
        "  invited_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (crew_id, invited_user_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    crews_tables_created = true;
}

// ── DB helper: escape string for safe SQL interpolation ────────────────────
inline std::string esc(const std::string& s) {
    std::string r;
    r.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\'') r += "\\'";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

// ── Get crew_id for a user (0 if not in a crew) ────────────────────────────
inline int64_t get_user_crew(Database* db, uint64_t user_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    std::string q = "SELECT crew_id FROM crew_members WHERE user_id=" + std::to_string(user_id) + " LIMIT 1";
    int64_t crew_id = 0;
    if (mysql_query(conn->get(), q.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row && row[0]) crew_id = std::stoll(row[0]);
            mysql_free_result(r);
        }
    }
    db->get_pool()->release(conn);
    return crew_id;
}

// ── Get number of members in a crew ───────────────────────────────────────
inline int get_crew_size(Database* db, int64_t crew_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return 0;
    std::string q = "SELECT COUNT(*) FROM crew_members WHERE crew_id=" + std::to_string(crew_id);
    int cnt = 0;
    if (mysql_query(conn->get(), q.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row && row[0]) cnt = std::stoi(row[0]);
            mysql_free_result(r);
        }
    }
    db->get_pool()->release(conn);
    return cnt;
}

// ============================================================================
// get_crew_bonus  ─ called by fish.h to apply crew multiplier to fish value
// Returns 1.0 (no bonus), 1.15, or 1.25 depending on how many crew members
// have fished within the past hour.
// ============================================================================
inline double get_crew_bonus(Database* db, uint64_t user_id) {
    int64_t crew_id = get_user_crew(db, user_id);
    if (crew_id == 0) return 1.0;

    auto conn = db->get_pool()->acquire();
    if (!conn) return 1.0;

    // Count crew members (including self) who fished in the last hour
    std::string q = "SELECT COUNT(*) FROM crew_members "
                    "WHERE crew_id=" + std::to_string(crew_id) +
                    " AND last_fished_at >= DATE_SUB(NOW(), INTERVAL 1 HOUR)";
    int active = 0;
    if (mysql_query(conn->get(), q.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row && row[0]) active = std::stoi(row[0]);
            mysql_free_result(r);
        }
    }

    // Total crew size
    std::string sq = "SELECT COUNT(*) FROM crew_members WHERE crew_id=" + std::to_string(crew_id);
    int total = 1;
    if (mysql_query(conn->get(), sq.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn->get());
        if (r) {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row && row[0]) total = std::stoi(row[0]);
            mysql_free_result(r);
        }
    }
    db->get_pool()->release(conn);

    if (total < 2) return 1.0;               // solo crew — no bonus
    if (active >= total && total >= 2) return 1.25;  // full crew active
    if (active >= 2) return 1.15;            // at least 2 active
    return 1.0;
}

// ============================================================================
// update_crew_activity  ─ call this after a successful fish catch.
// Updates last_fished_at and increments contribution counters.
// ============================================================================
inline void update_crew_activity(Database* db, uint64_t user_id, int fish_count, int64_t fish_value) {
    int64_t crew_id = get_user_crew(db, user_id);
    if (crew_id == 0) return;

    auto conn = db->get_pool()->acquire();
    if (!conn) return;

    std::string q = "UPDATE crew_members SET last_fished_at=NOW(), "
                    "contribution_fish=contribution_fish+" + std::to_string(fish_count) + ", "
                    "contribution_value=contribution_value+" + std::to_string(fish_value) +
                    " WHERE crew_id=" + std::to_string(crew_id) + " AND user_id=" + std::to_string(user_id);
    mysql_query(conn->get(), q.c_str());

    // Update crew totals
    std::string tq = "UPDATE crews SET total_fish_caught=total_fish_caught+" + std::to_string(fish_count) + ", "
                     "total_fish_value=total_fish_value+" + std::to_string(fish_value) +
                     " WHERE id=" + std::to_string(crew_id);
    mysql_query(conn->get(), tq.c_str());

    db->get_pool()->release(conn);
}

// ============================================================================
// Command builder
// ============================================================================
inline Command* get_crew_command(Database* db) {
    ensure_crews_tables(db);

    static Command* cmd = new Command(
        "crew",
        "manage your fishing crew",
        "fishing",
        {"crw"},
        false,
        // ── TEXT HANDLER ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                auto embed = bronx::info(
                    "**🎣 Fishing Crews**\n\n"
                    "Fish with your friends and earn bonus rewards!\n\n"
                    "**subcommands:**\n"
                    "`crew create <name>` — create a crew (**$10,000**)\n"
                    "`crew invite <@user>` — invite someone to your crew\n"
                    "`crew join <name>` — join a crew (must be invited or crew is open)\n"
                    "`crew leave` — leave your current crew\n"
                    "`crew info [name]` — view crew details\n"
                    "`crew kick <@user>` — kick a member (owner)\n"
                    "`crew open` / `crew close` — toggle open joining (owner)\n"
                    "`crew disband` — delete your crew (owner)\n"
                    "`crew leaderboard` — top crews by fish value\n\n"
                    "**🎣 Crew Bonuses:**\n"
                    "• 2+ members fished in last hour → **+15% fish value**\n"
                    "• Full crew active in last hour → **+25% fish value**"
                );
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            uint64_t uid = event.msg.author.id;

            // ───────────────────────────────── crew create <name>
            if (sub == "create") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `crew create <name>`"));
                    return;
                }
                std::string name = args[1];
                if (name.size() > 30) {
                    bronx::send_message(bot, event, bronx::error("crew name must be 30 characters or less"));
                    return;
                }
                // Check already in a crew
                if (get_user_crew(db, uid) > 0) {
                    bronx::send_message(bot, event, bronx::error("you're already in a crew — leave first"));
                    return;
                }
                // Check balance
                auto user = db->get_user(uid);
                if (!user || user->wallet < 10000) {
                    bronx::send_message(bot, event, bronx::error("you need $10,000 to create a crew"));
                    return;
                }
                // Deduct cost
                db->update_wallet(uid, -10000);

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

                std::string ins = "INSERT INTO crews (name, owner_id) VALUES ('" + esc(name) + "'," + std::to_string(uid) + ")";
                if (mysql_query(conn->get(), ins.c_str()) != 0) {
                    db->get_pool()->release(conn);
                    db->update_wallet(uid, 10000); // refund
                    bronx::send_message(bot, event, bronx::error("crew name already taken or invalid"));
                    return;
                }
                int64_t crew_id = (int64_t)mysql_insert_id(conn->get());
                // Add owner as member
                std::string addm = "INSERT INTO crew_members (crew_id, user_id) VALUES (" + std::to_string(crew_id) + "," + std::to_string(uid) + ")";
                mysql_query(conn->get(), addm.c_str());
                db->get_pool()->release(conn);

                auto embed = bronx::success("**⚓ Crew Created!**\n\n"
                    "**" + name + "** is ready for action!\n"
                    "invite up to 4 more members with `crew invite @user`\n"
                    "or set the crew to open with `crew open`\n\n"
                    "*$10,000 deducted*");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            // ───────────────────────────────── crew invite @user
            if (sub == "invite") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `crew invite <@user>`"));
                    return;
                }
                int64_t my_crew = get_user_crew(db, uid);
                if (my_crew == 0) {
                    bronx::send_message(bot, event, bronx::error("you're not in a crew"));
                    return;
                }
                // Check is owner
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string chk = "SELECT owner_id FROM crews WHERE id=" + std::to_string(my_crew);
                uint64_t owner = 0;
                if (mysql_query(conn->get(), chk.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) owner = std::stoull(row[0]); mysql_free_result(r); }
                }
                db->get_pool()->release(conn);

                if (owner != uid) {
                    bronx::send_message(bot, event, bronx::error("only the crew owner can invite members"));
                    return;
                }
                if (get_crew_size(db, my_crew) >= 5) {
                    bronx::send_message(bot, event, bronx::error("crew is full (max 5 members)"));
                    return;
                }

                // Parse target
                uint64_t target_id = 0;
                std::string mention = args[1];
                if (mention.size() > 2 && mention[0] == '<' && mention[1] == '@') {
                    std::string s = mention.substr(2, mention.size() - 3);
                    if (!s.empty() && s[0] == '!') s = s.substr(1);
                    try { target_id = std::stoull(s); } catch (...) {}
                } else { try { target_id = std::stoull(mention); } catch (...) {} }

                if (target_id == 0 || target_id == uid) {
                    bronx::send_message(bot, event, bronx::error("invalid target user"));
                    return;
                }
                if (get_user_crew(db, target_id) > 0) {
                    bronx::send_message(bot, event, bronx::error("that user is already in a crew"));
                    return;
                }

                auto conn2 = db->get_pool()->acquire();
                if (!conn2) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string ins = "INSERT IGNORE INTO crew_invites (crew_id, invited_user_id) VALUES (" + std::to_string(my_crew) + "," + std::to_string(target_id) + ")";
                mysql_query(conn2->get(), ins.c_str());
                db->get_pool()->release(conn2);

                bronx::send_message(bot, event, bronx::success(
                    "<@" + std::to_string(target_id) + "> has been invited to your crew!\n"
                    "They can join with `crew join <name>`"
                ));
                return;
            }

            // ───────────────────────────────── crew join <name>
            if (sub == "join") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `crew join <name>`"));
                    return;
                }
                if (get_user_crew(db, uid) > 0) {
                    bronx::send_message(bot, event, bronx::error("you're already in a crew — leave first"));
                    return;
                }
                std::string name = args[1];

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

                std::string sel = "SELECT id, open_join FROM crews WHERE name='" + esc(name) + "' LIMIT 1";
                int64_t crew_id = 0; int open_join = 0;
                if (mysql_query(conn->get(), sel.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) {
                        MYSQL_ROW row = mysql_fetch_row(r);
                        if (row) { crew_id = row[0] ? std::stoll(row[0]) : 0; open_join = row[1] ? std::stoi(row[1]) : 0; }
                        mysql_free_result(r);
                    }
                }
                db->get_pool()->release(conn);

                if (crew_id == 0) {
                    bronx::send_message(bot, event, bronx::error("crew \"" + name + "\" not found"));
                    return;
                }
                if (get_crew_size(db, crew_id) >= 5) {
                    bronx::send_message(bot, event, bronx::error("that crew is full"));
                    return;
                }

                // Check invite or open
                bool invited = false;
                auto conn2 = db->get_pool()->acquire();
                if (conn2) {
                    std::string ichk = "SELECT 1 FROM crew_invites WHERE crew_id=" + std::to_string(crew_id) + " AND invited_user_id=" + std::to_string(uid) + " LIMIT 1";
                    if (mysql_query(conn2->get(), ichk.c_str()) == 0) {
                        MYSQL_RES* r = mysql_store_result(conn2->get());
                        if (r) { invited = mysql_fetch_row(r) != nullptr; mysql_free_result(r); }
                    }
                    db->get_pool()->release(conn2);
                }

                if (!open_join && !invited) {
                    bronx::send_message(bot, event, bronx::error("you need an invite to join that crew"));
                    return;
                }

                // Add member
                auto conn3 = db->get_pool()->acquire();
                if (!conn3) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string ins = "INSERT IGNORE INTO crew_members (crew_id, user_id) VALUES (" + std::to_string(crew_id) + "," + std::to_string(uid) + ")";
                mysql_query(conn3->get(), ins.c_str());
                // Remove invite
                std::string del_inv = "DELETE FROM crew_invites WHERE crew_id=" + std::to_string(crew_id) + " AND invited_user_id=" + std::to_string(uid);
                mysql_query(conn3->get(), del_inv.c_str());
                db->get_pool()->release(conn3);

                bronx::send_message(bot, event, bronx::success(
                    "**You've joined crew **" + name + "**!**\n"
                    "fish together to earn crew bonuses 🎣"
                ));
                return;
            }

            // ───────────────────────────────── crew leave
            if (sub == "leave") {
                int64_t my_crew = get_user_crew(db, uid);
                if (my_crew == 0) {
                    bronx::send_message(bot, event, bronx::error("you're not in a crew"));
                    return;
                }
                // Check if owner — if so, transfer ownership or disband if alone
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string sel = "SELECT owner_id FROM crews WHERE id=" + std::to_string(my_crew);
                uint64_t owner = 0;
                if (mysql_query(conn->get(), sel.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) owner = std::stoull(row[0]); mysql_free_result(r); }
                }
                db->get_pool()->release(conn);

                int size = get_crew_size(db, my_crew);
                if (owner == uid && size == 1) {
                    // Disband alone
                    auto conn2 = db->get_pool()->acquire();
                    if (conn2) {
                        mysql_query(conn2->get(), ("DELETE FROM crew_members WHERE crew_id=" + std::to_string(my_crew)).c_str());
                        mysql_query(conn2->get(), ("DELETE FROM crews WHERE id=" + std::to_string(my_crew)).c_str());
                        db->get_pool()->release(conn2);
                    }
                    bronx::send_message(bot, event, bronx::success("crew disbanded (you were the last member)"));
                    return;
                }
                if (owner == uid) {
                    // Transfer ownership to next member
                    auto conn2 = db->get_pool()->acquire();
                    if (conn2) {
                        std::string next = "SELECT user_id FROM crew_members WHERE crew_id=" + std::to_string(my_crew) + " AND user_id!=" + std::to_string(uid) + " LIMIT 1";
                        uint64_t next_owner = 0;
                        if (mysql_query(conn2->get(), next.c_str()) == 0) {
                            MYSQL_RES* r = mysql_store_result(conn2->get());
                            if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) next_owner = std::stoull(row[0]); mysql_free_result(r); }
                        }
                        if (next_owner) mysql_query(conn2->get(), ("UPDATE crews SET owner_id=" + std::to_string(next_owner) + " WHERE id=" + std::to_string(my_crew)).c_str());
                        mysql_query(conn2->get(), ("DELETE FROM crew_members WHERE crew_id=" + std::to_string(my_crew) + " AND user_id=" + std::to_string(uid)).c_str());
                        db->get_pool()->release(conn2);
                    }
                    bronx::send_message(bot, event, bronx::success("you left the crew. ownership transferred to the next member."));
                    return;
                }

                auto conn2 = db->get_pool()->acquire();
                if (conn2) {
                    mysql_query(conn2->get(), ("DELETE FROM crew_members WHERE crew_id=" + std::to_string(my_crew) + " AND user_id=" + std::to_string(uid)).c_str());
                    db->get_pool()->release(conn2);
                }
                bronx::send_message(bot, event, bronx::success("you left your crew"));
                return;
            }

            // ───────────────────────────────── crew info [name]
            if (sub == "info") {
                int64_t crew_id = 0;
                if (args.size() >= 2) {
                    // look up by name
                    auto conn = db->get_pool()->acquire();
                    if (conn) {
                        std::string sel = "SELECT id FROM crews WHERE name='" + esc(args[1]) + "' LIMIT 1";
                        if (mysql_query(conn->get(), sel.c_str()) == 0) {
                            MYSQL_RES* r = mysql_store_result(conn->get());
                            if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) crew_id = std::stoll(row[0]); mysql_free_result(r); }
                        }
                        db->get_pool()->release(conn);
                    }
                } else {
                    crew_id = get_user_crew(db, uid);
                }
                if (crew_id == 0) {
                    bronx::send_message(bot, event, bronx::error("crew not found (or you're not in one)"));
                    return;
                }

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }

                std::string csel = "SELECT name, owner_id, open_join, created_at, total_fish_caught, total_fish_value FROM crews WHERE id=" + std::to_string(crew_id);
                std::string crew_name, created_at_str;
                uint64_t crew_owner = 0; int open_join = 0; int64_t total_fish = 0, total_val = 0;
                if (mysql_query(conn->get(), csel.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) {
                        MYSQL_ROW row = mysql_fetch_row(r);
                        if (row) {
                            crew_name = row[0] ? row[0] : "?";
                            crew_owner = row[1] ? std::stoull(row[1]) : 0;
                            open_join = row[2] ? std::stoi(row[2]) : 0;
                            created_at_str = row[3] ? row[3] : "?";
                            total_fish = row[4] ? std::stoll(row[4]) : 0;
                            total_val = row[5] ? std::stoll(row[5]) : 0;
                        }
                        mysql_free_result(r);
                    }
                }

                // Get members
                std::string msel = "SELECT user_id, last_fished_at, contribution_fish, contribution_value FROM crew_members WHERE crew_id=" + std::to_string(crew_id) + " ORDER BY contribution_value DESC";
                std::string members_str;
                int active_count = 0;
                if (mysql_query(conn->get(), msel.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) {
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(r))) {
                            uint64_t mid = row[0] ? std::stoull(row[0]) : 0;
                            std::string last_fish = row[1] ? row[1] : "never";
                            int cf = row[2] ? std::stoi(row[2]) : 0;
                            int64_t cv = row[3] ? std::stoll(row[3]) : 0;
                            bool active = (row[1] != nullptr); // simplified active check
                            if (active) active_count++;
                            members_str += std::string(mid == crew_owner ? "👑" : "🎣") + " <@" + std::to_string(mid) + ">"
                                + (active ? " 🟢" : " ⚫")
                                + " — " + std::to_string(cf) + " fish / $" + format_number(cv) + "\n";
                        }
                        mysql_free_result(r);
                    }
                }
                db->get_pool()->release(conn);

                double bonus = get_crew_bonus(db, uid);
                std::string bonus_str = bonus >= 1.25 ? "🌟 **+25%** (full crew active!)"
                                      : bonus >= 1.15 ? "✨ **+15%** (crew active)"
                                                       : "💤 *no bonus* (fish together to activate)";

                std::string desc = "**⚓ Crew: " + crew_name + "**"
                    + (open_join ? " [OPEN]" : "") + "\n\n"
                    + "**Members:**\n" + members_str + "\n"
                    + "**Total fish caught:** " + format_number(total_fish) + "\n"
                    + "**Total fish value:** $" + format_number(total_val) + "\n\n"
                    + "**Current bonus:** " + bonus_str;
                auto embed = bronx::info(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            // ───────────────────────────────── crew kick @user
            if (sub == "kick") {
                if (args.size() < 2) { bronx::send_message(bot, event, bronx::error("usage: `crew kick <@user>`")); return; }
                int64_t my_crew = get_user_crew(db, uid);
                if (my_crew == 0) { bronx::send_message(bot, event, bronx::error("you're not in a crew")); return; }

                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string chk = "SELECT owner_id FROM crews WHERE id=" + std::to_string(my_crew);
                uint64_t owner = 0;
                if (mysql_query(conn->get(), chk.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) owner = std::stoull(row[0]); mysql_free_result(r); }
                }
                db->get_pool()->release(conn);

                if (owner != uid) { bronx::send_message(bot, event, bronx::error("only the crew owner can kick members")); return; }

                uint64_t target_id = 0;
                std::string mention = args[1];
                if (mention.size() > 2 && mention[0] == '<' && mention[1] == '@') {
                    std::string s = mention.substr(2, mention.size() - 3);
                    if (!s.empty() && s[0] == '!') s = s.substr(1);
                    try { target_id = std::stoull(s); } catch (...) {}
                } else { try { target_id = std::stoull(mention); } catch (...) {} }

                if (target_id == 0 || target_id == uid) { bronx::send_message(bot, event, bronx::error("invalid target")); return; }

                auto conn2 = db->get_pool()->acquire();
                if (conn2) {
                    mysql_query(conn2->get(), ("DELETE FROM crew_members WHERE crew_id=" + std::to_string(my_crew) + " AND user_id=" + std::to_string(target_id)).c_str());
                    db->get_pool()->release(conn2);
                }
                bronx::send_message(bot, event, bronx::success("<@" + std::to_string(target_id) + "> was kicked from the crew"));
                return;
            }

            // ───────────────────────────────── crew open / close
            if (sub == "open" || sub == "close") {
                int64_t my_crew = get_user_crew(db, uid);
                if (my_crew == 0) { bronx::send_message(bot, event, bronx::error("you're not in a crew")); return; }
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string chk = "SELECT owner_id FROM crews WHERE id=" + std::to_string(my_crew);
                uint64_t owner = 0;
                if (mysql_query(conn->get(), chk.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) owner = std::stoull(row[0]); mysql_free_result(r); }
                }
                if (owner != uid) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("only the crew owner can do that")); return; }
                int new_val = (sub == "open") ? 1 : 0;
                mysql_query(conn->get(), ("UPDATE crews SET open_join=" + std::to_string(new_val) + " WHERE id=" + std::to_string(my_crew)).c_str());
                db->get_pool()->release(conn);
                bronx::send_message(bot, event, bronx::success("crew joining is now **" + sub + "**"));
                return;
            }

            // ───────────────────────────────── crew disband
            if (sub == "disband") {
                int64_t my_crew = get_user_crew(db, uid);
                if (my_crew == 0) { bronx::send_message(bot, event, bronx::error("you're not in a crew")); return; }
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string chk = "SELECT owner_id FROM crews WHERE id=" + std::to_string(my_crew);
                uint64_t owner = 0;
                if (mysql_query(conn->get(), chk.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) { MYSQL_ROW row = mysql_fetch_row(r); if (row && row[0]) owner = std::stoull(row[0]); mysql_free_result(r); }
                }
                if (owner != uid) { db->get_pool()->release(conn); bronx::send_message(bot, event, bronx::error("only the crew owner can disband")); return; }
                mysql_query(conn->get(), ("DELETE FROM crew_members WHERE crew_id=" + std::to_string(my_crew)).c_str());
                mysql_query(conn->get(), ("DELETE FROM crew_invites WHERE crew_id=" + std::to_string(my_crew)).c_str());
                mysql_query(conn->get(), ("DELETE FROM crews WHERE id=" + std::to_string(my_crew)).c_str());
                db->get_pool()->release(conn);
                bronx::send_message(bot, event, bronx::success("crew disbanded"));
                return;
            }

            // ───────────────────────────────── crew leaderboard
            if (sub == "leaderboard" || sub == "lb" || sub == "top") {
                auto conn = db->get_pool()->acquire();
                if (!conn) { bronx::send_message(bot, event, bronx::error("database error")); return; }
                std::string q = "SELECT name, owner_id, total_fish_caught, total_fish_value, "
                                "(SELECT COUNT(*) FROM crew_members cm WHERE cm.crew_id=c.id) as member_count "
                                "FROM crews c ORDER BY total_fish_value DESC LIMIT 10";
                std::string desc = "**⚓ Crew Leaderboard — Top 10 Crews by Fish Value**\n\n";
                int rank = 1;
                if (mysql_query(conn->get(), q.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn->get());
                    if (r) {
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(r))) {
                            std::string cname = row[0] ? row[0] : "?";
                            std::string owner_str = row[1] ? row[1] : "0";
                            int64_t tf = row[2] ? std::stoll(row[2]) : 0;
                            int64_t tv = row[3] ? std::stoll(row[3]) : 0;
                            int mc = row[4] ? std::stoi(row[4]) : 0;
                            std::string medal = rank == 1 ? "🥇" : rank == 2 ? "🥈" : rank == 3 ? "🥉" : std::to_string(rank) + ".";
                            desc += medal + " **" + cname + "** (" + std::to_string(mc) + " members)"
                                + " — $" + format_number(tv) + " • " + format_number(tf) + " fish\n";
                            rank++;
                        }
                        mysql_free_result(r);
                    }
                }
                db->get_pool()->release(conn);
                if (rank == 1) desc += "*no crews yet — be the first!*";
                auto embed = bronx::info(desc);
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }

            bronx::send_message(bot, event, bronx::error("unknown subcommand. try `crew` for help"));
        }
    );
    return cmd;
}

} // namespace crews
} // namespace fishing
} // namespace commands
