#pragma once

#include "../../command.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/logging_operations.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <string>

namespace commands {
namespace owner {

inline Command* get_log_beta_command(bronx::db::Database* db) {
    static Command* cmd = new Command(
        "logbeta",
        "Toggle beta tester status for a server (owner only)",
        "owner",
        {"betalog"},
        false, // is_slash
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args_vec) {
            
            if (event.msg.author.id != 205844421685477376ULL) {
                return;
            }

            dpp::snowflake target_guild = event.msg.guild_id;
            
            // Allow specifying a different guild ID as argument
            if (!args_vec.empty()) {
                try {
                    target_guild = std::stoull(args_vec[0]);
                } catch (...) {
                    event.reply(dpp::message().add_embed(bronx::create_embed("Invalid guild ID provided.")));
                    return;
                }
            }
            
            // Toggle the status
            bool current_status = bronx::db::logging_operations::is_guild_beta_tester(db, target_guild);
            bool new_status = !current_status;
            
            bool success = bronx::db::logging_operations::set_guild_beta_tester(db, target_guild, new_status);
            
            if (success) {
                std::string status_str = new_status ? "enabled" : "disabled";
                event.reply(dpp::message().add_embed(bronx::create_embed("Beta tester status **" + status_str + "** for server `" + std::to_string((uint64_t)target_guild) + "`")));
            } else {
                event.reply(dpp::message().add_embed(bronx::create_embed("Failed to update beta tester status in the database.")));
            }
        },
        nullptr, // no slash handler
        {}); // no options
        
    return cmd;
}

} // namespace owner
} // namespace commands
