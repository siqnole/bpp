#pragma once
// ---------------------------------------------------------------------------
// ApiCacheClient — Fetch pre-aggregated data from the site's Express API
//
// The dashboard (site/server.js) already maintains a mysql2 connection pool
// to the same Aiven database and exposes REST endpoints for leaderboards,
// fishing stats, user search, bazaar stats, etc.  Instead of the bot doing
// expensive aggregation queries over the network, we fetch from the API which
// runs on the same server (or a nearby one) and benefits from its own caching.
//
// Responses are cached in the LocalDB's api_cache table so repeated requests
// within the TTL window are served from disk without any network call.
// ---------------------------------------------------------------------------

#include "local_db.h"
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#include <string>
#include <optional>
#include <iostream>
#include <mutex>
#include <chrono>
#include <functional>

namespace bronx {
namespace api {

class ApiCacheClient {
public:
    explicit ApiCacheClient(bronx::local::LocalDB* local_db,
                            const std::string& base_url = "http://localhost:3000",
                            const std::string& api_key = "")
        : local_db_(local_db), base_url_(base_url), api_key_(api_key) {
        // Allow overriding via environment
        if (const char* env_url = std::getenv("SITE_API_URL")) {
            base_url_ = env_url;
        }
        if (const char* env_key = std::getenv("SITE_API_KEY")) {
            api_key_ = env_key;
        }
    }

    // =====================================================================
    // Generic API fetch with caching
    // =====================================================================

    // Fetch a URL, checking local cache first.  Returns the response body (JSON).
    std::optional<std::string> fetch_cached(const std::string& endpoint, int cache_ttl_seconds = 120) {
        std::string cache_key = "api:" + endpoint;

        // 1. Check local cache
        if (local_db_) {
            auto cached = local_db_->get_cached_api_response(cache_key);
            if (cached) {
                return cached;
            }
        }

        // 2. Cache miss — fetch from API
        auto response = http_get(base_url_ + endpoint);
        if (!response) {
            return std::nullopt;
        }

        // 3. Store in local cache
        if (local_db_) {
            local_db_->cache_api_response(cache_key, *response);
        }

        return response;
    }

    // =====================================================================
    // Pre-built API methods for common data
    // =====================================================================

    // Fetch leaderboard data from the API instead of querying remote DB directly
    // The API endpoint is: GET /api/leaderboard/:type
    std::optional<std::string> fetch_leaderboard(const std::string& type, int limit = 10) {
        return fetch_cached("/api/leaderboard/" + type + "?limit=" + std::to_string(limit), 120);
    }

    // Fetch fishing stats for a user
    std::optional<std::string> fetch_fishing_stats(uint64_t user_id) {
        return fetch_cached("/api/fishing/stats/" + std::to_string(user_id), 60);
    }

    // Fetch overview stats (total users, commands, etc.)
    std::optional<std::string> fetch_overview_stats() {
        return fetch_cached("/api/overview", 60);
    }

    // Fetch guild settings (already cached by the dashboard)
    std::optional<std::string> fetch_guild_settings(uint64_t guild_id) {
        return fetch_cached("/api/guild/" + std::to_string(guild_id) + "/settings", 300);
    }

    // Fetch bazaar stock prices
    std::optional<std::string> fetch_bazaar_prices() {
        return fetch_cached("/api/bazaar/prices", 120);
    }

    // Fetch user search results
    std::optional<std::string> fetch_user_search(uint64_t user_id) {
        return fetch_cached("/api/user/" + std::to_string(user_id), 60);
    }

    // Invalidate a specific cache entry (e.g., after a mutation)
    void invalidate(const std::string& endpoint) {
        if (local_db_) {
            local_db_->invalidate_api_cache("api:" + endpoint);
        }
    }

    // Check if the API is reachable
    bool health_check() {
        auto result = http_get(base_url_ + "/api/status");
        return result.has_value();
    }

    // Get base URL for diagnostics
    const std::string& get_base_url() const { return base_url_; }

private:
    bronx::local::LocalDB* local_db_;
    std::string base_url_;
    std::string api_key_;

#ifdef HAVE_LIBCURL
    // libcurl write callback
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* body = static_cast<std::string*>(userdata);
        body->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::optional<std::string> http_get(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) return std::nullopt;

        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);          // 5s timeout
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);    // 3s connect timeout
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);          // thread-safe
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // Add API key header if configured
        struct curl_slist* headers = nullptr;
        if (!api_key_.empty()) {
            std::string auth = "X-API-Key: " + api_key_;
            headers = curl_slist_append(headers, auth.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || http_code != 200) {
            static std::atomic<uint64_t> error_count{0};
            if (++error_count % 50 == 1) {
                std::cerr << "[api_cache] HTTP " << http_code << " from " << url
                          << " (curl: " << curl_easy_strerror(res) << ")\n";
            }
            return std::nullopt;
        }

        return body;
    }
#else
    // No libcurl — always return nullopt (fall back to direct DB queries)
    std::optional<std::string> http_get(const std::string& /*url*/) {
        return std::nullopt;
    }
#endif
};

} // namespace api
} // namespace bronx
