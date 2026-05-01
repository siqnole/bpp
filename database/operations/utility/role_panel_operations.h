#pragma once
#include "../../core/database.h"
#include "../../core/types.h"
#include <vector>
#include <optional>

namespace bronx {
namespace db {
namespace role_panel_operations {

uint64_t create_panel(Database* db, const InteractionPanel& panel);
bool delete_panel(Database* db, uint64_t panel_id, uint64_t guild_id);
std::optional<InteractionPanel> get_panel(Database* db, uint64_t panel_id);
std::vector<InteractionPanel> get_guild_panels(Database* db, uint64_t guild_id);

uint64_t add_role_to_panel(Database* db, const InteractionRole& role);
bool remove_role_from_panel(Database* db, uint64_t role_entry_id);
std::vector<InteractionRole> get_panel_roles(Database* db, uint64_t panel_id);

// For interaction handling
std::optional<InteractionRole> find_role_by_button_id(Database* db, const std::string& custom_id);

} // namespace role_panel_operations
} // namespace db
} // namespace bronx
