#pragma once
#include "../../core/database.h"
#include "../../core/types.h"
#include <vector>
#include <optional>

namespace bronx {
namespace db {
namespace ticket_operations {

uint64_t create_ticket_panel(Database* db, const TicketPanel& panel);
std::optional<TicketPanel> get_ticket_panel(Database* db, uint64_t panel_id);
std::vector<TicketPanel> get_guild_ticket_panels(Database* db, uint64_t guild_id);

uint64_t create_active_ticket(Database* db, const ActiveTicket& ticket);
bool update_ticket_status(Database* db, uint64_t ticket_id, const std::string& status, uint64_t claimed_by = 0);
std::optional<ActiveTicket> get_active_ticket_by_channel(Database* db, uint64_t channel_id);
std::vector<ActiveTicket> get_user_active_tickets(Database* db, uint64_t guild_id, uint64_t user_id);

} // namespace ticket_operations
} // namespace db
} // namespace bronx
