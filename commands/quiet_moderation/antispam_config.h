#pragma once
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <deque>
#include <string>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace commands {
namespace quiet_moderation {

// Per-detector behavior configuration (action/log/strict)
struct DetectionBehavior {
    // action: "none", "delete", "timeout", "kick", "ban"
    ::std::string action = "none";
    uint32_t timeout_seconds = 0; // used when action == "timeout"
    bool log = false;             // whether to log this detector's violations
    bool strict = true;          // if false, give leeway to detection thresholds
};

// Result returned by detect_spam
struct SpamResult {
    bool is_spam = false;
    ::std::string module; // which detector triggered: "ratelimit", "duplicates", "mentions", "emojis", "caps", "newlines"
    ::std::string reason;
};

// Antispam configuration per-guild
struct AntiSpamConfig {
    bool enabled = false;

    // Message spam detection
    int max_messages_per_interval = 5;
    int message_interval_seconds = 5;

    // Duplicate message detection
    bool detect_duplicates = true;
    int max_duplicate_messages = 3;

    // Mention spam detection
    bool detect_mention_spam = true;
    int max_mentions_per_message = 5;
    int max_role_mentions_per_message = 2;

    // Emoji spam detection
    bool detect_emoji_spam = true;
    int max_emojis_per_message = 10;

    // Caps spam detection
    bool detect_caps_spam = true;
    int min_caps_length = 10;              // Minimum message length to check
    float max_caps_percentage = 0.7f;      // 70% caps = spam

    // Newline spam detection
    bool detect_newline_spam = true;
    int max_newlines = 10;

    ::std::set<dpp::snowflake> whitelist_roles;
    ::std::set<dpp::snowflake> whitelist_users;
    ::std::set<dpp::snowflake> whitelist_channels;
    ::std::set<dpp::snowflake> blacklist_roles;
    ::std::set<dpp::snowflake> blacklist_users;
    ::std::set<dpp::snowflake> blacklist_channels;
    bool use_whitelist = true;
    bool use_blacklist = false;

    bool log_violations = true;
    dpp::snowflake log_channel = 0;
    bool delete_messages = true;
    bool warn_user = false;

    int max_violations_before_timeout = 2;
    uint32_t timeout_duration = 900;       // 15 minutes
    int max_violations_before_kick = 5;
    int max_violations_before_ban = 10;

