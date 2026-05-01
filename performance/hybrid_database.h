#pragma once
// ---------------------------------------------------------------------------
// HybridDatabase — Unified database layer combining:
//   1. Local SQLite cache (sub-ms reads)
//   2. Remote MariaDB (source of truth)
//   3. Write batch queue (non-blocking remote writes)
//   4. API cache client (leaderboards / aggregations)
//   5. TTL in-memory cache (existing CacheManager)
//
// Read path:  LocalDB → CacheManager → Remote DB (populate local on miss)
// Write path: LocalDB (immediate) → WriteBatchQueue → Remote DB (background)
// Aggregations: API client → LocalDB api_cache → Remote DB (fallback)
//
// This replaces CachedDatabase as the primary interface for commands.
// ---------------------------------------------------------------------------

#include "../database/core/database.h"
#include "cache_manager.h"
#include "local_db.h"
#include "write_batch_queue.h"
#include "api_cache_client.h"
#include <memory>
#include <iostream>
#include "../utils/logger.h"

namespace bronx {
namespace hybrid {

class HybridDatabase {
private:
    bronx::db::Database* db_;                    // remote MariaDB
    bronx::cache::CacheManager* cache_;          // in-memory TTL cache
    bronx::local::LocalDB* local_db_;            // local SQLite cache
    bronx::batch::WriteBatchQueue* batch_;        // write batch queue
    bronx::api::ApiCacheClient* api_;            // site API client

public:
    HybridDatabase(bronx::db::Database* db,
                   bronx::cache::CacheManager* cache,
                   bronx::local::LocalDB* local_db,
                   bronx::batch::WriteBatchQueue* batch,
                   bronx::api::ApiCacheClient* api)
        : db_(db), cache_(cache), local_db_(local_db), batch_(batch), api_(api) {}

    // =====================================================================
    // USER DATA — 3-tier read: local SQLite → remote DB (populate local)
    // Hot path: ~84 calls across 30 files
    // =====================================================================

    std::optional<bronx::db::UserData> get_user(uint64_t user_id) {
        // Tier 1: Local SQLite cache
        if (local_db_) {
            auto cached = local_db_->get_cached_user(user_id);
            if (cached) return cached;
        }

        // Tier 2: Remote DB (cold path)
        auto result = db_->get_user(user_id);
        if (result && local_db_) {
            local_db_->cache_user(*result);
        }
        return result;
    }

    bool ensure_user_exists(uint64_t user_id) {
        return db_->ensure_user_exists(user_id);
    }

    bool create_user(uint64_t user_id) {
        return db_->create_user(user_id);
    }

    // =====================================================================
    // WALLET — local-first with delta tracking
    // Hot path: get_wallet (52 calls, 22 files), update_wallet (161 calls, 48 files)
    // =====================================================================

    int64_t get_wallet(uint64_t user_id) {
        // Tier 1: In-memory cache (30s TTL, fastest)
        if (cache_) {
            auto cached = cache_->get_wallet(user_id);
            if (cached) return *cached;
        }

        // Tier 2: Local SQLite (sub-ms, includes pending deltas)
        if (local_db_) {
            auto cached = local_db_->get_cached_wallet(user_id);
            if (cached) {
                if (cache_) cache_->set_wallet(user_id, *cached);
                return *cached;
            }
        }

        // Tier 3: Remote DB (cold path, ~40-100ms)
        int64_t result = db_->get_wallet(user_id);
        if (cache_) cache_->set_wallet(user_id, result);
        // Populate local cache with the full user for future reads
        auto user = db_->get_user(user_id);
        if (user && local_db_) local_db_->cache_user(*user);
        return result;
    }

    int64_t get_bank(uint64_t user_id) {
        if (cache_) {
            auto cached = cache_->get_bank(user_id);
            if (cached) return *cached;
        }
        if (local_db_) {
            auto cached = local_db_->get_cached_bank(user_id);
            if (cached) {
                if (cache_) cache_->set_bank(user_id, *cached);
                return *cached;
            }
        }
        int64_t result = db_->get_bank(user_id);
        if (cache_) cache_->set_bank(user_id, result);
        auto user = db_->get_user(user_id);
        if (user && local_db_) local_db_->cache_user(*user);
        return result;
    }

    int64_t get_bank_limit(uint64_t user_id) {
        return db_->get_bank_limit(user_id);
    }

