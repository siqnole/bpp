#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../../security/input_validation.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace economy {

// Escape a string for safe MySQL query insertion
inline std::string db_escape(Database* db, const std::string& input) {
    auto conn = db->get_pool()->acquire();
    std::vector<char> buf(input.length() * 2 + 1);
    mysql_real_escape_string(conn->get(), buf.data(), input.c_str(), input.length());
    db->get_pool()->release(conn);
    return std::string(buf.data());
}

// Execute a SELECT query and return the result (caller must mysql_free_result)
// Returns nullptr on failure. Connection is acquired and released internally.
inline MYSQL_RES* db_select(Database* db, const std::string& sql) {
    auto conn = db->get_pool()->acquire();
    MYSQL_RES* res = nullptr;
    if (mysql_query(conn->get(), sql.c_str()) == 0) {
        res = mysql_store_result(conn->get());
    }
    db->get_pool()->release(conn);
    return res;
}

// Execute a DML query (INSERT/UPDATE/DELETE) via raw sql string
// DEPRECATED: Use db_exec_safe() with parameterized queries for new code.
[[deprecated("Use parameterized queries via prepared statements instead")]]
inline bool db_exec(Database* db, const std::string& sql) {
    auto conn = db->get_pool()->acquire();
    bool ok = (mysql_query(conn->get(), sql.c_str()) == 0);
    db->get_pool()->release(conn);
    return ok;
}

// Safe DML execution — still raw SQL but with explicit deprecation warning.
// For truly safe execution, use prepared statements via Database methods.
inline bool db_exec_raw(Database* db, const std::string& sql) {
    auto conn = db->get_pool()->acquire();
    bool ok = (mysql_query(conn->get(), sql.c_str()) == 0);
    db->get_pool()->release(conn);
    return ok;
}

// Helper function to parse amount strings (1k, 1.5m, 50%, all, etc.)
inline int64_t parse_amount(const ::std::string& input, int64_t user_balance) {
    ::std::string lower = input;
    ::std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Remove whitespace
    lower.erase(::std::remove_if(lower.begin(), lower.end(), ::isspace), lower.end());
    
    if (lower.empty()) {
        throw ::std::invalid_argument("amount cannot be empty");
    }
    
    // Special keywords
    if (lower == "all" || lower == "max" || lower == "lifesavings") {
        return user_balance;
    }
    if (lower == "half") {
        return user_balance / 2;
    }
    
    // Percentage
    if (lower.back() == '%') {
        ::std::string num_str = lower.substr(0, lower.length() - 1);
        if (num_str.empty()) {
            throw ::std::invalid_argument("invalid percentage format");
        }
        try {
            double percent = ::std::stod(num_str);
            if (percent < 0 || percent > 100) {
                throw ::std::invalid_argument("percentage must be between 0 and 100");
            }
            return static_cast<int64_t>(user_balance * (percent / 100.0));
        } catch (const ::std::invalid_argument&) {
            throw ::std::invalid_argument("invalid number in percentage");
        }
    }
    
    // Scientific notation (1e6, 2.5e5)
    // SECURITY FIX: Check for int64_t overflow before casting.
    if (lower.find('e') != ::std::string::npos) {
        auto result = bronx::security::safe_parse_scientific(lower);
        if (!result) {
            throw ::std::invalid_argument("invalid or overflowing scientific notation");
        }
        return *result;
    }
    
    // K/M/B/T/Qd/Qt/Sq/Sp/Oc/No/Dc suffix (case-insensitive)
    double multiplier = 1.0;
    // multi-char suffixes first (longest match)
    auto ends_with_ci = [&](const ::std::string& s, const ::std::string& suffix) -> bool {
        if (s.size() < suffix.size()) return false;
        return ::std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
            [](char a, char b){ return ::tolower(a) == ::tolower(b); });
    };
    struct SuffixEntry { const char* label; double factor; };
    static const SuffixEntry MULTI_SUFFIXES[] = {
        // longest first so "qd" is matched before "q" etc.
        {"dc",  1e33},  // decillion
        {"no",  1e30},  // nonillion
        {"oc",  1e27},  // octillion
        {"sp",  1e24},  // septillion
        {"sq",  1e21},  // sextillion (user notation)
        {"sx",  1e21},  // sextillion (alt)
        {"qt",  1e18},  // quintillion
        {"qd",  1e15},  // quadrillion
        {"ud",  1e36},  // undecillion
        {"dd",  1e39},  // duodecillion
        {"td",  1e42},  // tredecillion
        {"qa",  1e45},  // quattuordecillion
        {"qi",  1e48},  // quindecillion
    };
    bool matched_multi = false;
    for (const auto& se : MULTI_SUFFIXES) {
        if (ends_with_ci(lower, se.label)) {
            multiplier = se.factor;
            lower = lower.substr(0, lower.length() - ::std::strlen(se.label));
            matched_multi = true;
            break;
        }
    }
    if (!matched_multi) {
        if (lower.back() == 'k') {
            multiplier = 1e3;
            lower = lower.substr(0, lower.length() - 1);
        } else if (lower.back() == 'm') {
            multiplier = 1e6;
            lower = lower.substr(0, lower.length() - 1);
        } else if (lower.back() == 'b') {
            multiplier = 1e9;
            lower = lower.substr(0, lower.length() - 1);
        } else if (lower.back() == 't') {
            multiplier = 1e12;
            lower = lower.substr(0, lower.length() - 1);
        } else if (lower.back() == 'q') {
            multiplier = 1e15;  // quadrillion shorthand
            lower = lower.substr(0, lower.length() - 1);
        }
    }
    
    if (lower.empty()) {
        throw ::std::invalid_argument("invalid amount format");
    }
    
    try {
        double value = ::std::stod(lower);
        if (value < 0) {
            throw ::std::invalid_argument("amount cannot be negative");
        }
        // SECURITY FIX: Check for int64_t overflow before casting
        double product = value * multiplier;
        if (product > static_cast<double>(INT64_MAX) || std::isinf(product) || std::isnan(product)) {
            throw ::std::invalid_argument("amount too large");
        }
        return static_cast<int64_t>(product);
    } catch (const ::std::invalid_argument&) {
        throw; // re-throw our own invalid_argument messages
    } catch (const ::std::out_of_range&) {
        throw ::std::invalid_argument("number too large");
    }
}

