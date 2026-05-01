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
#include "../../utils/moderation/mod_utility.h"
#include "../../server_logger.h"
#include "../../utils/logger.h"

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

// ── Forward to canonical implementations in bronx::moderation (mod_utility.h) ──
using bronx::moderation::format_duration;
using bronx::moderation::get_action_color;
using bronx::moderation::parse_mention;

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
                         uint64_t origin_channel_id = 0) {
    
    // Use the new centralized logging utility
    bronx::moderation::log_mod_action(bot, db, guild_id, inf, origin_channel_id);

    // Also send to central server logger (LOG_TYPE_MODERATION) for internal auditing
    auto embed = bronx::create_embed("", bronx::moderation::get_action_color(inf.type));
    embed.set_title("case #" + std::to_string(inf.case_number) + " — " + inf.type);
    embed.add_field("user", "<@" + std::to_string(inf.user_id) + ">", true);
    embed.add_field("moderator", "<@" + std::to_string(inf.moderator_id) + ">", true);
    
    bronx::logger::ServerLogger::get().log_embed(guild_id, bronx::logger::LOG_TYPE_MODERATION, embed);
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
            bronx::logger::info("moderation", "Expired " + std::to_string(expired) + " infractions");
        }
    }, 60);
}

inline void restore_active_timers(dpp::cluster& bot, bronx::db::Database* db) {
    auto active = bronx::db::infraction_operations::get_active_timed_infractions(db);
    int restored = 0;
    auto now = std::chrono::system_clock::now();

    for (const auto& inf : active) {
        if (!inf.expires_at.has_value()) continue;
        
        auto expiry = inf.expires_at.value();
        if (expiry <= now) continue;

        auto diff = std::chrono::duration_cast<std::chrono::seconds>(expiry - now).count();
        if (diff > 0) {
            schedule_punishment_expiry(bot, db, inf.guild_id, inf.case_number, 
                                      inf.type, inf.user_id, (uint32_t)diff, inf.metadata);
            restored++;
        }
    }
    
    if (restored > 0) {
        bronx::logger::info("moderation", "Restored " + std::to_string(restored) + " active moderation timers");
    }
}

// ── Internal Apply Helpers (used by commands and modals) ──────────

inline bool is_action_quiet(const std::string& action, const bronx::db::InfractionConfig& config) {
    if (!config.quiet_overrides.empty() && config.quiet_overrides != "{}") {
        try {
            auto overrides = nlohmann::json::parse(config.quiet_overrides);
            if (overrides.contains(action) && overrides[action].is_boolean()) {
                return overrides[action].get<bool>();
            }
        } catch (...) {}
    }
    return config.quiet_global;
}

inline void apply_mute_internal(dpp::cluster& bot, bronx::db::Database* db, 
                               uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                               uint32_t duration, const std::string& reason, 
                               const bronx::db::InfractionConfig& config,
                               std::function<void(const bronx::db::InfractionRow&, bool)> success_cb,
                               std::function<void(const std::string&)> error_cb) {
    
    if (config.mute_role_id == 0) {
        error_cb("mute role is not configured — use the setup command to set one");
        return;
    }

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user if enabled
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "mute", reason, duration, config.point_mute);
    }

    // apply mute role
    bot.guild_member_add_role(guild_id, target_id, config.mute_role_id,
        [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, success_cb, error_cb](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                error_cb("failed to add mute role: " + cb.get_error().message);
                return;
            }

            // create infraction
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "mute", reason, config.point_mute,
                duration);

            if (!inf.has_value()) {
                error_cb("mute applied but failed to create infraction record");
                return;
            }

            // schedule expiry
            std::string meta = "{\"mute_role_id\":" + std::to_string(config.mute_role_id) + "}";
            schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "mute", target_id, duration, meta);

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            success_cb(inf.value(), is_action_quiet("mute", config));
        });
}

inline void apply_ban_internal(dpp::cluster& bot, bronx::db::Database* db, 
                              uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                              uint32_t duration, uint32_t delete_message_seconds, const std::string& reason, 
                              const bronx::db::InfractionConfig& config,
                              std::function<void(const bronx::db::InfractionRow&, bool)> success_cb,
                              std::function<void(const std::string&)> error_cb) {

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user BEFORE banning
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "ban", reason, duration, config.point_ban);
    }

    // ban member
    bot.guild_ban_add(guild_id, target_id, delete_message_seconds,
        [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, success_cb, error_cb](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                error_cb("failed to ban user: " + cb.get_error().message);
                return;
            }

            // create infraction
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "ban", reason, config.point_ban,
                duration);

            if (!inf.has_value()) {
                error_cb("user banned but failed to create infraction record");
                return;
            }

            // schedule unban if tempban
            if (duration > 0) {
                schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "ban", target_id, duration);
            }

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            // quiet check
            success_cb(inf.value(), is_action_quiet("ban", config));
        });
}

