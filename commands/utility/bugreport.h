#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/community/bugreport_operations.h"
#include "../../commands/economy_core.h" // for format_number
#include <dpp/dpp.h>
#include <chrono>
#include <map>
#include <sstream>

namespace commands {
namespace utility {

// Cooldown map to prevent spam
static std::map<uint64_t, std::chrono::system_clock::time_point> last_bugreport_time;

// Helper to join args
inline std::string join_report_args(const std::vector<std::string>& args) {
    std::ostringstream ss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) ss << " ";
        ss << args[i];
    }
    return ss.str();
}

inline Command* get_bugreport_command(bronx::db::Database* db) {
    // The report command shows a button that opens a modal for step-by-step bug reporting
    static Command bugreport("report", "submit a bug report for the bot", "utility", {"bugreport", "bug"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            auto now = std::chrono::system_clock::now();
            auto it = last_bugreport_time.find(user_id);
            if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
                bronx::send_message(bot, event, bronx::error("please wait 2 minutes before submitting another bug report"));
                return;
            }

            dpp::embed embed = bronx::create_embed(
                "Click the button below to submit a bug report.\n\n"
                "You will be asked to provide:\n"
                "• **What command/feature** has the issue\n"
                "• **How to reproduce** the bug\n"
                "• **What should have happened**\n"
                "• **What actually happened**"
            );
            embed.set_title("Bug Report");
            embed.set_color(0xED4245); // Red

            dpp::message msg(event.msg.channel_id, embed);
            
            dpp::component btn_row;
            btn_row.set_type(dpp::cot_action_row);
            
            dpp::component report_btn;
            report_btn.set_type(dpp::cot_button)
                .set_label("Report Bug")
                .set_style(dpp::cos_danger)
                .set_id("bugreport_start_" + std::to_string(user_id))
                .set_emoji("🐛");
            btn_row.add_component(report_btn);
            
            msg.add_component(btn_row);
            bot.message_create(msg);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            auto now = std::chrono::system_clock::now();
            auto it = last_bugreport_time.find(user_id);
            if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
                event.reply(dpp::message().add_embed(bronx::error("please wait 2 minutes before submitting another bug report")));
                return;
            }

            dpp::embed embed = bronx::create_embed(
                "Click the button below to submit a bug report.\n\n"
                "You will be asked to provide:\n"
                "• **What command/feature** has the issue\n"
                "• **How to reproduce** the bug\n"
                "• **What should have happened**\n"
                "• **What actually happened**"
            );
            embed.set_title("Bug Report");
            embed.set_color(0xED4245);

            dpp::message msg;
            msg.add_embed(embed);
            
            dpp::component btn_row;
            btn_row.set_type(dpp::cot_action_row);
            
            dpp::component report_btn;
            report_btn.set_type(dpp::cot_button)
                .set_label("Report Bug")
                .set_style(dpp::cos_danger)
                .set_id("bugreport_start_" + std::to_string(user_id))
                .set_emoji("🐛");
            btn_row.add_component(report_btn);
            
            msg.add_component(btn_row);
            event.reply(msg);
        },
        {});

    return &bugreport;
}

// Register interaction handlers for bug reports
inline void register_bugreport_interactions(dpp::cluster& bot, bronx::db::Database* db) {
    // Handle the Report Bug button click - show modal
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        std::string id = event.custom_id;
        
        if (id.rfind("bugreport_start_", 0) == 0) {
            // Extract user ID from button ID
            uint64_t btn_user_id = std::stoull(id.substr(16));
            uint64_t clicker_id = event.command.get_issuing_user().id;
            
            // Only the user who invoked the command can click
            if (btn_user_id != clicker_id) {
                event.reply(dpp::ir_channel_message_with_source, 
                    dpp::message().add_embed(bronx::error("this button is not for you")).set_flags(dpp::m_ephemeral));
                return;
            }

            // Show the bug report modal
            dpp::interaction_modal_response modal("bugreport_modal", "Bug Report");
            
            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("What command/feature has the issue?")
                    .set_id("command_or_feature")
                    .set_text_style(dpp::text_short)
                    .set_placeholder("e.g., fish, balance, shop buy")
                    .set_min_length(1)
                    .set_max_length(100)
                    .set_required(true)
            );
            
            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("How do you reproduce this bug?")
                    .set_id("reproduction_steps")
                    .set_text_style(dpp::text_paragraph)
                    .set_placeholder("Step-by-step instructions to reproduce the issue...")
                    .set_min_length(10)
                    .set_max_length(1000)
                    .set_required(true)
            );
            
            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("What should have happened?")
                    .set_id("expected_behavior")
                    .set_text_style(dpp::text_paragraph)
                    .set_placeholder("Describe what you expected to happen...")
                    .set_min_length(5)
                    .set_max_length(500)
                    .set_required(true)
            );
            
            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("What actually happened?")
                    .set_id("actual_behavior")
                    .set_text_style(dpp::text_paragraph)
                    .set_placeholder("Describe what actually happened instead...")
                    .set_min_length(5)
                    .set_max_length(500)
                    .set_required(true)
            );

            event.dialog(modal);
        }
    });

    // Handle modal submission
    bot.on_form_submit([&bot, db](const dpp::form_submit_t& event) {
        if (event.custom_id != "bugreport_modal") {
            return;
        }

        uint64_t user_id = event.command.get_issuing_user().id;

        // Check cooldown
        auto now = std::chrono::system_clock::now();
        auto it = last_bugreport_time.find(user_id);
        if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("please wait 2 minutes before submitting another bug report")).set_flags(dpp::m_ephemeral));
            return;
        }

        // Extract form values
        std::string command_or_feature, reproduction_steps, expected_behavior, actual_behavior;
        
        for (const auto& comp : event.components) {
            if (comp.custom_id == "command_or_feature") {
                command_or_feature = std::get<std::string>(comp.value);
            } else if (comp.custom_id == "reproduction_steps") {
                reproduction_steps = std::get<std::string>(comp.value);
            } else if (comp.custom_id == "expected_behavior") {
                expected_behavior = std::get<std::string>(comp.value);
            } else if (comp.custom_id == "actual_behavior") {
                actual_behavior = std::get<std::string>(comp.value);
            }
        }

        // Get user's networth
        int64_t networth = 0;
        auto user = db->get_user(user_id);
        if (user) {
            networth = user->wallet + user->bank;
        }

        // Save to database
        if (!bronx::db::bugreport_operations::add_bug_report(db, user_id, command_or_feature, 
                reproduction_steps, expected_behavior, actual_behavior, networth)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("failed to submit bug report")).set_flags(dpp::m_ephemeral));
            return;
        }

        // Update cooldown
        last_bugreport_time[user_id] = now;

        // Send confirmation
        dpp::embed embed = bronx::success("Bug report submitted successfully!\n\nThank you for helping improve the bot.");
        embed.set_title("Bug Report Submitted");
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());
        
        event.reply(dpp::ir_channel_message_with_source, dpp::message().add_embed(embed));
    });
}

} // namespace utility
} // namespace commands
