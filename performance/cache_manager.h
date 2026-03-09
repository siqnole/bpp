#pragma once
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace bronx {
namespace cache {

// Thread-safe cache with TTL support
template<typename K, typename V>
class TTLCache {
private:
    struct CacheEntry {
        V value;
        std::chrono::steady_clock::time_point expiry;
    };
    
    mutable std::shared_mutex mutex_;
    std::unordered_map<K, CacheEntry> cache_;
    std::chrono::milliseconds default_ttl_;

public:
    explicit TTLCache(std::chrono::milliseconds ttl = std::chrono::minutes(5))
        : default_ttl_(ttl) {}
    
    void set(const K& key, const V& value, std::chrono::milliseconds ttl = std::chrono::milliseconds(0)) {
        std::unique_lock lock(mutex_);
        auto expiry = std::chrono::steady_clock::now() + (ttl.count() > 0 ? ttl : default_ttl_);
        cache_[key] = {value, expiry};
    }
    
    std::optional<V> get(const K& key) {
        std::shared_lock lock(mutex_);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }
        
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            // Entry expired — return nullopt without upgrading to an exclusive
            // lock.  Attempting a shared→exclusive upgrade on the same
            // shared_mutex triggers EDEADLK on Linux's pthread rwlock even after
            // explicit unlock(), causing the "Resource deadlock avoided"
            // system_error in DPP's thread pool.  Expired entries will be
            // removed by the next cleanup() pass.
            return std::nullopt;
        }
        
        return it->second.value;
    }
    
    void invalidate(const K& key) {
        std::unique_lock lock(mutex_);
        cache_.erase(key);
    }
    
    void clear() {
        std::unique_lock lock(mutex_);
        cache_.clear();
    }
    
    size_t size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }
    
    // Clean up expired entries
    void cleanup() {
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now > it->second.expiry) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// High-performance cache manager for bot data
class CacheManager {
private:
    // User-related caches
    TTLCache<uint64_t, bool> blacklist_cache_;
    TTLCache<uint64_t, bool> whitelist_cache_;
    TTLCache<uint64_t, std::vector<std::string>> user_prefixes_cache_;
    TTLCache<std::string, std::chrono::steady_clock::time_point> cooldown_cache_;
    
    // Guild-related caches
    TTLCache<uint64_t, std::vector<std::string>> guild_prefixes_cache_;
    TTLCache<std::string, bool> guild_module_cache_; // "guild_id:module" -> enabled
    TTLCache<std::string, bool> guild_command_cache_; // "guild_id:command" -> enabled
    
    // Balance caches for frequently accessed data
    TTLCache<uint64_t, int64_t> wallet_cache_;
    TTLCache<uint64_t, int64_t> bank_cache_;
    
    std::shared_mutex cleanup_mutex_;
    std::chrono::steady_clock::time_point last_cleanup_;

public:
    CacheManager() : 
        blacklist_cache_(std::chrono::minutes(10)),  // blacklist rarely changes
        whitelist_cache_(std::chrono::minutes(10)),  // whitelist rarely changes  
        user_prefixes_cache_(std::chrono::minutes(5)), // user prefixes don't change often
        cooldown_cache_(std::chrono::minutes(1)),    // cooldowns are short-lived
        guild_prefixes_cache_(std::chrono::minutes(5)), // guild prefixes don't change often
        guild_module_cache_(std::chrono::minutes(15)), // module settings rarely change
        guild_command_cache_(std::chrono::minutes(15)), // command settings rarely change
        wallet_cache_(std::chrono::seconds(30)),     // balances change frequently
        bank_cache_(std::chrono::seconds(30)),       // bank balances change frequently
        last_cleanup_(std::chrono::steady_clock::now()) {}
    
    // Blacklist operations
    void set_blacklisted(uint64_t user_id, bool blacklisted) {
        blacklist_cache_.set(user_id, blacklisted);
    }
    
    std::optional<bool> is_blacklisted(uint64_t user_id) {
        return blacklist_cache_.get(user_id);
    }
    
    void invalidate_blacklist(uint64_t user_id) {
        blacklist_cache_.invalidate(user_id);
    }
    
    // Whitelist operations
    void set_whitelisted(uint64_t user_id, bool whitelisted) {
        whitelist_cache_.set(user_id, whitelisted);
    }
    
    std::optional<bool> is_whitelisted(uint64_t user_id) {
        return whitelist_cache_.get(user_id);
    }
    
    void invalidate_whitelist(uint64_t user_id) {
        whitelist_cache_.invalidate(user_id);
    }
    
    // User prefix operations
    void set_user_prefixes(uint64_t user_id, const std::vector<std::string>& prefixes) {
        user_prefixes_cache_.set(user_id, prefixes);
    }
    
    std::optional<std::vector<std::string>> get_user_prefixes(uint64_t user_id) {
        return user_prefixes_cache_.get(user_id);
    }
    
    void invalidate_user_prefixes(uint64_t user_id) {
        user_prefixes_cache_.invalidate(user_id);
    }
    
    // Guild prefix operations
    void set_guild_prefixes(uint64_t guild_id, const std::vector<std::string>& prefixes) {
        guild_prefixes_cache_.set(guild_id, prefixes);
    }
    
    std::optional<std::vector<std::string>> get_guild_prefixes(uint64_t guild_id) {
        return guild_prefixes_cache_.get(guild_id);
    }
    
