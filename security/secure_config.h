#pragma once
// ============================================================================
// SecureConfig — Centralized secret/config loading from environment variables.
// NEVER hardcode credentials.  All secrets MUST come from env vars.
// Missing required secrets cause a hard abort at startup.
// ============================================================================

#include "../database/core/types.h"
#include <string>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>

namespace bronx {
namespace security {

// Read an environment variable; returns empty string if unset.
inline std::string get_env(const char* name) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string();
}

// Read an environment variable; abort with clear message if missing/empty.
inline std::string require_env(const char* name) {
    const char* val = std::getenv(name);
    if (!val || std::string(val).empty()) {
        std::cerr << "\033[1;31m✘ FATAL: required environment variable "
                  << name << " is not set.\033[0m\n";
        std::cerr << "  Set it via: export " << name << "=<value>\n";
        std::cerr << "  Or add it to your systemd EnvironmentFile / .env\n";
        std::abort();
    }
    return std::string(val);
}

// Read a required env var as uint16_t.
inline uint16_t require_env_port(const char* name) {
    std::string val = require_env(name);
    try {
        int port = std::stoi(val);
        if (port < 1 || port > 65535) throw std::out_of_range("port");
        return static_cast<uint16_t>(port);
    } catch (...) {
        std::cerr << "\033[1;31m✘ FATAL: " << name << " is not a valid port number.\033[0m\n";
        std::abort();
    }
}

// Read an optional env var as int with a default.
inline int get_env_int(const char* name, int default_val) {
    const char* val = std::getenv(name);
    if (!val || std::string(val).empty()) return default_val;
    try {
        return std::stoi(std::string(val));
    } catch (...) {
        std::cerr << "\033[33m⚠ warning: " << name
                  << " is not a valid integer, using default " << default_val << "\033[0m\n";
        return default_val;
    }
}

// Read an optional env var as bool (truthy: 1, true, yes, on).
inline bool get_env_bool(const char* name, bool default_val = false) {
    const char* val = std::getenv(name);
    if (!val) return default_val;
    char c = val[0];
    return (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
}

// Load DatabaseConfig entirely from environment variables.
// Falls back to JSON file ONLY for non-secret fields if the env vars are set.
// The password MUST come from an env var.
inline db::DatabaseConfig load_secure_db_config() {
    db::DatabaseConfig config;

    // Allow loading from env vars with DB_ prefix
    // If DB_HOST is set, we use full env-based config
    std::string host = get_env("DB_HOST");
    if (!host.empty()) {
        config.host = host;
        config.port = static_cast<uint16_t>(get_env_int("DB_PORT", 3306));
        config.database = get_env("DB_DATABASE").empty() ? "bronxbot" : get_env("DB_DATABASE");
        config.user = get_env("DB_USER").empty() ? "root" : get_env("DB_USER");
        config.password = require_env("DB_PASSWORD");
        config.pool_size = static_cast<uint32_t>(get_env_int("DB_POOL_SIZE", 10));
        config.timeout_seconds = static_cast<uint32_t>(get_env_int("DB_TIMEOUT", 10));
        config.log_connections = get_env_bool("DB_LOG_CONNECTIONS");
        return config;
    }

    // Fall back to file-based config, but the file should NOT contain
    // the password — it should come from DB_PASSWORD env var.
    // This path is kept for backward compatibility during migration.
    std::cerr << "\033[33m⚠ DB_HOST not set — falling back to config file. "
              << "Migrate to env vars for production.\033[0m\n";
    return config;  // caller will load from file
}

// Mask a secret for safe logging (show first 3 chars + asterisks).
inline std::string mask_secret(const std::string& secret) {
    if (secret.size() <= 3) return "***";
    return secret.substr(0, 3) + std::string(secret.size() - 3, '*');
}

// Load owner IDs from environment (comma-separated list).
inline std::set<uint64_t> load_owner_ids() {
    std::set<uint64_t> ids;
    std::string val = get_env("BOT_OWNER_IDS");
    if (val.empty()) {
        // Fall back to single owner ID
        val = get_env("BOT_OWNER_ID");
    }
    if (val.empty()) return ids;

    // Parse comma-separated IDs
    size_t pos = 0;
    while (pos < val.size()) {
        size_t comma = val.find(',', pos);
        if (comma == std::string::npos) comma = val.size();
        std::string id_str = val.substr(pos, comma - pos);
        // trim
        while (!id_str.empty() && id_str.front() == ' ') id_str.erase(id_str.begin());
        while (!id_str.empty() && id_str.back() == ' ') id_str.pop_back();
        if (!id_str.empty()) {
            try {
                ids.insert(std::stoull(id_str));
            } catch (...) {
                std::cerr << "\033[33m⚠ invalid owner ID: " << id_str << "\033[0m\n";
            }
        }
        pos = comma + 1;
    }
    return ids;
}

// Validate that critical environment is configured.  Call at startup.
// Returns list of warnings (non-fatal).
inline std::vector<std::string> validate_security_config() {
    std::vector<std::string> warnings;

    if (get_env("BRONX_ENCRYPTION_KEY").empty()) {
        warnings.push_back("BRONX_ENCRYPTION_KEY not set — using insecure default encryption key");
    }
    if (get_env("BOT_OWNER_IDS").empty() && get_env("BOT_OWNER_ID").empty()) {
        warnings.push_back("BOT_OWNER_IDS not set — owner commands will be disabled");
    }
    if (get_env("BOT_TOKEN").empty() && get_env("DISCORD_TOKEN").empty() && get_env("TOKEN").empty()) {
        warnings.push_back("No bot token env var found (BOT_TOKEN / DISCORD_TOKEN / TOKEN)");
    }

    return warnings;
}

} // namespace security
} // namespace bronx
