#pragma once
#include "../command.h"
#include "../embed_style.h"
#include "../database/core/database.h"
#include "../database/operations/community/patch_operations.h"
#include <dpp/dpp.h>
#include <map>
#include <sstream>
#include <iomanip>

namespace commands {

// Pagination state for patch notes view
struct PatchState {
    int current_page = 0;
};
static std::map<uint64_t, PatchState> patch_states;

// Helper to format timestamp
static std::string format_patch_timestamp(std::chrono::system_clock::time_point tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%B %d, %Y");
    return oss.str();
}

std::vector<Command*> get_patch_commands(bronx::db::Database* db) {
    static std::vector<Command*> commands;
    
    // /patch - view latest patch (everyone)
    static Command* view_patch = new Command(
        "patch",
        "View the latest bot updates and patch notes",
        "utility",
        {},
        true, // has slash command
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            auto latest = bronx::db::patch_operations::get_latest_patch(db);
            
            if (!latest.has_value()) {
                bronx::send_message(bot, event, bronx::error("No patch notes available yet!"));
                return;
            }
            
            std::ostringstream desc;
            desc << "**v" << latest->version << "**\n";
            desc << "*released " << format_patch_timestamp(latest->created_at) << "*\n\n";
            desc << latest->notes << "\n\n";
            desc << "━━━━━━━━━━━━━━━━━━━━\n";
            desc << "view all patches: `/patch history`";
            
            auto embed = bronx::create_embed(desc.str());
            bronx::add_invoker_footer(embed, event.msg.author);
            
            bronx::send_message(bot, event, embed);
        },
        // Slash handler
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Check subcommand
            std::string subcommand = "latest";
            if (!event.command.get_command_interaction().options.empty()) {
                subcommand = event.command.get_command_interaction().options[0].name;
            }
            
            if (subcommand == "history") {
                // Show paginated history
                auto patches = bronx::db::patch_operations::get_all_patches(db, 5, 0);
                int total = bronx::db::patch_operations::get_patch_count(db);
                
                if (patches.empty()) {
                    event.reply(dpp::message(event.command.channel_id, bronx::error("No patch notes available yet!")));
                    return;
                }
                
                std::ostringstream desc;
                desc << "**all patch notes**\n\n";
                
                for (const auto& patch : patches) {
                    desc << "**v" << patch.version << "** — " << format_patch_timestamp(patch.created_at) << "\n";
                    desc << patch.notes << "\n\n";
                    desc << "━━━━━━━━━━━━━━━━━━━━\n\n";
                }
                
                desc << "Page 1 of " << ((total - 1) / 5 + 1);
                
                auto embed = bronx::create_embed(desc.str());
                bronx::add_invoker_footer(embed, event.command.usr);
                
                // Create navigation buttons
                dpp::component nav_row;
                nav_row.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_emoji("◀️")
                        .set_style(dpp::cos_primary)
                        .set_disabled(true)
                        .set_id("patch_prev_" + std::to_string(event.command.usr.id) + "_0")
                );
                nav_row.add_component(
                    dpp::component()
                        .set_type(dpp::cot_button)
                        .set_emoji("▶️")
                        .set_style(dpp::cos_primary)
                        .set_disabled(total <= 5)
                        .set_id("patch_next_" + std::to_string(event.command.usr.id) + "_0")
                );
                
                dpp::message msg(event.command.channel_id, embed);
                msg.add_component(nav_row);
                
                event.reply(msg);
                return;
            }
            
            // Default to "latest" subcommand
            auto latest = bronx::db::patch_operations::get_latest_patch(db);
            
            if (!latest.has_value()) {
                event.reply(dpp::message(event.command.channel_id, bronx::error("No patch notes available yet!")));
                return;
            }
            
            std::ostringstream desc;
            desc << "**Version " << latest->version << "**\n";
            desc << "*Released " << format_patch_timestamp(latest->created_at) << "*\n\n";
            desc << latest->notes << "\n\n";
            desc << "━━━━━━━━━━━━━━━━━━━━\n";
            desc << "view all: `/patch history`";
            
            auto embed = bronx::create_embed(desc.str());
            bronx::add_invoker_footer(embed, event.command.usr);
            
