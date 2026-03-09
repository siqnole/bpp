#pragma once
#include "../database/core/database.h"
#include "cache_manager.h"
#include <memory>

namespace bronx {
namespace cache {

// High-performance cached wrapper around Database operations
class CachedDatabase {
private:
    bronx::db::Database* db_;
    CacheManager* cache_;

public:
    explicit CachedDatabase(bronx::db::Database* db) : db_(db), cache_(global_cache.get()) {}
    
    // User blacklist operations with caching
    bool is_global_blacklisted(uint64_t user_id) {
        // Check cache first
        auto cached = cache_->is_blacklisted(user_id);
        if (cached) {
            return *cached;
        }
        
        // Cache miss - query database
        bool result = db_->is_global_blacklisted(user_id);
        cache_->set_blacklisted(user_id, result);
        return result;
    }
    
    bool add_global_blacklist(uint64_t user_id, const std::string& reason = "") {
        bool result = db_->add_global_blacklist(user_id, reason);
        if (result) {
            cache_->set_blacklisted(user_id, true);
        }
        return result;
    }
    
    bool remove_global_blacklist(uint64_t user_id) {
        bool result = db_->remove_global_blacklist(user_id);
        if (result) {
            cache_->invalidate_blacklist(user_id);
        }
        return result;
    }
    
    // User whitelist operations with caching
    bool is_global_whitelisted(uint64_t user_id) {
        auto cached = cache_->is_whitelisted(user_id);
        if (cached) {
            return *cached;
        }
        
        bool result = db_->is_global_whitelisted(user_id);
        cache_->set_whitelisted(user_id, result);
        return result;
    }
    
    bool add_global_whitelist(uint64_t user_id, const std::string& reason = "") {
        bool result = db_->add_global_whitelist(user_id, reason);
        if (result) {
            cache_->set_whitelisted(user_id, true);
        }
        return result;
    }
    
    bool remove_global_whitelist(uint64_t user_id) {
        bool result = db_->remove_global_whitelist(user_id);
        if (result) {
            cache_->invalidate_whitelist(user_id);
        }
        return result;
    }
    
    // Prefix operations with caching
    std::vector<std::string> get_user_prefixes(uint64_t user_id) {
        auto cached = cache_->get_user_prefixes(user_id);
        if (cached) {
            return *cached;
        }
        
        auto result = db_->get_user_prefixes(user_id);
        cache_->set_user_prefixes(user_id, result);
        return result;
    }
    
    std::vector<std::string> get_guild_prefixes(uint64_t guild_id) {
        auto cached = cache_->get_guild_prefixes(guild_id);
        if (cached) {
            return *cached;
        }
        
        auto result = db_->get_guild_prefixes(guild_id);
        cache_->set_guild_prefixes(guild_id, result);
        return result;
    }
    
    bool add_user_prefix(uint64_t user_id, const std::string& prefix) {
        bool result = db_->add_user_prefix(user_id, prefix);
        if (result) {
            cache_->invalidate_user_prefixes(user_id);
        }
        return result;
    }
    
    bool remove_user_prefix(uint64_t user_id, const std::string& prefix) {
        bool result = db_->remove_user_prefix(user_id, prefix);
        if (result) {
            cache_->invalidate_user_prefixes(user_id);
        }
        return result;
    }
    
    bool add_guild_prefix(uint64_t guild_id, const std::string& prefix) {
        bool result = db_->add_guild_prefix(guild_id, prefix);
        if (result) {
            cache_->invalidate_guild_prefixes(guild_id);
        }
        return result;
    }
    
    bool remove_guild_prefix(uint64_t guild_id, const std::string& prefix) {
        bool result = db_->remove_guild_prefix(guild_id, prefix);
        if (result) {
            cache_->invalidate_guild_prefixes(guild_id);
        }
        return result;
    }
    
    // Module/command enable status with caching
    // NOTE: The cache stores a simple guild_id:name → bool, but the real result
    // depends on the caller's channel, user, and roles (scoped overrides).
    // Therefore the cache is only used as a fast-path when NO context is
    // provided (guild-wide check).  When context IS provided we always query
    // the database so that channel/role/user scope overrides are respected.
    bool is_guild_module_enabled(uint64_t guild_id, const std::string& module,
                                uint64_t user_id = 0, uint64_t channel_id = 0,
                                const std::vector<uint64_t>& roles = {}) {
        bool has_context = (user_id != 0 || channel_id != 0 || !roles.empty());
        if (!has_context) {
            auto cached = cache_->is_module_enabled(guild_id, module);
            if (cached) return *cached;
        }
        bool result = db_->is_guild_module_enabled(guild_id, module, user_id, channel_id, roles);
        if (!has_context) {
            cache_->set_module_enabled(guild_id, module, result);
        }
        return result;
    }
    
    bool is_guild_command_enabled(uint64_t guild_id, const std::string& command,
                                 uint64_t user_id = 0, uint64_t channel_id = 0,
                                 const std::vector<uint64_t>& roles = {}) {
        bool has_context = (user_id != 0 || channel_id != 0 || !roles.empty());
        if (!has_context) {
            auto cached = cache_->is_command_enabled(guild_id, command);
            if (cached) return *cached;
        }
        bool result = db_->is_guild_command_enabled(guild_id, command, user_id, channel_id, roles);
        if (!has_context) {
            cache_->set_command_enabled(guild_id, command, result);
        }
        return result;
    }
    
