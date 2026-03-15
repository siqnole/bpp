#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../quiet_moderation/mod_log.h"
#include "../../embed_style.h"

#include <dpp/nlohmann/json.hpp>

namespace commands {
namespace moderation {

inline uint32_t parse_duration(const std::string& input) {
    uint32_t total = 0;
    uint32_t current_num = 0;
    for (char c : input) {
        if (c >= '0' && c <= '9') {
            current_num = current_num * 10 + (c - '0');
        } else {
            switch (c) {
                case 's': total += current_num; break;
                case 'm': total += current_num * 60; break;
                case 'h': total += current_num * 3600; break;
                case 'd': total += current_num * 86400; break;
                case 'w': total += current_num * 604800; break;
                default: break;
            }
            current_num = 0;
        }
    }
    if (current_num > 0 && total == 0) total = current_num;
    return total;
}

inline std::string format_duration(uint32_t seconds) {
    if (seconds == 0) return "permanent";
    std::string result;
    if (seconds >= 604800) { result += std::to_string(seconds / 604800) + "w "; seconds %= 604800; }
    if (seconds >= 86400)  { result += std::to_string(seconds / 86400) + "d "; seconds %= 86400; }
    if (seconds >= 3600)   { result += std::to_string(seconds / 3600) + "h "; seconds %= 3600; }
    if (seconds >= 60)     { result += std::to_string(seconds / 60) + "m "; seconds %= 60; }
    if (seconds > 0)       { result += std::to_string(seconds) + "s"; }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

inline uint32_t get_action_color(const std::string& type) {
    if (type == "warn" || type == "auto_nickname") return 0xF59E0B;
    if (type == "timeout" || type == "mute" || type == "jail") return 0xF97316;
    if (type == "kick") return 0xEF4444;
    if (type == "ban") return 0x991B1B;
    if (type.substr(0, 5) == "auto_") return 0x8B5CF6;
    return bronx::COLOR_DEFAULT;
}

inline std::vector<bronx::db::EscalationRule> parse_escalation_rules(const std::string& json_str) {
    std::vector<bronx::db::EscalationRule> rules;
    try {
        auto arr = nlohmann::json::parse(json_str);
        if (!arr.is_array()) return rules;
        for (auto& obj : arr) {
            bronx::db::EscalationRule rule;
            rule.threshold_points = obj.value("threshold_points", 0.0);
            rule.within_days = obj.value("within_days", 30);
            rule.action = obj.value("action", "timeout");
            rule.action_duration_seconds = obj.value("action_duration_seconds", 3600);
            rule.reason_template = obj.value("reason_template", "auto-escalation: {points} points in {days} days");
            rules.push_back(rule);
        }
    } catch (...) {}
    return rules;
}

inline void dm_user_action(dpp::cluster& bot, uint64_t user_id, const std::string& guild_name,
                           const std::string& action_type, const std::string& reason,
                           uint32_t duration_seconds, double points) {
    std::string desc = "you have been **" + action_type + "** in **" + guild_name + "**";
    if (!reason.empty()) desc += "\n**reason:** " + reason;
    if (duration_seconds > 0) desc += "\n**duration:** " + format_duration(duration_seconds);
    desc += "\n**points:** " + std::to_string(points);

    auto embed = bronx::create_embed(desc, get_action_color(action_type));
    embed.set_title("moderation action");

    bot.direct_message_create(user_id, dpp::message().add_embed(embed),
        [](const dpp::confirmation_callback_t&) {});
}

// ── Mod Log ────────────────────────────────────────────────────
inline void send_mod_log(dpp::cluster& bot, bronx::db::Database* db,
                         uint64_t guild_id, const bronx::db::InfractionRow& inf,
                         uint64_t log_channel_id = 0) {
    dpp::snowflake channel = log_channel_id;
    if (channel == 0) {
        auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
        if (config.has_value()) channel = config.value().log_channel_id;
    }
    if (channel == 0) return;

    auto embed = bronx::create_embed("", get_action_color(inf.type));
    embed.set_title("case #" + std::to_string(inf.case_number) + " — " + inf.type);
    embed.add_field("user", "<@" + std::to_string(inf.user_id) + ">", true);
    embed.add_field("moderator", "<@" + std::to_string(inf.moderator_id) + ">", true);
    embed.add_field("points", std::to_string(inf.points), true);
    if (!inf.reason.empty()) embed.add_field("reason", inf.reason, false);
    if (inf.duration_seconds > 0)
        embed.add_field("duration", format_duration(inf.duration_seconds), true);
    if (inf.expires_at.has_value()) {
        auto exp_time = std::chrono::system_clock::to_time_t(inf.expires_at.value());
        embed.add_field("expires", "<t:" + std::to_string(exp_time) + ":R>", true);
    }

    commands::quiet_moderation::send_embed_via_webhook(bot, channel, embed);
}

inline bool check_hierarchy(const dpp::guild& guild, dpp::snowflake actor_id, dpp::snowflake target_id) {
    const dpp::guild_member* actor_member = nullptr;
    const dpp::guild_member* target_member = nullptr;
    for (const auto& m : guild.members) {
        if (m.first == actor_id) actor_member = &m.second;
        if (m.first == target_id) target_member = &m.second;
    }
    if (!actor_member || !target_member) return false;

    int actor_highest = 0;
    int target_highest = 0;

    for (auto& role_id : actor_member->get_roles()) {
        const dpp::role* role = dpp::find_role(role_id);
        if (role && role->position > actor_highest)
            actor_highest = role->position;
    }
    for (auto& role_id : target_member->get_roles()) {
        const dpp::role* role = dpp::find_role(role_id);
        if (role && role->position > target_highest)
            target_highest = role->position;
    }
    return actor_highest > target_highest;
}

// ── Escalation Check ───────────────────────────────────────────
inline void check_and_escalate(dpp::cluster& bot, bronx::db::Database* db,
                                uint64_t guild_id, uint64_t user_id,
                                const std::string& guild_name = "") {
    auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
    if (!config_opt.has_value()) return;
    auto& config = config_opt.value();

    if (!config.escalation_rules.empty() && config.escalation_rules != "[]") {
        auto rules = parse_escalation_rules(config.escalation_rules);

        std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b) {
            return a.threshold_points < b.threshold_points;
        });

        for (auto& rule : rules) {
            double pts = bronx::db::infraction_operations::get_user_active_points(
                db, guild_id, user_id, rule.within_days);

            if (pts >= rule.threshold_points) {
                std::string reason = rule.reason_template;
                {
                    auto pos = reason.find("{points}");
                    if (pos != std::string::npos)
                        reason.replace(pos, 8, std::to_string(pts));
                    pos = reason.find("{days}");
                    if (pos != std::string::npos)
                        reason.replace(pos, 6, std::to_string(rule.within_days));
                }

                double action_points = 0;
                if (rule.action == "timeout") action_points = config.point_timeout;
                else if (rule.action == "mute") action_points = config.point_mute;
                else if (rule.action == "kick") action_points = config.point_kick;
                else if (rule.action == "ban") action_points = config.point_ban;

                auto inf = bronx::db::infraction_operations::create_infraction(
                    db, guild_id, user_id, bot.me.id,
                    rule.action, reason, action_points,
                    rule.action_duration_seconds, "{\"escalation\":true}");

                if (!inf.has_value()) continue;

                if (config.dm_on_action) {
                    dm_user_action(bot, user_id, guild_name, rule.action, reason,
                                   rule.action_duration_seconds, action_points);
                }

                if (rule.action == "timeout" && rule.action_duration_seconds > 0) {
                    bot.guild_member_timeout(guild_id, user_id,
                        time(0) + rule.action_duration_seconds,
                        [](const dpp::confirmation_callback_t&) {});
                } else if (rule.action == "kick") {
                    bot.guild_member_delete(guild_id, user_id,
                        [](const dpp::confirmation_callback_t&) {});
                } else if (rule.action == "ban") {
                    bot.guild_ban_add(guild_id, user_id, 0,
                        [](const dpp::confirmation_callback_t&) {});
                }

                send_mod_log(bot, db, guild_id, inf.value());
                break;
            }
        }
    }
}

inline std::map<uint64_t, std::map<uint32_t, dpp::timer>>& get_active_timers() {
    static std::map<uint64_t, std::map<uint32_t, dpp::timer>> timers;
    return timers;
}

inline void schedule_punishment_expiry(dpp::cluster& bot, bronx::db::Database* db,
                                        uint64_t guild_id, uint32_t case_number,
                                        const std::string& type, uint64_t user_id,
                                        uint32_t duration_seconds,
                                        const std::string& metadata = "{}") {
    if (duration_seconds == 0) return;

    auto timer = bot.start_timer([&bot, db, guild_id, case_number, type, user_id, metadata](dpp::timer) {
        bronx::db::infraction_operations::pardon_infraction(
            db, guild_id, case_number, bot.me.id, "auto-expired");

        if (type == "ban") {
            bot.guild_ban_delete(guild_id, user_id,
                [](const dpp::confirmation_callback_t&) {});
        } else if (type == "timeout") {
            bot.guild_member_timeout(guild_id, user_id, 0,
                [](const dpp::confirmation_callback_t&) {});
        }

        auto& timers = get_active_timers();
        if (timers.count(guild_id)) {
            timers[guild_id].erase(case_number);
        }
    }, duration_seconds);

    get_active_timers()[guild_id][case_number] = timer;
}

inline dpp::timer start_expiry_sweep(dpp::cluster& bot, bronx::db::Database* db) {
    return bot.start_timer([&bot, db](dpp::timer) {
        int expired = bronx::db::infraction_operations::expire_infractions(db);
        if (expired > 0) {
            std::cerr << "\033[35m[MOD]\033[0m expired " << expired << " infractions\n";
        }
    }, 60);
}

inline void restore_active_timers(dpp::cluster& bot, bronx::db::Database* db) {
    // TODO: implement once infraction_operations::get_active_timed_infractions() exists
}

} // namespace moderation
} // namespace commands