    // FAST wallet update: write to local immediately, queue remote batch
    std::optional<int64_t> update_wallet(uint64_t user_id, int64_t amount) {
        // Optimistic local update (sub-ms)
        int64_t new_balance = 0;
        if (local_db_) {
            new_balance = local_db_->apply_wallet_delta(user_id, amount);
            // Update in-memory cache too
            if (cache_) cache_->set_wallet(user_id, new_balance);
        }

        // Queue the delta for remote batch write (non-blocking)
        if (batch_) {
            batch_->enqueue_wallet_delta(user_id, amount);
            return new_balance;
        }

        // Fallback: synchronous remote write (no batch queue available)
        auto result = db_->update_wallet(user_id, amount);
        if (result && cache_) cache_->set_wallet(user_id, *result);
        return result;
    }

    std::optional<int64_t> update_bank(uint64_t user_id, int64_t amount) {
        int64_t new_balance = 0;
        if (local_db_) {
            new_balance = local_db_->apply_bank_delta(user_id, amount);
            if (cache_) cache_->set_bank(user_id, new_balance);
        }
        if (batch_) {
            batch_->enqueue_bank_delta(user_id, amount);
            return new_balance;
        }
        auto result = db_->update_bank(user_id, amount);
        if (result && cache_) cache_->set_bank(user_id, *result);
        return result;
    }

    bool deposit(uint64_t user_id, int64_t amount) {
        // Deposit = wallet - amount, bank + amount
        // SECURITY FIX: Validate balances before applying deltas locally.
        // Without validation, users could deposit more than their wallet holds
        // (negative wallet) or exceed their bank limit.
        if (local_db_ && batch_) {
            // Read current local balances for validation
            int64_t current_wallet = get_wallet(user_id);
            int64_t current_bank = get_bank(user_id);
            int64_t bank_limit_val = get_bank_limit(user_id);

            // Validate: wallet must have enough
            if (current_wallet < amount) return false;
            // Validate: bank must not exceed limit
            if (current_bank + amount > bank_limit_val) return false;

            local_db_->apply_wallet_delta(user_id, -amount);
            local_db_->apply_bank_delta(user_id, amount);
            if (cache_) cache_->invalidate_user_balance(user_id);
            batch_->enqueue_wallet_delta(user_id, -amount);
            batch_->enqueue_bank_delta(user_id, amount);
            return true;
        }
        // Fallback to synchronous (remote DB validates atomically)
        bool result = db_->deposit(user_id, amount);
        if (result && cache_) cache_->invalidate_user_balance(user_id);
        if (result && local_db_) local_db_->invalidate_user(user_id);
        return result;
    }

    bool withdraw(uint64_t user_id, int64_t amount) {
        // SECURITY FIX: Validate bank balance before allowing withdrawal.
        if (local_db_ && batch_) {
            int64_t current_bank = get_bank(user_id);

            // Validate: bank must have enough
            if (current_bank < amount) return false;

            local_db_->apply_wallet_delta(user_id, amount);
            local_db_->apply_bank_delta(user_id, -amount);
            if (cache_) cache_->invalidate_user_balance(user_id);
            batch_->enqueue_wallet_delta(user_id, amount);
            batch_->enqueue_bank_delta(user_id, -amount);
            return true;
        }
        bool result = db_->withdraw(user_id, amount);
        if (result && cache_) cache_->invalidate_user_balance(user_id);
        if (result && local_db_) local_db_->invalidate_user(user_id);
        return result;
    }

    bronx::db::TransactionResult transfer_money(uint64_t from_user, uint64_t to_user, int64_t amount) {
        // Transfers need atomicity — use remote DB directly but update local after
        auto result = db_->transfer_money(from_user, to_user, amount);
        if (result == bronx::db::TransactionResult::Success) {
            if (cache_) {
                cache_->invalidate_user_balance(from_user);
                cache_->invalidate_user_balance(to_user);
            }
            if (local_db_) {
                local_db_->invalidate_user(from_user);
                local_db_->invalidate_user(to_user);
            }
        }
        return result;
    }

    int64_t get_networth(uint64_t user_id) {
        return db_->get_networth(user_id);
    }

    int64_t get_total_networth(uint64_t user_id) {
        return db_->get_total_networth(user_id);
    }

