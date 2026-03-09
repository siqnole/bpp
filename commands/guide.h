#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../command_handler.h"
#include "../database/core/database.h"
#include "guide_data.h"
#include <dpp/dpp.h>
#include <algorithm>
#include <set>

namespace commands {

// ────────────────────────────────────────────────────────────────────────────
// helper: check if user has manage_guild permission (via role check)
// ────────────────────────────────────────────────────────────────────────────
inline bool has_manage_guild(const dpp::guild_member& member, uint64_t guild_id) {
    if (guild_id == 0) return false;
    
    // Check each role for manage_guild or administrator permission
    for (const auto& rid : member.get_roles()) {
        dpp::role* r = dpp::find_role(rid);
        if (r) {
            uint64_t perms = static_cast<uint64_t>(r->permissions);
            if ((perms & static_cast<uint64_t>(dpp::p_administrator)) ||
                (perms & static_cast<uint64_t>(dpp::p_manage_guild))) {
                return true;
            }
        }
    }
    return false;
}

// ────────────────────────────────────────────────────────────────────────────
// helper: track that a user read a guide section
// ────────────────────────────────────────────────────────────────────────────
inline void track_guide_read(bronx::db::Database* db, uint64_t user_id, const std::string& section_name) {
    if (!db) return;
    // store in stats table as "guide_read_<section>"
    std::string stat_key = "guide_read_" + section_name;
    // only increment once per section (check if already read)
    if (db->get_stat(user_id, stat_key) == 0) {
        db->increment_stat(user_id, stat_key, 1);
        db->increment_stat(user_id, "guide_sections_read", 1);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// helper: get guide progress for a user
// ────────────────────────────────────────────────────────────────────────────
struct GuideProgress {
    int sections_read;
    int total_sections;
    std::vector<std::string> read_sections;
    std::vector<std::string> unread_sections;
};

inline GuideProgress get_guide_progress(bronx::db::Database* db, uint64_t user_id, bool include_admin = false) {
    GuideProgress progress;
    progress.sections_read = 0;
    progress.total_sections = 0;
    
    auto sections = guide::get_guide_sections();
    for (const auto& section : sections) {
        if (section.admin_only && !include_admin) continue;
        progress.total_sections++;
        std::string stat_key = "guide_read_" + section.name;
        if (db && db->get_stat(user_id, stat_key) > 0) {
            progress.sections_read++;
            progress.read_sections.push_back(section.name);
        } else {
            progress.unread_sections.push_back(section.name);
        }
    }
    return progress;
}

// ────────────────────────────────────────────────────────────────────────────
// .guide / /guide — in-depth master guide to every aspect of the bot.
// uses a select menu → page navigation pattern similar to help.
// ────────────────────────────────────────────────────────────────────────────

Command* create_guide_command(bronx::db::Database* db) {
    static Command* guide_cmd = new Command("guide", "in-depth master guide to using the bot", "utility", {"g", "masterguide", "tutorial"}, true,

        // ── text command handler ────────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {

            bool is_admin = false;
            if (event.msg.guild_id != 0) {
                is_admin = has_manage_guild(event.msg.member, event.msg.guild_id);
            }

            auto sections = guide::get_guide_sections();

            // If an argument was supplied, check for special commands first
            if (!args.empty()) {
                ::std::string input;
                for (size_t i = 0; i < args.size(); i++) {
                    if (i > 0) input += " ";
                    input += args[i];
                }
                ::std::transform(input.begin(), input.end(), input.begin(), ::tolower);

                // ── search subcommand ──
                if (input.rfind("search ", 0) == 0) {
                    std::string query = input.substr(7); // "search "
                    if (query.empty()) {
                        bronx::send_message(bot, event,
                            bronx::error("usage: `.guide search <query>`"));
                        return;
                    }
                    
                    auto results = guide::search_guide(query, is_admin);
                    if (results.empty()) {
                        bronx::send_message(bot, event,
                            bronx::error("no results found for `" + query + "`"));
                        return;
                    }
                    
                    // show top 5 results
                    std::string desc = "**search results for:** `" + query + "`\n\n";
                    int count = 0;
                    for (const auto& r : results) {
                        if (count >= 5) break;
                        desc += "**" + std::to_string(count + 1) + ".** " + r.section_name + " → " + r.page_title + "\n";
                        desc += "> " + r.match_context + "\n\n";
                        count++;
                    }
                    desc += "_use `.guide <section>` to view a section_";
                    
                    auto embed = bronx::create_embed(desc);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
                
                // ── progress subcommand ──
                if (input == "progress") {
                    auto progress = get_guide_progress(db, event.msg.author.id, is_admin);
                    
                    std::string desc = "**your guide progress**\n\n";
                    desc += "📖 **" + std::to_string(progress.sections_read) + "/" + std::to_string(progress.total_sections) + "** sections read\n\n";
                    
                    if (!progress.read_sections.empty()) {
                        desc += "✅ **completed:**\n";
                        for (const auto& s : progress.read_sections) {
                            desc += "> " + s + "\n";
                        }
                        desc += "\n";
                    }
                    
                    if (!progress.unread_sections.empty()) {
                        desc += "📋 **not yet read:**\n";
                        for (const auto& s : progress.unread_sections) {
                            desc += "> " + s + "\n";
                        }
                    }
                    
                    // badge status
                    if (progress.sections_read == progress.total_sections && progress.total_sections > 0) {
                        desc += "\n🏆 **guide master badge earned!**";
                    } else {
                        int remaining = progress.total_sections - progress.sections_read;
                        desc += "\n_read " + std::to_string(remaining) + " more section(s) to earn the guide master badge_";
                    }
                    
                    auto embed = bronx::create_embed(desc);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }

                // ── section lookup ──
                for (size_t si = 0; si < sections.size(); si++) {
                    const auto& section = sections[si];
                    
                    // skip admin sections for non-admins
                    if (section.admin_only && !is_admin) continue;
                    
                    ::std::string section_lower = section.name;
                    ::std::transform(section_lower.begin(), section_lower.end(), section_lower.begin(), ::tolower);

                    if (section_lower == input || section_lower.find(input) != ::std::string::npos) {
                        // Track this read
                        track_guide_read(db, event.msg.author.id, section.name);
                        
                        const auto& page = section.pages[0];

                        auto embed = bronx::create_embed(page.content);
                        bronx::add_invoker_footer(embed, event.msg.author);

                        dpp::message msg(event.msg.channel_id, embed);

                        if (section.pages.size() > 1) {
                            dpp::component nav_row;
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_emoji("◀️")
                                    .set_style(dpp::cos_primary)
                                    .set_id("guide_prev_" + ::std::to_string(event.msg.author.id) + "_" + ::std::to_string(si) + "_0")
                            );
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_label(::std::to_string(1) + "/" + ::std::to_string(section.pages.size()))
                                    .set_style(dpp::cos_secondary)
                                    .set_id("guide_counter_" + ::std::to_string(event.msg.author.id))
                                    .set_disabled(true)
                            );
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_emoji("▶️")
                                    .set_style(dpp::cos_primary)
                                    .set_id("guide_next_" + ::std::to_string(event.msg.author.id) + "_" + ::std::to_string(si) + "_0")
                            );
                            msg.add_component(nav_row);
                        }

                        dpp::component back_row;
                        back_row.add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_emoji("📖")
                                .set_label("back to guide menu")
                                .set_style(dpp::cos_secondary)
                                .set_id("guide_home_" + ::std::to_string(event.msg.author.id))
                        );
                        msg.add_component(back_row);

                        bronx::send_message(bot, event, msg);
                        return;
                    }
                }

                // Not found
                bronx::send_message(bot, event,
                    bronx::error("guide section `" + input + "` not found — use `.guide` to see all topics"));
                return;
            }

            // ── Main guide menu ──
            auto progress = get_guide_progress(db, event.msg.author.id, is_admin);
            
            ::std::string description = "**bronx — master guide**\n\n"
                "everything you need to know about the bot in one place — select a topic below "
                "to learn how each system works, how to use it effectively, and how to get the most "
                "out of your time.\n\n";

            for (const auto& section : sections) {
                if (section.admin_only && !is_admin) continue;
                
                // show checkmark if read
                std::string stat_key = "guide_read_" + section.name;
                bool is_read = db && db->get_stat(event.msg.author.id, stat_key) > 0;
                std::string check = is_read ? " ✓" : "";
                
                description += section.emoji + " **" + section.name + "**" + check + " — " + section.description + "\n";
            }

            description += "\n📊 **progress:** " + std::to_string(progress.sections_read) + "/" + std::to_string(progress.total_sections) + " sections read";
            description += "\n\n`.guide <topic>` — jump to section\n`.guide search <query>` — search the guide\n`.guide progress` — view your progress";

            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);

