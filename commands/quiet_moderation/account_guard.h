#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <ctime>
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../moderation/infraction_engine.h"
#include "mod_log.h"

namespace commands {
namespace quiet_moderation {

namespace detail {

static constexpr uint64_t DISCORD_EPOCH_MS = 1420070400000ULL;

inline uint64_t snowflake_created_epoch(dpp::snowflake id) {
    uint64_t ms = (static_cast<uint64_t>(id) >> 22) + DISCORD_EPOCH_MS;
    return ms / 1000;
}

inline void execute_automod_action(dpp::cluster& bot, bronx::db::Database* db,
                                   uint64_t guild_id, uint64_t user_id,
                                   const std::string& action,
                                   const std::string& infraction_type,
                                   const std::string& reason,
                                   const std::string& guild_name,
                                   bool should_escalate = true) {
    // Unwrap infraction config — use defaults if not configured
    auto inf_config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                          .value_or(bronx::db::InfractionConfig{});

    double points = 0;
    uint32_t duration = 0;
    if (action == "kick") {
        points   = inf_config.point_kick;
        duration = inf_config.default_duration_kick;
    } else if (action == "ban") {
        points   = inf_config.point_ban;
        duration = inf_config.default_duration_ban;
    } else if (action == "timeout") {
        points   = inf_config.point_timeout;
        duration = 86400;
    } else if (action == "mute") {
        points   = inf_config.point_mute;
        duration = inf_config.default_duration_mute;
    }

    auto inf = bronx::db::infraction_operations::create_infraction(
        db, guild_id, user_id, bot.me.id,
        infraction_type, reason, points, duration,
        "{\"automod\":true}");

    if (inf_config.dm_on_action) {
        commands::moderation::dm_user_action(bot, user_id, guild_name,
                                             infraction_type, reason, duration, points);
    }

    if (action == "kick") {
        bot.guild_member_delete(guild_id, user_id,
            [](const dpp::confirmation_callback_t&) {});
    } else if (action == "ban") {
        bot.guild_ban_add(guild_id, user_id, 0,
            [](const dpp::confirmation_callback_t&) {});
    } else if (action == "timeout") {
        bot.guild_member_timeout(guild_id, user_id,
            time(0) + static_cast<time_t>(duration),
            [](const dpp::confirmation_callback_t&) {});
    } else if (action == "mute" && inf_config.mute_role_id != 0) {
        bot.guild_member_add_role(guild_id, user_id, inf_config.mute_role_id,
            [](const dpp::confirmation_callback_t&) {});
    }

    if (inf.has_value()) {
        commands::moderation::send_mod_log(bot, db, guild_id, inf.value());

        if (duration > 0 && (action == "ban" || action == "timeout" || action == "mute")) {
            commands::moderation::schedule_punishment_expiry(
                bot, db, guild_id, inf.value().case_number,
                action, user_id, duration);
        }
    }

    if (should_escalate) {
        auto am_config = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
        if (am_config.has_value() && am_config.value().infraction_escalation_enabled) {
            commands::moderation::check_and_escalate(bot, db, guild_id, user_id, guild_name);
        }
    }
}

} // namespace detail


inline void register_account_guard(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_guild_member_add([&bot, db](const dpp::guild_member_add_t& event) {
        dpp::snowflake guild_id = event.adding_guild.id;
        dpp::snowflake user_id  = event.added.user_id;

        if (event.added.get_user() && event.added.get_user()->is_bot()) return;

        auto config_opt = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
        if (!config_opt.has_value()) return;
        auto& config = config_opt.value();

        if (!config.account_age_enabled) return;

        uint64_t created_epoch = detail::snowflake_created_epoch(user_id);
        uint64_t now_epoch     = static_cast<uint64_t>(time(nullptr));

        if (created_epoch >= now_epoch) return;

        uint64_t age_seconds = now_epoch - created_epoch;
        uint64_t age_days    = age_seconds / 86400;

        if (age_days >= config.account_age_days) return;

        std::string reason = "account age: " + std::to_string(age_days) + " day"
                           + (age_days != 1 ? "s" : "")
                           + " (minimum: " + std::to_string(config.account_age_days) + " days)";

        std::string guild_name = event.adding_guild.name;

        detail::execute_automod_action(bot, db, guild_id, user_id,
                                       config.account_age_action,
                                       "auto_account_age", reason, guild_name);
    });
}

} // namespace quiet_moderation
} // namespace commands