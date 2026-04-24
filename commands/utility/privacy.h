#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/user/privacy_operations.h"
#include <dpp/dpp.h>
#include <chrono>
#include <map>
#include <mutex>

namespace commands {
namespace utility {

// ============================================================================
// PRIVACY COMMAND — opt-out / opt-in / data deletion
// ============================================================================

// confirmation cooldown to prevent accidental spam
static std::map<uint64_t, std::chrono::steady_clock::time_point> pending_optout_confirmation_;
static std::mutex optout_confirm_mutex_;

inline void register_privacy_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    // handle opt-out confirmation button
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        const auto& cid = event.custom_id;
        
        if (cid.rfind("privacy_confirm_optout_", 0) == 0) {
            uint64_t user_id = event.command.get_issuing_user().id;
            std::string expected = "privacy_confirm_optout_" + std::to_string(user_id);
            if (cid != expected) {
                event.reply(dpp::message()
                    .add_embed(bronx::error("this button isn't for you"))
                    .set_flags(dpp::m_ephemeral));
                return;
            }
            
            // opt out + delete all data
            bronx::db::privacy_operations::set_opted_out(db, user_id, true);
            bronx::db::privacy_operations::delete_all_user_data(db, user_id);
            
            auto embed = bronx::create_embed(
                "🔒 **privacy — opted out**\n\n"
                "your data has been **permanently deleted** and you have been opted out.\n\n"
                "• all economy data, inventory, fish, stats, and history have been removed\n"
                "• the bot will no longer process any of your commands\n"
                "• no new data will be collected about you\n\n"
                "if you ever want to come back, use `b.privacy optin` to re-enable your account.",
                bronx::COLOR_SUCCESS);
            embed.set_footer(dpp::embed_footer().set_text("your privacy matters"));
            
            event.reply(dpp::message()
                .add_embed(embed)
                .set_flags(dpp::m_ephemeral));
            
            std::cerr << "🔒 privacy: user " << user_id << " opted out and data deleted\n";
        }
        
        else if (cid.rfind("privacy_cancel_optout_", 0) == 0) {
            uint64_t user_id = event.command.get_issuing_user().id;
            std::string expected = "privacy_cancel_optout_" + std::to_string(user_id);
            if (cid != expected) {
                event.reply(dpp::message()
                    .add_embed(bronx::error("this button isn't for you"))
                    .set_flags(dpp::m_ephemeral));
                return;
            }
            
            event.reply(dpp::message()
                .add_embed(bronx::info("opt-out cancelled. your data is unchanged."))
                .set_flags(dpp::m_ephemeral));
        }
    });
}

