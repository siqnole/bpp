#include "cache_manager.h"
#include <iostream>
#include <memory>

namespace bronx {
namespace cache {

// Global cache instance
std::unique_ptr<CacheManager> global_cache = nullptr;

void initialize_cache() {
    if (!global_cache) {
        global_cache = std::make_unique<CacheManager>();
        std::cout << "Cache system initialized successfully\n";
    }
}

void shutdown_cache() {
    if (global_cache) {
        auto stats = global_cache->get_stats();
        std::cout << "Cache shutdown - Final stats:\n";
        std::cout << "  Total cached entries: " << stats.total_entries << "\n";
        std::cout << "  Blacklist entries: " << stats.blacklist_entries << "\n";
        std::cout << "  Whitelist entries: " << stats.whitelist_entries << "\n";
        std::cout << "  Cooldown entries: " << stats.cooldown_entries << "\n";
        std::cout << "  Balance entries: " << (stats.wallet_entries + stats.bank_entries) << "\n";
        global_cache.reset();
    }
}

} // namespace cache
} // namespace bronx