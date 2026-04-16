#pragma once

#include "../security/thread_safe_map.h"
#include <dpp/dpp.h>
#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <iostream>

namespace bronx {
namespace tracking {

struct InviteInfo {
    std::string code;
    uint64_t inviter_id = 0;
    uint32_t uses = 0;
};

class InviteCache {
public:
    static InviteCache& get() {
        static InviteCache instance;
        return instance;
    }

    // Refresh all invites for a guild
    void refresh_guild_invites(const dpp::invite_map& invites, uint64_t guild_id) {
        std::map<std::string, InviteInfo> local_map;
        for (const auto& [code, inv] : invites) {
            local_map[code] = {inv.code, inv.inviter_id, inv.uses};
        }
        
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[guild_id] = std::move(local_map);
    }

    // Called on invite create
    void add_invite(uint64_t guild_id, const dpp::invite& inv) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_[guild_id][inv.code] = {inv.code, inv.inviter_id, inv.uses};
    }

    // Called on invite delete
    void remove_invite(uint64_t guild_id, const std::string& code) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(guild_id);
        if (it != cache_.end()) {
            it->second.erase(code);
        }
    }

    // Diffs the newly fetched invites against the cached ones to find which one was used.
    // Returns the used invite (if any), and updates the cache automatically.
    std::optional<InviteInfo> extract_used_invite(uint64_t guild_id, const dpp::invite_map& latest_invites) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        std::optional<InviteInfo> used_invite;
        auto& guild_cache = cache_[guild_id];

        // Find which invite's use count incremented
        for (const auto& [code, latest_inv] : latest_invites) {
            auto old_it = guild_cache.find(code);
            if (old_it != guild_cache.end()) {
                if (latest_inv.uses > old_it->second.uses) {
                    // Match found! 
                    used_invite = {latest_inv.code, latest_inv.inviter_id, latest_inv.uses};
                }
            }
            // Update cache unconditionally
            guild_cache[code] = {latest_inv.code, latest_inv.inviter_id, latest_inv.uses};
        }

        // Clean up any deletes that were missed
        for (auto it = guild_cache.begin(); it != guild_cache.end(); ) {
            if (latest_invites.find(it->first) == latest_invites.end()) {
                it = guild_cache.erase(it);
            } else {
                ++it;
            }
        }

        return used_invite;
    }

private:
    InviteCache() = default;
    
    std::mutex cache_mutex_;
    // guild_id -> (invite_code -> InviteInfo)
    std::map<uint64_t, std::map<std::string, InviteInfo>> cache_;
};

} // namespace tracking
} // namespace bronx
