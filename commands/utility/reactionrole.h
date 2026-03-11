#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include <dpp/dpp.h>
#include <string>
#include <map>
#include <vector>
#include <regex>
#include <algorithm>

namespace commands {
namespace utility {

struct RREntry {
    uint64_t role_id;
    // store custom emoji id (0 if unicode) and unicode/name string for comparison
    uint64_t emoji_id = 0;
    ::std::string emoji_str;
    ::std::string raw; // raw user-provided emoji (used when adding reaction)
};

struct RRMessage {
    uint64_t channel_id = 0;
    ::std::vector<RREntry> entries;
};

// message_id -> RRMessage
static ::std::map<uint64_t, RRMessage> reaction_roles;

// Optional database pointer (set from main)
static bronx::db::Database* rr_db = nullptr;
inline void set_reactionrole_db(bronx::db::Database* db) { rr_db = db; }

// Validate and sanitize emoji string for database storage
static ::std::string sanitize_emoji_for_db(const ::std::string& input) {
    ::std::string result;
    result.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = input[i];
        // Only allow printable ASCII and valid UTF-8 start bytes
        if (c >= 32 && c <= 126) {
            result += c; // ASCII printable
        } else if (c >= 0xC2 && c <= 0xF4) {
            // Valid UTF-8 start byte, copy the full sequence
            result += c;
            // Determine sequence length
            int len = 1;
            if (c >= 0xF0) len = 3;      // 4-byte sequence
            else if (c >= 0xE0) len = 2; // 3-byte sequence
            else len = 1;                // 2-byte sequence
            
            // Copy continuation bytes
            for (int j = 0; j < len && i + 1 < input.size(); ++j) {
                ++i;
                unsigned char cont = input[i];
                if ((cont & 0xC0) == 0x80) { // Valid continuation byte
                    result += cont;
                } else {
                    break; // Invalid sequence, stop copying
                }
            }
        }
        // Skip invalid bytes (control characters, invalid UTF-8)
    }
    
    // Limit length to prevent database issues
    if (result.size() > 200) {
        result = result.substr(0, 200);
    }
    
    return result;
}

// Validate emoji string to prevent database crashes
static bool is_safe_emoji_string(const ::std::string& str) {
    // Check for basic safety: no null bytes, reasonable length
    if (str.empty() || str.size() > 255) return false;
    if (str.find('\0') != ::std::string::npos) return false;
    
    // Check for valid UTF-8 sequences and printable characters
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') return false; // Control chars
        if (c >= 127 && c < 192) return false; // Invalid UTF-8 continuation
    }
    return true;
}

// normalize string for message_add_reaction (forward declaration; defined below)
static ::std::string normalize_emoji_for_reaction(::std::string s);

// Sync existing reactions on a message for a given emoji — grant the role to every
// user who already reacted but may not have the role. Handles pagination (Discord caps at 100).
inline void sync_existing_reactions(dpp::cluster& bot, uint64_t message_id, uint64_t channel_id,
                                    const std::string& emoji_reaction, uint64_t role_id,
                                    uint64_t guild_id, uint64_t after_user = 0) {
    if (guild_id == 0) return;
    bot.message_get_reactions(message_id, channel_id, emoji_reaction, 0, after_user, 100,
        [&bot, message_id, channel_id, emoji_reaction, role_id, guild_id](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::cerr << "[reaction-roles] sync: failed to fetch reactions: " << cb.get_error().message << std::endl;
                return;
            }
            auto users = std::get<dpp::user_map>(cb.value);
            uint64_t last_id = 0;
            for (const auto& [uid, user] : users) {
                if (user.is_bot()) continue;
                bot.guild_member_add_role(guild_id, uid, role_id, [uid](const dpp::confirmation_callback_t& rcb) {
                    if (rcb.is_error()) {
                        std::cerr << "[reaction-roles] sync: could not add role to " << uid << ": " << rcb.get_error().message << std::endl;
                    }
                });
                last_id = static_cast<uint64_t>(uid);
            }
            // Paginate if we got a full page
            if (users.size() >= 100 && last_id != 0) {
                sync_existing_reactions(bot, message_id, channel_id, emoji_reaction, role_id, guild_id, last_id);
            }
        });
}