    // Wrapper that updates DB and cache when command state changes
    bool set_guild_command_enabled(uint64_t guild_id, const std::string& command, bool enabled,
                                   const std::string& scope_type = "guild", uint64_t scope_id = 0, bool exclusive = false) {
        bool success = db_->set_guild_command_enabled(guild_id, command, enabled, scope_type, scope_id, exclusive);
        if (success) {
            // Always invalidate so the next lookup goes to DB with full context.
            // We can only safely cache guild-wide defaults (no scoped overrides).
            cache_->invalidate_command(guild_id, command);
        }
        return success;
    }
    
    bool set_guild_module_enabled(uint64_t guild_id, const std::string& module, bool enabled,
                                  const std::string& scope_type = "guild", uint64_t scope_id = 0, bool exclusive = false) {
        bool success = db_->set_guild_module_enabled(guild_id, module, enabled, scope_type, scope_id, exclusive);
        if (success) {
            cache_->invalidate_module(guild_id, module);
        }
        return success;
    }
    
    // Invalidate cached command/module state (e.g. when state changes externally)
    void invalidate_command_cache(uint64_t guild_id, const std::string& command) {
        cache_->invalidate_command(guild_id, command);
    }
    
    void invalidate_module_cache(uint64_t guild_id, const std::string& module) {
        cache_->invalidate_module(guild_id, module);
    }
    
    // Cooldown operations with caching
    bool is_on_cooldown(uint64_t user_id, const std::string& command) {
        // Check cache first
        if (cache_->is_on_cooldown(user_id, command)) {
            return true;
        }
        
        // Cache miss or expired - check database
        bool result = db_->is_on_cooldown(user_id, command);
        if (result) {
            // If on cooldown in DB, get the expiry time and cache it
            auto expiry = db_->get_cooldown_expiry(user_id, command);
            if (expiry) {
                auto chrono_expiry = std::chrono::steady_clock::now() + 
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        *expiry - std::chrono::system_clock::now());
                cache_->set_cooldown_expiry(user_id, command, chrono_expiry);
            }
        }
        return result;
    }
    
    bool set_cooldown(uint64_t user_id, const std::string& command, int seconds) {
        bool result = db_->set_cooldown(user_id, command, seconds);
        if (result) {
            auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            cache_->set_cooldown_expiry(user_id, command, expiry);
        }
        return result;
    }
    
    // Balance operations with caching
    int64_t get_wallet(uint64_t user_id) {
        auto cached = cache_->get_wallet(user_id);
        if (cached) {
            return *cached;
        }
        
        int64_t result = db_->get_wallet(user_id);
        cache_->set_wallet(user_id, result);
        return result;
    }
    
    int64_t get_bank(uint64_t user_id) {
        auto cached = cache_->get_bank(user_id);
        if (cached) {
            return *cached;
        }
        
        int64_t result = db_->get_bank(user_id);
        cache_->set_bank(user_id, result);
        return result;
    }
    
    std::optional<int64_t> update_wallet(uint64_t user_id, int64_t amount) {
        auto result = db_->update_wallet(user_id, amount);
        if (result) {
            cache_->set_wallet(user_id, *result);
        }
        return result;
    }
    
    std::optional<int64_t> update_bank(uint64_t user_id, int64_t amount) {
        auto result = db_->update_bank(user_id, amount);
        if (result) {
            cache_->set_bank(user_id, *result);
        }
        return result;
    }
    
    bool deposit(uint64_t user_id, int64_t amount) {
        bool result = db_->deposit(user_id, amount);
        if (result) {
            // Invalidate both wallet and bank since both changed
            cache_->invalidate_user_balance(user_id);
        }
        return result;
    }
    
    bool withdraw(uint64_t user_id, int64_t amount) {
        bool result = db_->withdraw(user_id, amount);
        if (result) {
            cache_->invalidate_user_balance(user_id);
        }
        return result;
    }
    
    // Transfer operations invalidate both users
    bronx::db::TransactionResult transfer_money(uint64_t from_user, uint64_t to_user, int64_t amount) {
        auto result = db_->transfer_money(from_user, to_user, amount);
        if (result == bronx::db::TransactionResult::Success) {
            cache_->invalidate_user_balance(from_user);
            cache_->invalidate_user_balance(to_user);
        }
        return result;
    }
    
    // Pass-through methods for operations that don't benefit from caching
    // or are too complex to cache safely
    
    std::optional<bronx::db::UserData> get_user(uint64_t user_id) {
        return db_->get_user(user_id);
    }
    
    bool create_user(uint64_t user_id) {
        return db_->create_user(user_id);
    }
    
    bool ensure_user_exists(uint64_t user_id) {
        return db_->ensure_user_exists(user_id);
    }
    
    // Add other methods as needed...
    // For brevity, I'm not wrapping every single database method, 
    // just the most performance-critical ones
    
    // Stat tracking - not cached since we want accurate counts
    bool increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount = 1) {
        return db_->increment_stat(user_id, stat_name, amount);
    }
    
    // Direct access to underlying database for complex operations
    bronx::db::Database* get_raw_db() { return db_; }
    
    // Cache management
    void invalidate_user_cache(uint64_t user_id) {
        cache_->invalidate_blacklist(user_id);
        cache_->invalidate_whitelist(user_id);
        cache_->invalidate_user_prefixes(user_id);
        cache_->invalidate_user_balance(user_id);
    }
    
    void invalidate_guild_cache(uint64_t guild_id) {
        cache_->invalidate_guild_prefixes(guild_id);
        // Note: Module/command cache invalidation would need pattern matching
        // which is complex, so we'll let those expire naturally
    }
    
    void periodic_cleanup() {
        cache_->periodic_cleanup();
    }
    
    // Cache statistics
    CacheManager::CacheStats get_cache_stats() const {
        return cache_->get_stats();
    }
};

} // namespace cache
} // namespace bronx