inline void apply_timeout_internal(dpp::cluster& bot, bronx::db::Database* db, 
                                 uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                                 uint32_t duration, const std::string& reason, 
                                 const bronx::db::InfractionConfig& config,
                                 std::function<void(const bronx::db::InfractionRow&, bool)> success_cb,
                                 std::function<void(const std::string&)> error_cb) {

    // max discord timeout is 28 days
    constexpr uint32_t MAX_TIMEOUT_SECONDS = 2419200;
    if (duration > MAX_TIMEOUT_SECONDS) duration = MAX_TIMEOUT_SECONDS;

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user if enabled
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "timeout", reason, duration, config.point_timeout);
    }

    // apply discord timeout
    bot.guild_member_timeout(guild_id, target_id, time(0) + duration,
        [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, success_cb, error_cb](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                error_cb("failed to timeout user: " + cb.get_error().message);
                return;
            }

            // create infraction
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "timeout", reason, config.point_timeout,
                duration);

            if (!inf.has_value()) {
                error_cb("timeout applied but failed to create infraction record");
                return;
            }

            // schedule expiry
            schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "timeout", target_id, duration);

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            success_cb(inf.value(), is_action_quiet("timeout", config));
        });
}

inline void apply_kick_internal(dpp::cluster& bot, bronx::db::Database* db, 
                               uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                               const std::string& reason, 
                               const bronx::db::InfractionConfig& config,
                               std::function<void(const bronx::db::InfractionRow&, bool)> success_cb,
                               std::function<void(const std::string&)> error_cb) {

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user BEFORE kicking
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "kick", reason, 0, config.point_kick);
    }

    // kick member
    bot.guild_member_delete(guild_id, target_id,
        [&bot, db, guild_id, target_id, mod_id, reason, config, guild_name, success_cb, error_cb](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                error_cb("failed to kick user: " + cb.get_error().message);
                return;
            }

            // create infraction
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "kick", reason, config.point_kick,
                config.default_duration_kick);

            if (!inf.has_value()) {
                error_cb("user kicked but failed to create infraction record");
                return;
            }

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            success_cb(inf.value(), is_action_quiet("kick", config));
        });
}

inline void apply_warn_internal(dpp::cluster& bot, bronx::db::Database* db, 
                               uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                               const std::string& reason, 
                               const bronx::db::InfractionConfig& config,
                               std::function<void(const bronx::db::InfractionRow&, bool, double)> success_cb,
                               std::function<void(const std::string&)> error_cb) {

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user if enabled
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "warn", reason, 0, config.point_warn);
    }

    // create infraction
    auto inf = bronx::db::infraction_operations::create_infraction(
        db, guild_id, target_id, mod_id,
        "warn", reason, config.point_warn,
        config.default_duration_warn);

    if (!inf.has_value()) {
        error_cb("failed to create warning record");
        return;
    }

    // check escalation
    check_and_escalate(bot, db, guild_id, target_id, guild_name);

    // send mod log
    send_mod_log(bot, db, guild_id, inf.value());

    // get total active points for context
    double active_points = bronx::db::infraction_operations::get_user_active_points(db, guild_id, target_id);

    success_cb(inf.value(), is_action_quiet("warn", config), active_points);
}

inline void apply_jail_internal(dpp::cluster& bot, bronx::db::Database* db, 
                               uint64_t guild_id, uint64_t target_id, uint64_t mod_id, 
                               uint32_t duration, const std::string& reason, 
                               const bronx::db::InfractionConfig& config,
                               const std::vector<dpp::snowflake>& current_roles,
                               std::function<void(const bronx::db::InfractionRow&, bool)> success_cb,
                               std::function<void(const std::string&)> error_cb) {

    if (config.jail_role_id == 0) {
        error_cb("jail role is not configured — use the setup command to set one");
        return;
    }

    std::string guild_name = "unknown server";
    auto* g = dpp::find_guild(guild_id);
    if (g) guild_name = g->name;

    // dm user BEFORE jailing
    if (config.dm_on_action) {
        dm_user_action(bot, target_id, guild_name, "jail", reason, duration, config.point_mute);
    }

    // prepare metadata (stored roles)
    nlohmann::json meta;
    meta["stored_roles"] = nlohmann::json::array();
    for (auto& r : current_roles) {
        if (r == guild_id) continue;
        meta["stored_roles"].push_back(static_cast<uint64_t>(r));
    }
    meta["jail_role_id"] = config.jail_role_id;
    std::string metadata_str = meta.dump();

    // strip roles
    for (auto& role_id : current_roles) {
        if (role_id == guild_id) continue;
        bot.guild_member_remove_role(guild_id, target_id, role_id,
            [](const dpp::confirmation_callback_t&) {});
    }

    // add jail role
    bot.guild_member_add_role(guild_id, target_id, config.jail_role_id,
        [&bot, db, guild_id, target_id, mod_id, duration, reason, config, guild_name, metadata_str, success_cb, error_cb](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                error_cb("failed to add jail role: " + cb.get_error().message);
                return;
            }

            // create infraction
            auto inf = bronx::db::infraction_operations::create_infraction(
                db, guild_id, target_id, mod_id,
                "jail", reason, config.point_mute,
                duration, metadata_str);

            if (!inf.has_value()) {
                error_cb("jail applied but failed to create infraction record");
                return;
            }

            // schedule expiry
            schedule_punishment_expiry(bot, db, guild_id, inf->case_number, "jail", target_id, duration, metadata_str);

            // check escalation
            check_and_escalate(bot, db, guild_id, target_id, guild_name);

            // send mod log
            send_mod_log(bot, db, guild_id, inf.value());

            success_cb(inf.value(), is_action_quiet("jail", config));
        });
}

} // namespace moderation
} // namespace commands