// Load persisted reaction-roles from the database into memory and (best-effort) re-add reactions
inline void load_persistent_reaction_roles(dpp::cluster& bot) {
    if (!rr_db) return;
    // make sure the table exists (migrations may not have been applied)
    rr_db->execute("CREATE TABLE IF NOT EXISTS reaction_roles ("
                   "message_id BIGINT UNSIGNED NOT NULL,"
                   "channel_id BIGINT UNSIGNED NOT NULL,"
                   "emoji_raw VARCHAR(255) NOT NULL,"
                   "emoji_id BIGINT UNSIGNED DEFAULT 0,"
                   "role_id BIGINT UNSIGNED NOT NULL,"
                   "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                   "PRIMARY KEY (message_id, emoji_raw),"
                   "INDEX idx_message (message_id),"
                   "INDEX idx_channel (channel_id),"
                   "INDEX idx_role (role_id)"
                   ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;");
    // make sure the column charset is utf8mb4 as well (existing tables may have
    // latin1 defaults). this will convert any existing data in-place.
    rr_db->execute("ALTER TABLE reaction_roles CONVERT TO CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;");
    // also ensure the specific column is set correctly (some MySQL versions keep
    // the old charset on the column even after a table conversion)
    rr_db->execute("ALTER TABLE reaction_roles MODIFY emoji_raw VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL;");
    // sanitize any legacy values in the table by stripping all <,>, and : characters from the ends
    rr_db->execute("UPDATE reaction_roles SET emoji_raw = TRIM(BOTH ':' FROM TRIM(BOTH '<>' FROM emoji_raw))");
    // remove any duplicates that might result from the normalization above
    rr_db->execute(R"SQL(
        DELETE r1 FROM reaction_roles r1
        INNER JOIN reaction_roles r2
          ON r1.message_id = r2.message_id
         AND r1.emoji_raw = r2.emoji_raw
         AND r1.created_at > r2.created_at
    )SQL");
    // Clear in-memory map before loading (safe if on_ready fires multiple times due to reconnects)
    reaction_roles.clear();
    auto rows = rr_db->get_all_reaction_roles();
    std::cout << "[reaction-roles] loading " << rows.size() << " persisted reaction role(s) from database" << std::endl;
    for (const auto &r : rows) {
        reaction_roles[r.message_id].channel_id = r.channel_id;
        // normalize the stored emoji and update DB if it changed
        std::string norm = normalize_emoji_for_reaction(r.emoji_raw);
        if (norm != r.emoji_raw) {
            // add normalized row then remove old one so we don't end up with duplicates
            rr_db->add_reaction_role(r.message_id, r.channel_id, norm, r.emoji_id, r.role_id);
            rr_db->remove_reaction_role(r.message_id, r.emoji_raw);
        }
        RREntry e;
        e.role_id = r.role_id;
        e.emoji_id = r.emoji_id;
        e.emoji_str = norm;
        e.raw = norm;
        reaction_roles[r.message_id].entries.push_back(e);
        // best-effort: add reaction to message so users can click it
        // (staggered via timer to avoid rate limits on startup)
    }

    // Stagger reaction-adds and sync to avoid hitting rate limits on startup.
    // We queue each operation with a small delay between them.
    struct RRSyncItem {
        uint64_t message_id;
        uint64_t channel_id;
        std::string emoji;
        uint64_t role_id;
        uint64_t guild_id;
    };
    auto sync_items = std::make_shared<std::vector<RRSyncItem>>();

    for (const auto& r : rows) {
        std::string norm = normalize_emoji_for_reaction(r.emoji_raw);
        dpp::channel* ch = dpp::find_channel(r.channel_id);
        uint64_t guild_id = ch ? static_cast<uint64_t>(ch->guild_id) : 0;
        sync_items->push_back({r.message_id, r.channel_id, norm, r.role_id, guild_id});
    }

    if (!sync_items->empty()) {
        std::cout << "[reaction-roles] syncing existing reactions (" << sync_items->size() << " items, staggered)..." << std::endl;
        auto idx = std::make_shared<size_t>(0);
        bot.start_timer([&bot, sync_items, idx](dpp::timer t) {
            if (*idx >= sync_items->size()) {
                bot.stop_timer(t);
                std::cout << "[reaction-roles] sync complete" << std::endl;
                return;
            }
            const auto& item = (*sync_items)[*idx];
            // re-add bot reaction
            try { bot.message_add_reaction(item.message_id, item.channel_id, item.emoji); } catch (...) {}
            // sync existing user reactions
            if (item.guild_id != 0) {
                sync_existing_reactions(bot, item.message_id, item.channel_id, item.emoji, item.role_id, item.guild_id);
            }
            (*idx)++;
        }, 2); // 2-second interval between each reaction-role sync
    }
}

// Helper: check if member has Manage Roles or Administrator
static bool member_can_manage_roles(dpp::cluster& bot, const dpp::snowflake& guild_id, const dpp::guild_member& member) {
    // owner always allowed
    dpp::guild* g = dpp::find_guild(guild_id);
    if (g && g->owner_id == member.user_id) return true;

    for (const auto &rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (!r) continue;
        uint64_t perms = static_cast<uint64_t>(r->permissions);
        if (perms & static_cast<uint64_t>(dpp::p_administrator)) return true;
        if (perms & static_cast<uint64_t>(dpp::p_manage_roles)) return true;
    }
    return false;
}

// Parse message reference (message id or message link). If only message id is provided, use default_channel.
static bool parse_message_ref(const ::std::string& input, uint64_t default_channel, uint64_t& out_channel, uint64_t& out_message) {
    ::std::string s = input;
    // trim
    s.erase(0, s.find_first_not_of(" \t\n\r"));
    s.erase(s.find_last_not_of(" \t\n\r") + 1);

    // If URL-like contains /channels/, extract last two segments
    size_t pos = s.find("/channels/");
    if (pos != ::std::string::npos) {
        // split by '/'
        ::std::vector<::std::string> parts;
        ::std::stringstream ss(s.substr(pos + 10)); // after /channels/
        ::std::string item;
        while (::std::getline(ss, item, '/')) parts.push_back(item);
        if (parts.size() >= 3) {
            try {
                // parts[1] = channel, parts[2] = message
                out_channel = ::std::stoull(parts[1]);
                out_message = ::std::stoull(parts[2]);
                return true;
            } catch (...) { return false; }
        }
        return false;
    }

    // If just digits -> treat as message id in current channel
    bool alldigits = !s.empty() && (s.find_first_not_of("0123456789") == ::std::string::npos);
    if (alldigits) {
        try {
            out_message = ::std::stoull(s);
            out_channel = default_channel;
            return true;
        } catch (...) { return false; }
    }

    return false;
}

// Normalize a string for message_add_reaction: strip surrounding angle brackets if present
static ::std::string normalize_emoji_for_reaction(::std::string s) {
    // trim whitespace
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(0,1);
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    // strip any number of leading '<' or ':' characters
    while (!s.empty() && (s.front() == '<' || s.front() == ':')) s.erase(0,1);
    // strip any number of trailing '>' or ':' characters
    while (!s.empty() && (s.back() == '>' || s.back() == ':')) s.pop_back();
    return s;
}

// Parse emoji: return pair(emoji_id, emoji_string). emoji_id==0 => unicode (emoji_string holds text)
static ::std::pair<uint64_t, ::std::string> parse_emoji_raw(const ::std::string& input) {
    ::std::string s = input;
    // formats to accept: unicode (🎉), <:name:id>, <a:name:id>, name:id or just emoji name
    ::std::smatch m;
    // allow any characters except ':' or '>' for emoji name (Discord restricts names but
    // this avoids rejecting valid server emoji with tildes etc)
    ::std::regex custom_re("<a?:([^:>]+):([0-9]+)>");
    if (::std::regex_search(s, m, custom_re) && m.size() >= 3) {
        try {
            uint64_t id = ::std::stoull(m[2]);
            return { id, ::std::string(m[1]) }; // store name in emoji_str
        } catch (...) {}
    }
    // name:id (unbracketed)
    ::std::regex nameid_re("^([^:]+):([0-9]+)$");
    if (::std::regex_search(s, m, nameid_re) && m.size() >= 3) {
        try {
            uint64_t id = ::std::stoull(m[2]);
            return { id, ::std::string(m[1]) };
        } catch (...) {}
    }

    // fallback: treat entire string as unicode/emoji text
    return { 0, s };
}

// ── pending "next message" reaction tracking ──────────────────────────────────
// Stores pending reaction-role that will be applied to the user's next message
struct PendingNextReaction {
    uint64_t user_id;
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t role_id;
    uint64_t emoji_id;       // 0 = unicode
    std::string emoji_str;   // name or unicode codepoint
    std::string emoji_raw;   // normalized emoji for reaction
    std::chrono::steady_clock::time_point created_at;
    bool silent;             // whether to add the bot reaction
};

// user_id -> pending reaction (only one pending per user)
static std::map<uint64_t, PendingNextReaction> pending_next_reactions;
static std::mutex pending_next_mutex;

// Format emoji for display in messages
static std::string format_emoji_display(uint64_t emoji_id, const std::string& emoji_str) {
    if (emoji_id != 0) {
        return "<:" + emoji_str + ":" + std::to_string(emoji_id) + ">";
    }
    return emoji_str;
}

// Check and remove expired pending reactions (30 second timeout)
static void cleanup_expired_pending_reactions() {
    std::lock_guard<std::mutex> lock(pending_next_mutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = pending_next_reactions.begin(); it != pending_next_reactions.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at);
        if (elapsed.count() >= 30) {
            it = pending_next_reactions.erase(it);
        } else {
            ++it;
        }
    }
}

// ── rr check session tracking ──────────────────────────────────────────────
// One PendingRREmoji per unmapped bot-reaction found on the target message.
struct PendingRREmoji {
    uint64_t emoji_id;       // 0 = unicode
    std::string emoji_str;   // name or unicode codepoint
    std::string emoji_raw;   // raw reaction string for storage / matching
    uint64_t assigned_role;  // 0 until the user picks a role
};

// A full check session containing all unmapped emojis, current page, etc.
struct RRCheckSession {
    uint64_t message_id;
    uint64_t channel_id;
    uint64_t guild_id;
    uint64_t user_id;        // who initiated
    std::vector<PendingRREmoji> emojis;          // all unmapped emojis
    std::vector<std::pair<uint64_t, std::string>> guild_roles; // cached (id, name)
    size_t page;             // current page (0-indexed, 5 items per page)
    size_t answered_on_page; // how many dropdowns on the current page have been answered
};

// session_key -> session   (key = "rrcs_<message_id>_<user_id>")
static std::map<std::string, RRCheckSession> rr_check_sessions;

static std::string rr_session_key(uint64_t message_id, uint64_t user_id) {
    return "rrcs_" + std::to_string(message_id) + "_" + std::to_string(user_id);
}

