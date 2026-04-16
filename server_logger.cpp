#include "server_logger.h"
#include "database/operations/moderation/logging_operations.h"
#include "log.h"
#include <iostream>

namespace bronx {
namespace logger {

ServerLogger& ServerLogger::get() {
    static ServerLogger instance;
    return instance;
}

void ServerLogger::init(dpp::cluster* bot, db::Database* db) {
    bot_ = bot;
    db_ = db;
}

void ServerLogger::log_to_guild(uint64_t guild_id, const std::string& log_type, const dpp::message& msg) {
    if (!bot_ || !db_) return;

    // Fetch the stored webhook config for this log type
    auto config = db::logging_operations::get_log_config(db_, guild_id, log_type);
    if (!config || !config->enabled) {
        return; // Logging not configured or disabled for this type
    }
    
    dpp::webhook wh(config->webhook_id, config->webhook_token);
    
    bot_->execute_webhook(wh, msg, false, 0, "", [guild_id, log_type](const dpp::confirmation_callback_t& ev) {
        if (ev.is_error()) {
            std::cerr << "[WRN] Failed to dispatch webhook log for guild " << guild_id 
                      << " type: " << log_type << " (" << ev.get_error().message << ")" << std::endl;
            // Additional logic to disable broken webhooks can be placed here if desirable.
        }
    });
}

void ServerLogger::log_embed(uint64_t guild_id, const std::string& log_type, const dpp::embed& embed) {
    dpp::message msg;
    msg.add_embed(embed);
    // Set a generic fallback display name/avatar for the webhook message if you want, 
    // or let it use the bot's default webhook profile.
    log_to_guild(guild_id, log_type, msg);
}

} // namespace logger
} // namespace bronx
