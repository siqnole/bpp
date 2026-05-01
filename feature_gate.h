#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <optional>
#include <vector>
#include <cstdint>
#include <iostream>
#include <chrono>
#include "database/core/database.h"
#include "utils/logger.h"

namespace bronx {

// Feature flag modes
enum class FeatureMode {
    ENABLED,     // Available to all guilds (default/production)
    DISABLED,    // Blocked everywhere — kill switch
    WHITELIST    // Only available to whitelisted guilds
};

inline std::string feature_mode_to_string(FeatureMode m) {
    switch (m) {
        case FeatureMode::ENABLED:   return "enabled";
        case FeatureMode::DISABLED:  return "disabled";
        case FeatureMode::WHITELIST: return "whitelist";
    }
    return "unknown";
}

inline FeatureMode string_to_feature_mode(const std::string& s) {
    if (s == "disabled" || s == "off" || s == "kill") return FeatureMode::DISABLED;
    if (s == "whitelist" || s == "beta" || s == "wl")  return FeatureMode::WHITELIST;
    return FeatureMode::ENABLED;
}

// In-memory cached feature state
struct FeatureState {
    FeatureMode mode = FeatureMode::ENABLED;
    std::unordered_set<uint64_t> whitelist;   // guild IDs allowed when mode == WHITELIST
    std::string reason;                       // optional reason for disable/whitelist
};

// ============================================================================
// FeatureGate — thread-safe singleton for runtime feature flag checks
//
// Usage in any command:
//   if (!bronx::FeatureGate::get().check("fishing", guild_id)) {
//       event.reply(...); return;
//   }
//
// Or use the convenience macro:
//   FEATURE_CHECK(db, guild_id, "fishing", event)   // auto-replies and returns
// ============================================================================
class FeatureGate {
public:
    static FeatureGate& get() {
        static FeatureGate instance;
        return instance;
    }

    void init(db::Database* db) {
        db_ = db;
        reload();
    }

    // ── Core check ──────────────────────────────────────────────────────
    // Returns true if the feature is allowed for this guild.
    bool check(const std::string& feature, uint64_t guild_id) const {
        std::shared_lock lock(mutex_);
        auto it = features_.find(feature);
        if (it == features_.end()) return true; // unknown feature → default enabled

        switch (it->second.mode) {
            case FeatureMode::ENABLED:   return true;
            case FeatureMode::DISABLED:  return false;
            case FeatureMode::WHITELIST:
                return it->second.whitelist.count(guild_id) > 0;
        }
        return true;
    }

    // Get the reason for a feature being gated (for user-facing messages)
    std::string get_reason(const std::string& feature) const {
        std::shared_lock lock(mutex_);
        auto it = features_.find(feature);
        if (it == features_.end()) return "";
        return it->second.reason;
    }

    // Get current mode of a feature
    std::optional<FeatureMode> get_mode(const std::string& feature) const {
        std::shared_lock lock(mutex_);
        auto it = features_.find(feature);
        if (it == features_.end()) return std::nullopt;
        return it->second.mode;
    }

    // Get a snapshot of all features
    std::unordered_map<std::string, FeatureState> get_all() const {
        std::shared_lock lock(mutex_);
        return features_;
    }

    // ── Mutations (owner commands) ──────────────────────────────────────

    // Set a feature's mode (persists to DB)
    bool set_mode(const std::string& feature, FeatureMode mode, const std::string& reason = "") {
        if (!db_) return false;

        // Persist to DB
        if (!db_->set_feature_flag(feature, feature_mode_to_string(mode), reason)) {
            return false;
        }

        // Update cache
        std::unique_lock lock(mutex_);
        features_[feature].mode = mode;
        features_[feature].reason = reason;
        return true;
    }

    // Add a guild to a feature's whitelist
    bool add_whitelist(const std::string& feature, uint64_t guild_id) {
        if (!db_) return false;
        if (!db_->add_feature_flag_whitelist(feature, guild_id)) return false;

        std::unique_lock lock(mutex_);
        features_[feature].whitelist.insert(guild_id);
        return true;
    }

    // Remove a guild from a feature's whitelist
    bool remove_whitelist(const std::string& feature, uint64_t guild_id) {
        if (!db_) return false;
        if (!db_->remove_feature_flag_whitelist(feature, guild_id)) return false;

        std::unique_lock lock(mutex_);
        features_[feature].whitelist.erase(guild_id);
        return true;
    }

    // Delete a feature flag entirely (returns to default enabled)
    bool remove_feature(const std::string& feature) {
        if (!db_) return false;
        if (!db_->delete_feature_flag(feature)) return false;

        std::unique_lock lock(mutex_);
        features_.erase(feature);
        return true;
    }

    // ── Reload from DB ──────────────────────────────────────────────────
    void reload() {
        if (!db_) return;

        auto flags = db_->get_all_feature_flags();
        auto whitelist = db_->get_all_feature_flag_whitelists();

        std::unique_lock lock(mutex_);
        features_.clear();

        for (auto& [name, mode_str, reason] : flags) {
            FeatureState state;
            state.mode = string_to_feature_mode(mode_str);
            state.reason = reason;
            features_[name] = std::move(state);
        }

        for (auto& [name, guild_id] : whitelist) {
            features_[name].whitelist.insert(guild_id);
        }

        bronx::logger::info("feature", "loaded " + std::to_string(features_.size()) + " feature flags");
    }

private:
    FeatureGate() = default;
    db::Database* db_ = nullptr;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, FeatureState> features_;
};

} // namespace bronx

// ============================================================================
// Convenience macros for commands
// ============================================================================

// For text commands — sends an error embed and returns from the handler
#define FEATURE_CHECK_TEXT(bot, event, feature_name) \
    do { \
        if (!bronx::FeatureGate::get().check(feature_name, event.msg.guild_id)) { \
            std::string _reason = bronx::FeatureGate::get().get_reason(feature_name); \
            std::string _msg = "this feature is currently unavailable"; \
            if (!_reason.empty()) _msg += "\n*" + _reason + "*"; \
            bronx::send_message(bot, event, bronx::error(_msg)); \
            return; \
        } \
    } while (0)

// For slash commands — sends an ephemeral error embed and returns
#define FEATURE_CHECK_SLASH(event, feature_name) \
    do { \
        if (!bronx::FeatureGate::get().check(feature_name, event.command.guild_id)) { \
            std::string _reason = bronx::FeatureGate::get().get_reason(feature_name); \
            std::string _msg = "this feature is currently unavailable"; \
            if (!_reason.empty()) _msg += "\n*" + _reason + "*"; \
            event.reply(dpp::message().add_embed(bronx::error(_msg)).set_flags(dpp::m_ephemeral)); \
            return; \
        } \
    } while (0)
