#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../../core/database.h"  // for Database class definition

namespace bronx {
namespace db {

// Rows for module/command settings
struct GuildCommandSetting {
    uint64_t guild_id;
    std::string command;
    bool enabled;
};

struct GuildModuleSetting {
    uint64_t guild_id;
    std::string module;
    bool enabled;
};

// Scoped override row (channel/role/user level)
struct ScopedSetting {
    std::string name;        // command or module name
    std::string scope_type;  // "channel", "role", or "user"
    uint64_t scope_id;
    bool enabled;
    bool exclusive;
};

// Database methods for guild-level module/command toggles

// command settings
bool set_guild_command_enabled(Database* db, uint64_t guild_id, const std::string& command, bool enabled,
                               const std::string& scope_type = "guild", uint64_t scope_id = 0);
bool is_guild_command_enabled(Database* db, uint64_t guild_id, const std::string& command,
                               uint64_t user_id = 0, uint64_t channel_id = 0,
                               const std::vector<uint64_t>& roles = {});
std::vector<std::string> get_disabled_commands(Database* db, uint64_t guild_id);

// module settings
bool set_guild_module_enabled(Database* db, uint64_t guild_id, const std::string& module, bool enabled,
                              const std::string& scope_type = "guild", uint64_t scope_id = 0);
bool is_guild_module_enabled(Database* db, uint64_t guild_id, const std::string& module,
                              uint64_t user_id = 0, uint64_t channel_id = 0,
                              const std::vector<uint64_t>& roles = {});
std::vector<std::string> get_disabled_modules(Database* db, uint64_t guild_id);

// Fetch ALL settings (guild-wide and scoped overrides) for display
std::vector<GuildModuleSetting> get_all_module_settings(Database* db, uint64_t guild_id);
std::vector<GuildCommandSetting> get_all_command_settings(Database* db, uint64_t guild_id);
std::vector<ScopedSetting> get_all_module_scope_settings(Database* db, uint64_t guild_id);
std::vector<ScopedSetting> get_all_command_scope_settings(Database* db, uint64_t guild_id);

// User permission checks and management (uses guild_bot_staff table)
namespace permission_operations {
    // Check if user has admin/mod permissions
    // For server context: checks guild_bot_staff table
    // For global context: checks global database flags
    bool is_admin(Database* db, uint64_t user_id, uint64_t guild_id);
    bool is_mod(Database* db, uint64_t user_id, uint64_t guild_id);
    bool is_dev(Database* db, uint64_t user_id);  // Global only
    
    // Guild staff management (uses consolidated guild_bot_staff table)
    // role = "admin" or "mod"
    bool add_guild_staff(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& role, uint64_t granted_by);
    bool remove_guild_staff(Database* db, uint64_t guild_id, uint64_t user_id);
    bool is_guild_staff(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& role = "");
    std::vector<GuildBotStaffRow> get_guild_staff(Database* db, uint64_t guild_id, const std::string& role = "");
    
    // Global permission management (affects global database)
    bool set_global_admin(Database* db, uint64_t user_id, bool is_admin);
    bool set_global_mod(Database* db, uint64_t user_id, bool is_mod);
    bool set_dev(Database* db, uint64_t user_id, bool is_dev);
    
    // Get user permission status
    struct UserPermissions {
        bool admin;
        bool mod;
        bool dev;
        bool vip;
    };
    std::optional<UserPermissions> get_user_permissions(Database* db, uint64_t user_id);
    
    // Guild moderation config
    std::optional<GuildModerationConfig> get_guild_mod_config(Database* db, uint64_t guild_id);
    bool upsert_guild_mod_config(Database* db, const GuildModerationConfig& config);
}

} // namespace db
} // namespace bronx