// Format number with suffixes for large values (>= 100M), commas below
inline ::std::string format_number(int64_t num) {
    bool negative = (num < 0);
    // Use double abs so INT64_MIN doesn't overflow
    double abs_d = static_cast<double>(num < 0 ? -num : num);

    struct Tier { double threshold; const char* suffix; };
    static const Tier TIERS[] = {
        {1e48,  "Qi"},   // quindecillion
        {1e45,  "Qa"},   // quattuordecillion
        {1e42,  "Td"},   // tredecillion
        {1e39,  "Dd"},   // duodecillion
        {1e36,  "Ud"},   // undecillion
        {1e33,  "Dc"},   // decillion
        {1e30,  "No"},   // nonillion
        {1e27,  "Oc"},   // octillion
        {1e24,  "Sp"},   // septillion
        {1e21,  "Sq"},   // sextillion
        {1e18,  "Qt"},   // quintillion
        {1e15,  "Qd"},   // quadrillion
        {1e12,  "T"},    // trillion
        {1e9,   "B"},    // billion
        {1e6,   "M"},    // million
    };

    for (const auto& t : TIERS) {
        if (abs_d >= t.threshold) {
            double val = static_cast<double>(num) / t.threshold;
            // Format with up to 2 decimal places, strip trailing zeros
            char buf[64];
            ::snprintf(buf, sizeof(buf), "%.2f", ::std::abs(val));
            ::std::string s(buf);
            // Strip trailing zeros after decimal point
            if (s.find('.') != ::std::string::npos) {
                s.erase(s.find_last_not_of('0') + 1);
                if (!s.empty() && s.back() == '.') s.pop_back();
            }
            return (negative ? "-" : "") + s + t.suffix;
        }
    }

    // Comma-formatted for values < 100,000,000
    ::std::string str = ::std::to_string(num < 0 ? -num : num);
    int insert_position = static_cast<int>(str.length()) - 3;
    while (insert_position > 0) {
        str.insert(insert_position, ",");
        insert_position -= 3;
    }
    return (negative ? "-" : "") + str;
}

// Explicit overloads for int/unsigned int/uint64_t (distinct from int64_t = long on 64-bit Linux)
inline ::std::string format_number(int num)           { return format_number(static_cast<int64_t>(num)); }
inline ::std::string format_number(unsigned int num)  { return format_number(static_cast<int64_t>(num)); }
inline ::std::string format_number(unsigned long num) { return format_number(static_cast<int64_t>(num)); }  // uint64_t = unsigned long

// Overload for double — handles astronomical values beyond int64_t range
inline ::std::string format_number(double num) {
    bool negative = (num < 0.0);
    double abs_d = ::std::abs(num);

    struct Tier { double threshold; const char* suffix; };
    static const Tier TIERS[] = {
        {1e57,  "Vig"},  // vigintillion
        {1e54,  "Nvd"},  // novemdecillion
        {1e51,  "Ocd"},  // octodecillion
        {1e48,  "Qi"},   // quindecillion
        {1e45,  "Qa"},   // quattuordecillion
        {1e42,  "Td"},   // tredecillion
        {1e39,  "Dd"},   // duodecillion
        {1e36,  "Ud"},   // undecillion
        {1e33,  "Dc"},   // decillion
        {1e30,  "No"},   // nonillion
        {1e27,  "Oc"},   // octillion
        {1e24,  "Sp"},   // septillion
        {1e21,  "Sq"},   // sextillion
        {1e18,  "Qt"},   // quintillion
        {1e15,  "Qd"},   // quadrillion
        {1e12,  "T"},    // trillion
        {1e9,   "B"},    // billion
        {1e6,   "M"},    // million
    };

    for (const auto& t : TIERS) {
        if (abs_d >= t.threshold) {
            double val = num / t.threshold;
            char buf[64];
            ::snprintf(buf, sizeof(buf), "%.2f", ::std::abs(val));
            ::std::string s(buf);
            if (s.find('.') != ::std::string::npos) {
                s.erase(s.find_last_not_of('0') + 1);
                if (!s.empty() && s.back() == '.') s.pop_back();
            }
            return (negative ? "-" : "") + s + t.suffix;
        }
    }

    // Comma-formatted for values < 100,000,000
    int64_t inum = static_cast<int64_t>(num);
    bool ineg = (inum < 0);
    ::std::string str = ::std::to_string(ineg ? -inum : inum);
    int insert_position = static_cast<int>(str.length()) - 3;
    while (insert_position > 0) {
        str.insert(insert_position, ",");
        insert_position -= 3;
    }
    return (ineg ? "-" : "") + str;
}

// Format a requirement line with check/deny emoji
inline ::std::string format_requirement(bool met, const ::std::string& text) {
    return (met ? bronx::EMOJI_CHECK + " " : bronx::EMOJI_DENY + " ") + text;
}

} // namespace economy
} // namespace commands
