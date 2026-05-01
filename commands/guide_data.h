#pragma once
#include <string>
#include <vector>

namespace commands {
namespace guide {

struct GuidePage {
    std::string title;       // short label for select menu
    std::string emoji;       // emoji prefix for the menu option
    std::string content;     // full markdown description shown in embed
};

struct GuideSection {
    std::string name;        // section name (e.g. "economy", "server setup")
    std::string emoji;       // emoji for the section in the main menu
    std::string description; // one-liner for the main menu
    std::vector<GuidePage> pages;
    bool admin_only = false; // only show to users with manage_guild permission
};

// search result structure
struct GuideSearchResult {
    size_t section_idx;
    size_t page_idx;
    std::string section_name;
    std::string page_title;
    std::string match_context;  // snippet showing match
    int relevance;              // higher = better match
};

std::vector<GuideSection> get_guide_sections();
std::vector<GuideSearchResult> search_guide(const std::string& query, bool include_admin = false);
std::vector<std::string> get_section_names(bool include_admin = false);

} // namespace guide
} // namespace commands