            event.reply(dpp::message(event.command.channel_id, embed));
        },
        {  // inline options vector
            dpp::command_option(dpp::co_sub_command, "latest", "View the latest patch notes"),
            dpp::command_option(dpp::co_sub_command, "history", "Browse all previous patch notes")
        }
    );
    
    // .patchadd - owner only prefix command
    static Command* add_patch = new Command(
        "patchadd",
        "Add new patch notes (owner only)",
        "owner",
        {},
        false, // no slash command
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Owner check - defined in commands/owner.h
            extern bool is_owner(uint64_t);
            if (!is_owner(event.msg.author.id)) {
                bronx::send_message(bot, event, bronx::error("command is owner-only!"));
                return;
            }
            
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `.patchadd <patch notes>`\nexample: `.patchadd Added new leveling system with XP tracking`"));
                return;
            }
            
            // Join all args as the patch note
            std::string notes;
            for (size_t i = 0; i < args.size(); i++) {
                if (i > 0) notes += " ";
                notes += args[i];
            }
            
            if (notes.length() > 4000) {
                bronx::send_message(bot, event, bronx::error("patch notes too long! maximum 4000 characters."));
                return;
            }
            
            bool success = bronx::db::patch_operations::create_patch_note(db, notes, event.msg.author.id);
            
            if (success) {
                auto latest = bronx::db::patch_operations::get_latest_patch(db);
                
                std::ostringstream response;
                response << "**notes added!** - v" << latest->version << "\n\n";
                response << "**notes:** " << "\n" << notes << "\n\n";
                response << "users can view this with `/patch` or `.patch`";
                
                bronx::send_message(bot, event, bronx::success(response.str()));
            } else {
                bronx::send_message(bot, event, bronx::error("failed to add patch notes. check database connection."));
            }
        },
        nullptr // no slash handler
    );
    
    // .patchdelete - owner only prefix command to delete patches
    static Command* delete_patch = new Command(
        "patchdelete",
        "Delete a patch note by ID or version (owner only)",
        "owner",
        {"patchdel"},
        false, // no slash command
        // Text handler
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Owner check
            extern bool is_owner(uint64_t);
            if (!is_owner(event.msg.author.id)) {
                bronx::send_message(bot, event, bronx::error("command is owner-only!"));
                return;
            }
            
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("usage: `.patchdelete <id or version>`\nexample: `.patchdelete 7` or `.patchdelete 1.0.6`"));
                return;
            }
            
            bool success = false;
            std::string identifier = args[0];
            
            // Check if it's a number (ID) or version string
            if (std::all_of(identifier.begin(), identifier.end(), ::isdigit)) {
                // It's an ID
                uint32_t patch_id = std::stoul(identifier);
                success = bronx::db::patch_operations::delete_patch_by_id(db, patch_id);
                
                if (success) {
                    bronx::send_message(bot, event, bronx::success("✅ patch note #" + identifier + " deleted"));
                } else {
                    bronx::send_message(bot, event, bronx::error("patch note #" + identifier + " not found"));
                }
            } else {
                // It's a version string
                success = bronx::db::patch_operations::delete_patch_by_version(db, identifier);
                
                if (success) {
                    bronx::send_message(bot, event, bronx::success("✅ patch note v" + identifier + " deleted"));
                } else {
                    bronx::send_message(bot, event, bronx::error("patch note v" + identifier + " not found"));
                }
            }
        },
        nullptr // no slash handler
    );
    
    commands.push_back(view_patch);
    commands.push_back(add_patch);
    commands.push_back(delete_patch);
    
    return commands;
}

// Button handler for patch history pagination
inline void handle_patch_buttons(dpp::cluster& bot, const dpp::button_click_t& event, bronx::db::Database* db) {
    std::string custom_id = event.custom_id;
    
    if (custom_id.find("patch_") != 0) return;
    
    // Parse button ID: patch_prev_USERID_PAGE or patch_next_USERID_PAGE
    size_t second_underscore = custom_id.find('_', 6);
    size_t third_underscore = custom_id.find('_', second_underscore + 1);
    
    if (second_underscore == std::string::npos || third_underscore == std::string::npos) return;
    
    std::string action = custom_id.substr(6, second_underscore - 6);
    uint64_t user_id = std::stoull(custom_id.substr(second_underscore + 1, third_underscore - second_underscore - 1));
    int current_page = std::stoi(custom_id.substr(third_underscore + 1));
    
    // Verify user
    if (event.command.usr.id != user_id) {
        event.reply(dpp::ir_channel_message_with_source, 
            dpp::message("❌ This is not your pagination control!").set_flags(dpp::m_ephemeral));
        return;
    }
    
    // Calculate new page
    int new_page = current_page;
    if (action == "prev" && current_page > 0) {
        new_page--;
    } else if (action == "next") {
        // Check if there are more pages
        int total = bronx::db::patch_operations::get_patch_count(db);
        int max_page = (total - 1) / 5; // 5 patches per page
        if (current_page < max_page) {
            new_page++;
        }
    }
    
    if (new_page == current_page) {
        event.reply(dpp::ir_update_message, event.command.msg);
        return;
    }
    
    // Fetch page data
    auto patches = bronx::db::patch_operations::get_all_patches(db, 5, new_page * 5);
    int total = bronx::db::patch_operations::get_patch_count(db);
    
    if (patches.empty()) {
        event.reply(dpp::ir_update_message, event.command.msg);
        return;
    }
    
    std::ostringstream desc;
    desc << "📜 **All Patch Notes**\n\n";
    
    for (const auto& patch : patches) {
        desc << "**v" << patch.version << "** — " << format_patch_timestamp(patch.created_at) << "\n";
        desc << patch.notes << "\n\n";
        desc << "━━━━━━━━━━━━━━━━━━━━\n\n";
    }
    
    desc << "Page " << (new_page + 1) << " of " << ((total - 1) / 5 + 1);
    
    auto embed = bronx::create_embed(desc.str());
    bronx::add_invoker_footer(embed, event.command.usr);
    
    // Create navigation buttons
    dpp::component nav_row;
    nav_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_emoji("◀️")
            .set_style(dpp::cos_primary)
            .set_disabled(new_page == 0)
            .set_id("patch_prev_" + std::to_string(user_id) + "_" + std::to_string(new_page))
    );
    nav_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_emoji("▶️")
            .set_style(dpp::cos_primary)
            .set_disabled(new_page >= (total - 1) / 5)
            .set_id("patch_next_" + std::to_string(user_id) + "_" + std::to_string(new_page))
    );
    
    dpp::message msg(event.command.channel_id, embed);
    msg.add_component(nav_row);
    
    event.reply(dpp::ir_update_message, msg);
}

} // namespace commands
