#include "setup.h"
#include <iostream>
#include <sstream>
#include <mutex>
#include <map>
#include "../database/core/database.h"
#include "../database/operations/economy/server_economy_operations.h"
#include "../embed_style.h"
#include "../performance/async_stat_writer.h"
#include "quiet_moderation/antispam_config.h"
#include "quiet_moderation/text_filter_config.h"
#include "quiet_moderation/url_guard.h"
#include "quiet_moderation/reaction_filter.h"
#include "../utils/logger.h"

namespace commands {
namespace setup {

// Global map to track active setup sessions
std::map<uint64_t, SetupState> active_setups;
std::recursive_mutex setup_mutex;

bool has_admin_permission(dpp::cluster& bot, uint64_t guild_id, uint64_t user_id) {
    dpp::guild* g = dpp::find_guild(guild_id);
    if (!g) return false;
    
    auto member_it = g->members.find(user_id);
    if (member_it == g->members.end()) return false;
    
    dpp::permission perms = g->base_permissions(member_it->second);
    return perms.can(dpp::p_administrator);
}

void send_welcome_message(dpp::cluster& bot, uint64_t guild_id, uint64_t channel_id, uint64_t admin_id) {
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "thanks for adding me\n\n"
            "__i can help with__\n"
            "- economy (earn, trade, gamble)\n"
            "- fishing (catch and sell fish)\n"
            "- leveling (xp and role rewards)\n"
            "- casino (blackjack, slots, roulette)\n"
            "- games (trivia, connect4, hangman)\n"
            "- moderation (keep things clean)\n\n"
            "want to configure settings now?"
        )
        .add_field("default prefix", "`b.`", true)
        .add_field("get started", "`b.help`", true)
        .set_footer(dpp::embed_footer().set_text("setup takes ~2 minutes"))
        .set_timestamp(time(0));

    dpp::message msg;
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_label("start setup")
            .set_id("setup_start_" + std::to_string(admin_id))
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("skip for now")
            .set_id("setup_skip_" + std::to_string(admin_id))
        )
    );

    bot.message_create(dpp::message(channel_id, "").add_embed(embed).add_component(msg.components[0]),
        [&bot, admin_id, channel_id, guild_id](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                std::string guild_name;
                auto* g = dpp::find_guild(guild_id);
                if (g) guild_name = g->name;

                dpp::embed dm_embed = dpp::embed()
                    .set_color(0x5865F2)
                    .set_description(
                        "thanks for adding me to **" + guild_name + "**!\n\n"
                        + std::string(bronx::EMOJI_WARNING) + " i couldn't send a message in your server — i might be missing permissions.\n\n"
                        "**please make sure i have:**\n"
                        "• `Send Messages`\n"
                        "• `Embed Links`\n"
                        "• `Use External Emojis`\n\n"
                        "once that's fixed, use `b.help` to get started!"
                    )
                    .set_footer(dpp::embed_footer().set_text("default prefix: b."))
                    .set_timestamp(time(0));

                bot.direct_message_create(admin_id, dpp::message().add_embed(dm_embed),
                    [admin_id](const dpp::confirmation_callback_t& dm_cb) {
                        if (dm_cb.is_error()) {
                            bronx::logger::error("setup", "Could not DM guild owner " + std::to_string(admin_id) + " about missing permissions");
                        }
                    });
            }
        });
}

void send_economy_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id) {
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__economy mode__\n\n"
            "**global economy**\n"
            "- balances work across all servers\n"
            "- good for multi-server communities\n"
            "- uses default settings\n\n"
            "**server economy**\n"
            "- independent economy just for this server\n"
            "- customize cooldowns and rewards\n"
            "- users start fresh here\n\n"
            "change later with `/servereconomy`"
        )
        .set_footer(dpp::embed_footer().set_text("step 1/4"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_label("global economy")
            .set_id("setup_economy_global")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_success)
            .set_label("server economy")
            .set_id("setup_economy_server")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_features_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, const std::string& economy_mode) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    active_setups[guild_id].config["economy_mode"] = economy_mode;
    
    active_setups[guild_id].config["gambling"] = "on";
    active_setups[guild_id].config["fishing"] = "on";
    active_setups[guild_id].config["trading"] = "on";
    active_setups[guild_id].config["robbery"] = "on";

    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__module configuration__\n\n"
            "toggle features on/off\n"
            "- gambling (casino, slots, roulette)\n"
            "- fishing (catch and sell)\n"
            "- trading (swap items with others)\n"
            "- robbery (steal from users)\n\n"
            "all enabled by default"
        )
        .set_footer(dpp::embed_footer().set_text("step 2/4 | click to toggle"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    
    if (economy_mode == "server") {
        msg.add_component(dpp::component()
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_success)
                .set_label("gambling: on")
                .set_id("setup_toggle_gambling")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_success)
                .set_label("fishing: on")
                .set_id("setup_toggle_fishing")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_success)
                .set_label("trading: on")
                .set_id("setup_toggle_trading")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_success)
                .set_label("robbery: on")
                .set_id("setup_toggle_robbery")
            )
        );
        msg.add_component(dpp::component()
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_primary)
                .set_label("continue")
                .set_id("setup_features_done")
            )
        );
    } else {
        msg.add_component(dpp::component()
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_primary)
                .set_label("continue")
                .set_id("setup_features_skip")
            )
        );
    }

    event.reply(dpp::ir_update_message, msg);
}

