#pragma once
// ============================================================================
// InputValidation — Safe parsing utilities to replace raw stoi/stoull/stod.
// All functions return std::optional instead of throwing on invalid input.
// ============================================================================

#include <string>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cfloat>

namespace bronx {
namespace security {

// Safe integer parsing — returns nullopt on invalid input, overflow, etc.
inline std::optional<int> safe_stoi(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        int result = std::stoi(s, &pos);
        if (pos != s.size()) return std::nullopt;  // trailing chars
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<int64_t> safe_stoll(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        int64_t result = std::stoll(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<uint64_t> safe_stoull(const std::string& s) {
    if (s.empty()) return std::nullopt;
    // Reject negative numbers (stoull accepts them via wrap-around)
    std::string trimmed = s;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    if (!trimmed.empty() && trimmed.front() == '-') return std::nullopt;
    try {
        size_t pos = 0;
        uint64_t result = std::stoull(trimmed, &pos);
        if (pos != trimmed.size()) return std::nullopt;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<double> safe_stod(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        double result = std::stod(s, &pos);
        if (pos != s.size()) return std::nullopt;
        if (std::isnan(result) || std::isinf(result)) return std::nullopt;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

// Validate a string for safe database insertion.
// Allows alphanumeric, spaces, basic punctuation. Rejects control chars.
// Enforces a maximum length.
inline bool is_safe_string(const std::string& input, size_t max_length = 100) {
    if (input.empty() || input.size() > max_length) return false;
    for (unsigned char c : input) {
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return false;  // control chars
        if (c == 0x7F) return false;  // DEL
    }
    return true;
}

// Sanitize a string: remove control characters, enforce length.
inline std::string sanitize_string(const std::string& input, size_t max_length = 100) {
    std::string result;
    result.reserve(std::min(input.size(), max_length));
    for (unsigned char c : input) {
        if (result.size() >= max_length) break;
        if (c >= 0x20 && c != 0x7F) {
            result += static_cast<char>(c);
        }
    }
    return result;
}

// Extract a uint64_t from a Discord custom_id suffix after the last underscore.
// Returns nullopt if the format is unexpected.
inline std::optional<uint64_t> parse_custom_id_suffix(const std::string& custom_id) {
    size_t last_underscore = custom_id.rfind('_');
    if (last_underscore == std::string::npos || last_underscore + 1 >= custom_id.size()) {
        return std::nullopt;
    }
    return safe_stoull(custom_id.substr(last_underscore + 1));
}

// Safe amount parsing with overflow protection.
// This is a hardened version that rejects values that overflow int64_t.
inline std::optional<int64_t> safe_parse_scientific(const std::string& input) {
    auto val = safe_stod(input);
    if (!val) return std::nullopt;
    double d = *val;
    if (d < 0) return std::nullopt;
    // Check for overflow before casting
    // INT64_MAX = 9223372036854775807 ≈ 9.22e18
    if (d > static_cast<double>(INT64_MAX)) return std::nullopt;
    return static_cast<int64_t>(d);
}

} // namespace security
} // namespace bronx
