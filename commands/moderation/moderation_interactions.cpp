#include <dpp/dpp.h>
#include "infraction_engine.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/infraction_config_operations.h"
#include "../../embed_style.h"
#include <string>

namespace commands {

void register_moderation_handlers(dpp::cluster& bot, bronx::db::Database* db) {
    using namespace moderation;
    
    // Handle modal submissions for moderation actions
    bot.on_form_submit([db, &bot](const dpp::form_submit_t& event) {
        
        // Mute Modal: mod_mute_modal_<target_id>
        if (event.custom_id.rfind("mod_mute_modal_", 0) == 0) {
            uint64_t target_id = std::stoull(event.custom_id.substr(15));
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::string duration_str = std::get<std::string>(event.components[0].components[0].value);
            std::string reason = std::get<std::string>(event.components[1].components[0].value);

            uint32_t duration = parse_duration(duration_str);
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            if (duration == 0 && config.has_value()) {
                duration = config.value().default_duration_mute;
            }

            apply_mute_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config.value(),
                [event](const bronx::db::InfractionRow& inf, bool quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **muted** <@" + std::to_string(inf.user_id) + ">"
                        + "\n**duration:** " + format_duration(inf.duration_seconds)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("mute"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& error_msg) {
                    event.reply(dpp::message().add_embed(bronx::error(error_msg)).set_flags(dpp::m_ephemeral));
                });
        }
        
        // Ban Modal: mod_ban_modal_<target_id>_<delete_days>
        else if (event.custom_id.rfind("mod_ban_modal_", 0) == 0) {
            // Format: mod_ban_modal_<target_id>_<delete_days>
            std::string remaining = event.custom_id.substr(14);
            size_t sep = remaining.find('_');
            if (sep == std::string::npos) return;

            uint64_t target_id = std::stoull(remaining.substr(0, sep));
            uint32_t delete_days = std::stoul(remaining.substr(sep + 1));
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::string duration_str = std::get<std::string>(event.components[0].components[0].value);
            std::string reason = std::get<std::string>(event.components[1].components[0].value);

            uint32_t duration = parse_duration(duration_str);
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            if (duration == 0 && config.has_value() && duration_str != "0") {
                duration = config.value().default_duration_ban;
            }

            uint32_t delete_message_seconds = delete_days * 86400;

            apply_ban_internal(bot, db, guild_id, target_id, mod_id, duration, delete_message_seconds, reason, config.value(),
                [event](const bronx::db::InfractionRow& inf, bool quiet) {
                    bool is_temp = inf.duration_seconds > 0 && inf.duration_seconds < 15552000;
                    std::string desc = bronx::EMOJI_CHECK + " **banned** <@" + std::to_string(inf.user_id) + ">";
                    if (is_temp) {
                        desc += "\n**duration:** " + format_duration(inf.duration_seconds);
                    } else {
                        desc += "\n**duration:** permanent";
                    }
                    desc += "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());

                    dpp::message msg;
                    msg.add_embed(embed);
                    if (quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& error_msg) {
                    event.reply(dpp::message().add_embed(bronx::error(error_msg)).set_flags(dpp::m_ephemeral));
                });
        }
        
        // Timeout Modal: mod_timeout_modal_<target_id>
        else if (event.custom_id.rfind("mod_timeout_modal_", 0) == 0) {
            uint64_t target_id = std::stoull(event.custom_id.substr(18));
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::string duration_str = std::get<std::string>(event.components[0].components[0].value);
            std::string reason = std::get<std::string>(event.components[1].components[0].value);

            uint32_t duration = parse_duration(duration_str);
            auto config = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);

            if (duration == 0 && config.has_value() && duration_str != "0") {
                duration = config.value().default_duration_timeout;
            }

            apply_timeout_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config.value(),
                [event](const bronx::db::InfractionRow& inf, bool quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **timed out** <@" + std::to_string(inf.user_id) + ">"
                        + "\n**duration:** " + format_duration(inf.duration_seconds)
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("timeout"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& error_msg) {
                    event.reply(dpp::message().add_embed(bronx::error(error_msg)).set_flags(dpp::m_ephemeral));
                });
        }
    });
}

} // namespace commands