// Build / rebuild the check message for the current page of a session.
static dpp::message build_check_page(const RRCheckSession& s) {
    dpp::message resp;
    size_t start = s.page * 5;
    size_t end = std::min(start + 5, s.emojis.size());
    size_t total_pages = (s.emojis.size() + 4) / 5;

    for (size_t i = start; i < end; ++i) {
        const auto& em = s.emojis[i];
        std::string emoji_display;
        if (em.emoji_id != 0)
            emoji_display = "<:" + em.emoji_str + ":" + std::to_string(em.emoji_id) + ">";
        else
            emoji_display = em.emoji_str;

        // unique custom_id: rr_chk_<message_id>_<emoji_index>_<user_id>
        std::string custom_id = "rr_chk_" + std::to_string(s.message_id) + "_" + std::to_string(i) + "_" + std::to_string(s.user_id);

        if (em.assigned_role != 0) {
            // already answered — show disabled select with the chosen role
            dpp::role* r = dpp::find_role(em.assigned_role);
            std::string role_label = r ? r->name : std::to_string(em.assigned_role);

            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder(emoji_display + " → " + role_label + " ✓")
                  .set_id(custom_id)
                  .set_disabled(true);
            // need at least one option even if disabled
            select.add_select_option(dpp::select_option(role_label, std::to_string(em.assigned_role)));
            resp.add_component(dpp::component().add_component(select));
        } else {
            dpp::component select;
            select.set_type(dpp::cot_selectmenu)
                  .set_placeholder("what role is " + emoji_display + " for?")
                  .set_id(custom_id);
            for (const auto& [role_id, role_name] : s.guild_roles) {
                select.add_select_option(dpp::select_option(role_name, std::to_string(role_id)));
            }
            resp.add_component(dpp::component().add_component(select));
        }
    }

    // summary text
    size_t total_done = 0;
    for (const auto& em : s.emojis) if (em.assigned_role != 0) ++total_done;
    std::string status = "assigned " + std::to_string(total_done) + "/" + std::to_string(s.emojis.size());
    if (total_pages > 1) status += "  •  page " + std::to_string(s.page + 1) + "/" + std::to_string(total_pages);

    resp.add_embed(bronx::info("please remind me, what role is each emoji for?\n" + status));
    return resp;
}

// Build the final confirmation summary and a confirm button.
static dpp::message build_check_confirm(const RRCheckSession& s) {
    dpp::message resp;
    std::string summary;
    for (const auto& em : s.emojis) {
        std::string emoji_display;
        if (em.emoji_id != 0)
            emoji_display = "<:" + em.emoji_str + ":" + std::to_string(em.emoji_id) + ">";
        else
            emoji_display = em.emoji_str;

        dpp::role* r = dpp::find_role(em.assigned_role);
        std::string role_name = r ? r->name : std::to_string(em.assigned_role);
        summary += emoji_display + " → <@&" + std::to_string(em.assigned_role) + "> (**" + role_name + "**)\n";
    }
    resp.add_embed(bronx::success("all reaction roles assigned!\n\n" + summary));

    dpp::component btn;
    btn.set_type(dpp::cot_button)
       .set_label("Confirm & Sync")
       .set_style(dpp::cos_success)
       .set_id("rr_chk_confirm_" + std::to_string(s.message_id) + "_" + std::to_string(s.user_id));
    resp.add_component(dpp::component().add_component(btn));

    return resp;
}

// Handle "rr check": fetch the target message, find reactions that the bot made,
// and start an interactive session with role dropdowns.
inline void handle_rr_check(dpp::cluster& bot, uint64_t channel_id, uint64_t message_id,
                            uint64_t guild_id, uint64_t user_id,
                            std::function<void(const dpp::embed&)> reply_embed,
                            std::function<void(dpp::message)> reply_msg) {
    bot.message_get(message_id, channel_id, [&bot, channel_id, message_id, guild_id, user_id, reply_embed, reply_msg](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            reply_embed(bronx::error("failed to fetch message"));
            return;
        }
        auto msg = std::get<dpp::message>(cb.value);

        // collect bot reactions not already tracked
        std::vector<PendingRREmoji> unmapped;
        for (const auto& r : msg.reactions) {
            if (!r.me) continue;
            auto it = reaction_roles.find(message_id);
            bool already = false;
            if (it != reaction_roles.end()) {
                for (const auto& e : it->second.entries) {
                    if (r.emoji_id != 0 && e.emoji_id == static_cast<uint64_t>(r.emoji_id)) { already = true; break; }
                    if (r.emoji_id == 0 && (e.emoji_str == r.emoji_name || e.raw == r.emoji_name)) { already = true; break; }
                }
            }
            if (already) continue;
            PendingRREmoji pe;
            pe.emoji_id = static_cast<uint64_t>(r.emoji_id);
            pe.emoji_str = r.emoji_name;
            if (pe.emoji_id != 0)
                pe.emoji_raw = r.emoji_name + ":" + std::to_string(pe.emoji_id);
            else
                pe.emoji_raw = r.emoji_name;
            pe.assigned_role = 0;
            unmapped.push_back(pe);
        }

        if (unmapped.empty()) {
            reply_embed(bronx::info("no unmapped bot reactions found on that message"));
            return;
        }

        // cache guild roles
        dpp::guild* g = dpp::find_guild(guild_id);
        std::vector<std::pair<uint64_t, std::string>> guild_roles;
        if (g) {
            for (const auto& rid : g->roles) {
                dpp::role* role = dpp::find_role(static_cast<uint64_t>(rid));
                if (!role) continue;
                if (role->is_managed()) continue;
                if (role->name == "@everyone") continue;
                guild_roles.push_back({static_cast<uint64_t>(role->id), role->name});
            }
        }
        if (guild_roles.empty()) {
            reply_embed(bronx::error("no assignable roles found in this server"));
            return;
        }
        if (guild_roles.size() > 25) guild_roles.resize(25);

        // create session
        RRCheckSession session;
        session.message_id = message_id;
        session.channel_id = channel_id;
        session.guild_id = guild_id;
        session.user_id = user_id;
        session.emojis = std::move(unmapped);
        session.guild_roles = std::move(guild_roles);
        session.page = 0;
        session.answered_on_page = 0;

        std::string key = rr_session_key(message_id, user_id);
        rr_check_sessions[key] = std::move(session);

        reply_msg(build_check_page(rr_check_sessions[key]));
    });
}

