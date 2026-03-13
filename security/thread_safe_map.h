#pragma once
// ============================================================================
// ThreadSafeMap — Drop-in replacement for unprotected static maps.
// Wraps std::unordered_map with a std::shared_mutex for safe concurrent access.
// Use this for ALL shared mutable state in multi-shard DPP bots.
// ============================================================================

#include <unordered_map>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <functional>
#include <vector>

namespace bronx {
namespace security {

template<typename K, typename V, template<typename...> class MapType = std::unordered_map>
class ThreadSafeMap {
private:
    mutable std::shared_mutex mutex_;
    MapType<K, V> map_;

public:
    ThreadSafeMap() = default;

    // Read: get a copy of the value (shared lock)
    std::optional<V> get(const K& key) const {
        std::shared_lock lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    // Read: check if key exists (shared lock)
    bool contains(const K& key) const {
        std::shared_lock lock(mutex_);
        return map_.find(key) != map_.end();
    }

    // Write: insert or update (exclusive lock)
    void set(const K& key, const V& value) {
        std::unique_lock lock(mutex_);
        map_[key] = value;
    }

    // Write: insert or update with move (exclusive lock)
    void set(const K& key, V&& value) {
        std::unique_lock lock(mutex_);
        map_[key] = std::move(value);
    }

    // Write: emplace (exclusive lock)
    template<typename... Args>
    bool emplace(const K& key, Args&&... args) {
        std::unique_lock lock(mutex_);
        auto [it, inserted] = map_.try_emplace(key, std::forward<Args>(args)...);
        return inserted;
    }

    // Write: erase (exclusive lock)
    bool erase(const K& key) {
        std::unique_lock lock(mutex_);
        return map_.erase(key) > 0;
    }

    // Read: size (shared lock)
    size_t size() const {
        std::shared_lock lock(mutex_);
        return map_.size();
    }

    bool empty() const {
        std::shared_lock lock(mutex_);
        return map_.empty();
    }

    // Execute a callback with exclusive access to the map.
    // Use for complex operations that need multiple reads/writes atomically.
    template<typename Func>
    auto with_lock(Func&& func) -> decltype(func(map_)) {
        std::unique_lock lock(mutex_);
        return func(map_);
    }

    // Execute a callback with shared (read-only) access.
    template<typename Func>
    auto with_shared_lock(Func&& func) const -> decltype(func(map_)) {
        std::shared_lock lock(mutex_);
        return func(map_);
    }

    // Write: clear all entries (exclusive lock)
    void clear() {
        std::unique_lock lock(mutex_);
        map_.clear();
    }

    // Read: get all keys (shared lock)
    std::vector<K> keys() const {
        std::shared_lock lock(mutex_);
        std::vector<K> result;
        result.reserve(map_.size());
        for (const auto& [k, v] : map_) {
            result.push_back(k);
        }
        return result;
    }

    // Conditional erase: remove entries matching a predicate (exclusive lock)
    size_t erase_if(std::function<bool(const K&, const V&)> predicate) {
        std::unique_lock lock(mutex_);
        size_t count = 0;
        for (auto it = map_.begin(); it != map_.end();) {
            if (predicate(it->first, it->second)) {
                it = map_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    // Get-or-create: returns existing value or creates default (exclusive lock)
    V& get_or_create(const K& key) {
        std::unique_lock lock(mutex_);
        return map_[key];
    }

    // Find and modify: atomically find + modify in one lock acquisition
    // Returns true if key was found and callback was invoked
    bool find_and_modify(const K& key, std::function<void(V&)> modifier) {
        std::unique_lock lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        modifier(it->second);
        return true;
    }
};

// Convenience alias for std::map-based variant (ordered)
template<typename K, typename V>
using ThreadSafeOrderedMap = ThreadSafeMap<K, V, std::map>;

} // namespace security
} // namespace bronx
