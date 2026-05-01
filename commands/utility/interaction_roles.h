#pragma once
#include <dpp/dpp.h>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/core/types.h"
#include "../../database/operations/moderation/permission_operations.h"

#include "../../database/operations/utility/role_panel_operations.h"

namespace commands {
namespace utility {

// Forward declare for the interaction handler
inline void register_interaction_role_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        if (event.custom_id.length() > 3 && event.custom_id.substr(0, 3) == "br_") {
            dpp::snowflake role_id = std::stoull(event.custom_id.substr(3));
            
            bot.guild_get_member(event.command.guild_id, event.command.usr.id, [&bot, event, role_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                
                dpp::guild_member m = std::get<dpp::guild_member>(cb.value);
                bool has_role = false;
                for (auto r : m.get_roles()) {
                    if (r == role_id) {
                        has_role = true;
                        break;
                    }
                }

                if (has_role) {
                    bot.guild_member_delete_role(event.command.guild_id, event.command.usr.id, role_id, [event, role_id](const dpp::confirmation_callback_t& cb) {
                        if (cb.is_error()) {
                            event.reply(dpp::message().add_embed(bronx::error("failed to remove role: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        } else {
                            event.reply(dpp::message().add_embed(bronx::info("removed role <@&" + std::to_string(role_id) + ">")).set_flags(dpp::m_ephemeral));
                        }
                    });
                } else {
                    bot.guild_member_add_role(event.command.guild_id, event.command.usr.id, role_id, [event, role_id](const dpp::confirmation_callback_t& cb) {
                        if (cb.is_error()) {
                            event.reply(dpp::message().add_embed(bronx::error("failed to add role: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                        } else {
                            event.reply(dpp::message().add_embed(bronx::info("added role <@&" + std::to_string(role_id) + ">")).set_flags(dpp::m_ephemeral));
                        }
                    });
                }
            });
        }
    });
}

inline Command* get_interaction_role_command(bronx::db::Database* db) {
    static Command* cmd = nullptr;
    if (cmd) return cmd;

    cmd = new Command("rolepanel", "manage interaction-based role panels (buttons/select)", "utility", {"rp"}, true,
        // text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!bronx::db::permission_operations::is_admin(db, event.msg.author.id, event.msg.guild_id)) {
                bronx::send_message(bot, event, bronx::error("only admins can manage role panels"));
                return;
            }
            bronx::send_message(bot, event, bronx::info("please use the slash command `/rolepanel` for an easier experience"));
        },
        // slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!bronx::db::permission_operations::is_admin(db, event.command.usr.id, event.command.guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("only admins can manage role panels")).set_flags(dpp::m_ephemeral));
                return;
            }

            auto sub = event.command.get_command_interaction().options[0];

            if (sub.name == "create") {
                std::string name = std::get<std::string>(event.get_parameter("name"));
                std::string desc = std::get<std::string>(event.get_parameter("description"));
                
                bronx::db::InteractionPanel panel;
                panel.guild_id = event.command.guild_id;
                panel.name = name;
                panel.description = desc;
                panel.panel_type = "button";
                panel.channel_id = 0;
                panel.message_id = 0;

                uint64_t id = bronx::db::role_panel_operations::create_panel(db, panel);
                if (id > 0) {
                    event.reply(dpp::message().add_embed(bronx::success("created role panel **" + name + "** with ID: `" + std::to_string(id) + "`\nuse `/rolepanel add` to add roles to this panel.")).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("failed to create role panel")).set_flags(dpp::m_ephemeral));
                }
            } else if (sub.name == "add") {
                uint64_t panel_id = static_cast<uint64_t>(std::get<int64_t>(event.get_parameter("panel_id")));
                dpp::snowflake role_id = std::get<dpp::snowflake>(event.get_parameter("role"));
                std::string label = std::get<std::string>(event.get_parameter("label"));
                
                auto panel = bronx::db::role_panel_operations::get_panel(db, panel_id);
                if (!panel.has_value() || panel->guild_id != event.command.guild_id) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid panel ID")).set_flags(dpp::m_ephemeral));
                    return;
                }

                bronx::db::InteractionRole role;
                role.panel_id = panel_id;
                role.role_id = role_id;
                role.label = label;
                role.style = 1; // default primary
                
                auto style_param = event.get_parameter("style");
                if (std::holds_alternative<int64_t>(style_param)) role.style = static_cast<int>(std::get<int64_t>(style_param));

                auto emoji_param = event.get_parameter("emoji");
                if (std::holds_alternative<std::string>(emoji_param)) role.emoji_raw = std::get<std::string>(emoji_param);

                uint64_t rid = bronx::db::role_panel_operations::add_role_to_panel(db, role);
                if (rid > 0) {
                    event.reply(dpp::message().add_embed(bronx::success("added role <@&" + std::to_string(role_id) + "> to panel **" + panel->name + "**")).set_flags(dpp::m_ephemeral));
                } else {
                    event.reply(dpp::message().add_embed(bronx::error("failed to add role to panel")).set_flags(dpp::m_ephemeral));
                }
            } else if (sub.name == "post") {
                uint64_t panel_id = static_cast<uint64_t>(std::get<int64_t>(event.get_parameter("panel_id")));
                dpp::snowflake channel_id = event.command.channel_id;
                auto chan_param = event.get_parameter("channel");
                if (std::holds_alternative<dpp::snowflake>(chan_param)) channel_id = std::get<dpp::snowflake>(chan_param);

                auto panel = bronx::db::role_panel_operations::get_panel(db, panel_id);
                if (!panel.has_value() || panel->guild_id != event.command.guild_id) {
                    event.reply(dpp::message().add_embed(bronx::error("invalid panel ID")).set_flags(dpp::m_ephemeral));
                    return;
                }

                auto roles = bronx::db::role_panel_operations::get_panel_roles(db, panel_id);
                if (roles.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("this panel has no roles. use `/rolepanel add` first.")).set_flags(dpp::m_ephemeral));
                    return;
                }

