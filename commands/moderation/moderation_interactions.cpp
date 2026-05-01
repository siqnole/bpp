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
            } else if (event.custom_id.find("mod_kick_modal_") == 0) {
                uint64_t target_id = std::stoull(event.custom_id.substr(15));
                std::string reason = std::get<std::string>(event.components[0].components[0].value);
                uint64_t mod_id = event.command.get_issuing_user().id;
                uint64_t guild_id = event.command.guild_id;

                auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
                if (!config_opt.has_value()) {
                    event.reply(dpp::message().add_embed(bronx::error("failed to fetch moderation config")).set_flags(dpp::m_ephemeral));
                    return;
                }

                apply_kick_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                    [event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                        if (is_quiet) {
                            event.reply(dpp::message().add_embed(bronx::success("user kicked (quiet mode)")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        
                        std::string desc = bronx::EMOJI_CHECK + " **kicked** <@" + std::to_string(target_id) + ">"
                            + "\n**case:** #" + std::to_string(inf.case_number);
                        if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                        auto embed = bronx::create_embed(desc, get_action_color("kick"));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.reply(dpp::message().add_embed(embed));
                    },
                    [event](const std::string& err) {
                        event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                    });

            } else if (event.custom_id.find("mod_warn_modal_") == 0) {
                uint64_t target_id = std::stoull(event.custom_id.substr(15));
                std::string reason = std::get<std::string>(event.components[0].components[0].value);
                uint64_t mod_id = event.command.get_issuing_user().id;
                uint64_t guild_id = event.command.guild_id;

                auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
                if (!config_opt.has_value()) {
                    event.reply(dpp::message().add_embed(bronx::error("failed to fetch moderation config")).set_flags(dpp::m_ephemeral));
                    return;
                }

                apply_warn_internal(bot, db, guild_id, target_id, mod_id, reason, config_opt.value(),
                    [event, target_id, config_opt](const bronx::db::InfractionRow& inf, bool is_quiet, double active_points) {
                        if (is_quiet) {
                            event.reply(dpp::message().add_embed(bronx::success("user warned (quiet mode)")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        
                        std::string desc = bronx::EMOJI_CHECK + " **warned** <@" + std::to_string(target_id) + ">"
                            + "\n**case:** #" + std::to_string(inf.case_number)
                            + "\n**reason:** " + inf.reason
                            + "\n**points:** +" + std::to_string(config_opt.value().point_warn)
                            + " (total active: " + std::to_string(active_points) + ")";

                        auto embed = bronx::create_embed(desc, get_action_color("warn"));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.reply(dpp::message().add_embed(embed));
                    },
                    [event](const std::string& err) {
                        event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                    });
            } else if (event.custom_id.find("mod_jail_modal_") == 0) {
            std::string remaining = event.custom_id.substr(15);
            size_t delim_pos = remaining.find("_");
            if (delim_pos != std::string::npos) {
                uint64_t target_id = std::stoull(remaining.substr(0, delim_pos));
                uint32_t duration = std::stoul(remaining.substr(delim_pos + 1));
                std::string reason = std::get<std::string>(event.components[0].components[0].value);
                uint64_t mod_id = event.command.get_issuing_user().id;
                uint64_t guild_id = event.command.guild_id;

                auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
                if (!config_opt.has_value() || config_opt.value().jail_role_id == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("jail role is not configured — use the setup command to set one")).set_flags(dpp::m_ephemeral));
                    return;
                }

                auto* guild = dpp::find_guild(guild_id);
                std::vector<dpp::snowflake> current_roles;
                if (guild) {
                    auto it = guild->members.find(target_id);
                    if (it != guild->members.end()) current_roles = it->second.get_roles();
                }

                apply_jail_internal(bot, db, guild_id, target_id, mod_id, duration, reason, config_opt.value(), current_roles,
                    [event, target_id, duration](const bronx::db::InfractionRow& inf, bool is_quiet) {
                        if (is_quiet) {
                            event.reply(dpp::message().add_embed(bronx::success("user jailed (quiet mode)")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                        
                        std::string desc = bronx::EMOJI_CHECK + " **jailed** <@" + std::to_string(target_id) + ">"
                            + "\n**duration:** " + format_duration(duration)
                            + "\n**case:** #" + std::to_string(inf.case_number);
                        if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                        int roles_stored = 0;
                        try {
                            auto meta = nlohmann::json::parse(inf.metadata);
                            if (meta.contains("stored_roles") && meta["stored_roles"].is_array())
                                roles_stored = static_cast<int>(meta["stored_roles"].size());
                        } catch (...) {}
                        desc += "\n**roles stored:** " + std::to_string(roles_stored) + " roles saved for restoration";

                        auto embed = bronx::create_embed(desc, get_action_color("jail"));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.reply(dpp::message().add_embed(embed));
                    },
                    [event](const std::string& err) {
                        event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                    });
            }
        }
    });
}

} // namespace commands
