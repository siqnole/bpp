#include "mod_utility.h"
#include "../../embed_style.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../commands/quiet_moderation/mod_log.h"
#include <dpp/nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

namespace bronx {
namespace moderation {

std::string format_duration(uint32_t seconds) {
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

uint32_t get_action_color(const std::string& type) {
    if (type == "warn" || type == "auto_nickname") return 0xF59E0B;
    if (type == "timeout" || type == "mute" || type == "jail") return 0xF97316;
    if (type == "kick") return 0xEF4444;
    if (type == "ban") return 0x991B1B;
    if (type.substr(0, 5) == "auto_") return 0x8B5CF6;
    return bronx::COLOR_DEFAULT;
}

void log_mod_action(dpp::cluster& bot, db::Database* db, uint64_t guild_id, 
                   const db::InfractionRow& inf, uint64_t channel_id) {
    
    auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
    if (!config_opt.has_value()) return;
    auto& config = config_opt.value();

    // 1. Private Staff Log
    if (config.log_channel_id != 0) {
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

        commands::quiet_moderation::send_embed_via_webhook(bot, config.log_channel_id, embed);
    }

    // 2. Determine if we should post a public notice
    bool is_quiet = config.quiet_global;
    try {
        auto overrides = nlohmann::json::parse(config.quiet_overrides);
        if (overrides.contains(inf.type) && overrides[inf.type].is_boolean()) {
            is_quiet = overrides[inf.type].get<bool>();
        }
    } catch (...) {}

    // 3. Public Notice (if not quiet and channel_id is provided)
    if (!is_quiet && channel_id != 0) {
        std::string desc = "**" + inf.type + "** <@" + std::to_string(inf.user_id) + ">";
        if (inf.duration_seconds > 0) {
            desc += " for **" + format_duration(inf.duration_seconds) + "**";
        }
        if (!inf.reason.empty()) {
            desc += "\n**reason:** " + inf.reason;
        }

        auto public_embed = bronx::create_embed(desc, get_action_color(inf.type));
        bot.message_create(dpp::message(channel_id, public_embed));
    }
}

uint64_t parse_mention(const std::string& s) {
    if (s.size() > 2 && s[0] == '<' && s[1] == '@') {
        std::string stripped = s.substr(2, s.size() - 3);
        if (!stripped.empty() && stripped[0] == '!') stripped = stripped.substr(1);
        try { return std::stoull(stripped); } catch (...) { return 0; }
    }
    try { return std::stoull(s); } catch (...) { return 0; }
}

} // namespace moderation
} // namespace bronx
