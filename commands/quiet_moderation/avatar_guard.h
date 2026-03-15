// =====================================================================
// avatar_guard.h
// =====================================================================
#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../moderation/infraction_engine.h"
#include "mod_log.h"
#include "account_guard.h"

namespace commands {
namespace quiet_moderation {

inline void register_avatar_guard(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_guild_member_add([&bot, db](const dpp::guild_member_add_t& event) {
        dpp::snowflake guild_id = event.adding_guild.id;
        dpp::snowflake user_id  = event.added.user_id;

        if (event.added.get_user() && event.added.get_user()->is_bot()) return;

        auto config_opt = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
        if (!config_opt.has_value()) return;
        auto config = config_opt.value(); // copy so lambda capture is safe

        if (!config.default_avatar_enabled) return;

        std::string guild_name = event.adding_guild.name;

        bot.user_get(user_id, [&bot, db, guild_id, user_id, config, guild_name](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;

            auto user = cb.get<dpp::user_identified>();

            if (!user.avatar.to_string().empty()) return;

            std::string reason = "default discord avatar";

            detail::execute_automod_action(bot, db, guild_id, user_id,
                                           config.default_avatar_action,
                                           "auto_avatar", reason, guild_name);
        });
    });
}

} // namespace quiet_moderation
} // namespace commands