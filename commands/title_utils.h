#pragma once
#include <string>

namespace commands {

// Encode a title display string as valid JSON for the metadata column.
// Format: {"display":"VALUE"} — handles embedded quotes and backslashes.
inline std::string title_display_to_json(const std::string& display) {
    std::string result = "{\"display\":\"";
    for (char c : display) {
        if      (c == '"')  result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else                result += c;
    }
    result += "\"}";
    return result;
}

} // namespace commands
