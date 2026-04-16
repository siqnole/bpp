#pragma once

#include <dpp/dpp.h>
#include "database/core/database.h"
#include <string>
#include <vector>
#include <map>

namespace bronx {
namespace logger {

// Supported log types
constexpr const char* LOG_TYPE_MODERATION = "moderation";
constexpr const char* LOG_TYPE_MESSAGES   = "messages";
constexpr const char* LOG_TYPE_MEMBERS    = "members";
constexpr const char* LOG_TYPE_SERVER     = "server";
constexpr const char* LOG_TYPE_ECONOMY    = "economy";

class ServerLogger {
public:
    static ServerLogger& get();

    void init(dpp::cluster* bot, db::Database* db);

    // Send a message via webhook for a specific log type
    void log_to_guild(uint64_t guild_id, const std::string& log_type, const dpp::message& msg);
    
    // Send an embed directly
    void log_embed(uint64_t guild_id, const std::string& log_type, const dpp::embed& embed);

private:
    ServerLogger() = default;
    
    dpp::cluster* bot_ = nullptr;
    db::Database* db_ = nullptr;
};

} // namespace logger
} // namespace bronx
