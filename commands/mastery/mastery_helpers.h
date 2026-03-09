#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <algorithm>

namespace commands {
namespace mastery {

// ============================================================================
// MASTERY TIER DEFINITIONS
// ============================================================================

struct MasteryTier {
    std::string name;
    std::string emoji;
    int catches_required;      // total catches to reach this tier
    double value_bonus;        // permanent % bonus to sell value of this species
    std::string color_hex;     // for display
};

// Tiers: each tier grants an *additional* value bonus on top of previous tiers
inline const std::vector<MasteryTier>& get_mastery_tiers() {
    static const std::vector<MasteryTier> tiers = {
        {"Novice",       "\xE2\xAC\x9C",                       1,   0.00, "808080"},  // gray
        {"Apprentice",   "\xF0\x9F\x9F\xA2",                  10,   0.01, "22C55E"},  // green
        {"Journeyman",   "\xF0\x9F\x94\xB5",                  25,   0.02, "3B82F6"},  // blue
        {"Expert",       "\xF0\x9F\x9F\xA3",                  50,   0.03, "8B5CF6"},  // purple
        {"Master",       "\xF0\x9F\x9F\xA1",                 100,   0.05, "F59E0B"},  // gold
        {"Grandmaster",  "\xF0\x9F\x94\xB4",                 250,   0.07, "EF4444"},  // red
        {"Legend",       "\xF0\x9F\x92\x8E",                  500,   0.10, "06B6D4"},  // diamond
        {"Mythic",       "\xE2\xAD\x90",                    1000,   0.15, "FBBF24"},  // star
    };
    return tiers;
}

// Get the mastery tier for a given catch count
inline const MasteryTier& get_tier_for_catches(int catches) {
    const auto& tiers = get_mastery_tiers();
    const MasteryTier* best = &tiers[0];
    for (const auto& tier : tiers) {
        if (catches >= tier.catches_required) {
            best = &tier;
        }
    }
    return *best;
}

// Get total cumulative value bonus for a given catch count
inline double get_total_value_bonus(int catches) {
    double total = 0.0;
    for (const auto& tier : get_mastery_tiers()) {
        if (catches >= tier.catches_required) {
            total = tier.value_bonus; // tiers are cumulative, each tier's bonus is the total at that point
        }
    }
    return total;
}

// Get progress to next tier
struct MasteryProgress {
    const MasteryTier* current_tier;
    const MasteryTier* next_tier;  // nullptr if max tier
    int current_catches;
    int catches_to_next;           // 0 if max tier
    double progress_percent;       // 0-100
    double total_bonus;            // cumulative value bonus
};

inline MasteryProgress get_mastery_progress(int catches) {
    const auto& tiers = get_mastery_tiers();
    MasteryProgress prog;
    prog.current_catches = catches;
    prog.current_tier = &tiers[0];
    prog.next_tier = nullptr;
    
    for (size_t i = 0; i < tiers.size(); i++) {
        if (catches >= tiers[i].catches_required) {
            prog.current_tier = &tiers[i];
            if (i + 1 < tiers.size()) {
                prog.next_tier = &tiers[i + 1];
            } else {
                prog.next_tier = nullptr;
            }
        }
    }
    
    prog.total_bonus = get_total_value_bonus(catches);
    
    if (prog.next_tier) {
        prog.catches_to_next = prog.next_tier->catches_required - catches;
        int range = prog.next_tier->catches_required - prog.current_tier->catches_required;
        int progress = catches - prog.current_tier->catches_required;
        prog.progress_percent = range > 0 ? (progress * 100.0 / range) : 100.0;
    } else {
        prog.catches_to_next = 0;
        prog.progress_percent = 100.0;
    }
    
    return prog;
}

// Build a visual progress bar
inline std::string build_progress_bar(double percent, int width = 10) {
    int filled = static_cast<int>(percent / 100.0 * width);
    filled = std::max(0, std::min(filled, width));
    
    std::string bar;
    for (int i = 0; i < width; i++) {
        bar += (i < filled) ? "\xE2\x96\x93" : "\xE2\x96\x91"; // filled vs empty block
    }
    return bar;
}

// ============================================================================
// MASTERY TYPE CLASSIFICATION
// ============================================================================

enum class MasteryType {
    Fish,
    Ore
};

inline std::string mastery_type_name(MasteryType type) {
    switch (type) {
        case MasteryType::Fish: return "Fish";
        case MasteryType::Ore: return "Ore";
        default: return "Unknown";
    }
}

inline std::string mastery_type_emoji(MasteryType type) {
    switch (type) {
        case MasteryType::Fish: return "\xF0\x9F\x8E\xA3";
        case MasteryType::Ore: return "\xE2\x9B\x8F\xEF\xB8\x8F";
        default: return "\xE2\x9D\x93";
    }
}

// ============================================================================
// SUMMARY STATS
// ============================================================================

struct MasterySummary {
    int total_species;        // total unique species tracked
    int mastered_species;     // species at Master tier or above
    int mythic_species;       // species at Mythic tier
    double avg_bonus;         // average value bonus across all species
    std::string best_species; // species with highest catch count
    int best_catches;         // catch count of best species
};

inline MasterySummary calculate_summary(const std::map<std::string, int64_t>& catch_counts) {
    MasterySummary summary = {};
    double total_bonus = 0.0;
    
    for (const auto& [species, count] : catch_counts) {
        summary.total_species++;
        auto prog = get_mastery_progress(static_cast<int>(count));
        total_bonus += prog.total_bonus;
        
        if (count >= 100) summary.mastered_species++;
        if (count >= 1000) summary.mythic_species++;
        
        if (count > summary.best_catches) {
            summary.best_catches = static_cast<int>(count);
            summary.best_species = species;
        }
    }
    
    summary.avg_bonus = summary.total_species > 0 ? (total_bonus / summary.total_species) : 0.0;
    return summary;
}

} // namespace mastery
} // namespace commands
