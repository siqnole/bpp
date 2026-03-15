#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <mutex>
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../moderation/infraction_engine.h"
#include "mod_log.h"
#include "account_guard.h"

namespace commands {
namespace quiet_moderation {

namespace mutual_detail {

inline int count_mutual_guilds_cached(dpp::cluster& bot, dpp::snowflake user_id) {
    int count = 0;
    dpp::cache<dpp::guild>* gc = dpp::get_guild_cache();
    if (!gc) return 0;

    std::shared_lock lk(gc->get_mutex());
    auto& container = gc->get_container();
    for (auto& [id, guild_ptr] : container) {
        if (!guild_ptr) continue;
        if (guild_ptr->members.find(user_id) != guild_ptr->members.end()) {
            ++count;
        }
    }
    return count;
}

} // namespace mutual_detail

inline void register_mutual_guard(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_guild_member_add([&bot, db](const dpp::guild_member_add_t& event) {
        dpp::snowflake guild_id = event.adding_guild.id;
        dpp::snowflake user_id  = event.added.user_id;

        if (event.added.get_user() && event.added.get_user()->is_bot()) return;

        auto config_opt = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
        if (!config_opt.has_value()) return;
        auto& config = config_opt.value();

        if (!config.mutual_servers_enabled) return;

        int mutual_count = mutual_detail::count_mutual_guilds_cached(bot, user_id);
        if (mutual_count < 1) mutual_count = 1;

        if (mutual_count >= static_cast<int>(config.mutual_servers_min)) return;

        std::string reason = "mutual servers: " + std::to_string(mutual_count)
                           + " (minimum: " + std::to_string(config.mutual_servers_min) + ")";

        std::string guild_name = event.adding_guild.name;

        detail::execute_automod_action(bot, db, guild_id, user_id,
                                       config.mutual_servers_action,
                                       "auto_mutual", reason, guild_name);
    });
}

} // namespace quiet_moderation
} // namespace commands