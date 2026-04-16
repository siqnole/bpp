#pragma once

// ============================================================================
// shop.h — DECLARATIONS ONLY
// All implementations are in shop.cpp to avoid recompiling
// the entire project when shop logic changes (~1,840 lines).
// ============================================================================

#include "../../command.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <vector>
#include <string>
#include <optional>

using namespace bronx::db;

namespace commands {

// Shop pagination constant
constexpr int ITEMS_PER_PAGE = 5;

// Convenience alias
using bronx::db::ShopItem;

// ── Utility functions kept inline (used by other headers) ────────────

// Parse required prestige from item metadata JSON
inline int get_required_prestige(const std::string& metadata) {
    size_t pos = metadata.find("\"prestige\":");
    if (pos == std::string::npos) return 0;
    pos = metadata.find_first_of("0123456789", pos + 11);
    if (pos == std::string::npos) return 0;
    size_t end = pos;
    while (end < metadata.size() && std::isdigit(metadata[end])) end++;
    try { return std::stoi(metadata.substr(pos, end - pos)); } catch(...) {}
    return 0;
}

// Format prestige requirement indicator
inline std::string prestige_indicator(int prestige) {
    if (prestige <= 0) return "";
    return " " + bronx::EMOJI_STAR + "P" + std::to_string(prestige);
}

// Normalize category aliases
inline std::string normalize_category(std::string cat) {
    std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
    if (cat == "fishing bait" || cat == "fishingbait") return "bait";
    if (cat == "fishing rod" || cat == "fishingrod" || cat == "rods") return "rod";
    if (cat == "baits") return "bait";
    if (cat == "title" || cat == "titles" || cat == "cosmetic" || cat == "cosmetics") return "title";
    if (cat == "pickaxes" || cat == "pick") return "pickaxe";
    if (cat == "minecarts" || cat == "cart" || cat == "carts") return "minecart";
    if (cat == "bags" || cat == "mining bag" || cat == "mining bags") return "bag";
    if (cat == "lootbox" || cat == "lootboxes" || cat == "crate" || cat == "crates" || cat == "box" || cat == "boxes") return "lootbox";
    if (cat == "automation" || cat == "autofisher" || cat == "auto") return "automation";
    return cat;
}

// Friendly item name lookup
inline std::string friendly_item_name(Database* db, const std::string& item_id) {
    auto maybe = db->get_shop_item(item_id);
    if (maybe) return maybe->name;
    return item_id;
}

// Fuzzy item resolver
inline std::optional<ShopItem> find_shop_item(Database* db, const std::string& token) {
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto items = db->get_shop_items();
    for (const auto& it : items) { std::string id = it.item_id; std::transform(id.begin(), id.end(), id.begin(), ::tolower); if (id == lower) return it; }
    for (const auto& it : items) { std::string name = it.name; std::transform(name.begin(), name.end(), name.begin(), ::tolower); if (name == lower) return it; }
    if (lower == "pi") { for (const auto& it : items) { if (it.item_id == "bait_pi") return it; } }
    for (const auto& it : items) { std::string id = it.item_id; std::transform(id.begin(), id.end(), id.begin(), ::tolower); if (id.rfind(lower, 0) == 0) return it; }
    for (const auto& it : items) { std::string name = it.name; std::transform(name.begin(), name.end(), name.begin(), ::tolower); if (name.rfind(lower, 0) == 0) return it; }
    for (const auto& it : items) { std::string id = it.item_id; std::transform(id.begin(), id.end(), id.begin(), ::tolower); if (id.find(lower) != std::string::npos) return it; }
    for (const auto& it : items) { std::string name = it.name; std::transform(name.begin(), name.end(), name.begin(), ::tolower); if (name.find(lower) != std::string::npos) return it; }
    return {};
}

// ── Heavy implementations (defined in shop.cpp) ──────────────────────
std::vector<Command*> get_shop_commands(Database* db);
void register_shop_interactions(dpp::cluster& bot, Database* db);

} // namespace commands