    // =====================================================================
    // INVENTORY — cached locally, mutations batched
    // Hot path: get_inventory (44 calls, 19 files)
    // =====================================================================

    std::vector<bronx::db::InventoryItem> get_inventory(uint64_t user_id) {
        // Tier 1: Local SQLite
        if (local_db_) {
            auto cached = local_db_->get_cached_inventory(user_id);
            if (cached) return *cached;
        }

        // Tier 2: Remote DB (cold path)
        auto result = db_->get_inventory(user_id);
        if (local_db_) {
            local_db_->cache_inventory(user_id, result);
        }
        return result;
    }

    bool has_item(uint64_t user_id, const std::string& item_id, int quantity = 1) {
        // Quick check from local cache
        if (local_db_) {
            auto qty = local_db_->get_cached_item_quantity(user_id, item_id);
            if (qty) return *qty >= quantity;
        }
        return db_->has_item(user_id, item_id, quantity);
    }

    int get_item_quantity(uint64_t user_id, const std::string& item_id) {
        if (local_db_) {
            auto qty = local_db_->get_cached_item_quantity(user_id, item_id);
            if (qty) return *qty;
        }
        return db_->get_item_quantity(user_id, item_id);
    }

    std::optional<bronx::db::InventoryItem> get_item(uint64_t user_id, const std::string& item_id) {
        return db_->get_item(user_id, item_id);
    }

    bool add_item(uint64_t user_id, const std::string& item_id, const std::string& item_type,
                  int quantity, const std::string& category = "", int level = 1) {
        // Invalidate local inventory cache (will be refreshed on next read)
        if (local_db_) local_db_->invalidate_inventory(user_id);

        // Queue for remote batch write
        if (batch_) {
            batch_->enqueue_add_item(user_id, item_id, item_type, quantity, category, level);
            return true;
        }

        return db_->add_item(user_id, item_id, item_type, quantity, category, level);
    }

    bool remove_item(uint64_t user_id, const std::string& item_id, int quantity = 1) {
        if (local_db_) local_db_->invalidate_inventory(user_id);

        if (batch_) {
            batch_->enqueue_remove_item(user_id, item_id, quantity);
            return true;
        }

        return db_->remove_item(user_id, item_id, quantity);
    }

    // =====================================================================
    // STATS — cached locally
    // Hot path: get_stat (43 calls, 12 files), increment_stat (61 calls)
    // =====================================================================

    int64_t get_stat(uint64_t user_id, const std::string& stat_name) {
        // Tier 1: Local SQLite
        if (local_db_) {
            auto cached = local_db_->get_cached_stat(user_id, stat_name);
            if (cached) return *cached;
        }

        // Tier 2: Remote DB
        int64_t result = db_->get_stat(user_id, stat_name);
        if (local_db_) local_db_->cache_stat(user_id, stat_name, result);
        return result;
    }

    bool increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount = 1) {
        // Update local cache optimistically
        if (local_db_) {
            auto current = local_db_->get_cached_stat(user_id, stat_name);
            if (current) {
                local_db_->cache_stat(user_id, stat_name, *current + amount);
            }
        }

        // Queue for remote batch
        if (batch_) {
            batch_->enqueue_stat_delta(user_id, stat_name, amount);
            return true;
        }

        return db_->increment_stat(user_id, stat_name, amount);
    }

    // =====================================================================
    // PRESTIGE — cached locally
    // Hot path: get_prestige (35 calls, 14 files)
    // =====================================================================

    int get_prestige(uint64_t user_id) {
        // Check local user cache
        if (local_db_) {
            auto user = local_db_->get_cached_user(user_id);
            if (user) return user->prestige;
        }
        return db_->get_prestige(user_id);
    }

    bool is_passive(uint64_t user_id) {
        if (local_db_) {
            auto user = local_db_->get_cached_user(user_id);
            if (user) return user->passive;
        }
        return db_->is_passive(user_id);
    }

    // =====================================================================
    // SHOP ITEMS — cached locally (rarely change, 5min TTL)
    // =====================================================================

    std::vector<bronx::db::ShopItem> get_shop_items() {
        if (local_db_) {
            auto cached = local_db_->get_cached_shop_items();
            if (cached) return *cached;
        }
        auto result = db_->get_shop_items();
        if (local_db_) local_db_->cache_shop_items(result);
        return result;
    }

