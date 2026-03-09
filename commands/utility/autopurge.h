#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <regex>
#include <cctype>
#include <chrono>

namespace commands {
namespace utility {

// Optional database pointer (set from main)
static bronx::db::Database* autopurge_db = nullptr;
inline void set_autopurge_db(bronx::db::Database* db) { autopurge_db = db; }

// Forward declaration of schedule helper
static void schedule_autopurge(dpp::cluster& bot, const bronx::db::AutopurgeRow& row);

// Load persisted autopurge entries from the database and start their timers
inline void load_autopurges(dpp::cluster& bot) {
    if (!autopurge_db) return;
    // ensure database table exists before querying
    autopurge_db->execute("CREATE TABLE IF NOT EXISTS autopurges ("
                           "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                           "user_id BIGINT UNSIGNED NOT NULL,"
                           "guild_id BIGINT UNSIGNED NULL,"
                           "channel_id BIGINT UNSIGNED NOT NULL,"
                           "interval_seconds INT NOT NULL,"
                           "message_limit INT NOT NULL,"
                           "target_user_id BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                           "target_role_id BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                           "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                           "INDEX idx_user (user_id),"
                           "INDEX idx_target_user (target_user_id),"
                           "INDEX idx_target_role (target_role_id),"
                           "INDEX idx_guild (guild_id),"
                           "INDEX idx_channel (channel_id)"
                           ") ENGINE=InnoDB;");
    // migration: add new columns, ignore "duplicate column" errors
    auto add_column_safely = [&](const std::string& sql) {
        if (!autopurge_db->execute(sql)) {
            std::string err = autopurge_db->get_last_error();
            // warn only if it's not a duplicate-column message
            if (err.find("Duplicate column name") == std::string::npos) {
                std::cerr << "Autopurge migration error: " << err << std::endl;
            }
        }
    };
    add_column_safely("ALTER TABLE autopurges ADD COLUMN target_user_id BIGINT UNSIGNED NOT NULL DEFAULT 0");
    add_column_safely("ALTER TABLE autopurges ADD COLUMN target_role_id BIGINT UNSIGNED NOT NULL DEFAULT 0");
    // clear any leftover error from migration steps (duplicate-column is expected)
    autopurge_db->execute("SELECT 1");
    auto rows = autopurge_db->get_all_autopurges();
    for (const auto& r : rows) {
        schedule_autopurge(bot, r);
    }
}

// Helper: parse a duration string like "30s", "5m", "2h", or plain number (seconds)
static bool parse_interval(const ::std::string& s, int& out_seconds) {
    if (s.empty()) return false;
    char unit = s.back();
    ::std::string num = s;
    int multiplier = 1;
    if (unit == 's' || unit == 'm' || unit == 'h' || unit == 'd') {
        num = s.substr(0, s.size() - 1);
        if (unit == 's') multiplier = 1;
        else if (unit == 'm') multiplier = 60;
        else if (unit == 'h') multiplier = 3600;
        else if (unit == 'd') multiplier = 86400;
    }
    try {
        long long val = ::std::stoll(num);
        out_seconds = static_cast<int>(val * multiplier);
        return out_seconds > 0;
    } catch (...) {
        return false;
    }
}

// Human-readable formatting for an interval (pick largest whole unit)
static ::std::string format_interval(int seconds) {
    if (seconds % 86400 == 0) {
        return ::std::to_string(seconds / 86400) + "d";
    } else if (seconds % 3600 == 0) {
        return ::std::to_string(seconds / 3600) + "h";
    } else if (seconds % 60 == 0) {
        return ::std::to_string(seconds / 60) + "m";
    } else {
        return ::std::to_string(seconds) + "s";
    }
}

// Helper: parse channel mention/ID to snowflake
static bool parse_channel(const ::std::string& input, uint64_t& out) {
    ::std::string s = input;
    // strip <# and > if present
    if (!s.empty() && s.front() == '<' && s.back() == '>') {
        s = s.substr(1, s.size() - 2);
    }
    if (!s.empty() && s.front() == '#') s = s.substr(1);
    try {
        out = ::std::stoull(s);
        return true;
    } catch (...) {
        return false;
    }
}

// parse a user mention/id/name in a guild context
static bool parse_user_spec(const ::std::string& input, dpp::snowflake guild, uint64_t& out) {
    ::std::string s = input;
    // mention forms <@123> or <@!123>
    if (s.rfind("<@", 0) == 0 && s.back() == '>') {
        size_t start = 2;
        if (s.size() > 3 && s[2] == '!') start = 3;
        try {
            out = ::std::stoull(s.substr(start, s.size() - start - 1));
            return true;
        } catch (...) {}
    }
    // numeric id
    bool alldigits = !s.empty() && (s.find_first_not_of("0123456789") == ::std::string::npos);
    if (alldigits) {
        try {
            out = ::std::stoull(s);
            return true;
        } catch (...) {}
    }
    // try lookup by name or nickname
    if (guild) {
        dpp::guild* g = dpp::find_guild(guild);
        if (g) {
            for (auto& pr : g->members) {
                const dpp::guild_member& member = pr.second;
                dpp::user* u = member.get_user();
                if ((u && u->username == s) || member.get_nickname() == s) {
                    out = pr.first;
                    return true;
                }
            }
        }
    }
    return false;
}

// parse role mention/id/name in a guild
static bool parse_role_spec(const ::std::string& input, dpp::snowflake guild, uint64_t& out) {
    ::std::string s = input;
    // mention <@&id>
    if (s.rfind("<@&", 0) == 0 && s.back() == '>') {
        try {
            out = ::std::stoull(s.substr(3, s.size() - 4));
            return true;
        } catch (...) {}
    }
    bool alldigits = !s.empty() && (s.find_first_not_of("0123456789") == ::std::string::npos);
    if (alldigits) {
        try { out = ::std::stoull(s); return true; } catch (...) {}
    }
    if (guild) {
        dpp::guild* g = dpp::find_guild(guild);
        if (g) {
            for (auto& rid : g->roles) {
                dpp::role* role = dpp::find_role(static_cast<uint64_t>(rid));
                if (role && role->name == s) {
                    out = static_cast<uint64_t>(rid);
                    return true;
                }
            }
        }
    }
    return false;
}

// Scheduler implementation.  Each time the timer fires it will check that the
// corresponding database row still exists; if not, the timer is stopped.  After
// performing a purge it schedules itself again for the next interval.
static void schedule_autopurge(dpp::cluster& bot, const bronx::db::AutopurgeRow& row) {
    if (!autopurge_db) return;
    bot.start_timer([&bot, row](dpp::timer timer) mutable {
        // verify that this autopurge still exists
        bool still = false;
        auto user_rows = autopurge_db->get_autopurges_for_user(row.user_id);
        for (const auto& r : user_rows) {
            if (r.id == row.id) {
                still = true;
                break;
            }
        }
        if (!still) {
            bot.stop_timer(timer);
            return;
        }

        // recursive helper to page through messages until we've deleted enough or no more messages.
        // Stored in a shared_ptr so it can safely capture itself by value across async callbacks.
        auto purge_page_ptr = std::make_shared<std::function<void(dpp::snowflake,int)>>();
        *purge_page_ptr = [&bot, row, purge_page_ptr](dpp::snowflake before, int remaining) {
            if (remaining <= 0) return;
            int fetch = remaining > 100 ? 100 : remaining;
            bot.messages_get(row.channel_id, before, 0, 0, fetch, [&bot, row, purge_page_ptr, remaining](const dpp::confirmation_callback_t& cb) mutable {
                if (cb.is_error()) return;
                auto messages = cb.get<dpp::message_map>();
                ::std::vector<dpp::snowflake> to_delete;
                dpp::snowflake next_before = 0;
                for (const auto& [msg_id, msg] : messages) {
                    if (next_before == 0 || msg_id < next_before) next_before = msg_id;
                    bool match = false;
                    if (row.target_role_id != 0) {
                        // check member roles
                        dpp::guild* g = dpp::find_guild(row.guild_id);
                        if (g) {
                            auto it = g->members.find(msg.author.id);
                            if (it != g->members.end()) {
                                for (auto rid : it->second.get_roles()) {
                                    if (rid == row.target_role_id) {
                                        match = true;
                                        break;
                                    }
                                }
                            }
                        }
                    } else {
                        uint64_t target = (row.target_user_id != 0 ? row.target_user_id : row.user_id);
                        if (msg.author.id == target) {
                            match = true;
                        }
                    }
                    if (match) {
                        to_delete.push_back(msg_id);
                        if ((int)to_delete.size() >= remaining) break;
                    }
                }
                if (!to_delete.empty()) {
                    if (to_delete.size() >= 2) {
                        bot.message_delete_bulk(to_delete, row.channel_id);
                    } else {
                        bot.message_delete(to_delete.front(), row.channel_id);
                    }
                    remaining -= to_delete.size();
                }
                if (remaining > 0 && next_before != 0 && !messages.empty()) {
                    (*purge_page_ptr)(next_before, remaining);
                }
            });
        };
        (*purge_page_ptr)(0, row.message_limit);

        // schedule next iteration
        schedule_autopurge(bot, row);
        bot.stop_timer(timer);
    }, row.interval_seconds);
}

// The actual command exposed to users
inline Command* get_autopurge_command() {
    static Command cmd("autopurge", "automatically delete your messages on a timer (multiple schedules allowed)", "utility", {"ap"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Help text
            auto help_embed = bronx::info("Autopurge Command Help");
            help_embed.set_description(
                "Usage:\n"
                "`b.autopurge add [<user>] [-u <user>] [-c <channel>] [-r <role>] <interval> <limit>`\n"
                "   start a new autopurge. Interval may end in s/m/h/d. Limit up to 1000 messages.\n"
                "   flags: `-u` specify target user (default you), `-c` specify channel, `-r` specify role.\nFlags can be used in conjunction (e.g. target messages from users with a role, or a specific user in a specific channel)\n"
                "`b.autopurge list` - show your active autopurges in this guild.\n"
                "`b.autopurge remove <id>` - cancel an autopurge by its ID.\n"
                "Example: `b.autopurge rolename -r 1d 1000` will delete messages from anyone with that role every day.\n"
            );

            if (args.empty()) {
                bronx::send_message(bot, event, help_embed);
                return;
            }

            const ::std::string& sub = args[0];
            if (sub == "help" || sub == "-h" || sub == "--help") {
                bronx::send_message(bot, event, help_embed);
                return;
            }

            if (sub == "list") {
                if (!autopurge_db) {
                    bronx::send_message(bot, event, bronx::error("database not configured"));
                    return;
                }
                auto rows = autopurge_db->get_autopurges_for_user(event.msg.author.id);
                ::std::string desc;
                for (const auto& r : rows) {
                    if (r.guild_id != event.msg.guild_id) continue;
                    desc += "ID " + ::std::to_string(r.id) + ": `#" + ::std::to_string(r.channel_id) + "` ";
                    if (r.target_role_id != 0) {
                        desc += "role=<@&" + ::std::to_string(r.target_role_id) + "> ";
                    } else if (r.target_user_id != 0) {
                        desc += "user=<@" + ::std::to_string(r.target_user_id) + "> ";
                    }
                    desc += "every " + format_interval(r.interval_seconds) + ", " + ::std::to_string(r.message_limit) + " msgs\n";
                }
                if (desc.empty()) desc = "no autopurges registered in this server";
                auto embed = bronx::create_embed(desc);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
            }

            if (sub == "remove" || sub == "cancel" || sub == "delete") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("please specify the ID of the autopurge to remove"));
                    return;
                }
                uint64_t id;
                try {
                    id = ::std::stoull(args[1]);
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid autopurge ID"));
                    return;
                }
                if (!autopurge_db) {
                    bronx::send_message(bot, event, bronx::error("database not configured"));
                    return;
                }
                bool ok = autopurge_db->remove_autopurge(id, event.msg.author.id);
                if (ok) {
                    auto embed = bronx::info("autopurge removed");
                    bot.message_create(dpp::message(event.msg.channel_id, embed));
                } else {
                    bronx::send_message(bot, event, bronx::error("could not remove autopurge (maybe wrong ID?)"));
                }
                return;
            }

