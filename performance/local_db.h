#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <chrono>
#include "../database/core/database.h"

namespace bronx {
namespace local {

/**
 * @brief Local SQLite-based cache for frequent operations.
 * This reduces latency and database load by keeping hot data locally.
 */
class LocalDB {
public:
    struct TTL {
        int64_t user_data = 300;     // 5 mins
        int64_t inventory = 300;     // 5 mins
        int64_t stats = 300;         // 5 mins
        int64_t shop_items = 3600;    // 1 hour
        int64_t api_cache = 1800;    // 30 mins
        int64_t blacklist = 3600;    // 1 hour
        int64_t whitelist = 3600;    // 1 hour
        int64_t prefixes = 3600;     // 1 hour
        int64_t guild_toggles = 600;  // 10 mins
    };

    struct CacheStats {
        int user_entries;
        int inventory_entries;
        int stat_entries;
        int shop_entries;
        int api_entries;
        int pending_wallet_deltas;
        int pending_bank_deltas;
    };

    struct ScopeEntry {
        std::string scope_type;
        uint64_t scope_id;
        bool enabled;
        bool exclusive;
    };

    LocalDB(const std::string& path) : db_path_(path) {}
    ~LocalDB() { if (db_) sqlite3_close(db_); }

    bool initialize();

    // User Data Cache
    void cache_user(const bronx::db::UserData& u);
    std::optional<bronx::db::UserData> get_cached_user(uint64_t user_id);
    std::optional<int64_t> get_cached_wallet(uint64_t user_id);
    std::optional<int64_t> get_cached_bank(uint64_t user_id);
    
    // Delta Tracking (updates local cache + marks for sync to remote)
    int64_t apply_wallet_delta(uint64_t user_id, int64_t delta);
    int64_t apply_bank_delta(uint64_t user_id, int64_t delta);
    std::unordered_map<uint64_t, int64_t> drain_wallet_deltas();
    std::unordered_map<uint64_t, int64_t> drain_bank_deltas();

    // Inventory Cache
    void cache_inventory(uint64_t user_id, const std::vector<bronx::db::InventoryItem>& items);
    std::optional<std::vector<bronx::db::InventoryItem>> get_cached_inventory(uint64_t user_id);
    std::optional<int> get_cached_item_quantity(uint64_t user_id, const std::string& item_id);

    // Stats Cache
    void cache_stat(uint64_t user_id, const std::string& stat_name, int64_t value);
    std::optional<int64_t> get_cached_stat(uint64_t user_id, const std::string& stat_name);

    // Shop Cache
    void cache_shop_items(const std::vector<bronx::db::ShopItem>& items);
    std::optional<std::vector<bronx::db::ShopItem>> get_cached_shop_items();
    std::optional<bronx::db::ShopItem> get_cached_shop_item(const std::string& item_id);

    // API Cache
    void cache_api_response(const std::string& key, const std::string& json);
    std::optional<std::string> get_cached_api_response(const std::string& key);

    // Invalidation
    void invalidate_user(uint64_t user_id);
    void invalidate_inventory(uint64_t user_id);
    void invalidate_stats(uint64_t user_id);
    void invalidate_shop();
    void invalidate_api_cache(const std::string& key = "");
    void flush_all();

    // Pre-check Caches
    void cache_blacklist(uint64_t user_id, bool is_blacklisted);
    std::optional<bool> is_blacklisted(uint64_t user_id);
    void cache_whitelist(uint64_t user_id, bool is_whitelisted);
    std::optional<bool> is_whitelisted(uint64_t user_id);
    
    void cache_user_prefixes(uint64_t user_id, const std::vector<std::string>& prefixes);
    std::optional<std::vector<std::string>> get_user_prefixes(uint64_t user_id);
    void cache_guild_prefixes(uint64_t guild_id, const std::vector<std::string>& prefixes);
    std::optional<std::vector<std::string>> get_guild_prefixes(uint64_t guild_id);

    // Toggle Caches
    bool has_fresh_guild_toggles(uint64_t guild_id);
    void cache_guild_toggles(uint64_t guild_id,
                             const std::unordered_map<std::string, bool>& module_defaults,
                             const std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>>& module_scopes,
                             const std::unordered_map<std::string, bool>& command_defaults,
                             const std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>>& command_scopes);
    
    std::optional<bool> get_module_default(uint64_t guild_id, const std::string& module);
    std::optional<bool> get_command_default(uint64_t guild_id, const std::string& command);
    std::vector<ScopeEntry> get_module_scopes(uint64_t guild_id, const std::string& module);
    std::vector<ScopeEntry> get_command_scopes(uint64_t guild_id, const std::string& command);
    
    bool evaluate_module_enabled(uint64_t guild_id, const std::string& module, uint64_t user_id, uint64_t channel_id, const std::vector<uint64_t>& roles);
    bool evaluate_command_enabled(uint64_t guild_id, const std::string& command, uint64_t user_id, uint64_t channel_id, const std::vector<uint64_t>& roles);
    
    void invalidate_guild_toggles(uint64_t guild_id);

    // Maintenance
    void cleanup_expired();
    CacheStats get_stats();

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex db_mutex_;
    TTL ttl_;
    bool initialized_ = false;

    void init_schema();
    
    inline bool exec(const char* sql) {
        return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    void exec_bind(const char* sql, int64_t p1, int64_t p2);

    inline int64_t now_unix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    inline bool is_fresh(int64_t fetched_at, int64_t ttl) {
        return (now_unix() - fetched_at) < ttl;
    }

    int64_t get_pending_wallet_delta_unlocked(uint64_t user_id);
    int64_t get_pending_bank_delta_unlocked(uint64_t user_id);

    std::vector<std::string> parse_string_array_json(const std::string& json);
};

} // namespace local
} // namespace bronx