            // Build select menu (only non-admin sections for non-admins)
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a guide topic")
                .set_id("guide_section_" + ::std::to_string(event.msg.author.id));

            for (size_t i = 0; i < sections.size(); i++) {
                if (sections[i].admin_only && !is_admin) continue;
                
                ::std::string page_count = ::std::to_string(sections[i].pages.size()) + " page" + (sections[i].pages.size() > 1 ? "s" : "");
                select_menu.add_select_option(
                    dpp::select_option(sections[i].name, ::std::to_string(i), page_count)
                        .set_emoji(sections[i].emoji)
                );
            }

            dpp::message msg(event.msg.channel_id, embed);
            msg.add_component(dpp::component().add_component(select_menu));

            bronx::send_message(bot, event, msg);
        },

        // ── slash command handler ───────────────────────────────────────
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {

            bool is_admin = false;
            if (event.command.guild_id != 0) {
                is_admin = has_manage_guild(event.command.member, event.command.guild_id);
            }

            auto sections = guide::get_guide_sections();

            // Check for topic parameter
            auto topic_param = event.get_parameter("topic");
            if (::std::holds_alternative<::std::string>(topic_param)) {
                ::std::string input = ::std::get<::std::string>(topic_param);
                ::std::transform(input.begin(), input.end(), input.begin(), ::tolower);

                // ── search subcommand ──
                if (input.rfind("search ", 0) == 0) {
                    std::string query = input.substr(7);
                    if (query.empty()) {
                        event.reply(dpp::message().add_embed(
                            bronx::error("usage: `/guide topic:search <query>`")));
                        return;
                    }
                    
                    auto results = guide::search_guide(query, is_admin);
                    if (results.empty()) {
                        event.reply(dpp::message().add_embed(
                            bronx::error("no results found for `" + query + "`")));
                        return;
                    }
                    
                    std::string desc = "**search results for:** `" + query + "`\n\n";
                    int count = 0;
                    for (const auto& r : results) {
                        if (count >= 5) break;
                        desc += "**" + std::to_string(count + 1) + ".** " + r.section_name + " → " + r.page_title + "\n";
                        desc += "> " + r.match_context + "\n\n";
                        count++;
                    }
                    desc += "_use `/guide topic:<section>` to view a section_";
                    
                    auto embed = bronx::create_embed(desc);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }
                
                // ── progress subcommand ──
                if (input == "progress") {
                    auto progress = get_guide_progress(db, event.command.get_issuing_user().id, is_admin);
                    
                    std::string desc = "**your guide progress**\n\n";
                    desc += "📖 **" + std::to_string(progress.sections_read) + "/" + std::to_string(progress.total_sections) + "** sections read\n\n";
                    
                    if (!progress.read_sections.empty()) {
                        desc += "✅ **completed:**\n";
                        for (const auto& s : progress.read_sections) {
                            desc += "> " + s + "\n";
                        }
                        desc += "\n";
                    }
                    
                    if (!progress.unread_sections.empty()) {
                        desc += "📋 **not yet read:**\n";
                        for (const auto& s : progress.unread_sections) {
                            desc += "> " + s + "\n";
                        }
                    }
                    
                    if (progress.sections_read == progress.total_sections && progress.total_sections > 0) {
                        desc += "\n🏆 **guide master badge earned!**";
                    } else {
                        int remaining = progress.total_sections - progress.sections_read;
                        desc += "\n_read " + std::to_string(remaining) + " more section(s) to earn the guide master badge_";
                    }
                    
                    auto embed = bronx::create_embed(desc);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    event.reply(dpp::message().add_embed(embed));
                    return;
                }

                for (size_t si = 0; si < sections.size(); si++) {
                    const auto& section = sections[si];
                    if (section.admin_only && !is_admin) continue;
                    
                    ::std::string section_lower = section.name;
                    ::std::transform(section_lower.begin(), section_lower.end(), section_lower.begin(), ::tolower);

                    if (section_lower == input || section_lower.find(input) != ::std::string::npos) {
                        track_guide_read(db, event.command.get_issuing_user().id, section.name);
                        
                        const auto& page = section.pages[0];

                        auto embed = bronx::create_embed(page.content);
                        bronx::add_invoker_footer(embed, event.command.get_issuing_user());

                        dpp::message msg;
                        msg.add_embed(embed);

                        if (section.pages.size() > 1) {
                            dpp::component nav_row;
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_emoji("◀️")
                                    .set_style(dpp::cos_primary)
                                    .set_id("guide_prev_" + ::std::to_string(event.command.get_issuing_user().id) + "_" + ::std::to_string(si) + "_0")
                            );
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_label("1/" + ::std::to_string(section.pages.size()))
                                    .set_style(dpp::cos_secondary)
                                    .set_id("guide_counter_" + ::std::to_string(event.command.get_issuing_user().id))
                                    .set_disabled(true)
                            );
                            nav_row.add_component(
                                dpp::component()
                                    .set_type(dpp::cot_button)
                                    .set_emoji("▶️")
                                    .set_style(dpp::cos_primary)
                                    .set_id("guide_next_" + ::std::to_string(event.command.get_issuing_user().id) + "_" + ::std::to_string(si) + "_0")
                            );
                            msg.add_component(nav_row);
                        }

                        dpp::component back_row;
                        back_row.add_component(
                            dpp::component()
                                .set_type(dpp::cot_button)
                                .set_emoji("📖")
                                .set_label("back to guide menu")
                                .set_style(dpp::cos_secondary)
                                .set_id("guide_home_" + ::std::to_string(event.command.get_issuing_user().id))
                        );
                        msg.add_component(back_row);

                        event.reply(msg);
                        return;
                    }
                }

                event.reply(dpp::message().add_embed(
                    bronx::error("guide section `" + input + "` not found — use `/guide` to see all topics")));
                return;
            }

            // ── Main guide menu ──
            auto progress = get_guide_progress(db, event.command.get_issuing_user().id, is_admin);
            
            ::std::string description = "**bronx — master guide**\n\n"
                "everything you need to know about the bot in one place — select a topic below "
                "to learn how each system works, how to use it effectively, and how to get the most "
                "out of your time.\n\n";

            for (const auto& section : sections) {
                if (section.admin_only && !is_admin) continue;
                
                std::string stat_key = "guide_read_" + section.name;
                bool is_read = db && db->get_stat(event.command.get_issuing_user().id, stat_key) > 0;
                std::string check = is_read ? " ✓" : "";
                
                description += section.emoji + " **" + section.name + "**" + check + " — " + section.description + "\n";
            }

            description += "\n📊 **progress:** " + std::to_string(progress.sections_read) + "/" + std::to_string(progress.total_sections) + " sections read";
            description += "\n\n`/guide topic:<section>` — jump to section\n`/guide topic:search <query>` — search\n`/guide topic:progress` — view progress";

            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());

            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a guide topic")
                .set_id("guide_section_" + ::std::to_string(event.command.get_issuing_user().id));

            for (size_t i = 0; i < sections.size(); i++) {
                if (sections[i].admin_only && !is_admin) continue;
                
                ::std::string page_count = ::std::to_string(sections[i].pages.size()) + " page" + (sections[i].pages.size() > 1 ? "s" : "");
                select_menu.add_select_option(
                    dpp::select_option(sections[i].name, ::std::to_string(i), page_count)
                        .set_emoji(sections[i].emoji)
                );
            }

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(dpp::component().add_component(select_menu));

            event.reply(msg);
        },
        {dpp::command_option(dpp::co_string, "topic", "topic, 'search <query>', or 'progress'", false)}
    );

    return guide_cmd;
}


