#include "cache_manager.h"
#include <iostream>
#include <memory>
#include "../utils/logger.h"

namespace bronx {
namespace cache {

// Global cache instance
std::unique_ptr<CacheManager> global_cache = nullptr;

void initialize_cache() {
    if (!global_cache) {
        global_cache = std::make_unique<CacheManager>();
        bronx::logger::success("system cache", "cache system initialized successfully");
    }
}

void shutdown_cache() {
    if (global_cache) {
        auto stats = global_cache->get_stats();
        bronx::logger::info("system cache", "cache shutdown - final stats:");
        bronx::logger::info("system cache", "  total cached entries: " + std::to_string(stats.total_entries));
        bronx::logger::info("system cache", "  blacklist entries: " + std::to_string(stats.blacklist_entries));
        bronx::logger::info("system cache", "  whitelist entries: " + std::to_string(stats.whitelist_entries));
        bronx::logger::info("system cache", "  cooldown entries: " + std::to_string(stats.cooldown_entries));
        bronx::logger::info("system cache", "  balance entries: " + std::to_string(stats.wallet_entries + stats.bank_entries));
        global_cache.reset();
    }
}

} // namespace cache
} // namespace bronx