void send_leveling_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db) {
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__leveling system__\n\n"
            "enable xp from chatting?\n\n"
            "**features**\n"
            "- users gain xp from messages\n"
            "- level up and compete on leaderboards\n"
            "- role rewards at certain levels\n"
            "- optional coin rewards per message\n"
            "- customizable xp rates\n\n"
            "configure details later with `/levelconfig`"
        )
        .set_footer(dpp::embed_footer().set_text("step 3/4"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_success)
            .set_label("enable leveling")
            .set_id("setup_leveling_on")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("disable leveling")
            .set_id("setup_leveling_off")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_moderation_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    auto& state = active_setups[guild_id];
    if (state.config.find("antispam") == state.config.end()) {
        state.config["antispam"] = "off";
        state.config["textfilter"] = "off";
        state.config["urlguard"] = "off";
        state.config["reactionfilter"] = "off";
    }
    
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__silent moderation__\n\n"
            "enable automatic moderation?\n\n"
            "**modules available**\n"
            "- antispam (rate limits, duplicates, mentions)\n"
            "- text filter (block words/phrases)\n"
            "- url guard (block links/invites)\n"
            "- reaction filter (block emoji reactions)\n\n"
            "all disabled by default"
        )
        .set_footer(dpp::embed_footer().set_text("step 4/5"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_success)
            .set_label("enable & customize")
            .set_id("setup_moderation_enable")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("skip moderation")
            .set_id("setup_moderation_skip")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_moderation_toggles(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    auto& config = active_setups[guild_id].config;
    
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__moderation modules__\n\n"
            "select which modules to enable\n\n"
            "- antispam: prevent spam/flooding\n"
            "- text filter: block specific words\n"
            "- url guard: control link sharing\n"
            "- reaction filter: manage emoji reactions\n\n"
            "click to toggle, then continue for detailed setup"
        )
        .set_footer(dpp::embed_footer().set_text("step 4/5 | toggle modules"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(config["antispam"] == "on" ? dpp::cos_success : dpp::cos_secondary)
            .set_label("antispam: " + config["antispam"])
            .set_id("setup_mod_toggle_antispam")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(config["textfilter"] == "on" ? dpp::cos_success : dpp::cos_secondary)
            .set_label("text filter: " + config["textfilter"])
            .set_id("setup_mod_toggle_textfilter")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(config["urlguard"] == "on" ? dpp::cos_success : dpp::cos_secondary)
            .set_label("url guard: " + config["urlguard"])
            .set_id("setup_mod_toggle_urlguard")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(config["reactionfilter"] == "on" ? dpp::cos_success : dpp::cos_secondary)
            .set_label("reaction filter: " + config["reactionfilter"])
            .set_id("setup_mod_toggle_reactionfilter")
        )
    );
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_label("continue to customize")
            .set_id("setup_moderation_customize")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_moderation_customization(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    auto& config = active_setups[guild_id].config;
    
    if (config["antispam"] == "on") {
        if (config.find("antispam_max_msg") == config.end()) {
            config["antispam_max_msg"] = "5";
            config["antispam_interval"] = "5";
            config["antispam_delete"] = "on";
            config["antispam_action"] = "timeout";
        }
    }
    if (config["textfilter"] == "on") {
        if (config.find("textfilter_delete") == config.end()) {
            config["textfilter_delete"] = "on";
            config["textfilter_action"] = "timeout";
        }
    }
    if (config["urlguard"] == "on") {
        if (config.find("urlguard_block_invites") == config.end()) {
            config["urlguard_block_invites"] = "on";
            config["urlguard_block_all"] = "off";
            config["urlguard_delete"] = "on";
        }
    }
    if (config["reactionfilter"] == "on") {
        if (config.find("reactionfilter_action") == config.end()) {
            config["reactionfilter_action"] = "remove";
        }
    }
    
    std::stringstream desc;
    desc << "__moderation settings__\n\n";
    desc << "customize enabled modules\n\n";
    
    if (config["antispam"] == "on") {
        desc << "**antispam**\n";
        desc << "- max messages: " << config["antispam_max_msg"] << " per " << config["antispam_interval"] << "s\n";
        desc << "- delete spam: " << config["antispam_delete"] << "\n";
        desc << "- action: " << config["antispam_action"] << "\n\n";
    }
    
    if (config["textfilter"] == "on") {
        desc << "**text filter**\n";
        desc << "- delete messages: " << config["textfilter_delete"] << "\n";
        desc << "- action on violation: " << config["textfilter_action"] << "\n\n";
    }
    
    if (config["urlguard"] == "on") {
        desc << "**url guard**\n";
        desc << "- block invites: " << config["urlguard_block_invites"] << "\n";
        desc << "- block all links: " << config["urlguard_block_all"] << "\n";
        desc << "- delete messages: " << config["urlguard_delete"] << "\n\n";
    }
    
    if (config["reactionfilter"] == "on") {
        desc << "**reaction filter**\n";
        desc << "- action: " << config["reactionfilter_action"] << "\n\n";
    }
    
    desc << "use `/antispam`, `/textfilter`, `/urlguard`, `/reactionfilter` for detailed config";
    
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(desc.str())
        .set_footer(dpp::embed_footer().set_text("step 4/5 | quick settings applied"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_primary)
            .set_label("looks good, continue")
            .set_id("setup_moderation_done")
        )
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_secondary)
            .set_label("back to toggles")
            .set_id("setup_moderation_enable")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_prefix_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db) {
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "__almost done__\n\n"
            "**default prefix:** `b.`\n"
            "change with `/prefix set <prefix>`\n\n"
            "**optional logging:**\n"
            "- moderation actions\n"
            "- economy transactions\n"
            "- level ups\n\n"
            "set up logging channels later"
        )
        .set_footer(dpp::embed_footer().set_text("step 5/5"))
        .set_timestamp(time(0));

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);
    msg.add_component(dpp::component()
        .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_style(dpp::cos_success)
            .set_label("finish setup")
            .set_id("setup_prefix_default")
        )
    );

    event.reply(dpp::ir_update_message, msg);
}