// register handlers and command
inline Command* get_reactionrole_command() {
    static Command* rr = new Command("reactionrole", "create a reaction role on a message (manage roles required)", "utility", {"rr"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // permission check
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command must be used in a server"));
                return;
            }
            dpp::guild_member member = event.msg.member;
            if (!member_can_manage_roles(bot, event.msg.guild_id, member)) {
                bronx::send_message(bot, event, bronx::error("you need Manage Roles or Administrator to use this command"));
                return;
            }

            // accept optional leading subcommand token
            size_t offset = 0;
            bool silent = false;
            if (!args.empty()) {
                ::std::string first = args[0];
                ::std::transform(first.begin(), first.end(), first.begin(), ::tolower);
                if (first == "add" || first == "create") offset = 1;
                // ---- rr check <^|id|link> ----
                if (first == "check") {
                    if (args.size() < 2) {
                        bronx::send_message(bot, event, bronx::error("usage: rr check <messageid|messagelink|^>"));
                        return;
                    }
                    uint64_t ch_id = 0, msg_id = 0;
                    if (args[1] == "^") {
                        ch_id = event.msg.channel_id;
                        // fetch recent messages to get the one above
                        uint64_t cur_msg = static_cast<uint64_t>(event.msg.id);
                        uint64_t gid = static_cast<uint64_t>(event.msg.guild_id);
                        uint64_t uid = static_cast<uint64_t>(event.msg.author.id);
                        bot.messages_get(ch_id, 0, 0, 0, 3, [&bot, event, ch_id, cur_msg, gid, uid](const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                bronx::send_message(bot, event, bronx::error("failed to fetch recent messages"));
                                return;
                            }
                            auto messages = std::get<dpp::message_map>(cb.value);
                            uint64_t target = 0;
                            for (const auto& [mid, m] : messages) {
                                if (static_cast<uint64_t>(mid) < cur_msg) {
                                    if (target == 0 || static_cast<uint64_t>(mid) > target) target = static_cast<uint64_t>(mid);
                                }
                            }
                            if (target == 0) {
                                bronx::send_message(bot, event, bronx::error("no message found above this command"));
                                return;
                            }
                            handle_rr_check(bot, ch_id, target, gid, uid,
                                [&bot, event](const dpp::embed& e) { bronx::send_message(bot, event, e); },
                                [&bot, event](dpp::message msg) { bot.message_create(msg.set_channel_id(event.msg.channel_id)); });
                        });
                    } else {
                        if (!parse_message_ref(args[1], event.msg.channel_id, ch_id, msg_id)) {
                            bronx::send_message(bot, event, bronx::error("invalid message id or link"));
                            return;
                        }
                        uint64_t gid = static_cast<uint64_t>(event.msg.guild_id);
                        uint64_t uid = static_cast<uint64_t>(event.msg.author.id);
                        handle_rr_check(bot, ch_id, msg_id, gid, uid,
                            [&bot, event](const dpp::embed& e) { bronx::send_message(bot, event, e); },
                            [&bot, event](dpp::message msg) { bot.message_create(msg.set_channel_id(event.msg.channel_id)); });
                    }
                    return;
                }
            }
            // consume any number of -s/--silent or -n/--next flags before the message id
            bool next_mode = false;
            while (offset < args.size()) {
                if (args[offset] == "-s" || args[offset] == "--silent") {
                    silent = true;
                    offset++;
                } else if (args[offset] == "-n" || args[offset] == "--next") {
                    next_mode = true;
                    offset++;
                } else {
                    break;
                }
            }

            // Handle --next mode: wait for user's next message to add reaction
            if (next_mode) {
                if (args.size() < offset + 2) {
                    bronx::send_message(bot, event, bronx::error("usage: reactionrole -n <emoji> <@role|roleid|role name>"));
                    return;
                }

                // Parse emoji
                ::std::string emoji_raw = args[offset];
                auto [emoji_id, emoji_str] = parse_emoji_raw(emoji_raw);
                if (emoji_id != 0) {
                    std::string prefix = "<";
                    if (emoji_raw.rfind("<a:", 0) == 0) prefix += "a:";
                    else prefix += ":";
                    emoji_raw = prefix + emoji_str + ":" + std::to_string(emoji_id) + ">";
                } else if (emoji_raw.find(":") != ::std::string::npos) {
                    bronx::send_message(bot, event, bronx::error("invalid emoji format; please provide a unicode emoji or a custom one with its id"));
                    return;
                }
                emoji_raw = normalize_emoji_for_reaction(emoji_raw);

                // Parse role argument
                ::std::string role_arg = args[offset + 1];
                if (args.size() > offset + 2) {
                    for (size_t i = offset + 2; i < args.size(); ++i) role_arg += " " + args[i];
                }

                uint64_t role_id = 0;
                ::std::smatch rm;
                ::std::regex mention_re("<@&([0-9]+)>");
                if (::std::regex_search(role_arg, rm, mention_re) && rm.size() >= 2) {
                    role_id = ::std::stoull(rm[1]);
                } else {
                    try { role_id = ::std::stoull(role_arg); } catch (...) { role_id = 0; }
                    if (role_id == 0) {
                        dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                        if (!g) {
                            bronx::send_message(bot, event, bronx::error("guild not cached; please use a role mention or id"));
                            return;
                        }
                        ::std::string query = role_arg;
                        ::std::transform(query.begin(), query.end(), query.begin(), ::tolower);
                        ::std::vector<dpp::role*> exact_matches;
                        ::std::vector<dpp::role*> partial_matches;
                        for (const auto& rid : g->roles) {
                            dpp::role* rr = dpp::find_role(static_cast<uint64_t>(rid));
                            if (!rr) continue;
                            ::std::string name = rr->name;
                            ::std::string lname = name;
                            ::std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                            if (lname == query) exact_matches.push_back(rr);
                            else if (lname.find(query) != ::std::string::npos) partial_matches.push_back(rr);
                        }
                        if (exact_matches.size() == 1) role_id = exact_matches[0]->id;
                        else if (exact_matches.size() > 1) {
                            bronx::send_message(bot, event, bronx::error("multiple roles match that name; use role mention or id"));
                            return;
                        } else if (partial_matches.size() == 1) role_id = partial_matches[0]->id;
                        else if (partial_matches.size() > 1) {
                            bronx::send_message(bot, event, bronx::error("multiple roles partially match; use role mention or id"));
                            return;
                        } else {
                            bronx::send_message(bot, event, bronx::error("role not found"));
                            return;
                        }
                    }
                }

                dpp::role* r = dpp::find_role(role_id);
                if (!r) {
                    bronx::send_message(bot, event, bronx::error("role not found"));
                    return;
                }

                // Store pending reaction
                uint64_t user_id = static_cast<uint64_t>(event.msg.author.id);
                uint64_t channel_id = static_cast<uint64_t>(event.msg.channel_id);
                uint64_t guild_id = static_cast<uint64_t>(event.msg.guild_id);

                {
                    std::lock_guard<std::mutex> lock(pending_next_mutex);
                    PendingNextReaction pending;
                    pending.user_id = user_id;
                    pending.channel_id = channel_id;
                    pending.guild_id = guild_id;
                    pending.role_id = role_id;
                    pending.emoji_id = emoji_id;
                    pending.emoji_str = emoji_str;
                    pending.emoji_raw = emoji_raw;
                    pending.created_at = std::chrono::steady_clock::now();
                    pending.silent = silent;
                    pending_next_reactions[user_id] = pending;
                }

                // Build confirmation message
                std::string emoji_display = format_emoji_display(emoji_id, emoji_str);
                std::string desc = "waiting to react with\n";
                desc += "<@&" + std::to_string(role_id) + "> - " + emoji_display + "\n";
                desc += "to your next message\n\n";
                desc += "type `cancel` to cancel";

                bronx::send_message(bot, event, bronx::info(desc));
                return;
            }

            if (args.size() < offset + 3) {
                bronx::send_message(bot, event, bronx::error("usage: reactionrole [add|check] [-s] [-n] <messageid|messagelink|^> <emoji> <@role|roleid|role name>\n^ = message above this command\n-n = react to your next message"));
                return;
            }

            uint64_t channel_id = 0, message_id = 0;
            
            // Handle "^" to reference message above current message
            if (args[offset] == "^") {
                channel_id = event.msg.channel_id;
                
                // Extract the values we need before async call to avoid memory issues
                ::std::string emoji_arg = args[offset + 1];
                ::std::string role_arg = args[offset + 2];
                if (args.size() > offset + 3) {
                    for (size_t i = offset + 3; i < args.size(); ++i) role_arg += " " + args[i];
                }
                uint64_t current_msg_id = static_cast<uint64_t>(event.msg.id);
                uint64_t guild_id = static_cast<uint64_t>(event.msg.guild_id);
                
                // Fetch only 3 recent messages to reduce memory usage
                bot.messages_get(channel_id, 0, 0, 0, 3, [&bot, event, emoji_arg, role_arg, channel_id, current_msg_id, guild_id](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        bronx::send_message(bot, event, bronx::error("failed to fetch recent messages"));
                        return;
                    }
                    
                    auto messages = std::get<dpp::message_map>(cb.value);
                    uint64_t target_message_id = 0;
                    
                    // Find message immediately before the current command message
                    for (const auto& [msg_id, msg] : messages) {
                        if (static_cast<uint64_t>(msg_id) < current_msg_id) {
                            if (target_message_id == 0 || static_cast<uint64_t>(msg_id) > target_message_id) {
                                target_message_id = static_cast<uint64_t>(msg_id);
                            }
                        }
                    }
                    
                    if (target_message_id == 0) {
                        bronx::send_message(bot, event, bronx::error("no message found above this command"));
                        return;
                    }
                    
                    // Parse emoji
                    ::std::string emoji_raw_local = emoji_arg;
                    auto [emoji_id, emoji_str] = parse_emoji_raw(emoji_raw_local);
                    if (emoji_id != 0) {
                        std::string prefix = "<";
                        if (emoji_raw_local.rfind("<a:", 0) == 0) prefix += "a:";
                        else prefix += ":";
                        emoji_raw_local = prefix + emoji_str + ":" + std::to_string(emoji_id) + ">";
                    } else if (emoji_raw_local.find(":") != ::std::string::npos) {
                        bronx::send_message(bot, event, bronx::error("invalid emoji format; please provide a unicode emoji or a custom one with its id (e.g. <:name:id> or name:id)"));
                        return;
                    }
                    emoji_raw_local = normalize_emoji_for_reaction(emoji_raw_local);

                    // Parse role
                    uint64_t role_id = 0;
                    ::std::smatch rm;
                    ::std::regex mention_re("<@&([0-9]+)>");
                    if (::std::regex_search(role_arg, rm, mention_re) && rm.size() >= 2) {
                        role_id = ::std::stoull(rm[1]);
                    } else {
                        try { role_id = ::std::stoull(role_arg); } catch (...) { role_id = 0; }
                        if (role_id == 0) {
                            dpp::guild* g = dpp::find_guild(guild_id);
                            if (!g) {
                                bronx::send_message(bot, event, bronx::error("guild not cached; please use a role mention or id"));
                                return;
                            }
                            ::std::string query = role_arg;
                            ::std::transform(query.begin(), query.end(), query.begin(), ::tolower);
                            ::std::vector<dpp::role*> exact_matches;
                            ::std::vector<dpp::role*> partial_matches;
                            for (const auto &role_id : g->roles) {
                                dpp::role* rr = dpp::find_role(static_cast<uint64_t>(role_id));
                                if (!rr) continue;
                                ::std::string name = rr->name;
                                ::std::string lname = name;
                                ::std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                                if (lname == query) exact_matches.push_back(rr);
                                else if (lname.find(query) != ::std::string::npos) partial_matches.push_back(rr);
                            }
                            if (exact_matches.size() == 1) {
                                role_id = exact_matches[0]->id;
                            } else if (exact_matches.size() > 1) {
                                ::std::string list = "multiple roles match that name; specify mention or id:\\n";
                                for (auto *rr : exact_matches) list += "`" + rr->name + "` (<@&" + ::std::to_string(rr->id) + ")\\n";
                                bronx::send_message(bot, event, bronx::error(list));
                                return;
                            } else if (partial_matches.size() == 1) {
                                role_id = partial_matches[0]->id;
                            } else if (partial_matches.size() > 1) {
                                ::std::string list = "multiple roles partially match; specify mention or id:\\n";
                                for (auto *rr : partial_matches) list += "`" + rr->name + "` (<@&" + ::std::to_string(rr->id) + ")\\n";
                                bronx::send_message(bot, event, bronx::error(list));
                                return;
                            } else {
                                bronx::send_message(bot, event, bronx::error("role not found"));
                                return;
                            }
                        }
                    }

                    dpp::role* r = dpp::find_role(role_id);
                    if (!r) {
                        bronx::send_message(bot, event, bronx::error("role not found"));
                        return;
                    }

                    // Add reaction role
                    RREntry e;
                    e.role_id = role_id;
                    e.emoji_id = emoji_id;
                    e.emoji_str = emoji_str;
                    e.raw = emoji_raw_local;
                    reaction_roles[target_message_id].channel_id = channel_id;
                    reaction_roles[target_message_id].entries.push_back(e);
                    
                    if (rr_db) {
                        try {
                            if (is_safe_emoji_string(emoji_raw_local)) {
                                rr_db->add_reaction_role(target_message_id, channel_id, emoji_raw_local, emoji_id, role_id);
                            } else {
                                std::cerr << "Emoji string failed safety check, skipping DB storage" << std::endl;
                            }
                        } catch (const std::exception& ex) {
                            std::cerr << "DB add error: " << ex.what() << std::endl;
                        }
                    }
                    
                    // Sync existing reactions — grant the role to anyone who already reacted
                    sync_existing_reactions(bot, target_message_id, channel_id, emoji_raw_local, role_id, guild_id);

                    bot.message_add_reaction(target_message_id, channel_id, emoji_raw_local,
                        [&bot, event](const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                std::cerr << "reaction add failed: " << cb.get_error().message << std::endl;
                                bronx::send_message(bot, event, bronx::error("could not add reaction: " + cb.get_error().message));
                            } else {
                                bronx::send_message(bot, event, bronx::success("reaction role added to message above"));
                            }
                        });
                });
                return;
            }
            
            // Standard message reference parsing
            if (!parse_message_ref(args[offset], event.msg.channel_id, channel_id, message_id)) {
                bronx::send_message(bot, event, bronx::error("invalid message id or link"));
                return;
            }

            ::std::string emoji_raw = args[offset + 1];
            auto [emoji_id, emoji_str] = parse_emoji_raw(emoji_raw);
            if (emoji_id != 0) {
                // rebuild canonical raw string (<:name:id> or <a:name:id>) and then normalize
                std::string prefix = "<";
                if (emoji_raw.rfind("<a:", 0) == 0) prefix += "a:";
                else prefix += ":";
                emoji_raw = prefix + emoji_str + ":" + std::to_string(emoji_id) + ">";
            } else if (emoji_raw.find(":") != ::std::string::npos) {
                bronx::send_message(bot, event, bronx::error("invalid emoji format; please provide a unicode emoji or a custom one with its id (e.g. <:name:id> or name:id)"));
                return;
            }
            emoji_raw = normalize_emoji_for_reaction(emoji_raw);

            // parse role argument (mention, id, or name). join remaining args so multi-word role names work
            ::std::string role_arg = args[offset + 2];
            if (args.size() > offset + 3) {
                for (size_t i = offset + 3; i < args.size(); ++i) role_arg += " " + args[i];
            }

            uint64_t role_id = 0;
            // mention format <@&id>
            ::std::smatch rm;
            ::std::regex mention_re("<@&([0-9]+)>");
            if (::std::regex_search(role_arg, rm, mention_re) && rm.size() >= 2) {
                role_id = ::std::stoull(rm[1]);
            } else {
                // try numeric id first
                try { role_id = ::std::stoull(role_arg); } catch (...) { role_id = 0; }

                // if still 0, attempt to match role by name (case-insensitive)
                if (role_id == 0) {
                    dpp::guild* g = dpp::find_guild(event.msg.guild_id);
                    if (!g) {
                        bronx::send_message(bot, event, bronx::error("guild not cached; please use a role mention or id"));
                        return;
                    }

                    ::std::string query = role_arg;
                    ::std::transform(query.begin(), query.end(), query.begin(), ::tolower);

                    ::std::vector<dpp::role*> exact_matches;
                    ::std::vector<dpp::role*> partial_matches;

                    for (const auto &role_id : g->roles) {
                        // guild::roles in this DPP build stores role ids — look up actual role object
                        dpp::role* rr = dpp::find_role(static_cast<uint64_t>(role_id));
                        if (!rr) continue;
                        ::std::string name = rr->name;
                        ::std::string lname = name;
                        ::std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                        if (lname == query) exact_matches.push_back(rr);
                        else if (lname.find(query) != ::std::string::npos) partial_matches.push_back(rr);
                    }

                    if (exact_matches.size() == 1) {
                        role_id = exact_matches[0]->id;
                    } else if (exact_matches.size() > 1) {
                        ::std::string list = "multiple roles match that name; specify mention or id:\n";
                        for (auto *rr : exact_matches) list += "`" + rr->name + "` (<@&" + ::std::to_string(rr->id) + ")\n";
                        bronx::send_message(bot, event, bronx::error(list));
                        return;
                    } else if (partial_matches.size() == 1) {
                        role_id = partial_matches[0]->id;
                    } else if (partial_matches.size() > 1) {
                        ::std::string list = "multiple roles partially match; specify mention or id:\n";
                        for (auto *rr : partial_matches) list += "`" + rr->name + "` (<@&" + ::std::to_string(rr->id) + ")\n";
                        bronx::send_message(bot, event, bronx::error(list));
                        return;
                    } else {
                        bronx::send_message(bot, event, bronx::error("role not found"));
                        return;
                    }
                }
            }


            // Verify role exists in guild
            dpp::role* r = dpp::find_role(role_id);
            if (!r) {
                bronx::send_message(bot, event, bronx::error("role not found"));
                return;
            }

            // fetch message to ensure it exists
            uint64_t guild_id_cap = static_cast<uint64_t>(event.msg.guild_id);
            bot.message_get(message_id, channel_id, [&bot, &event, channel_id, message_id, emoji_raw, emoji_id, emoji_str, role_id, silent, guild_id_cap](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    bronx::send_message(bot, event, bronx::error("failed to fetch message (unknown channel/message)"));
                    return;
                }

                // add reaction to message (best-effort) using normalized string
                std::string norm = normalize_emoji_for_reaction(emoji_raw);
                auto store = [&](bool reacted) {
                    RREntry e;
                    e.role_id = role_id;
                    e.emoji_id = emoji_id;
                    e.emoji_str = emoji_str;
                    e.raw = norm;
                    reaction_roles[message_id].channel_id = channel_id;
                    reaction_roles[message_id].entries.push_back(e);
                    if (rr_db) {
                        try {
                            if (emoji_id != 0) {
                                std::string bracket = "<:" + emoji_str + ":" + std::to_string(emoji_id) + ">";
                                rr_db->remove_reaction_role(message_id, bracket);
                            }
                            if (is_safe_emoji_string(norm)) {
                                rr_db->add_reaction_role(message_id, channel_id, norm, emoji_id, role_id);
                            } else {
                                std::cerr << "Emoji string failed safety check, skipping DB storage" << std::endl;
                            }
                        } catch (const std::exception& ex) {
                            std::cerr << "DB error: " << ex.what() << std::endl;
                        }
                    }
                    // Sync existing reactions — grant the role to anyone who already reacted
                    sync_existing_reactions(bot, message_id, channel_id, norm, role_id, guild_id_cap);
                    bronx::send_message(bot, event, bronx::success("reaction role added"));
                };
                if (!silent) {
                    bot.message_add_reaction(message_id, channel_id, norm,
                        [&bot,&event,store](const dpp::confirmation_callback_t& cb) {
                            if (cb.is_error()) {
                                std::cerr << "reaction add failed: " << cb.get_error().message << std::endl;
                                bronx::send_message(bot, event, bronx::error("could not add reaction: " + cb.get_error().message));
                            } else {
                                store(true);
                            }
                        });
                } else {
                    store(false);
                }
            });
        },
        // slash handler
        [](dpp::cluster& /*bot2*/, const dpp::slashcommand_t& /*event*/) {},
        {
            dpp::command_option(dpp::co_string, "message", "message id, message link, or ^ for message above", true),
            dpp::command_option(dpp::co_string, "emoji", "emoji (unicode or <:name:id>)", true),
            dpp::command_option(dpp::co_role, "role", "role to assign/remove", true),
            dpp::command_option(dpp::co_boolean, "silent", "do not add the initial reaction", false)
        }
    );

    // replace the dummy slash handler with real implementation (can't capture bot above easily in-line)
    rr->slash_handler = [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
        if (!event.command.guild_id) {
            event.reply(dpp::message().add_embed(bronx::error("this command must be used in a server")));
            return;
        }

        // permission: fetch member
        uint64_t user_id = event.command.get_issuing_user().id;
        bot.guild_get_member(event.command.guild_id, user_id, [&](const dpp::confirmation_callback_t& memb_cb) {
            if (memb_cb.is_error()) {
                event.reply(dpp::message().add_embed(bronx::error("failed to verify permissions")));
                return;
            }
            auto member = ::std::get<dpp::guild_member>(memb_cb.value);
            if (!member_can_manage_roles(bot, event.command.guild_id, member)) {
                event.reply(dpp::message().add_embed(bronx::error("you need Manage Roles or Administrator to use this command")));
                return;
            }

            ::std::string message_ref = ::std::get<::std::string>(event.get_parameter("message"));
            ::std::string emoji_raw = ::std::get<::std::string>(event.get_parameter("emoji"));
            uint64_t role_id = ::std::get<dpp::snowflake>(event.get_parameter("role"));
            bool silent = false;
            if (std::holds_alternative<bool>(event.get_parameter("silent"))) {
                silent = std::get<bool>(event.get_parameter("silent"));
            }

            // Handle "^" to reference message above current interaction  
            if (message_ref == "^") {
                uint64_t channel_id = event.command.channel_id;
                
                // Extract values needed for async call to prevent memory issues
                ::std::string emoji_arg = emoji_raw;
                bool silent_flag = silent;
                
                // For slash commands, find the most recent non-bot message (limit to 3 messages)
                bot.messages_get(channel_id, 0, 0, 0, 3, [&bot, event, emoji_arg, role_id, silent_flag, channel_id](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to fetch recent messages")));
                        return;
                    }
                    
                    auto messages = std::get<dpp::message_map>(cb.value);
                    uint64_t target_message_id = 0;
                    
                    // Find the most recent non-bot message
                    for (const auto& [msg_id, msg] : messages) {
                        if (!msg.author.is_bot()) {
                            if (target_message_id == 0 || static_cast<uint64_t>(msg_id) > target_message_id) {
                                target_message_id = static_cast<uint64_t>(msg_id);
                            }
                        }
                    }
                    
                    if (target_message_id == 0) {
                        event.reply(dpp::message().add_embed(bronx::error("no user message found in recent history")));
                        return;
                    }

                    // Parse emoji
                    auto [emoji_id, emoji_str] = parse_emoji_raw(emoji_arg);
                    std::string emoji_raw_local = emoji_arg;
                    if (emoji_id != 0) {
                        std::string prefix = "<";
                        if (emoji_raw_local.rfind("<a:", 0) == 0) prefix += "a:";
                        else prefix += ":";
                        emoji_raw_local = prefix + emoji_str + ":" + std::to_string(emoji_id) + ">";
                    } else if (emoji_raw_local.find(":") != ::std::string::npos) {
                        event.reply(dpp::message().add_embed(bronx::error("invalid emoji format; please provide a unicode emoji or a custom one with its id (e.g. <:name:id> or name:id)")));
                        return;
                    }

                    dpp::role* r = dpp::find_role(role_id);
                    if (!r) {
                        event.reply(dpp::message().add_embed(bronx::error("role not found")));
                        return;
                    }

                    emoji_raw_local = normalize_emoji_for_reaction(emoji_raw_local);

                    // Add reaction role directly
                    RREntry e;
                    e.role_id = role_id;
                    e.emoji_id = emoji_id;
                    e.emoji_str = emoji_str;
                    e.raw = emoji_raw_local;
                    reaction_roles[target_message_id].channel_id = channel_id;
                    reaction_roles[target_message_id].entries.push_back(e);
                    
                    if (rr_db) {
                        try {
                            if (is_safe_emoji_string(emoji_raw_local)) {
                                rr_db->add_reaction_role(target_message_id, channel_id, emoji_raw_local, emoji_id, role_id);
                            } else {
                                std::cerr << "Emoji string failed safety check, skipping DB storage" << std::endl;
                            }
                        } catch (const std::exception& ex) {
                            std::cerr << "DB add error: " << ex.what() << std::endl;
                        }
                    }
                    
                    // Sync existing reactions — grant the role to anyone who already reacted
                    {
                        dpp::channel* ch = dpp::find_channel(channel_id);
                        uint64_t gid = ch ? static_cast<uint64_t>(ch->guild_id) : 0;
                        sync_existing_reactions(bot, target_message_id, channel_id, emoji_raw_local, role_id, gid);
                    }

                    if (!silent_flag) {
                        bot.message_add_reaction(target_message_id, channel_id, emoji_raw_local,
                            [event](const dpp::confirmation_callback_t& cb) {
                                if (cb.is_error()) {
                                    std::cerr << "reaction add failed: " << cb.get_error().message << std::endl;
                                    event.reply(dpp::message().add_embed(bronx::error("could not add reaction: " + cb.get_error().message)));
                                } else {
                                    event.reply(dpp::message().add_embed(bronx::success("reaction role added to recent message")));
                                }
                            });
                    } else {
                        event.reply(dpp::message().add_embed(bronx::success("reaction role added to recent message")));
                    }
                });
                return;
            }

            // Standard message reference parsing for slash commands

            // Standard message reference parsing for slash commands
            auto [emoji_id, emoji_str] = parse_emoji_raw(emoji_raw);
            if (emoji_id != 0) {
                std::string prefix = "<";
                if (emoji_raw.rfind("<a:", 0) == 0) prefix += "a:";
                else prefix += ":";
                emoji_raw = prefix + emoji_str + ":" + std::to_string(emoji_id) + ">";
            } else if (emoji_raw.find(":") != ::std::string::npos) {
                event.reply(dpp::message().add_embed(bronx::error("invalid emoji format; please provide a unicode emoji or a custom one with its id (e.g. <:name:id> or name:id)")));
                return;
            }

            uint64_t channel_id = 0, message_id = 0;
            if (!parse_message_ref(message_ref, event.command.channel_id, channel_id, message_id)) {
                event.reply(dpp::message().add_embed(bronx::error("invalid message id or link")));
                return;
            }

            dpp::role* r = dpp::find_role(role_id);
            if (!r) {
                event.reply(dpp::message().add_embed(bronx::error("role not found")));
                return;
            }

            // normalize final emoji string for reaction
            emoji_raw = normalize_emoji_for_reaction(emoji_raw);

            // verify message
            uint64_t slash_guild_id = static_cast<uint64_t>(event.command.guild_id);
            bot.message_get(message_id, channel_id, [&bot, &event, channel_id, message_id, emoji_raw, emoji_id, emoji_str, role_id, silent, slash_guild_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    event.reply(dpp::message().add_embed(bronx::error("failed to fetch message (unknown channel/message)")));
                    return;
                }

                {
                    std::string norm = normalize_emoji_for_reaction(emoji_raw);
                    auto store = [&](bool reacted) {
                        RREntry e;
                        e.role_id = role_id;
                        e.emoji_id = emoji_id;
                        e.emoji_str = emoji_str;
                        e.raw = norm;
                        reaction_roles[message_id].channel_id = channel_id;
                        reaction_roles[message_id].entries.push_back(e);
                        if (rr_db) {
                            try {
                                if (emoji_id != 0) {
                                    std::string bracket = "<:" + emoji_str + ":" + std::to_string(emoji_id) + ">";
                                    rr_db->remove_reaction_role(message_id, bracket);
                                }
                                if (is_safe_emoji_string(norm)) {
                                    rr_db->add_reaction_role(message_id, channel_id, norm, emoji_id, role_id);
                                } else {
                                    std::cerr << "Emoji string failed safety check, skipping DB storage" << std::endl;
                                }
                            } catch (const std::exception& ex) {
                                std::cerr << "DB error: " << ex.what() << std::endl;
                            }
                        }
                        // Sync existing reactions — grant the role to anyone who already reacted
                        sync_existing_reactions(bot, message_id, channel_id, norm, role_id, slash_guild_id);
                        event.reply(dpp::message().add_embed(bronx::success("reaction role added")));
                    };
                    if (!silent) {
                        bot.message_add_reaction(message_id, channel_id, norm,
                            [&bot,&event,store](const dpp::confirmation_callback_t& cb) {
                                if (cb.is_error()) {
                                    std::cerr << "reaction add failed: " << cb.get_error().message << std::endl;
                                    event.reply(dpp::message().add_embed(bronx::error("could not add reaction: " + cb.get_error().message)));
                                } else {
                                    store(true);
                                }
                            });
                    } else {
                        store(false);
                    }
                }
            });
        });
    };

    return rr;
}

