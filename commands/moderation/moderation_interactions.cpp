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
        
        // Purge Modal: mod_purge_modal_<channel_id>
        if (event.custom_id.rfind("mod_purge_modal_", 0) == 0) {
            uint64_t channel_id = std::stoull(event.custom_id.substr(16));
            std::string amount_str = std::get<std::string>(event.components[0].components[0].value);
            std::string user_str = std::get<std::string>(event.components[1].components[0].value);

            int amount = 0;
            try { amount = std::stoi(amount_str); } catch (...) {}

            if (amount < 1 || amount > 100) {
                event.reply(dpp::message().add_embed(bronx::error("amount must be between 1 and 100")).set_flags(dpp::m_ephemeral));
                return;
            }

            uint64_t target_user = 0;
            if (!user_str.empty()) {
                target_user = parse_mention(user_str);
            }

            event.thinking(true);

            bot.messages_get(channel_id, 0, 0, 0, amount, [channel_id, amount, target_user, &bot, event](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("failed to fetch messages")));
                    return;
                }
                auto msgs = std::get<dpp::message_map>(callback.value);
                std::vector<dpp::snowflake> to_delete;
                for (const auto& [id, msg] : msgs) {
                    if (time(0) - msg.sent > 14 * 24 * 60 * 60) continue;
                    if (target_user != 0 && msg.author.id != target_user) continue;
                    to_delete.push_back(id);
                }

                if (to_delete.empty()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("no valid messages found to delete")));
                    return;
                }

                bot.message_delete_bulk(to_delete, channel_id, [to_delete, event, target_user](const dpp::confirmation_callback_t& ccb) {
                    if (ccb.is_error()) {
                        event.edit_original_response(dpp::message().add_embed(bronx::error("failed to delete messages")));
                    } else {
                        auto embed = bronx::create_embed("purged **" + std::to_string(to_delete.size()) + "** messages", get_action_color("purge"));
                        if (target_user != 0) embed.set_description("purged **" + std::to_string(to_delete.size()) + "** messages from <@" + std::to_string(target_user) + ">");
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.edit_original_response(dpp::message().add_embed(embed));
                    }
                });
            });
        }
        // Slowmode Modal: mod_slowmode_modal_<channel_id>
        if (event.custom_id.rfind("mod_slowmode_modal_", 0) == 0) {
            uint64_t channel_id = std::stoull(event.custom_id.substr(19));
            std::string dur_str = std::get<std::string>(event.components[0].components[0].value);
            
            uint32_t duration = 0;
            if (dur_str != "off" && dur_str != "0") {
                duration = parse_duration(dur_str);
                if (duration == 0) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid duration format (e.g., 5s, 1m, 1h)")).set_flags(dpp::m_ephemeral));
                    return;
                }
                if (duration > 21600) {
                    event.reply(dpp::message().add_embed(bronx::error("slowmode cannot exceed 6 hours")).set_flags(dpp::m_ephemeral));
                    return;
                }
            }

            event.thinking(false);

            bot.channel_get(channel_id, [&bot, event, duration](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.edit_original_response(dpp::message().add_embed(bronx::error("failed to fetch channel")));
                    return;
                }
                dpp::channel ch = std::get<dpp::channel>(callback.value);
                ch.rate_limit_per_user = duration;
                
                bot.channel_edit(ch, [event, duration](const dpp::confirmation_callback_t& ccb) {
                    if (ccb.is_error()) {
                        event.edit_original_response(dpp::message().add_embed(bronx::error("failed to update channel slowmode")));
                    } else {
                        std::string desc = duration == 0 ? "slowmode has been disabled" : "slowmode set to **" + format_duration(duration) + "**";
                        auto embed = bronx::create_embed(desc, get_action_color("mute"));
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                        event.edit_original_response(dpp::message().add_embed(embed));
                    }
                });
            });
        }
        
        // Note Modal: mod_note_modal_<target_id>
        if (event.custom_id.rfind("mod_note_modal_", 0) == 0) {
            uint64_t target_id = std::stoull(event.custom_id.substr(15));
            std::string note_text = std::get<std::string>(event.components[0].components[0].value);
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_note_internal(bot, db, guild_id, target_id, mod_id, note_text, config,
                [event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    std::string desc = bronx::EMOJI_CHECK + " **note added to** <@" + std::to_string(target_id) + ">"
                        + "\n**case:** #" + std::to_string(inf.case_number)
                        + "\n**note:** " + inf.reason;
                    auto embed = bronx::create_embed(desc, get_action_color("note"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    dpp::message msg;
                    msg.add_embed(embed);
                    if (is_quiet) msg.set_flags(dpp::m_ephemeral);
                    event.reply(msg);
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        }
        // Reason Modal: mod_reason_modal_<case_number>
        if (event.custom_id.rfind("mod_reason_modal_", 0) == 0) {
            uint32_t case_number = std::stoul(event.custom_id.substr(17));
            std::string new_reason = std::get<std::string>(event.components[0].components[0].value);
            
            uint64_t guild_id = event.command.guild_id;

            if (bronx::db::infraction_operations::update_infraction_reason(db, guild_id, case_number, new_reason)) {
                auto inf_opt = bronx::db::infraction_operations::get_infraction(db, guild_id, case_number);
                if (inf_opt) {
                    auto embed = bronx::create_embed("updated reason for case **#" + std::to_string(case_number) + "**", get_action_color("warn"));
                    embed.add_field("user", "<@" + std::to_string(inf_opt->user_id) + ">", true);
                    embed.add_field("new reason", new_reason, false);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    
                    // send mod log update
                    send_mod_log(bot, db, guild_id, *inf_opt);
                } else {
                    event.reply(dpp::message().add_embed(bronx::success("reason updated")).set_flags(dpp::m_ephemeral));
                }
            } else {
                event.reply(dpp::message().add_embed(bronx::error("failed to update reason")).set_flags(dpp::m_ephemeral));
            }
        }
        // Massban Modal
        if (event.custom_id == "mod_massban_modal") {
            std::string users_str = std::get<std::string>(event.components[0].components[0].value);
            std::string reason = std::get<std::string>(event.components[1].components[0].value);
            if (reason.empty()) reason = "mass ban via interaction";
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::vector<uint64_t> targets;
            std::stringstream ss(users_str);
            std::string item;
            while (ss >> item) {
                uint64_t tid = parse_mention(item);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no valid users to ban provided")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.thinking(false);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            struct Context {
                int count = 0;
                int success = 0;
                int fail = 0;
            };
            auto ctx = std::make_shared<Context>();

            for (uint64_t tid : targets) {
                apply_ban_internal(bot, db, guild_id, tid, mod_id, 0, 0, reason, config,
                    [ctx, targets, &bot, event](const bronx::db::InfractionRow&, bool) {
                        ctx->count++;
                        ctx->success++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass banned **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("ban"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    },
                    [ctx, targets, &bot, event](const std::string&) {
                        ctx->count++;
                        ctx->fail++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass banned **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("ban"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    });
            }
        }
        // Masskick Modal
        if (event.custom_id == "mod_masskick_modal") {
            std::string users_str = std::get<std::string>(event.components[0].components[0].value);
            std::string reason = std::get<std::string>(event.components[1].components[0].value);
            if (reason.empty()) reason = "mass kick via interaction";
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::vector<uint64_t> targets;
            std::stringstream ss(users_str);
            std::string item;
            while (ss >> item) {
                uint64_t tid = parse_mention(item);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no valid users to kick provided")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.thinking(false);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            struct Context {
                int count = 0;
                int success = 0;
                int fail = 0;
            };
            auto ctx = std::make_shared<Context>();

            for (uint64_t tid : targets) {
                apply_kick_internal(bot, db, guild_id, tid, mod_id, reason, config,
                    [ctx, targets, &bot, event](const bronx::db::InfractionRow&, bool) {
                        ctx->count++;
                        ctx->success++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass kicked **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("kick"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    },
                    [ctx, targets, &bot, event](const std::string&) {
                        ctx->count++;
                        ctx->fail++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass kicked **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("kick"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    });
            }
        }
        // Massmute Modal
        if (event.custom_id == "mod_massmute_modal") {
            std::string users_str = std::get<std::string>(event.components[0].components[0].value);
            std::string duration_str = std::get<std::string>(event.components[1].components[0].value);
            std::string reason = std::get<std::string>(event.components[2].components[0].value);
            if (reason.empty()) reason = "mass mute via interaction";
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;
            uint32_t duration = parse_duration(duration_str);

            std::vector<uint64_t> targets;
            std::stringstream ss(users_str);
            std::string item;
            while (ss >> item) {
                uint64_t tid = parse_mention(item);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no valid users to mute provided")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.thinking(false);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};
            if (duration == 0) duration = config.default_duration_mute;

            struct Context {
                int count = 0;
                int success = 0;
                int fail = 0;
            };
            auto ctx = std::make_shared<Context>();

            for (uint64_t tid : targets) {
                apply_mute_internal(bot, db, guild_id, tid, mod_id, duration, reason, config,
                    [ctx, targets, &bot, event](const bronx::db::InfractionRow&, bool) {
                        ctx->count++;
                        ctx->success++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass muted **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("mute"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    },
                    [ctx, targets, &bot, event](const std::string&) {
                        ctx->count++;
                        ctx->fail++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass muted **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("mute"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    });
            }
        }
        // Masstimeout Modal
        if (event.custom_id == "mod_masstimeout_modal") {
            std::string users_str = std::get<std::string>(event.components[0].components[0].value);
            std::string duration_str = std::get<std::string>(event.components[1].components[0].value);
            std::string reason = std::get<std::string>(event.components[2].components[0].value);
            if (reason.empty()) reason = "mass timeout via interaction";
            
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;
            uint32_t duration = parse_duration(duration_str);

            std::vector<uint64_t> targets;
            std::stringstream ss(users_str);
            std::string item;
            while (ss >> item) {
                uint64_t tid = parse_mention(item);
                if (tid != 0 && tid != mod_id && tid != bot.me.id) {
                    targets.push_back(tid);
                }
            }

            if (targets.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("no valid users to timeout provided")).set_flags(dpp::m_ephemeral));
                return;
            }

            event.thinking(false);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};
            if (duration == 0) duration = config.default_duration_timeout;

            struct Context {
                int count = 0;
                int success = 0;
                int fail = 0;
            };
            auto ctx = std::make_shared<Context>();

            for (uint64_t tid : targets) {
                apply_timeout_internal(bot, db, guild_id, tid, mod_id, duration, reason, config,
                    [ctx, targets, &bot, event](const bronx::db::InfractionRow&, bool) {
                        ctx->count++;
                        ctx->success++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    },
                    [ctx, targets, &bot, event](const std::string&) {
                        ctx->count++;
                        ctx->fail++;
                        if (ctx->count == (int)targets.size()) {
                            auto final_embed = bronx::create_embed("mass timed out **" + std::to_string(ctx->success) + "** users (" + std::to_string(ctx->fail) + " failed)", get_action_color("timeout"));
                            bronx::add_invoker_footer(final_embed, event.command.get_issuing_user());
                            event.edit_original_response(dpp::message().add_embed(final_embed));
                        }
                    });
            }
        }
        // Softban Modal: mod_softban_modal_<target_id>
        if (event.custom_id.rfind("mod_softban_modal_", 0) == 0) {
            uint64_t target_id = std::stoull(event.custom_id.substr(18));
            uint64_t guild_id = event.command.guild_id;
            uint64_t mod_id = event.command.get_issuing_user().id;

            std::string reason = std::get<std::string>(event.components[0].components[0].value);

            auto config_opt = bronx::db::infraction_config_operations::get_infraction_config(db, guild_id);
            bronx::db::InfractionConfig config = config_opt.has_value() ? config_opt.value() : bronx::db::InfractionConfig{};

            apply_softban_internal(bot, db, guild_id, target_id, mod_id, reason, config,
                [event, target_id](const bronx::db::InfractionRow& inf, bool is_quiet) {
                    if (is_quiet) {
                        event.reply(dpp::message().add_embed(bronx::success("user softbanned (quiet mode)")).set_flags(dpp::m_ephemeral));
                        return;
                    }
                    std::string desc = bronx::EMOJI_CHECK + " **softbanned** <@" + std::to_string(target_id) + "> (messages cleared)"
                        + "\n**case:** #" + std::to_string(inf.case_number);
                    if (!inf.reason.empty()) desc += "\n**reason:** " + inf.reason;

                    auto embed = bronx::create_embed(desc, get_action_color("ban"));
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                },
                [event](const std::string& err) {
                    event.reply(dpp::message().add_embed(bronx::error(err)).set_flags(dpp::m_ephemeral));
                });
        }
    });
}

} // namespace commands