    // Per-detector behavior settings (action/log/strict)
    DetectionBehavior ratelimit_behavior;   // controls behavior when rate limit triggers
    DetectionBehavior duplicates_behavior;  // behavior for duplicate detection
    DetectionBehavior mentions_behavior;    // behavior for mention detection
    DetectionBehavior emojis_behavior;      // behavior for emoji detection
    DetectionBehavior caps_behavior;        // behavior for caps detection
    DetectionBehavior newlines_behavior;    // behavior for newline detection
};

// Violation counters and tracking structures (defined in one .cpp)
extern ::std::map<dpp::snowflake, AntiSpamConfig> guild_antispam_configs;
extern ::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> antispam_violations;

// Track recent messages per user
struct UserMessageData {
    ::std::deque<::std::chrono::steady_clock::time_point> message_times;
    ::std::deque<::std::string> recent_messages;
};
extern ::std::map<dpp::snowflake, ::std::map<dpp::snowflake, UserMessageData>> user_message_tracking;

// Helper: Count emojis in message (simple heuristic)
inline int count_emojis(const ::std::string& content) {
    int count = 0;

    // Count custom emojis
    size_t pos = 0;
    while ((pos = content.find("<:", pos)) != ::std::string::npos) {
        count++;
        pos += 2;
    }
    pos = 0;
    while ((pos = content.find("<a:", pos)) != ::std::string::npos) {
        count++;
        pos += 3;
    }

    // Rough estimate for unicode emojis (not perfect but good enough)
    for (size_t i = 0; i < content.length(); i++) {
        unsigned char c = content[i];
        if (c >= 0xF0) count++;
    }

    return count;
}

// Helper: Calculate caps percentage
inline float get_caps_percentage(const ::std::string& content) {
    if (content.length() < 5) return 0.0f;

    int caps_count = 0;
    int letter_count = 0;

    for (char c : content) {
        if (::std::isalpha(static_cast<unsigned char>(c))) {
            letter_count++;
            if (::std::isupper(static_cast<unsigned char>(c))) caps_count++;
        }
    }

    if (letter_count == 0) return 0.0f;
    return static_cast<float>(caps_count) / letter_count;
}

// Helper: Count newlines
inline int count_newlines(const ::std::string& content) {
    return static_cast<int>(::std::count(content.begin(), content.end(), '\n'));
}



// Helper: Check for spam (returns SpamResult)
inline SpamResult detect_spam(const dpp::message_create_t& event,
                              const AntiSpamConfig& config,
                              UserMessageData& user_data) {
    auto now = ::std::chrono::steady_clock::now();
    SpamResult res;

    // Convenience lambdas to apply "non-strict" leeway
    auto relaxed_int = [](int value, bool strict) -> int {
        if (strict) return value;
        return static_cast<int>(::std::ceil(value * 1.5));
    };
    auto relaxed_float = [](float value, bool strict) -> float {
        if (strict) return value;
        return ::std::min(1.0f, value * 1.2f);
    };

    // Remove old message timestamps
    while (!user_data.message_times.empty()) {
        auto age = ::std::chrono::duration_cast<::std::chrono::seconds>(now - user_data.message_times.front()).count();
        if (age > config.message_interval_seconds) {
            user_data.message_times.pop_front();
        } else {
            break;
        }
    }

    // Rate limit (ratelimit detector)
    user_data.message_times.push_back(now);
    int rate_limit_allowed = relaxed_int(config.max_messages_per_interval, config.ratelimit_behavior.strict);
    if (user_data.message_times.size() > static_cast<size_t>(rate_limit_allowed)) {
        res.is_spam = true;
        res.module = "ratelimit";
        res.reason = "Sending messages too quickly";
        return res;
    }

    // Check duplicate messages
    if (config.detect_duplicates) {
        user_data.recent_messages.push_back(event.msg.content);
        if (user_data.recent_messages.size() > 10) {
            user_data.recent_messages.pop_front();
        }

        int duplicate_count = 0;
        for (const auto& msg : user_data.recent_messages) {
            if (msg == event.msg.content) duplicate_count++;
        }

        int dup_threshold = relaxed_int(config.max_duplicate_messages, config.duplicates_behavior.strict);
        if (duplicate_count >= dup_threshold) {
            res.is_spam = true;
            res.module = "duplicates";
            res.reason = "Sending duplicate messages";
            return res;
        }
    }

    // Check mention spam
    if (config.detect_mention_spam) {
        int mentions_allowed = relaxed_int(config.max_mentions_per_message, config.mentions_behavior.strict);
        if (static_cast<int>(event.msg.mentions.size()) > mentions_allowed) {
            res.is_spam = true;
            res.module = "mentions";
            res.reason = "Too many user mentions (" + ::std::to_string(event.msg.mentions.size()) + ")";
            return res;
        }

        int role_mentions_allowed = relaxed_int(config.max_role_mentions_per_message, config.mentions_behavior.strict);
        if (static_cast<int>(event.msg.mention_roles.size()) > role_mentions_allowed) {
            res.is_spam = true;
            res.module = "mentions";
            res.reason = "Too many role mentions (" + ::std::to_string(event.msg.mention_roles.size()) + ")";
            return res;
        }
    }

    // Check emoji spam
    if (config.detect_emoji_spam) {
        int emoji_count = count_emojis(event.msg.content);
        int emoji_allowed = relaxed_int(config.max_emojis_per_message, config.emojis_behavior.strict);
        if (emoji_count > emoji_allowed) {
            res.is_spam = true;
            res.module = "emojis";
            res.reason = "Too many emojis (" + ::std::to_string(emoji_count) + ")";
            return res;
        }
    }

    // Check caps spam
    if (config.detect_caps_spam && event.msg.content.length() >= static_cast<size_t>(config.min_caps_length)) {
        float caps_pct = get_caps_percentage(event.msg.content);
        float allowed_caps = relaxed_float(config.max_caps_percentage, config.caps_behavior.strict);
        if (caps_pct > allowed_caps) {
            res.is_spam = true;
            res.module = "caps";
            res.reason = "Excessive caps (" + ::std::to_string(static_cast<int>(caps_pct * 100)) + "%)";
            return res;
        }
    }

    // Check newline spam
    if (config.detect_newline_spam) {
        int newline_count = count_newlines(event.msg.content);
        int newline_allowed = relaxed_int(config.max_newlines, config.newlines_behavior.strict);
        if (newline_count > newline_allowed) {
            res.is_spam = true;
            res.module = "newlines";
            res.reason = "Too many newlines (" + ::std::to_string(newline_count) + ")";
            return res;
        }
    }

    return res;
}

} // namespace quiet_moderation
} // namespace commands
