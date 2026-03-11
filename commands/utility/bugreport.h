#pragma once

#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <chrono>
#include <map>
#include <sstream>
#include <algorithm>

namespace commands {
namespace utility {

// cooldown map to prevent forum spam
static std::map<uint64_t, std::chrono::system_clock::time_point> last_bugreport_time;

// forum channel where bug report threads are created
static constexpr uint64_t BUG_REPORT_FORUM_ID = 1377305431243751586ULL;

inline Command* get_bugreport_command() {
    static Command bugreport("report", "submit a bug report for the bot", "utility", {"bugreport", "bug"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t user_id = event.msg.author.id;
            auto now = std::chrono::system_clock::now();
            auto it = last_bugreport_time.find(user_id);
            if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
                bronx::send_message(bot, event, bronx::error("please wait 2 minutes before submitting another bug report"));
                return;
            }

            dpp::embed embed = bronx::create_embed(
                "found something broken? click below to let us know\n\n"
                "you'll be asked for:\n"
                "\xE2\x80\xA2 the **command or feature** with the issue\n"
                "\xE2\x80\xA2 **steps to reproduce** the bug\n"
                "\xE2\x80\xA2 what **should** have happened\n"
                "\xE2\x80\xA2 what **actually** happened",
                bronx::COLOR_WARNING
            );

            dpp::message msg(event.msg.channel_id, embed);

            dpp::component btn_row;
            btn_row.set_type(dpp::cot_action_row);

            dpp::component report_btn;
            report_btn.set_type(dpp::cot_button)
                .set_label("report bug")
                .set_style(dpp::cos_danger)
                .set_id("bugreport_start_" + std::to_string(user_id))
                .set_emoji("\xF0\x9F\x90\x9B");
            btn_row.add_component(report_btn);

            msg.add_component(btn_row);
            bot.message_create(msg);
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t user_id = event.command.get_issuing_user().id;
            auto now = std::chrono::system_clock::now();
            auto it = last_bugreport_time.find(user_id);
            if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
                event.reply(dpp::message().add_embed(bronx::error("please wait 2 minutes before submitting another bug report")));
                return;
            }

            dpp::embed embed = bronx::create_embed(
                "found something broken? click below to let us know\n\n"
                "you'll be asked for:\n"
                "\xE2\x80\xA2 the **command or feature** with the issue\n"
                "\xE2\x80\xA2 **steps to reproduce** the bug\n"
                "\xE2\x80\xA2 what **should** have happened\n"
                "\xE2\x80\xA2 what **actually** happened",
                bronx::COLOR_WARNING
            );

            dpp::message msg;
            msg.add_embed(embed);

            dpp::component btn_row;
            btn_row.set_type(dpp::cot_action_row);

            dpp::component report_btn;
            report_btn.set_type(dpp::cot_button)
                .set_label("report bug")
                .set_style(dpp::cos_danger)
                .set_id("bugreport_start_" + std::to_string(user_id))
                .set_emoji("\xF0\x9F\x90\x9B");
            btn_row.add_component(report_btn);

            msg.add_component(btn_row);
            event.reply(msg);
        },
        {});

    return &bugreport;
}

// register interaction handlers for bug reports → forum threads
inline void register_bugreport_interactions(dpp::cluster& bot) {
    // handle the report bug button click — show modal
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        std::string id = event.custom_id;

        if (id.rfind("bugreport_start_", 0) == 0) {
            uint64_t btn_user_id = std::stoull(id.substr(16));
            uint64_t clicker_id = event.command.get_issuing_user().id;

            if (btn_user_id != clicker_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("this button is not for you")).set_flags(dpp::m_ephemeral));
                return;
            }

            dpp::interaction_modal_response modal("bugreport_modal", "bug report");

            modal.add_component(
                dpp::component()
                    .set_type(dpp::cot_text)
                    .set_label("What command or feature has the issue?")
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
                    .set_placeholder("step-by-step instructions to reproduce the issue...")
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
                    .set_placeholder("describe what you expected to happen...")
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
                    .set_placeholder("describe what actually happened instead...")
                    .set_min_length(5)
                    .set_max_length(500)
                    .set_required(true)
            );

            event.dialog(modal);
        }
    });

    // handle modal submission — create forum thread
    bot.on_form_submit([&bot](const dpp::form_submit_t& event) {
        if (event.custom_id != "bugreport_modal") {
            return;
        }

        uint64_t user_id = event.command.get_issuing_user().id;
        const dpp::user& reporter = event.command.get_issuing_user();

        // check cooldown
        auto now = std::chrono::system_clock::now();
        auto it = last_bugreport_time.find(user_id);
        if (it != last_bugreport_time.end() && now - it->second < std::chrono::seconds(120)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("please wait 2 minutes before submitting another bug report")).set_flags(dpp::m_ephemeral));
            return;
        }

        // extract form values
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

        // lowercase the feature name for the thread title
        std::string feature_lower = command_or_feature;
        std::transform(feature_lower.begin(), feature_lower.end(), feature_lower.begin(), ::tolower);

        // build the reporter display name
        std::string reporter_name = reporter.global_name.empty() ? reporter.username : reporter.global_name;

        // thread title: "feature — reporter"
        std::string thread_title = feature_lower + " \xE2\x80\x94 " + reporter_name;
        if (thread_title.size() > 100) {
            thread_title = thread_title.substr(0, 97) + "...";
        }

        // build the forum post embed
        std::string desc;
        desc += "\xF0\x9F\x94\xA7 **command / feature**\n";
        desc += command_or_feature + "\n\n";
        desc += "\xF0\x9F\x93\x8B **reproduction steps**\n";
        desc += reproduction_steps + "\n\n";
        desc += "\xE2\x9C\x85 **expected behavior**\n";
        desc += expected_behavior + "\n\n";
        desc += "\xE2\x9D\x8C **actual behavior**\n";
        desc += actual_behavior;

        dpp::embed embed = bronx::create_embed(desc, bronx::COLOR_WARNING);
        embed.set_footer(
            dpp::embed_footer()
                .set_text("reported by " + reporter_name + " (" + std::to_string(user_id) + ")")
                .set_icon(reporter.get_avatar_url())
        );

        // create the forum thread
        dpp::message forum_msg;
        forum_msg.add_embed(embed);

        bot.thread_create_in_forum(
            thread_title,
            BUG_REPORT_FORUM_ID,
            forum_msg,
            dpp::arc_1_day, // auto-archive after 24h of inactivity
            0,    // no slowmode
            {},   // no tags
            [&bot, event, user_id](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("something went wrong submitting your report \xE2\x80\x94 try again later")).set_flags(dpp::m_ephemeral));
                    return;
                }

                // update cooldown on success
                last_bugreport_time[user_id] = std::chrono::system_clock::now();

                dpp::embed confirm = bronx::create_embed(
                    bronx::EMOJI_CHECK + " bug report submitted \xE2\x80\x94 a forum thread has been created for tracking\n\n"
                    "thank you for helping improve the bot",
                    bronx::COLOR_SUCCESS
                );
                bronx::maybe_add_support_link(confirm);

                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(confirm).set_flags(dpp::m_ephemeral));
            }
        );
    });
}

} // namespace utility
} // namespace commands