// ────────────────────────────────────────────────────────────────────────────
// register interaction handlers (select menu + buttons)
// ────────────────────────────────────────────────────────────────────────────

void register_guide_interactions(dpp::cluster& bot, bronx::db::Database* db) {

    // ── select menu: pick a section ─────────────────────────────────────
    bot.on_select_click([&bot, db](const dpp::select_click_t& event) {
        if (event.custom_id.find("guide_section_") != 0) return;

        // Check admin status
        bool is_admin = false;
        if (event.command.guild_id != 0) {
            is_admin = has_manage_guild(event.command.member, event.command.guild_id);
        }

        auto sections = guide::get_guide_sections();
        int section_idx = 0;
        try {
            section_idx = ::std::stoi(event.values[0]);
        } catch (...) {
            event.reply(dpp::ir_update_message,
                dpp::message().add_embed(bronx::error("invalid section")));
            return;
        }

        if (section_idx < 0 || section_idx >= (int)sections.size()) {
            event.reply(dpp::ir_update_message,
                dpp::message().add_embed(bronx::error("section not found")));
            return;
        }

        const auto& section = sections[section_idx];
        
        // Block non-admins from admin-only sections
        if (section.admin_only && !is_admin) {
            event.reply(dpp::ir_update_message,
                dpp::message().add_embed(bronx::error("this section requires admin permissions")));
            return;
        }

        // Track this read
        track_guide_read(db, event.command.usr.id, section.name);
        
        const auto& page = section.pages[0];

        ::std::string user_id_str = event.custom_id.substr(14); // "guide_section_"

        auto embed = bronx::create_embed(page.content);
        bronx::add_invoker_footer(embed, event.command.usr);

        dpp::message msg;
        msg.add_embed(embed);

        // Page navigation
        if (section.pages.size() > 1) {
            dpp::component nav_row;
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_emoji("◀️")
                    .set_style(dpp::cos_primary)
                    .set_id("guide_prev_" + user_id_str + "_" + ::std::to_string(section_idx) + "_0")
            );
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_label("1/" + ::std::to_string(section.pages.size()))
                    .set_style(dpp::cos_secondary)
                    .set_id("guide_counter_" + user_id_str)
                    .set_disabled(true)
            );
            nav_row.add_component(
                dpp::component()
                    .set_type(dpp::cot_button)
                    .set_emoji("▶️")
                    .set_style(dpp::cos_primary)
                    .set_id("guide_next_" + user_id_str + "_" + ::std::to_string(section_idx) + "_0")
            );
            msg.add_component(nav_row);
        }

        // Section select menu (only show sections user can access)
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a guide topic")
            .set_id("guide_section_" + user_id_str);

        for (size_t i = 0; i < sections.size(); i++) {
            if (sections[i].admin_only && !is_admin) continue;
            ::std::string page_count = ::std::to_string(sections[i].pages.size()) + " page" + (sections[i].pages.size() > 1 ? "s" : "");
            select_menu.add_select_option(
                dpp::select_option(sections[i].name, ::std::to_string(i), page_count)
                    .set_emoji(sections[i].emoji)
            );
        }
        msg.add_component(dpp::component().add_component(select_menu));

        event.reply(dpp::ir_update_message, msg);
    });

    // ── button clicks: page navigation + back to menu ───────────────────
    bot.on_button_click([&bot, db](const dpp::button_click_t& event) {
        const auto& cid = event.custom_id;

        // ── back to menu ──
        if (cid.find("guide_home_") == 0) {
            ::std::string user_id_str = cid.substr(11); // "guide_home_"
            
            bool is_admin = false;
            if (event.command.guild_id != 0) {
                is_admin = has_manage_guild(event.command.member, event.command.guild_id);
            }

            auto sections = guide::get_guide_sections();
            auto progress = get_guide_progress(db, event.command.usr.id, is_admin);

            ::std::string description = "**bronx — master guide**\n\n"
                "everything you need to know about the bot in one place — select a topic below "
                "to learn how each system works, how to use it effectively, and how to get the most "
                "out of your time.\n\n";

            for (const auto& section : sections) {
                if (section.admin_only && !is_admin) continue;
                
                std::string stat_key = "guide_read_" + section.name;
                bool is_read = db && db->get_stat(event.command.usr.id, stat_key) > 0;
                std::string check = is_read ? " ✓" : "";
                
                description += section.emoji + " **" + section.name + "**" + check + " — " + section.description + "\n";
            }

            description += "\n📊 **progress:** " + std::to_string(progress.sections_read) + "/" + std::to_string(progress.total_sections) + " sections read";
            description += "\n\n`.guide <topic>` — jump to section\n`.guide search <query>` — search\n`.guide progress` — view progress";

            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.usr);

            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a guide topic")
                .set_id("guide_section_" + user_id_str);

            for (size_t i = 0; i < sections.size(); i++) {
                if (sections[i].admin_only && !is_admin) continue;
                ::std::string page_count = ::std::to_string(sections[i].pages.size()) + " page" + (sections[i].pages.size() > 1 ? "s" : "");
                select_menu.add_select_option(
                    dpp::select_option(sections[i].name, ::std::to_string(i), page_count)
                        .set_emoji(sections[i].emoji)
                );
            }

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(dpp::component().add_component(select_menu));

            event.reply(dpp::ir_update_message, msg);
            return;
        }

        // ── page navigation ──
        bool is_prev = (cid.find("guide_prev_") == 0);
        bool is_next = (cid.find("guide_next_") == 0);
        if (!is_prev && !is_next) return;

        bool is_admin = false;
        if (event.command.guild_id != 0) {
            is_admin = has_manage_guild(event.command.member, event.command.guild_id);
        }

        // Parse: guide_prev_<user_id>_<section_idx>_<page_idx>
        size_t prefix_len = is_next ? 11 : 11; // "guide_next_" or "guide_prev_"
        ::std::string remainder = cid.substr(prefix_len);

        // user_id
        size_t first_underscore = remainder.find('_');
        if (first_underscore == ::std::string::npos) return;
        ::std::string user_id_str = remainder.substr(0, first_underscore);
        remainder = remainder.substr(first_underscore + 1);

        // section_idx
        size_t second_underscore = remainder.find('_');
        if (second_underscore == ::std::string::npos) return;
        int section_idx = 0;
        int current_page = 0;
        try {
            section_idx = ::std::stoi(remainder.substr(0, second_underscore));
            current_page = ::std::stoi(remainder.substr(second_underscore + 1));
        } catch (...) { return; }

        auto sections = guide::get_guide_sections();
        if (section_idx < 0 || section_idx >= (int)sections.size()) return;

        const auto& section = sections[section_idx];
        
        // Block non-admins from admin-only sections
        if (section.admin_only && !is_admin) return;
        
        int total_pages = (int)section.pages.size();

        int new_page = current_page;
        if (is_next) {
            new_page = (current_page + 1) % total_pages;
        } else {
            new_page = (current_page - 1 + total_pages) % total_pages;
        }

        const auto& page = section.pages[new_page];
        auto embed = bronx::create_embed(page.content);
        bronx::add_invoker_footer(embed, event.command.usr);

        dpp::message msg;
        msg.add_embed(embed);

        // Navigation buttons
        dpp::component nav_row;
        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_emoji("◀️")
                .set_style(dpp::cos_primary)
                .set_id("guide_prev_" + user_id_str + "_" + ::std::to_string(section_idx) + "_" + ::std::to_string(new_page))
        );
        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label(::std::to_string(new_page + 1) + "/" + ::std::to_string(total_pages))
                .set_style(dpp::cos_secondary)
                .set_id("guide_counter_" + user_id_str)
                .set_disabled(true)
        );
        nav_row.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_emoji("▶️")
                .set_style(dpp::cos_primary)
                .set_id("guide_next_" + user_id_str + "_" + ::std::to_string(section_idx) + "_" + ::std::to_string(new_page))
        );
        msg.add_component(nav_row);

        // Section select menu (only show sections user can access)
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a guide topic")
            .set_id("guide_section_" + user_id_str);

        for (size_t i = 0; i < sections.size(); i++) {
            if (sections[i].admin_only && !is_admin) continue;
            ::std::string page_count = ::std::to_string(sections[i].pages.size()) + " page" + (sections[i].pages.size() > 1 ? "s" : "");
            select_menu.add_select_option(
                dpp::select_option(sections[i].name, ::std::to_string(i), page_count)
                    .set_emoji(sections[i].emoji)
            );
        }
        msg.add_component(dpp::component().add_component(select_menu));

        event.reply(dpp::ir_update_message, msg);
    });
}

// ────────────────────────────────────────────────────────────────────────────
// helper for profile badge: check if user has completed the guide
// ────────────────────────────────────────────────────────────────────────────
inline bool has_guide_master_badge(bronx::db::Database* db, uint64_t user_id) {
    if (!db) return false;
    auto progress = get_guide_progress(db, user_id, false); // only count public sections
    return progress.sections_read == progress.total_sections && progress.total_sections > 0;
}

} // namespace commands