    std::optional<bronx::db::ShopItem> get_shop_item(const std::string& item_id) {
        if (local_db_) {
            auto cached = local_db_->get_cached_shop_item(item_id);
            if (cached) return cached;
        }
        return db_->get_shop_item(item_id);
    }

    // =====================================================================
    // COOLDOWNS — use in-memory cache (already cached by CachedDatabase)
    // =====================================================================

    bool is_on_cooldown(uint64_t user_id, const std::string& command) {
        if (cache_ && cache_->is_on_cooldown(user_id, command)) return true;
        bool result = db_->is_on_cooldown(user_id, command);
        if (result && cache_) {
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
        if (result && cache_) {
            auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            cache_->set_cooldown_expiry(user_id, command, expiry);
        }
        return result;
    }

    bool try_claim_cooldown(uint64_t user_id, const std::string& command, int seconds) {
        return db_->try_claim_cooldown(user_id, command, seconds);
    }

    std::optional<std::chrono::system_clock::time_point> get_cooldown_expiry(uint64_t user_id, const std::string& command) {
        return db_->get_cooldown_expiry(user_id, command);
    }

    // =====================================================================
    // BLACKLIST/WHITELIST — 3-tier: CacheManager → LocalDB → Remote DB
    // Critical hot path — checked on EVERY command
    // =====================================================================

    bool is_global_blacklisted(uint64_t user_id) {
        // Tier 0: In-memory cache (fastest, no lock)
        if (cache_) {
            auto cached = cache_->is_blacklisted(user_id);
            if (cached) return *cached;
        }
        // Tier 1: Local SQLite (sub-ms)
        if (local_db_) {
            auto cached = local_db_->is_blacklisted(user_id);
            if (cached) {
                // Promote to in-memory cache
                if (cache_) cache_->set_blacklisted(user_id, *cached);
                return *cached;
            }
        }
        // Tier 2: Remote DB (cold path)
        bool result = db_->is_global_blacklisted(user_id);
        if (cache_) cache_->set_blacklisted(user_id, result);
        if (local_db_) local_db_->cache_blacklist(user_id, result);
        return result;
    }

    bool is_global_whitelisted(uint64_t user_id) {
        // Tier 0: In-memory cache (fastest, no lock)
        if (cache_) {
            auto cached = cache_->is_whitelisted(user_id);
            if (cached) return *cached;
        }
        // Tier 1: Local SQLite (sub-ms)
        if (local_db_) {
            auto cached = local_db_->is_whitelisted(user_id);
            if (cached) {
                // Promote to in-memory cache
                if (cache_) cache_->set_whitelisted(user_id, *cached);
                return *cached;
            }
        }
        // Tier 2: Remote DB (cold path)
        bool result = db_->is_global_whitelisted(user_id);
        if (cache_) cache_->set_whitelisted(user_id, result);
        if (local_db_) local_db_->cache_whitelist(user_id, result);
        return result;
    }

    // =====================================================================
    // PREFIX — 3-tier: CacheManager → LocalDB → Remote DB
    // =====================================================================

    std::vector<std::string> get_user_prefixes(uint64_t user_id) {
        // Tier 0: In-memory cache (fastest)
        if (cache_) {
            auto cached = cache_->get_user_prefixes(user_id);
            if (cached) return *cached;
        }
        // Tier 1: Local SQLite
        if (local_db_) {
            auto cached = local_db_->get_user_prefixes(user_id);
            if (cached) {
                // Promote to in-memory cache
                if (cache_) cache_->set_user_prefixes(user_id, *cached);
                return *cached;
            }
        }
        // Tier 2: Remote DB
        auto result = db_->get_user_prefixes(user_id);
        if (cache_) cache_->set_user_prefixes(user_id, result);
        if (local_db_) local_db_->cache_user_prefixes(user_id, result);
        return result;
    }

    std::vector<std::string> get_guild_prefixes(uint64_t guild_id) {
        // Tier 0: In-memory cache (fastest)
        if (cache_) {
            auto cached = cache_->get_guild_prefixes(guild_id);
            if (cached) return *cached;
        }
        // Tier 1: Local SQLite
        if (local_db_) {
            auto cached = local_db_->get_guild_prefixes(guild_id);
            if (cached) {
                // Promote to in-memory cache
                if (cache_) cache_->set_guild_prefixes(guild_id, *cached);
                return *cached;
            }
        }
        // Tier 2: Remote DB
        auto result = db_->get_guild_prefixes(guild_id);
        if (cache_) cache_->set_guild_prefixes(guild_id, result);
        if (local_db_) local_db_->cache_guild_prefixes(guild_id, result);
        return result;
    }

    // =====================================================================
    // MODULE/COMMAND TOGGLES — 2-tier: LocalDB → Remote DB (bulk load)
    // On first access for a guild, loads ALL 4 toggle tables in ~1 round-trip
    // window, caches locally in SQLite, serves subsequent checks from local.
    // =====================================================================

    bool is_guild_module_enabled(uint64_t guild_id, const std::string& module,
                                 uint64_t user_id = 0, uint64_t channel_id = 0,
                                 const std::vector<uint64_t>& roles = {}) {
        // Tier 0: In-memory cache (fastest, no lock overhead)
        if (cache_) {
            auto cached = cache_->get_guild_toggles(guild_id);
            if (cached) return cached->is_module_enabled(module, user_id, channel_id, roles);
        }
        
        // Tier 1: Check LocalDB for fresh toggle data
        if (local_db_ && local_db_->has_fresh_guild_toggles(guild_id)) {
            return local_db_->evaluate_module_enabled(guild_id, module, user_id, channel_id, roles);
        }
        
        // Tier 2: Bulk load from remote + cache in LocalDB
        auto data = load_guild_toggles(guild_id);
        if (data) return data->is_module_enabled(module, user_id, channel_id, roles);
        
        // Fallback to per-query if bulk load failed
        return db_->is_guild_module_enabled(guild_id, module, user_id, channel_id, roles);
    }

    bool is_guild_command_enabled(uint64_t guild_id, const std::string& command,
                                  uint64_t user_id = 0, uint64_t channel_id = 0,
                                  const std::vector<uint64_t>& roles = {}) {
        // Tier 0: In-memory cache (fastest, no lock overhead)
        if (cache_) {
            auto cached = cache_->get_guild_toggles(guild_id);
            if (cached) return cached->is_command_enabled(command, user_id, channel_id, roles);
        }
        
        // Tier 1: Check LocalDB for fresh toggle data
        if (local_db_ && local_db_->has_fresh_guild_toggles(guild_id)) {
            return local_db_->evaluate_command_enabled(guild_id, command, user_id, channel_id, roles);
        }
        
        // Tier 2: Bulk load from remote + cache in LocalDB
        auto data = load_guild_toggles(guild_id);
        if (data) return data->is_command_enabled(command, user_id, channel_id, roles);
        
        return db_->is_guild_command_enabled(guild_id, command, user_id, channel_id, roles);
    }

    // =====================================================================
    // LEADERBOARDS — fetch from API when available, fallback to remote DB
    // =====================================================================

    // Generic leaderboard that tries API first
    std::vector<bronx::db::LeaderboardEntry> get_leaderboard(
            const std::string& type, int limit = 10,
            std::function<std::vector<bronx::db::LeaderboardEntry>()> db_fallback = nullptr) {
        // Try API first (has its own caching)
        if (api_) {
            auto json = api_->fetch_leaderboard(type, limit);
            if (json) {
                auto parsed = parse_leaderboard_json(*json);
                if (!parsed.empty()) return parsed;
            }
        }

        // Fallback to remote DB
        if (db_fallback) return db_fallback();
        return {};
    }

    // =====================================================================
    // FISHING — pass-through (complex operations best done on remote)
    // =====================================================================

    // These are complex multi-table operations, pass through to remote
    // but cache the active gear locally
    auto get_active_fishing_gear(uint64_t user_id) { return db_->get_active_fishing_gear(user_id); }
    auto get_unsold_fish(uint64_t user_id) { return db_->get_unsold_fish(user_id); }
    auto sell_fish(uint64_t user_id) { return db_->sell_fish(user_id); }
    auto sell_all_fish_by_rarity(uint64_t u, const std::string& r) { return db_->sell_all_fish_by_rarity(u, r); }

    // =====================================================================
    // INTEREST / LOANS — pass-through to remote
    // =====================================================================

    bool can_claim_interest(uint64_t user_id) { return db_->can_claim_interest(user_id); }
    int64_t claim_interest(uint64_t user_id) {
        int64_t result = db_->claim_interest(user_id);
        if (local_db_) local_db_->invalidate_user(user_id);
        if (cache_) cache_->invalidate_user_balance(user_id);
        return result;
    }
    bool has_active_loan(uint64_t user_id) { return db_->has_active_loan(user_id); }
    std::optional<bronx::db::LoanData> get_loan(uint64_t user_id) { return db_->get_loan(user_id); }

    // =====================================================================
    // PASSIVE MODE
    // =====================================================================

    bool set_passive(uint64_t user_id, bool passive) {
        bool result = db_->set_passive(user_id, passive);
        if (result && local_db_) local_db_->invalidate_user(user_id);
        return result;
    }

    // =====================================================================
    // PRESTIGE
    // =====================================================================

    bool perform_prestige(uint64_t user_id) {
        bool result = db_->perform_prestige(user_id);
        if (result) {
            if (local_db_) {
                local_db_->invalidate_user(user_id);
                local_db_->invalidate_inventory(user_id);
                local_db_->invalidate_stats(user_id);
            }
            if (cache_) cache_->invalidate_user_balance(user_id);
        }
        return result;
    }

    // =====================================================================
    // INVALIDATION — clear local caches
    // =====================================================================

    void invalidate_user(uint64_t user_id) {
        if (local_db_) {
            local_db_->invalidate_user(user_id);
            local_db_->invalidate_inventory(user_id);
            local_db_->invalidate_stats(user_id);
        }
        if (cache_) {
            cache_->invalidate_blacklist(user_id);
            cache_->invalidate_whitelist(user_id);
            cache_->invalidate_user_prefixes(user_id);
            cache_->invalidate_user_balance(user_id);
        }
    }

    void invalidate_guild(uint64_t guild_id) {
        if (cache_) cache_->invalidate_guild_prefixes(guild_id);
    }

    void invalidate_command_cache(uint64_t guild_id, const std::string& command) {
        if (cache_) cache_->invalidate_command(guild_id, command);
    }

    void invalidate_module_cache(uint64_t guild_id, const std::string& module) {
        if (cache_) cache_->invalidate_module(guild_id, module);
    }

    // =====================================================================
    // SETTINGS SYNC — bulk refresh from remote DB into caches
    // =====================================================================

    void refresh_all_settings() {
        try {
            // Guild prefixes
            {
                auto rows = db_->get_all_guild_prefixes_bulk();
                std::unordered_map<uint64_t, std::vector<std::string>> grouped;
                for (auto& r : rows) grouped[r.guild_id].push_back(std::move(r.prefix));
                for (auto& [gid, prefixes] : grouped) {
                    if (cache_) cache_->set_guild_prefixes(gid, prefixes);
                }
            }
            // Blacklist — bulk-load into cache
            {
                auto entries = db_->get_global_blacklist();
                for (auto& e : entries) {
                    if (cache_) cache_->set_blacklisted(e.user_id, true);
                }
            }
            // Whitelist — bulk-load into cache
            {
                auto entries = db_->get_global_whitelist();
                for (auto& e : entries) {
                    if (cache_) cache_->set_whitelisted(e.user_id, true);
                }
            }
            // Module/command toggles — build GuildToggleData per guild
            {
                std::unordered_map<uint64_t, bronx::cache::GuildToggleData> guild_data;

                auto mod_rows = db_->get_all_module_settings_bulk();
                for (auto& r : mod_rows) {
                    guild_data[r.guild_id].module_defaults[r.module] = r.enabled;
                    if (cache_) cache_->set_module_enabled(r.guild_id, r.module, r.enabled);
                }

                auto cmd_rows = db_->get_all_command_settings_bulk();
                for (auto& r : cmd_rows) {
                    guild_data[r.guild_id].command_defaults[r.command] = r.enabled;
                    if (cache_) cache_->set_command_enabled(r.guild_id, r.command, r.enabled);
                }

                // Push GuildToggleData into cache for fast in-memory evaluation
                for (auto& [gid, data] : guild_data) {
                    if (cache_) cache_->set_guild_toggles(gid, data);
                    
                    // Also push to LocalDB for persistence across restarts
                    if (local_db_) {
                        std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>> mod_scope_tuples;
                        for (const auto& [name, scopes] : data.module_scopes) {
                            for (const auto& s : scopes) {
                                mod_scope_tuples[name].push_back({s.scope_type, s.scope_id, s.enabled, s.exclusive});
                            }
                        }
                        std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>> cmd_scope_tuples;
                        for (const auto& [name, scopes] : data.command_scopes) {
                            for (const auto& s : scopes) {
                                cmd_scope_tuples[name].push_back({s.scope_type, s.scope_id, s.enabled, s.exclusive});
                            }
                        }
                        local_db_->cache_guild_toggles(gid, data.module_defaults, mod_scope_tuples,
                                                       data.command_defaults, cmd_scope_tuples);
                    }
                }
            }
            
            // Also populate LocalDB with bulk-loaded data for persistence
            if (local_db_) {
                // Guild prefixes
                auto rows = db_->get_all_guild_prefixes_bulk();
                std::unordered_map<uint64_t, std::vector<std::string>> grouped;
                for (auto& r : rows) grouped[r.guild_id].push_back(std::move(r.prefix));
                for (auto& [gid, prefixes] : grouped) {
                    local_db_->cache_guild_prefixes(gid, prefixes);
                }
                
                // Blacklist
                auto bl_entries = db_->get_global_blacklist();
                for (auto& e : bl_entries) {
                    local_db_->cache_blacklist(e.user_id, true);
                }
                
                // Whitelist
                auto wl_entries = db_->get_global_whitelist();
                for (auto& e : wl_entries) {
                    local_db_->cache_whitelist(e.user_id, true);
                }
            }
        } catch (const std::exception& e) {
            bronx::logger::error("hybrid_db", "Settings sync error: " + std::string(e.what()));
        }
    }

    // =====================================================================
    // PERIODIC MAINTENANCE
    // =====================================================================

    void periodic_cleanup() {
        if (cache_) cache_->periodic_cleanup();
        if (local_db_) local_db_->cleanup_expired();
    }

    // =====================================================================
    // DIRECT ACCESS — for operations not wrapped by HybridDatabase
    // =====================================================================

    bronx::db::Database* get_raw_db() { return db_; }
    bronx::local::LocalDB* get_local_db() { return local_db_; }
    bronx::batch::WriteBatchQueue* get_batch_queue() { return batch_; }
    bronx::api::ApiCacheClient* get_api_client() { return api_; }
    bronx::cache::CacheManager* get_cache() { return cache_; }

    // Cache stats combining all layers
    struct HybridStats {
        size_t memory_cache_entries = 0;
        int local_user_entries = 0;
        int local_inventory_entries = 0;
        int local_stat_entries = 0;
        int local_shop_entries = 0;
        int local_api_entries = 0;
        int pending_wallet_syncs = 0;
        int pending_bank_syncs = 0;
    };

    HybridStats get_hybrid_stats() {
        HybridStats stats{};
        if (cache_) {
            auto cs = cache_->get_stats();
            stats.memory_cache_entries = cs.total_entries;
        }
        if (local_db_) {
            auto ls = local_db_->get_stats();
            stats.local_user_entries = ls.user_entries;
            stats.local_inventory_entries = ls.inventory_entries;
            stats.local_stat_entries = ls.stat_entries;
            stats.local_shop_entries = ls.shop_entries;
            stats.local_api_entries = ls.api_entries;
            stats.pending_wallet_syncs = ls.pending_wallet_deltas;
            stats.pending_bank_syncs = ls.pending_bank_deltas;
        }
        return stats;
    }

private:
    // Bulk-load all toggle settings for a guild.  Returns cached data on hit,
    // otherwise fetches the 4 toggle tables from the remote DB and caches them
    // in both in-memory cache AND LocalDB for persistence across restarts.
    std::optional<bronx::cache::GuildToggleData> load_guild_toggles(uint64_t guild_id) {
        if (cache_) {
            auto cached = cache_->get_guild_toggles(guild_id);
            if (cached) return cached;
        }

        // Cache miss — bulk-fetch all 4 toggle tables for this guild
        try {
            bronx::cache::GuildToggleData data;

            // 1) Module defaults
            auto mod_defaults = db_->get_all_module_settings(guild_id);
            for (auto& row : mod_defaults) {
                data.module_defaults[row.module] = row.enabled;
            }

            // 2) Module scope overrides
            auto mod_scopes = db_->get_all_module_scope_settings(guild_id);
            for (auto& row : mod_scopes) {
                data.module_scopes[row.name].push_back({row.scope_type, row.scope_id, row.enabled, row.exclusive});
            }

            // 3) Command defaults
            auto cmd_defaults = db_->get_all_command_settings(guild_id);
            for (auto& row : cmd_defaults) {
                data.command_defaults[row.command] = row.enabled;
            }

            // 4) Command scope overrides
            auto cmd_scopes = db_->get_all_command_scope_settings(guild_id);
            for (auto& row : cmd_scopes) {
                data.command_scopes[row.name].push_back({row.scope_type, row.scope_id, row.enabled, row.exclusive});
            }

            // Cache in memory (fast but volatile)
            if (cache_) {
                cache_->set_guild_toggles(guild_id, data);
            }
            
            // Cache in LocalDB (persistent across restarts)
            if (local_db_) {
                // Convert GuildToggleData to LocalDB format
                std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>> mod_scope_tuples;
                for (const auto& [name, scopes] : data.module_scopes) {
                    for (const auto& s : scopes) {
                        mod_scope_tuples[name].push_back({s.scope_type, s.scope_id, s.enabled, s.exclusive});
                    }
                }
                std::unordered_map<std::string, std::vector<std::tuple<std::string, uint64_t, bool, bool>>> cmd_scope_tuples;
                for (const auto& [name, scopes] : data.command_scopes) {
                    for (const auto& s : scopes) {
                        cmd_scope_tuples[name].push_back({s.scope_type, s.scope_id, s.enabled, s.exclusive});
                    }
                }
                local_db_->cache_guild_toggles(guild_id, data.module_defaults, mod_scope_tuples,
                                               data.command_defaults, cmd_scope_tuples);
            }
            
            return data;
        } catch (...) {
            return std::nullopt;
        }
    }
    // Parse a JSON leaderboard response from the API into LeaderboardEntry vector
    // Simple JSON parsing without external library — handles the API's format:
    // [{"user_id": "123", "username": "foo", "value": 1000, "rank": 1}, ...]
    std::vector<bronx::db::LeaderboardEntry> parse_leaderboard_json(const std::string& json) {
        std::vector<bronx::db::LeaderboardEntry> entries;
        // Basic JSON array parsing — find objects between braces
        size_t pos = 0;
        int rank = 1;
        while ((pos = json.find('{', pos)) != std::string::npos) {
            size_t end = json.find('}', pos);
            if (end == std::string::npos) break;

            std::string obj = json.substr(pos, end - pos + 1);
            bronx::db::LeaderboardEntry entry{};
            entry.rank = rank++;

            // Extract user_id
            auto extract_num = [&obj](const std::string& key) -> uint64_t {
                auto kp = obj.find("\"" + key + "\"");
                if (kp == std::string::npos) return 0;
                auto cp = obj.find(':', kp);
                if (cp == std::string::npos) return 0;
                // Skip whitespace and optional quotes
                size_t vstart = cp + 1;
                while (vstart < obj.size() && (obj[vstart] == ' ' || obj[vstart] == '"')) vstart++;
                size_t vend = vstart;
                while (vend < obj.size() && (obj[vend] >= '0' && obj[vend] <= '9')) vend++;
                if (vend > vstart) {
                    try { return std::stoull(obj.substr(vstart, vend - vstart)); }
                    catch (...) { return 0; }
                }
                return 0;
            };

            auto extract_str = [&obj](const std::string& key) -> std::string {
                auto kp = obj.find("\"" + key + "\"");
                if (kp == std::string::npos) return "";
                auto cp = obj.find(':', kp);
                if (cp == std::string::npos) return "";
                auto q1 = obj.find('"', cp + 1);
                if (q1 == std::string::npos) return "";
                auto q2 = obj.find('"', q1 + 1);
                if (q2 == std::string::npos) return "";
                return obj.substr(q1 + 1, q2 - q1 - 1);
            };

            entry.user_id = extract_num("user_id");
            entry.username = extract_str("username");
            entry.value = static_cast<int64_t>(extract_num("value"));
            if (entry.user_id != 0) {
                entries.push_back(std::move(entry));
            }

            pos = end + 1;
        }
        return entries;
    }
};

} // namespace hybrid
} // namespace bronx
