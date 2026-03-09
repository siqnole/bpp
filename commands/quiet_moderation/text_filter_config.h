#pragma once
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <string>
#include <regex>
#include <vector>
#include <algorithm>
#include "../../command.h"

namespace commands {
namespace quiet_moderation {

// Structure to hold text filter settings for a guild
struct TextFilterConfig {
    bool enabled = false;
    ::std::set<::std::string> blocked_words;         // Exact word matches (case-insensitive)
    ::std::set<::std::string> blocked_patterns;      // Regex patterns
    ::std::set<::std::string> blocked_emoji_names;   // Emoji names in messages
    ::std::set<dpp::snowflake> whitelist_roles;
    ::std::set<dpp::snowflake> whitelist_users;
    ::std::set<dpp::snowflake> whitelist_channels; // Channels where filter is disabled
    ::std::set<dpp::snowflake> blacklist_roles;
    ::std::set<dpp::snowflake> blacklist_users;
    ::std::set<dpp::snowflake> blacklist_channels;
    bool use_whitelist = true;
    bool use_blacklist = false;
    bool log_violations = true;
    dpp::snowflake log_channel = 0;
    bool delete_message = true;
    bool warn_user = false;
    int max_violations_before_timeout = 3;
    uint32_t timeout_duration = 600;             // 10 minutes default
    bool check_usernames = false;                // Also check nicknames/usernames
    bool check_attachments = false;              // Check attachment filenames
};

// Global storage (defined in one TU)
extern ::std::map<dpp::snowflake, TextFilterConfig> guild_text_filters;
extern ::std::map<dpp::snowflake, ::std::map<dpp::snowflake, int>> text_filter_violations;

// Helper: Extract custom emoji names from message content
inline ::std::vector<::std::string> extract_emoji_names(const ::std::string& content) {
    ::std::vector<::std::string> emoji_names;
    ::std::regex emoji_regex("<a?:(\\w+):\\d+>");
    ::std::smatch match;
    ::std::string text = content;

    while (::std::regex_search(text, match, emoji_regex)) {
        emoji_names.push_back(match[1].str());
        text = match.suffix();
    }

    return emoji_names;
}

// Helper: Check if text contains blocked content
inline ::std::pair<bool, ::std::string> contains_blocked_content(const ::std::string& text, const TextFilterConfig& config) {
    ::std::string lower_text = text;
    ::std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);

    // Check blocked words (whole word matching)
    for (const auto& word : config.blocked_words) {
        ::std::string lower_word = word;
        ::std::transform(lower_word.begin(), lower_word.end(), lower_word.begin(), ::tolower);

        // Create word boundary regex for whole word matching
        ::std::regex word_regex("\\b" + ::std::regex_replace(lower_word, ::std::regex("[.*+?^${}()|\\\\[\\]\\\\]"), "\\\\$&") + "\\b");
        if (::std::regex_search(lower_text, word_regex)) {
            return {true, "blocked word: " + word};
        }
    }

    // Check regex patterns
    for (const auto& pattern : config.blocked_patterns) {
        try {
            ::std::regex regex_pattern(pattern, ::std::regex::icase);
            if (::std::regex_search(text, regex_pattern)) {
                return {true, "matched pattern: " + pattern};
            }
        } catch (...) {
            // Invalid regex, skip
        }
    }

    // Check emoji names
    auto emoji_names = extract_emoji_names(text);
    for (const auto& emoji_name : emoji_names) {
        ::std::string lower_emoji = emoji_name;
        ::std::transform(lower_emoji.begin(), lower_emoji.end(), lower_emoji.begin(), ::tolower);

        for (const auto& blocked_emoji : config.blocked_emoji_names) {
            ::std::string lower_blocked = blocked_emoji;
            ::std::transform(lower_blocked.begin(), lower_blocked.end(), lower_blocked.begin(), ::tolower);

            if (lower_emoji.find(lower_blocked) != ::std::string::npos) {
                return {true, "blocked emoji: " + emoji_name};
            }
        }
    }

    return {false, ""};
}

// Helper: Check if user/channel is whitelisted
inline bool is_whitelisted(const dpp::snowflake& guild_id, const dpp::snowflake& user_id,
                           const dpp::snowflake& channel_id, const ::std::vector<dpp::snowflake>& user_roles,
                           const TextFilterConfig& config) {
    if (!config.use_whitelist) return false; // whitelist disabled => ignore

    // Blacklist overrides whitelist
    if (config.use_blacklist) {
        if (config.blacklist_users.count(user_id) > 0) return false;
        if (config.blacklist_channels.count(channel_id) > 0) return false;
        for (const auto& role : user_roles) {
            if (config.blacklist_roles.count(role) > 0) return false;
        }
    }

    if (config.whitelist_users.count(user_id) > 0) return true;
    if (config.whitelist_channels.count(channel_id) > 0) return true;
    for (const auto& role : user_roles) {
        if (config.whitelist_roles.count(role) > 0) return true;
    }

    return false;
}

// Public API (implemented in text_filter_handler.cpp / text_filter_command.cpp)
Command* get_text_filter_command();
void register_text_filter(dpp::cluster& bot);

} // namespace quiet_moderation
} // namespace commands