inline Command* get_privacy_command(bronx::db::Database* db) {
    static Command privacy("privacy", "manage your data privacy and opt-out preferences", "utility", {"optout", "optin", "datadelete", "gdpr"}, false,
        // ── text handler ────────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            
            if (args.empty()) {
                // show privacy status
                bool opted_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                
                auto embed = bronx::create_embed(
                    "🔒 **privacy settings**\n\n"
                    "**status:** " + std::string(opted_out ? "opted out — no data collected" : "active — data is collected per our privacy policy") + "\n\n"
                    "**commands:**\n"
                    "> `b.privacy optout` — opt out and delete all your data\n"
                    "> `b.privacy optin` — opt back in and start using the bot again\n"
                    "> `b.privacy info` — see what data we collect\n\n"
                    "read our full privacy policy at **https://docs.bronxbot.xyz/privacy**",
                    bronx::COLOR_DEFAULT);
                embed.set_footer(dpp::embed_footer().set_text("your privacy matters"));
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string action = args[0];
            std::transform(action.begin(), action.end(), action.begin(), ::tolower);
            
            // ── opt out ─────────────────────────────────────────────────────
            if (action == "optout" || action == "opt-out") {
                bool already_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                if (already_out) {
                    bronx::send_message(bot, event, bronx::info("you're already opted out. no data is being collected."));
                    return;
                }
                
                // send confirmation with buttons
                auto embed = bronx::create_embed(
                    "🔒 **privacy — confirm opt-out**\n\n"
                    "this will **permanently delete** all of your data including:\n\n"
                    "• wallet, bank, and all economy progress\n"
                    "• fish inventory, catches, and autofisher data\n"
                    "• gambling stats, daily streaks, and challenges\n"
                    "• pets, mining claims, and skill trees\n"
                    "• XP, levels, and leaderboard positions\n"
                    "• command history and all other stored data\n\n"
                    "**this action is irreversible.** you will not be able to use any bot commands until you opt back in.\n\n"
                    "are you sure?",
                    bronx::COLOR_WARNING);
                embed.set_footer(dpp::embed_footer().set_text("this cannot be undone"));
                
                dpp::message msg;
                msg.add_embed(embed);
                msg.add_component(
                    dpp::component()
                        .set_type(dpp::cot_action_row)
                        .add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_label("yes, delete my data")
                                .set_style(dpp::cos_danger)
                                .set_id("privacy_confirm_optout_" + std::to_string(user_id))
                        )
                        .add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_label("cancel")
                                .set_style(dpp::cos_secondary)
                                .set_id("privacy_cancel_optout_" + std::to_string(user_id))
                        )
                );
                
                bronx::send_message(bot, event, msg);
                return;
            }
            
            // ── opt in ──────────────────────────────────────────────────────
            if (action == "optin" || action == "opt-in") {
                bool was_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                if (!was_out) {
                    bronx::send_message(bot, event, bronx::info("you're already opted in. everything is working normally."));
                    return;
                }
                
                bronx::db::privacy_operations::set_opted_out(db, user_id, false);
                
                auto embed = bronx::create_embed(
                    "🔒 **privacy — opted back in**\n\n"
                    "welcome back! you can now use all bot commands again.\n\n"
                    "note: your previous data was deleted when you opted out and cannot be recovered. "
                    "you'll start fresh with a new account.",
                    bronx::COLOR_SUCCESS);
                embed.set_footer(dpp::embed_footer().set_text("your privacy matters"));
                bronx::send_message(bot, event, embed);
                
                std::cerr << "🔒 privacy: user " << user_id << " opted back in\n";
                return;
            }
            
            // ── info ────────────────────────────────────────────────────────
            if (action == "info" || action == "policy") {
                auto embed = bronx::create_embed(
                    "🔒 **what data does bronx collect?**\n\n"
                    "**stored in the database (keyed by your discord user ID):**\n"
                    "> • economy data — wallet, bank, net worth, loan info\n"
                    "> • gameplay stats — fish catches, gambling results, daily streaks\n"
                    "> • inventory — items, rods, bait, pets, mining claims\n"
                    "> • progression — XP, levels, skill trees, prestige\n"
                    "> • command usage — which commands you run and when\n"
                    "> • moderation flags — blacklist/whitelist, anti-cheat records\n"
                    "> • preferences — custom prefixes, passive mode, AFK status\n"
                    "> • submissions — suggestions and bug reports you send\n\n"
                    "**NOT stored in the database:**\n"
                    "> • usernames, nicknames, or display names\n"
                    "> • avatar images or URLs\n"
                    "> • email addresses or IP addresses\n"
                    "> • message content (except suggestions/bug reports you submit)\n"
                    "> • DM content\n\n"
                    "**identity data (username, avatar) is:**\n"
                    "> • fetched live from discord when displaying embeds\n"
                    "> • encrypted with AES-256 if temporarily cached (30-day expiry)\n"
                    "> • never sold, shared, or used for advertising\n\n"
                    "**dashboard (website):**\n"
                    "> • discord OAuth2 session (username, avatar) stored server-side for 24h\n"
                    "> • guild member lists fetched live from discord API, cached 30s\n"
                    "> • aggregated server stats (message counts, command usage)\n\n"
                    "full privacy policy: **https://docs.bronxbot.xyz/privacy**",
                    bronx::COLOR_INFO);
                embed.set_footer(dpp::embed_footer().set_text("your privacy matters • docs.bronxbot.xyz/privacy"));
                bronx::send_message(bot, event, embed);
                return;
            }
            
            // unknown subcommand
            bronx::send_message(bot, event, bronx::error("unknown action. use `optout`, `optin`, or `info`"));
        },
        // ── slash handler ───────────────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            
            std::string action = "status";
            auto action_param = event.get_parameter("action");
            if (std::holds_alternative<std::string>(action_param)) {
                action = std::get<std::string>(action_param);
            }
            
            if (action == "status") {
                bool opted_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                auto embed = bronx::create_embed(
                    "🔒 **privacy settings**\n\n"
                    "**status:** " + std::string(opted_out ? "opted out — no data collected" : "active — data is collected per our privacy policy") + "\n\n"
                    "use `/privacy optout` to opt out and delete your data\n"
                    "use `/privacy optin` to re-enable your account\n"
                    "use `/privacy info` to see what we collect\n\n"
                    "full privacy policy: **https://docs.bronxbot.xyz/privacy**",
                    bronx::COLOR_DEFAULT);
                embed.set_footer(dpp::embed_footer().set_text("your privacy matters"));
                event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            else if (action == "optout") {
                bool already_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                if (already_out) {
                    event.reply(dpp::message()
                        .add_embed(bronx::info("you're already opted out. no data is being collected."))
                        .set_flags(dpp::m_ephemeral));
                    return;
                }
                
                auto embed = bronx::create_embed(
                    "🔒 **privacy — confirm opt-out**\n\n"
                    "this will **permanently delete** all your data and prevent the bot from processing your commands.\n\n"
                    "**this action is irreversible.**\n\n"
                    "click the button below to confirm.",
                    bronx::COLOR_WARNING);
                
                dpp::message msg;
                msg.add_embed(embed);
                msg.set_flags(dpp::m_ephemeral);
                msg.add_component(
                    dpp::component()
                        .set_type(dpp::cot_action_row)
                        .add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_label("yes, delete my data")
                                .set_style(dpp::cos_danger)
                                .set_id("privacy_confirm_optout_" + std::to_string(user_id))
                        )
                        .add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_label("cancel")
                                .set_style(dpp::cos_secondary)
                                .set_id("privacy_cancel_optout_" + std::to_string(user_id))
                        )
                );
                
                event.reply(msg);
            }
            else if (action == "optin") {
                bool was_out = bronx::db::privacy_operations::is_opted_out(db, user_id);
                if (!was_out) {
                    event.reply(dpp::message()
                        .add_embed(bronx::info("you're already opted in. everything is working normally."))
                        .set_flags(dpp::m_ephemeral));
                    return;
                }
                
                bronx::db::privacy_operations::set_opted_out(db, user_id, false);
                
                auto embed = bronx::create_embed(
                    "🔒 **privacy — opted back in**\n\n"
                    "welcome back! you can now use all bot commands again.\n"
                    "your previous data was deleted and cannot be recovered — you'll start fresh.",
                    bronx::COLOR_SUCCESS);
                event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
            else if (action == "info") {
                auto embed = bronx::create_embed(
                    "🔒 **what data does bronx collect?**\n\n"
                    "**stored:** economy data, gameplay stats, inventory, XP/levels, "
                    "command usage, moderation flags, preferences, submissions\n\n"
                    "**NOT stored:** usernames, avatars, emails, IPs, message content, DMs\n\n"
                    "**identity data:** encrypted with AES-256 if cached, 30-day expiry\n\n"
                    "full details: **https://docs.bronxbot.xyz/privacy**",
                    bronx::COLOR_INFO);
                event.reply(dpp::message().add_embed(embed).set_flags(dpp::m_ephemeral));
            }
        },
        // slash command options
        {
            dpp::command_option(dpp::co_string, "action", "what to do", false)
                .add_choice(dpp::command_option_choice("view status", std::string("status")))
                .add_choice(dpp::command_option_choice("opt out & delete data", std::string("optout")))
                .add_choice(dpp::command_option_choice("opt back in", std::string("optin")))
                .add_choice(dpp::command_option_choice("view collected data info", std::string("info")))
        }
    );
    
    // extended help
    privacy.extended_description = "manage your privacy preferences. you can opt out of all data collection, "
                                    "which will delete all your data and prevent the bot from processing your commands.";
    privacy.subcommands = {
        {"optout", "opt out and permanently delete all your data"},
        {"optin", "opt back in and start using the bot again (fresh start)"},
        {"info", "see exactly what data the bot collects about you"},
    };
    privacy.examples = {"b.privacy", "b.privacy optout", "b.privacy optin", "b.privacy info"};
    privacy.notes = "opting out is irreversible — all data is permanently deleted. "
                     "you can opt back in at any time but you'll start with a fresh account.";
    
    return &privacy;
}

} // namespace utility
} // namespace commands