void send_completion(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    auto& state = active_setups[guild_id];
    
    std::string economy_mode = state.config["economy_mode"];
    bool leveling_enabled = state.config["leveling"] == "on";
    
    if (economy_mode == "server") {
        server_economy_operations::set_economy_mode(db, guild_id, "server");
        
        auto conn = db->get_pool()->acquire();
        if (conn) {
            std::string query = "UPDATE guild_settings SET "
                              "allow_gambling = " + std::string(state.config["gambling"] == "on" ? "1" : "0") + ", "
                              "allow_fishing = " + std::string(state.config["fishing"] == "on" ? "1" : "0") + ", "
                              "allow_trading = " + std::string(state.config["trading"] == "on" ? "1" : "0") + ", "
                              "allow_robbery = " + std::string(state.config["robbery"] == "on" ? "1" : "0") + " "
                              "WHERE guild_id = " + std::to_string(guild_id);
            mysql_query(conn->get(), query.c_str());
            db->get_pool()->release(conn);
        }
    }
    
    auto leveling_config = db->get_guild_leveling_config(guild_id);
    if (!leveling_config) {
        db->create_guild_leveling_config(guild_id);
        leveling_config = db->get_guild_leveling_config(guild_id);
    }
    
    if (leveling_config) {
        leveling_config->enabled = leveling_enabled;
        db->update_guild_leveling_config(*leveling_config);
    }
    
    using namespace quiet_moderation;
    
    if (state.config["antispam"] == "on") {
        AntiSpamConfig antispam;
        antispam.enabled = true;
        antispam.max_messages_per_interval = std::stoi(state.config["antispam_max_msg"]);
        antispam.message_interval_seconds = std::stoi(state.config["antispam_interval"]);
        antispam.delete_messages = state.config["antispam_delete"] == "on";
        antispam.log_violations = true;
        guild_antispam_configs[guild_id] = antispam;
    }
    
    if (state.config["textfilter"] == "on") {
        TextFilterConfig textfilter;
        textfilter.enabled = true;
        textfilter.delete_message = state.config["textfilter_delete"] == "on";
        textfilter.log_violations = true;
        guild_text_filters[guild_id] = textfilter;
    }
    
    if (state.config["urlguard"] == "on") {
        URLGuardConfig urlguard;
        urlguard.enabled = true;
        urlguard.block_discord_invites = state.config["urlguard_block_invites"] == "on";
        urlguard.block_all_links = state.config["urlguard_block_all"] == "on";
        urlguard.delete_message = state.config["urlguard_delete"] == "on";
        urlguard.log_violations = true;
        guild_url_guards[guild_id] = urlguard;
    }
    
    if (state.config["reactionfilter"] == "on") {
        ReactionFilterConfig reactionfilter;
        reactionfilter.enabled = true;
        reactionfilter.action = state.config["reactionfilter_action"];
        reactionfilter.log_violations = true;
        guild_reaction_filters[guild_id] = reactionfilter;
    }

    std::stringstream summary;
    summary << "setup complete\n\n";
    summary << "__your configuration__\n";
    summary << "- economy: " << (economy_mode == "server" ? "server-specific" : "global") << "\n";
    summary << "- leveling: " << (leveling_enabled ? "enabled" : "disabled") << "\n";
    
    bool any_moderation = false;
    std::stringstream mod_summary;
    if (state.config["antispam"] == "on") {
        mod_summary << "antispam ";
        any_moderation = true;
    }
    if (state.config["textfilter"] == "on") {
        mod_summary << "textfilter ";
        any_moderation = true;
    }
    if (state.config["urlguard"] == "on") {
        mod_summary << "urlguard ";
        any_moderation = true;
    }
    if (state.config["reactionfilter"] == "on") {
        mod_summary << "reactionfilter ";
        any_moderation = true;
    }
    
    if (any_moderation) {
        summary << "- moderation: " << mod_summary.str() << "\n";
    } else {
        summary << "- moderation: disabled\n";
    }
    
    summary << "- prefix: `b.`\n\n";
    
    summary << "__quick start__\n";
    summary << "- `/help` - view all commands\n";
    summary << "- `/balance` - check your coins\n";
    if (leveling_enabled) {
        summary << "- `/rank` - check your level\n";
    }
    if (economy_mode == "server") {
        summary << "- `/servereconomy status` - view settings\n";
    }
    if (any_moderation) {
        summary << "- use individual moderation commands for detailed config\n";
    }
    summary << "\nuse `b.help` or `/help` to explore features";

    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(summary.str())
        .set_footer(dpp::embed_footer().set_text("have fun"))
        .set_timestamp(time(0));

    bronx::add_support_link(embed);

    dpp::message msg(event.command.channel_id, "");
    msg.add_embed(embed);

    event.reply(dpp::ir_update_message, msg);

    if (bronx::perf::g_stat_writer) {
        std::string user_name = event.command.usr.global_name.empty() 
            ? event.command.usr.username 
            : event.command.usr.global_name;
        
        std::stringstream action;
        action << "Completed initial setup: ";
        action << "<b>" << (economy_mode == "server" ? "server economy" : "global economy") << "</b>, ";
        action << "leveling <b>" << (leveling_enabled ? "on" : "off") << "</b>";
        if (any_moderation) {
            action << ", moderation <b>on</b>";
        }
        
        bronx::perf::g_stat_writer->enqueue_activity_log(
            guild_id,
            event.command.usr.id,
            user_name,
            action.str()
        );
    }

    active_setups.erase(guild_id);
}