                dpp::message msg(channel_id, "");
                auto embed = bronx::create_embed(panel->description, bronx::COLOR_DEFAULT);
                embed.set_title(panel->name);
                msg.add_embed(embed);

                dpp::component row;
                int count = 0;
                for (const auto& r : roles) {
                    if (count > 0 && count % 5 == 0) {
                        msg.add_component(row);
                        row = dpp::component();
                    }
                    dpp::component btn;
                    btn.set_label(r.label)
                       .set_style((dpp::component_style)r.style)
                       .set_id("br_" + std::to_string(r.role_id));
                    
                    if (!r.emoji_raw.empty()) {
                        btn.set_emoji(r.emoji_raw);
                    }
                    
                    row.add_component(btn);
                    count++;
                }
                msg.add_component(row);

                bot.message_create(msg, [event](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) {
                        event.reply(dpp::message().add_embed(bronx::error("failed to post panel: " + cb.get_error().message)).set_flags(dpp::m_ephemeral));
                    } else {
                        event.reply(dpp::message().add_embed(bronx::success("panel posted successfully!")).set_flags(dpp::m_ephemeral));
                    }
                });
            }
        },
        {
            dpp::command_option(dpp::co_sub_command, "create", "create a new role panel")
                .add_option(dpp::command_option(dpp::co_string, "name", "the name of the panel", true))
                .add_option(dpp::command_option(dpp::co_string, "description", "the description for the panel embed", true)),
            dpp::command_option(dpp::co_sub_command, "add", "add a role to a panel")
                .add_option(dpp::command_option(dpp::co_integer, "panel_id", "the ID of the panel", true))
                .add_option(dpp::command_option(dpp::co_role, "role", "the role to add", true))
                .add_option(dpp::command_option(dpp::co_string, "label", "the button label", true))
                .add_option(dpp::command_option(dpp::co_string, "emoji", "optional emoji", false))
                .add_option(dpp::command_option(dpp::co_integer, "style", "button style (1=primary, 2=secondary, 3=success, 4=danger)", false)),
            dpp::command_option(dpp::co_sub_command, "post", "post a role panel to a channel")
                .add_option(dpp::command_option(dpp::co_integer, "panel_id", "the ID of the panel", true))
                .add_option(dpp::command_option(dpp::co_channel, "channel", "the channel to post in", false))
        }
    );

    return cmd;
}

} // namespace utility
} // namespace commands