    void invalidate_guild_prefixes(uint64_t guild_id) {
        guild_prefixes_cache_.invalidate(guild_id);
    }
    
    // Module/command enable status
    std::string make_module_key(uint64_t guild_id, const std::string& module) {
        return std::to_string(guild_id) + ":" + module;
    }
    
    std::string make_command_key(uint64_t guild_id, const std::string& command) {
        return std::to_string(guild_id) + ":" + command;
    }
    
    void set_module_enabled(uint64_t guild_id, const std::string& module, bool enabled) {
        guild_module_cache_.set(make_module_key(guild_id, module), enabled);
    }
    
    std::optional<bool> is_module_enabled(uint64_t guild_id, const std::string& module) {
        return guild_module_cache_.get(make_module_key(guild_id, module));
    }
    
    void set_command_enabled(uint64_t guild_id, const std::string& command, bool enabled) {
        guild_command_cache_.set(make_command_key(guild_id, command), enabled);
    }
    
    std::optional<bool> is_command_enabled(uint64_t guild_id, const std::string& command) {
        return guild_command_cache_.get(make_command_key(guild_id, command));
    }
    
    void invalidate_command(uint64_t guild_id, const std::string& command) {
        guild_command_cache_.invalidate(make_command_key(guild_id, command));
    }
    
    void invalidate_module(uint64_t guild_id, const std::string& module) {
        guild_module_cache_.invalidate(make_module_key(guild_id, module));
    }
    
    // Cooldown operations
    void set_cooldown_expiry(uint64_t user_id, const std::string& command, std::chrono::steady_clock::time_point expiry) {
        std::string key = std::to_string(user_id) + ":" + command;
        cooldown_cache_.set(key, expiry);
    }
    
    std::optional<std::chrono::steady_clock::time_point> get_cooldown_expiry(uint64_t user_id, const std::string& command) {
        std::string key = std::to_string(user_id) + ":" + command;
        return cooldown_cache_.get(key);
    }
    
    bool is_on_cooldown(uint64_t user_id, const std::string& command) {
        auto expiry = get_cooldown_expiry(user_id, command);
        if (!expiry) return false;
        return std::chrono::steady_clock::now() < *expiry;
    }
    
    // Balance caching
    void set_wallet(uint64_t user_id, int64_t amount) {
        wallet_cache_.set(user_id, amount);
    }
    
    std::optional<int64_t> get_wallet(uint64_t user_id) {
        return wallet_cache_.get(user_id);
    }
    
    void invalidate_wallet(uint64_t user_id) {
        wallet_cache_.invalidate(user_id);
    }
    
    void set_bank(uint64_t user_id, int64_t amount) {
        bank_cache_.set(user_id, amount);
    }
    
    std::optional<int64_t> get_bank(uint64_t user_id) {
        return bank_cache_.get(user_id);
    }
    
    void invalidate_bank(uint64_t user_id) {
        bank_cache_.invalidate(user_id);
    }
    
    void invalidate_user_balance(uint64_t user_id) {
        invalidate_wallet(user_id);
        invalidate_bank(user_id);
    }
    
    // Periodic cleanup
    void periodic_cleanup() {
        std::unique_lock lock(cleanup_mutex_);
        auto now = std::chrono::steady_clock::now();
        
        // Only cleanup every 5 minutes to avoid performance impact
        if (now - last_cleanup_ < std::chrono::minutes(5)) {
            return;
        }
        
        blacklist_cache_.cleanup();
        whitelist_cache_.cleanup();
        user_prefixes_cache_.cleanup();
        cooldown_cache_.cleanup();
        guild_prefixes_cache_.cleanup();
        guild_module_cache_.cleanup();
        guild_command_cache_.cleanup();
        wallet_cache_.cleanup();
        bank_cache_.cleanup();
        
        last_cleanup_ = now;
    }
    
    // Cache statistics
    struct CacheStats {
        size_t blacklist_entries;
        size_t whitelist_entries;
        size_t user_prefixes_entries;
        size_t cooldown_entries;
        size_t guild_prefixes_entries;
        size_t module_entries;
        size_t command_entries;
        size_t wallet_entries;
        size_t bank_entries;
        size_t total_entries;
    };
    
    CacheStats get_stats() const {
        CacheStats stats{};
        stats.blacklist_entries = blacklist_cache_.size();
        stats.whitelist_entries = whitelist_cache_.size();
        stats.user_prefixes_entries = user_prefixes_cache_.size();
        stats.cooldown_entries = cooldown_cache_.size();
        stats.guild_prefixes_entries = guild_prefixes_cache_.size();
        stats.module_entries = guild_module_cache_.size();
        stats.command_entries = guild_command_cache_.size();
        stats.wallet_entries = wallet_cache_.size();
        stats.bank_entries = bank_cache_.size();
        stats.total_entries = stats.blacklist_entries + stats.whitelist_entries +
                            stats.user_prefixes_entries + stats.cooldown_entries +
                            stats.guild_prefixes_entries + stats.module_entries +
                            stats.command_entries + stats.wallet_entries + stats.bank_entries;
        return stats;
    }
};

// Global cache instance
extern std::unique_ptr<CacheManager> global_cache;

// Initialize cache system
void initialize_cache();

// Shutdown cache system
void shutdown_cache();

} // namespace cache
} // namespace bronx