// Register reaction handlers
inline void register_reactionrole_interactions(dpp::cluster& bot) {
    // ── handle dropdown selection from rr check sessions ──
    bot.on_select_click([&bot](const dpp::select_click_t& event) {
        std::string cid = event.custom_id;
        if (cid.rfind("rr_chk_", 0) != 0) return; // not ours

        // parse custom_id: rr_chk_<message_id>_<emoji_index>_<user_id>
        // extract message_id and user_id to find the session
        auto parts_start = cid.find('_', 7);  // after "rr_chk_"
        if (parts_start == std::string::npos) return;
        uint64_t target_msg = 0;
        try { target_msg = std::stoull(cid.substr(7, parts_start - 7)); } catch (...) { return; }

        // extract emoji index
        auto idx_start = parts_start + 1;
        auto idx_end = cid.find('_', idx_start);
        if (idx_end == std::string::npos) return;
        size_t emoji_idx = 0;
        try { emoji_idx = std::stoull(cid.substr(idx_start, idx_end - idx_start)); } catch (...) { return; }

        // extract user_id
        uint64_t session_uid = 0;
        try { session_uid = std::stoull(cid.substr(idx_end + 1)); } catch (...) { return; }

        std::string key = rr_session_key(target_msg, session_uid);
        auto sit = rr_check_sessions.find(key);
        if (sit == rr_check_sessions.end()) {
            event.reply(dpp::message().add_embed(bronx::error("this check session has expired, please run the check again")).set_flags(dpp::m_ephemeral));
            return;
        }

        RRCheckSession& s = sit->second;

        // verify user
        if (static_cast<uint64_t>(event.command.get_issuing_user().id) != s.user_id) {
            event.reply(dpp::message().add_embed(bronx::error("only the person who ran the check can assign roles")).set_flags(dpp::m_ephemeral));
            return;
        }

        if (event.values.empty()) {
            event.reply(dpp::message().add_embed(bronx::error("no role selected")).set_flags(dpp::m_ephemeral));
            return;
        }

        uint64_t role_id = 0;
        try { role_id = std::stoull(event.values[0]); } catch (...) {}
        if (role_id == 0 || emoji_idx >= s.emojis.size()) {
            event.reply(dpp::message().add_embed(bronx::error("invalid selection")).set_flags(dpp::m_ephemeral));
            return;
        }

        // mark this emoji as assigned
        s.emojis[emoji_idx].assigned_role = role_id;
        s.answered_on_page++;

        // check how many on this page are done
        size_t page_start = s.page * 5;
        size_t page_end = std::min(page_start + 5, s.emojis.size());
        size_t page_size = page_end - page_start;

        // check if all emojis total are done
        size_t total_done = 0;
        for (const auto& em : s.emojis) if (em.assigned_role != 0) ++total_done;
        bool all_done = (total_done == s.emojis.size());

        if (all_done) {
            // show confirmation summary
            event.reply(dpp::ir_update_message, build_check_confirm(s));
        } else if (s.answered_on_page >= page_size) {
            // current page fully answered — advance to next page
            s.page++;
            s.answered_on_page = 0;
            event.reply(dpp::ir_update_message, build_check_page(s));
        } else {
            // update current page (show the just-picked one as disabled)
            event.reply(dpp::ir_update_message, build_check_page(s));
        }
    });

    // ── handle confirm button from rr check sessions ──
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        std::string cid = event.custom_id;
        if (cid.rfind("rr_chk_confirm_", 0) != 0) return; // not ours

        // parse: rr_chk_confirm_<message_id>_<user_id>
        auto mid_start = 15; // strlen("rr_chk_confirm_")
        auto mid_end = cid.find('_', mid_start);
        if (mid_end == std::string::npos) return;
        uint64_t target_msg = 0;
        try { target_msg = std::stoull(cid.substr(mid_start, mid_end - mid_start)); } catch (...) { return; }
        uint64_t session_uid = 0;
        try { session_uid = std::stoull(cid.substr(mid_end + 1)); } catch (...) { return; }

        std::string key = rr_session_key(target_msg, session_uid);
        auto sit = rr_check_sessions.find(key);
        if (sit == rr_check_sessions.end()) {
            event.reply(dpp::message().add_embed(bronx::error("this check session has expired")).set_flags(dpp::m_ephemeral));
            return;
        }

        RRCheckSession s = sit->second; // copy before erase
        rr_check_sessions.erase(sit);

        // verify user
        if (static_cast<uint64_t>(event.command.get_issuing_user().id) != s.user_id) {
            event.reply(dpp::message().add_embed(bronx::error("only the person who ran the check can confirm")).set_flags(dpp::m_ephemeral));
            rr_check_sessions[key] = std::move(s); // put back
            return;
        }

        // persist all assigned reaction roles, sync existing reactions
        int saved = 0;
        for (const auto& em : s.emojis) {
            if (em.assigned_role == 0) continue;

            RREntry e;
            e.role_id = em.assigned_role;
            e.emoji_id = em.emoji_id;
            e.emoji_str = em.emoji_str;
            e.raw = em.emoji_raw;
            reaction_roles[s.message_id].channel_id = s.channel_id;
            reaction_roles[s.message_id].entries.push_back(e);

            if (rr_db) {
                try {
                    if (is_safe_emoji_string(em.emoji_raw)) {
                        rr_db->add_reaction_role(s.message_id, s.channel_id, em.emoji_raw, em.emoji_id, em.assigned_role);
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "DB add error (rr check confirm): " << ex.what() << std::endl;
                }
            }

            sync_existing_reactions(bot, s.message_id, s.channel_id, em.emoji_raw, em.assigned_role, s.guild_id);
            ++saved;
        }

        // final updated message — remove button, show done
        dpp::message final_msg;
        std::string summary;
        for (const auto& em : s.emojis) {
            if (em.assigned_role == 0) continue;
            std::string emoji_display;
            if (em.emoji_id != 0)
                emoji_display = "<:" + em.emoji_str + ":" + std::to_string(em.emoji_id) + ">";
            else
                emoji_display = em.emoji_str;

            dpp::role* r = dpp::find_role(em.assigned_role);
            std::string role_name = r ? r->name : std::to_string(em.assigned_role);
            summary += emoji_display + " → <@&" + std::to_string(em.assigned_role) + "> (**" + role_name + "**)\n";
        }
        final_msg.add_embed(bronx::success(std::to_string(saved) + " reaction role(s) saved and syncing!\n\n" + summary));
        event.reply(dpp::ir_update_message, final_msg);
    });

    // add role when reaction added
    bot.on_message_reaction_add([&bot](const dpp::message_reaction_add_t& event) {
        if (!event.reacting_guild.id) return; // only in guilds
        if (event.reacting_user.is_bot()) return;

        auto it = reaction_roles.find(static_cast<uint64_t>(event.message_id));
        if (it == reaction_roles.end()) return;

        const RRMessage& m = it->second;
        // find matching entry
        for (const auto& e : m.entries) {
            if (e.emoji_id != 0) {
                if (static_cast<uint64_t>(event.reacting_emoji.id) == e.emoji_id) {
                    // assign role
                    bot.guild_member_add_role(event.reacting_guild.id, event.reacting_user.id, e.role_id, [](const dpp::confirmation_callback_t& cb){});
                    return;
                }
            } else {
                // compare unicode/name
                if (event.reacting_emoji.name == e.emoji_str || event.reacting_emoji.name == e.raw) {
                    bot.guild_member_add_role(event.reacting_guild.id, event.reacting_user.id, e.role_id, [](const dpp::confirmation_callback_t& cb){});
                    return;
                }
            }
        }
    });

    // remove role when reaction removed
    bot.on_message_reaction_remove([&bot](const dpp::message_reaction_remove_t& event) {
        if (!event.reacting_guild.id) return;
        // ignore bot reactions
        if (event.reacting_user_id == bot.me.id) return;

        auto it = reaction_roles.find(static_cast<uint64_t>(event.message_id));
        if (it == reaction_roles.end()) return;

        const RRMessage& m = it->second;
        for (const auto& e : m.entries) {
            if (e.emoji_id != 0) {
                if (static_cast<uint64_t>(event.reacting_emoji.id) == e.emoji_id) {
                    bot.guild_member_remove_role(event.reacting_guild.id, event.reacting_user_id, e.role_id, [](const dpp::confirmation_callback_t& cb){});
                    return;
                }
            } else {
                if (event.reacting_emoji.name == e.emoji_str || event.reacting_emoji.name == e.raw) {
                    bot.guild_member_remove_role(event.reacting_guild.id, event.reacting_user_id, e.role_id, [](const dpp::confirmation_callback_t& cb){});
                    return;
                }
            }
        }
    });

    // cleanup persisted mappings when the message is deleted
    bot.on_message_delete([&bot](const dpp::message_delete_t& event) {
        auto it = reaction_roles.find(static_cast<uint64_t>(event.id));
        if (it == reaction_roles.end()) return;

        // remove persisted rows if DB available
        if (rr_db) {
            for (const auto &e : it->second.entries) {
                try {
                    rr_db->remove_reaction_role(event.id, e.raw);
                } catch (const std::exception& ex) {
                    std::cerr << "DB remove error on cleanup: " << ex.what() << std::endl;
                }
            }
        }

        // remove from memory
        reaction_roles.erase(it);
    });

    // ── handle pending "next message" reactions ──
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (!event.msg.guild_id) return;
        if (event.msg.author.is_bot()) return;

        uint64_t user_id = static_cast<uint64_t>(event.msg.author.id);
        uint64_t channel_id = static_cast<uint64_t>(event.msg.channel_id);

        // Check for "cancel" command
        std::string content = event.msg.content;
        std::transform(content.begin(), content.end(), content.begin(), ::tolower);
        if (content == "cancel") {
            std::lock_guard<std::mutex> lock(pending_next_mutex);
            auto it = pending_next_reactions.find(user_id);
            if (it != pending_next_reactions.end() && it->second.channel_id == channel_id) {
                pending_next_reactions.erase(it);
                bronx::send_message(bot, event, bronx::info("reaction role setup cancelled"));
            }
            return;
        }

        // Clean up expired pending reactions
        cleanup_expired_pending_reactions();

        // Check if user has a pending reaction for this channel
        PendingNextReaction pending;
        {
            std::lock_guard<std::mutex> lock(pending_next_mutex);
            auto it = pending_next_reactions.find(user_id);
            if (it == pending_next_reactions.end()) return;
            if (it->second.channel_id != channel_id) return;
            pending = it->second;
            pending_next_reactions.erase(it);
        }

        // Apply the reaction role to this message
        uint64_t message_id = static_cast<uint64_t>(event.msg.id);

        RREntry e;
        e.role_id = pending.role_id;
        e.emoji_id = pending.emoji_id;
        e.emoji_str = pending.emoji_str;
        e.raw = pending.emoji_raw;
        reaction_roles[message_id].channel_id = channel_id;
        reaction_roles[message_id].entries.push_back(e);

        if (rr_db) {
            try {
                if (is_safe_emoji_string(pending.emoji_raw)) {
                    rr_db->add_reaction_role(message_id, channel_id, pending.emoji_raw, pending.emoji_id, pending.role_id);
                }
            } catch (const std::exception& ex) {
                std::cerr << "DB add error (next message): " << ex.what() << std::endl;
            }
        }

        // Sync existing reactions
        sync_existing_reactions(bot, message_id, channel_id, pending.emoji_raw, pending.role_id, pending.guild_id);

        // Add bot reaction
        if (!pending.silent) {
            bot.message_add_reaction(message_id, channel_id, pending.emoji_raw,
                [&bot, event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        std::cerr << "reaction add failed: " << cb.get_error().message << std::endl;
                        bronx::send_message(bot, event, bronx::error("could not add reaction: " + cb.get_error().message));
                    } else {
                        bronx::send_message(bot, event, bronx::success("reaction role added to your message"));
                    }
                });
        } else {
            bronx::send_message(bot, event, bronx::success("reaction role added to your message"));
        }
    });
}

} // namespace utility
} // namespace commands