void handle_skip_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    dpp::embed embed = dpp::embed()
        .set_color(0x5865F2)
        .set_description(
            "setup skipped\n\n"
            "__configure anytime with__\n"
            "- `/help` - view all commands\n"
            "- `/servereconomy` - economy settings\n"
            "- `/levelconfig` - leveling system\n"
            "- `/antispam`, `/textfilter`, `/urlguard` - moderation\n"
            "- `/prefix` - change prefix\n\n"
            "__quick start__\n"
            "try `/balance` to start earning"
        )
        .add_field("prefix", "`b.`", true)
        .add_field("help", "`b.help`", true)
        .set_footer(dpp::embed_footer().set_text("have fun"))
        .set_timestamp(time(0));

    bronx::add_support_link(embed);

    event.reply(dpp::ir_update_message, dpp::message(event.command.channel_id, "").add_embed(embed));

    active_setups.erase(guild_id);
}

void handle_setup_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db) {
    std::lock_guard<std::recursive_mutex> lock(setup_mutex);
    std::string custom_id = event.custom_id;
    uint64_t guild_id = event.command.guild_id;
    uint64_t user_id = event.command.usr.id;

    if (custom_id.find("setup_") != 0) return;

    if (custom_id.find("setup_start_") == 0) {
        std::string admin_id_str = custom_id.substr(12);
        uint64_t allowed_admin = std::stoull(admin_id_str);
        
        if (user_id != allowed_admin) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message(std::string(bronx::EMOJI_DENY) + " Only the server admin can run the initial setup.").set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (!has_admin_permission(bot, guild_id, user_id)) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message(std::string(bronx::EMOJI_DENY) + " You need Administrator permission to use setup.").set_flags(dpp::m_ephemeral));
            return;
        }
        
        active_setups[guild_id].admin_id = user_id;
        send_economy_setup(bot, event, guild_id);
        return;
    }

    if (!has_admin_permission(bot, guild_id, user_id)) {
        event.reply(dpp::ir_channel_message_with_source, 
            dpp::message(std::string(bronx::EMOJI_DENY) + " You need Administrator permission to configure the bot.").set_flags(dpp::m_ephemeral));
        return;
    }

    if (custom_id.find("setup_skip_") == 0) {
        handle_skip_setup(bot, event, guild_id);
        return;
    }

    if (custom_id == "setup_economy_global") {
        send_features_setup(bot, event, guild_id, "global");
        return;
    }
    if (custom_id == "setup_economy_server") {
        send_features_setup(bot, event, guild_id, "server");
        return;
    }

    if (custom_id == "setup_toggle_gambling" || custom_id == "setup_toggle_fishing" || custom_id == "setup_toggle_trading" || custom_id == "setup_toggle_robbery") {
        auto& config = active_setups[guild_id].config;
        std::string key = custom_id.substr(13); // toggle_gambling -> gambling
        bool current = config[key] == "on";
        config[key] = current ? "off" : "on";
        
        dpp::embed embed = dpp::embed()
            .set_color(0x5865F2)
            .set_description(
                "__module configuration__\n\n"
                "toggle features on/off\n"
                "- gambling (casino, slots, roulette)\n"
                "- fishing (catch and sell)\n"
                "- trading (swap items with others)\n"
                "- robbery (steal from users)\n\n"
                "all enabled by default"
            )
            .set_footer(dpp::embed_footer().set_text("step 2/4 | click to toggle"))
            .set_timestamp(time(0));

        dpp::message msg(event.command.channel_id, "");
        msg.add_embed(embed);
        msg.add_component(dpp::component()
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(config["gambling"] == "on" ? dpp::cos_success : dpp::cos_secondary)
                .set_label("gambling: " + config["gambling"])
                .set_id("setup_toggle_gambling")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(config["fishing"] == "on" ? dpp::cos_success : dpp::cos_secondary)
                .set_label("fishing: " + config["fishing"])
                .set_id("setup_toggle_fishing")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(config["trading"] == "on" ? dpp::cos_success : dpp::cos_secondary)
                .set_label("trading: " + config["trading"])
                .set_id("setup_toggle_trading")
            )
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(config["robbery"] == "on" ? dpp::cos_success : dpp::cos_secondary)
                .set_label("robbery: " + config["robbery"])
                .set_id("setup_toggle_robbery")
            )
        );
        msg.add_component(dpp::component()
            .add_component(dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_primary)
                .set_label("continue")
                .set_id("setup_features_done")
            )
        );
        event.reply(dpp::ir_update_message, msg);
        return;
    }

    if (custom_id == "setup_features_done" || custom_id == "setup_features_skip") {
        send_leveling_setup(bot, event, guild_id, db);
        return;
    }

    if (custom_id == "setup_leveling_on" || custom_id == "setup_leveling_off") {
        active_setups[guild_id].config["leveling"] = (custom_id == "setup_leveling_on" ? "on" : "off");
        send_moderation_setup(bot, event, guild_id, db);
        return;
    }

    if (custom_id == "setup_moderation_enable") {
        send_moderation_toggles(bot, event, guild_id);
        return;
    }

    if (custom_id.find("setup_mod_toggle_") == 0) {
        auto& config = active_setups[guild_id].config;
        std::string key = custom_id.substr(17); // mod_toggle_antispam -> antispam
        bool current = config[key] == "on";
        config[key] = current ? "off" : "on";
        send_moderation_toggles(bot, event, guild_id);
        return;
    }

    if (custom_id == "setup_moderation_customize") {
        send_moderation_customization(bot, event, guild_id);
        return;
    }

    if (custom_id == "setup_moderation_done" || custom_id == "setup_moderation_skip") {
        send_prefix_setup(bot, event, guild_id, db);
        return;
    }

    if (custom_id == "setup_prefix_default") {
        send_completion(bot, event, guild_id, db);
        return;
    }
}

} // namespace setup
} // namespace commands
