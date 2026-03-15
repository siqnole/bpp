#pragma once
#include <dpp/dpp.h>
#include <dpp/nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <atomic>
#include <cstdint>
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_operations.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../moderation/infraction_engine.h"
#include "mod_log.h"

namespace commands {
namespace quiet_moderation {

namespace nick_detail {

inline std::atomic<uint64_t>& nick_counter() {
    static std::atomic<uint64_t> counter{0};
    return counter;
}

inline std::vector<std::regex> parse_bad_patterns(const std::string& json_str) {
    std::vector<std::regex> patterns;
    if (json_str.empty()) return patterns;
    try {
        auto arr = nlohmann::json::parse(json_str);
        if (!arr.is_array()) return patterns;
        for (auto& elem : arr) {
            if (!elem.is_string()) continue;
            try {
                patterns.emplace_back(elem.get<std::string>(),
                                      std::regex::icase | std::regex::ECMAScript);
            } catch (const std::regex_error&) {}
        }
    } catch (...) {}
    return patterns;
}

inline std::string build_sanitized_name(const std::string& format) {
    uint64_t n = nick_counter().fetch_add(1) + 1;
    std::string result = format;
    auto pos = result.find("{n}");
    if (pos != std::string::npos) {
        result.replace(pos, 3, std::to_string(n));
    }
    return result;
}

inline std::string get_display_name(const dpp::guild_member& member) {
    if (!member.get_nickname().empty()) return member.get_nickname();
    auto* user = member.get_user();
    if (user) {
        if (!user->global_name.empty()) return user->global_name;
        return user->username;
    }
    return "";
}

inline void check_nickname(dpp::cluster& bot, bronx::db::Database* db,
                           dpp::snowflake guild_id, dpp::snowflake user_id,
                           const dpp::guild_member& member,
                           const std::string& guild_name) {
    auto config_opt = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
    if (!config_opt.has_value()) return;
    auto& config = config_opt.value();

    if (!config.nickname_sanitize_enabled) return;

    auto patterns = parse_bad_patterns(config.nickname_bad_patterns);
    if (patterns.empty()) return;

    std::string display = get_display_name(member);
    if (display.empty()) return;

    bool matched = false;
    for (auto& pat : patterns) {
        if (std::regex_search(display, pat)) {
            matched = true;
            break;
        }
    }
    if (!matched) return;

    std::string new_name = build_sanitized_name(config.nickname_sanitize_format);
    if (new_name.size() > 32) new_name.resize(32);

    dpp::guild_member gm;
    gm.guild_id = guild_id;
    gm.user_id = user_id;
    gm.set_nickname(new_name);
    bot.guild_edit_member(gm, [&bot, db, guild_id, user_id, display, new_name, guild_name](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) return;

        auto inf_config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id)
                              .value_or(bronx::db::InfractionConfig{});
        double points = inf_config.point_warn;

        std::string reason = "sanitized nickname: " + display + " → " + new_name;

        auto inf = bronx::db::infraction_operations::create_infraction(
            db, guild_id, user_id, bot.me.id,
            "auto_nickname", reason, points, 0,
            "{\"automod\":true,\"original\":\"" + display + "\",\"replacement\":\"" + new_name + "\"}");

        if (inf.has_value()) {
            commands::moderation::send_mod_log(bot, db, guild_id, inf.value());
        }
    });
}

} // namespace nick_detail


inline void register_nickname_guard(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_guild_member_add([&bot, db](const dpp::guild_member_add_t& event) {
        dpp::snowflake guild_id = event.adding_guild.id;
        dpp::snowflake user_id  = event.added.user_id;

        if (event.added.get_user() && event.added.get_user()->is_bot()) return;

        std::string guild_name = event.adding_guild.name;
        nick_detail::check_nickname(bot, db, guild_id, user_id, event.added, guild_name);
    });

    bot.on_guild_member_update([&bot, db](const dpp::guild_member_update_t& event) {
        dpp::snowflake guild_id = event.updated.guild_id;
        dpp::snowflake user_id  = event.updated.user_id;

        if (event.updated.get_user() && event.updated.get_user()->is_bot()) return;

        // Avoid recursion: skip if already sanitized
        auto am_config_opt = bronx::db::infraction_config_operations::get_automod_config(db, guild_id);
        if (am_config_opt.has_value()) {
            auto& am_config = am_config_opt.value();
            if (am_config.nickname_sanitize_enabled) {
                std::string fmt = am_config.nickname_sanitize_format;
                auto brace = fmt.find("{n}");
                std::string prefix = (brace != std::string::npos) ? fmt.substr(0, brace) : fmt;
                std::string current = event.updated.get_nickname();
                if (!prefix.empty() && current.rfind(prefix, 0) == 0) return;
            }
        }

        std::string guild_name;
        dpp::guild* guild = dpp::find_guild(guild_id);
        if (guild) guild_name = guild->name;

        nick_detail::check_nickname(bot, db, guild_id, user_id, event.updated, guild_name);
    });
}

} // namespace quiet_moderation
} // namespace commands