            // at this point expect "add" or default to add if first token is numeric
            if (sub == "add" || (::std::isdigit(sub.empty() ? '\0' : sub[0]))) {
                size_t offset = (sub == "add") ? 1 : 0;

                // parse optional flags/identifiers before interval
                uint64_t channel_id = event.msg.channel_id;
                uint64_t target_user = 0;
                uint64_t target_role = 0;

                size_t i = offset;
                while (i < args.size()) {
                    const auto& tok = args[i];
                    if (tok == "-u") {
                        i++;
                        if (i < args.size() && parse_user_spec(args[i], event.msg.guild_id, target_user)) {
                            i++;
                        } else {
                            // no explicit user given; leave target_user as-is (author if still 0)
                        }
                        continue;
                    }
                    if (tok == "-c") {
                        i++;
                        if (i < args.size() && parse_channel(args[i], channel_id)) {
                            i++;
                        }
                        continue;
                    }
                    if (tok == "-r") {
                        i++;
                        if (i < args.size() && parse_role_spec(args[i], event.msg.guild_id, target_role)) {
                            i++;
                        }
                        continue;
                    }
                    int tmpsecs;
                    if (parse_interval(tok, tmpsecs)) break;
                    uint64_t tmpid;
                    if (parse_user_spec(tok, event.msg.guild_id, tmpid)) {
                        target_user = tmpid;
                        i++;
                        continue;
                    }
                    if (parse_channel(tok, tmpid)) {
                        channel_id = tmpid;
                        i++;
                        continue;
                    }
                    if (parse_role_spec(tok, event.msg.guild_id, tmpid)) {
                        target_role = tmpid;
                        i++;
                        continue;
                    }
                    break;
                }

                if (i + 1 >= args.size()) {
                    bronx::send_message(bot, event, bronx::error("not enough arguments for add (interval and limit required)"));
                    return;
                }

                int interval_secs;
                if (!parse_interval(args[i], interval_secs)) {
                    bronx::send_message(bot, event, bronx::error("invalid interval"));
                    return;
                }
                // enforce minimum interval to avoid bulk-delete rate limits
                const int MIN_INTERVAL = 300; // 5 minutes in seconds
                if (interval_secs < MIN_INTERVAL) {
                    bronx::send_message(bot, event, bronx::error("interval must be at least 5m (" + ::std::to_string(MIN_INTERVAL) + "s)"));
                    return;
                }
                int limit;
                try {
                    limit = ::std::stoi(args[i + 1]);
                } catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid limit"));
                    return;
                }
                if (limit < 1) limit = 1;
                const int MAX_LIMIT = 1000;
                // compute dynamic cap based on interval (approx 1 msg per 3s)
                int max_allowed = interval_secs / 3;
                if (max_allowed < 1) max_allowed = 1;
                if (max_allowed > MAX_LIMIT) max_allowed = MAX_LIMIT;
                if (limit > max_allowed) {
                    limit = max_allowed;
                    bronx::send_message(bot, event, bronx::error("limit capped to " + ::std::to_string(max_allowed) + " based on interval"));
                }

                if (!autopurge_db) {
                    bronx::send_message(bot, event, bronx::error("database not configured"));
                    return;
                }
                if (target_user == 0) target_user = event.msg.author.id;
                uint64_t rowid = autopurge_db->add_autopurge(event.msg.author.id, event.msg.guild_id,
                                                            channel_id, interval_secs, limit,
                                                            target_user, target_role);
                if (rowid == 0) {
                    ::std::string msg = "failed to create autopurge";
                    if (autopurge_db) {
                        ::std::string err = autopurge_db->get_last_error();
                        if (!err.empty()) msg += ": " + err;
                    }
                    bronx::send_message(bot, event, bronx::error(msg));
                    return;
                }
                auto info_embed = bronx::info("autopurge scheduled (id " + ::std::to_string(rowid) + ")");
                bot.message_create(dpp::message(event.msg.channel_id, info_embed));
                schedule_autopurge(bot, {rowid, event.msg.author.id, event.msg.guild_id,
                                         channel_id, interval_secs, limit, target_user, target_role});
                return;
            }

            // unknown subcommand
            bronx::send_message(bot, event, help_embed);
        });
    return &cmd;
}

} // namespace utility
} // namespace commands
