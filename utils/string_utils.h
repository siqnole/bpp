#pragma once
#include <string>
#include <unordered_map>
#include <algorithm>

namespace bronx {
namespace utils {

/**
 * @brief Replaces placeholders in a string with values from a map.
 * 
 * Supports placeholders like {name}, {level}, etc.
 * Case-insensitive for the tags.
 */
inline std::string replace_placeholders(std::string text, const std::unordered_map<std::string, std::string>& placeholders) {
    for (const auto& [placeholder, value] : placeholders) {
        std::string tag = "{" + placeholder + "}";
        std::string tag_lower = tag;
        std::transform(tag_lower.begin(), tag_lower.end(), tag_lower.begin(), ::tolower);
        
        size_t pos = 0;
        // Simple case-insensitive search and replace
        while (true) {
            std::string text_lower = text;
            std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
            
            pos = text_lower.find(tag_lower, pos);
            if (pos == std::string::npos) break;
            
            text.replace(pos, tag.length(), value);
            pos += value.length();
        }
    }
    return text;
}

/**
 * @brief Escapes a string for use in a bash shell command.
 * 
 * Simple but effective single-quote escaping. 
 * ' becomes '\'' and the entire string is wrapped in single quotes.
 */
inline std::string shell_escape(const std::string& text) {
    if (text.empty()) return "''";
    
    std::string res = "'";
    for (char c : text) {
        if (c == '\'') {
            res += "'\\''";
        } else {
            res += c;
        }
    }
    res += "'";
    return res;
}

/**
 * @brief Returns a lowercase version of a string.
 */
inline std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

} // namespace utils
} // namespace bronx
