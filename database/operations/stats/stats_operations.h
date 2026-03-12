#pragma once

#include "../../core/database.h"
#include <cstdint>
#include <string>
#include <iostream>

namespace bronx {
namespace db {
namespace stats_operations {

// log a member join or leave event into guild_member_events
inline bool log_member_event(Database* db, uint64_t guild_id, uint64_t user_id,
                             const std::string& event_type) {
    if (!db) return false;
    try {
        std::string sql = "INSERT INTO guild_member_events (guild_id, user_id, event_type) VALUES ('"
            + std::to_string(guild_id) + "', '"
            + std::to_string(user_id) + "', '"
            + event_type + "')";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[stats] log_member_event failed: " << e.what() << "\n";
        return false;
    }
}

// log a message / edit / delete event into guild_message_events
inline bool log_message_event(Database* db, uint64_t guild_id, uint64_t user_id,
                              uint64_t channel_id, const std::string& event_type) {
    if (!db) return false;
    try {
        std::string sql = "INSERT INTO guild_message_events (guild_id, user_id, channel_id, event_type) VALUES ('"
            + std::to_string(guild_id) + "', '"
            + std::to_string(user_id) + "', '"
            + std::to_string(channel_id) + "', '"
            + event_type + "')";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[stats] log_message_event failed: " << e.what() << "\n";
        return false;
    }
}

// upsert command usage counter in guild_command_usage for today
inline bool increment_command_usage(Database* db, uint64_t guild_id,
                                    const std::string& command_name,
                                    uint64_t channel_id) {
    if (!db) return false;
    try {
        std::string sql = "INSERT INTO guild_command_usage (guild_id, command_name, channel_id, usage_date, use_count) VALUES ('"
            + std::to_string(guild_id) + "', '"
            + command_name + "', '"
            + std::to_string(channel_id) + "', CURDATE(), 1) "
            "ON DUPLICATE KEY UPDATE use_count = use_count + 1";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[stats] increment_command_usage failed: " << e.what() << "\n";
        return false;
    }
}

// log a voice channel join or leave event into guild_voice_events
inline bool log_voice_event(Database* db, uint64_t guild_id, uint64_t user_id,
                            uint64_t channel_id, const std::string& event_type) {
    if (!db) return false;
    try {
        std::string sql = "INSERT INTO guild_voice_events (guild_id, user_id, channel_id, event_type) VALUES ('"
            + std::to_string(guild_id) + "', '"
            + std::to_string(user_id) + "', '"
            + std::to_string(channel_id) + "', '"
            + event_type + "')";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[stats] log_voice_event failed: " << e.what() << "\n";
        return false;
    }
}

// log a server boost / unboost event into guild_boost_events
inline bool log_boost_event(Database* db, uint64_t guild_id, uint64_t user_id,
                            const std::string& event_type, const std::string& boost_id = "") {
    if (!db) return false;
    try {
        std::string sql = "INSERT INTO guild_boost_events (guild_id, user_id, event_type, boost_id) VALUES ('"
            + std::to_string(guild_id) + "', '"
            + std::to_string(user_id) + "', '"
            + event_type + "', '"
            + boost_id + "')";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[stats] log_boost_event failed: " << e.what() << "\n";
        return false;
    }
}

} // namespace stats_operations
} // namespace db
} // namespace bronx
