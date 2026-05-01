#include "ticket_operations.h"
#include "../../core/database.h"
#include "../../../utils/logger.h"

namespace bronx {
namespace db {
namespace ticket_operations {

uint64_t create_ticket_panel(Database* db, const TicketPanel& panel) {
    const char* q = "INSERT INTO guild_ticket_panels (guild_id, channel_id, message_id, name, category_id, support_role_id, ticket_type) VALUES (?,?,?,?,?,?,?)";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("create_ticket_panel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&panel.guild_id;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&panel.channel_id;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&panel.message_id;

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)panel.name.c_str();
    bind[3].buffer_length = panel.name.length();

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = (char*)&panel.category_id;

    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = (char*)&panel.support_role_id;

    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer = (char*)panel.ticket_type.c_str();
    bind[6].buffer_length = panel.ticket_type.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("create_ticket_panel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("create_ticket_panel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    uint64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return id;
}

std::optional<TicketPanel> get_ticket_panel(Database* db, uint64_t panel_id) {
    const char* q = "SELECT id, guild_id, channel_id, message_id, name, category_id, support_role_id, ticket_type FROM guild_ticket_panels WHERE id = ?";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("get_ticket_panel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&panel_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("get_ticket_panel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("get_ticket_panel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND res[8];
    memset(res, 0, sizeof(res));

    uint64_t r_id, r_guild_id, r_channel_id, r_message_id, r_cat_id, r_sup_id;
    char r_name[256], r_type[50];
    unsigned long l_name, l_type;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &r_id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &r_guild_id;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = &r_channel_id;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG; res[3].buffer = &r_message_id;
    res[4].buffer_type = MYSQL_TYPE_STRING;   res[4].buffer = r_name; res[4].buffer_length = 256; res[4].length = &l_name;
    res[5].buffer_type = MYSQL_TYPE_LONGLONG; res[5].buffer = &r_cat_id;
    res[6].buffer_type = MYSQL_TYPE_LONGLONG; res[6].buffer = &r_sup_id;
    res[7].buffer_type = MYSQL_TYPE_STRING;   res[7].buffer = r_type; res[7].buffer_length = 50; res[7].length = &l_type;

    if (mysql_stmt_bind_result(stmt, res)) {
        db->log_error("get_ticket_panel bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (!mysql_stmt_fetch(stmt)) {
        TicketPanel p;
        p.id = r_id;
        p.guild_id = r_guild_id;
        p.channel_id = r_channel_id;
        p.message_id = r_message_id;
        p.name = std::string(r_name, l_name);
        p.category_id = r_cat_id;
        p.support_role_id = r_sup_id;
        p.ticket_type = std::string(r_type, l_type);
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return p;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return std::nullopt;
}

uint64_t create_active_ticket(Database* db, const ActiveTicket& ticket) {
    const char* q = "INSERT INTO guild_active_tickets (guild_id, channel_id, user_id, panel_id, status) VALUES (?,?,?,?,?)";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("create_active_ticket prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&ticket.guild_id;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&ticket.channel_id;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&ticket.user_id;

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (char*)&ticket.panel_id;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)ticket.status.c_str();
    bind[4].buffer_length = ticket.status.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("create_active_ticket bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("create_active_ticket execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    uint64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return id;
}

bool update_ticket_status(Database* db, uint64_t ticket_id, const std::string& status, uint64_t claimed_by) {
    const char* q = "UPDATE guild_active_tickets SET status = ?, claimed_by = ? WHERE id = ?";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("update_ticket_status prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)status.c_str();
    bind[0].buffer_length = status.length();

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&claimed_by;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&ticket_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("update_ticket_status bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return ok;
}

std::optional<ActiveTicket> get_active_ticket_by_channel(Database* db, uint64_t channel_id) {
    const char* q = "SELECT id, guild_id, channel_id, user_id, panel_id, status, claimed_by FROM guild_active_tickets WHERE channel_id = ?";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("get_active_ticket_by_channel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&channel_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("get_active_ticket_by_channel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("get_active_ticket_by_channel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND res[7];
    memset(res, 0, sizeof(res));

    uint64_t r_id, r_guild_id, r_channel_id, r_user_id, r_panel_id, r_claimed_by;
    char r_status[50];
    unsigned long l_status;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &r_id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &r_guild_id;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = &r_channel_id;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG; res[3].buffer = &r_user_id;
    res[4].buffer_type = MYSQL_TYPE_LONGLONG; res[4].buffer = &r_panel_id;
    res[5].buffer_type = MYSQL_TYPE_STRING;   res[5].buffer = r_status; res[5].buffer_length = 50; res[5].length = &l_status;
    res[6].buffer_type = MYSQL_TYPE_LONGLONG; res[6].buffer = &r_claimed_by;

    if (mysql_stmt_bind_result(stmt, res)) {
        db->log_error("get_active_ticket_by_channel bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (!mysql_stmt_fetch(stmt)) {
        ActiveTicket t;
        t.id = r_id;
        t.guild_id = r_guild_id;
        t.channel_id = r_channel_id;
        t.user_id = r_user_id;
        t.panel_id = r_panel_id;
        t.status = std::string(r_status, l_status);
        t.claimed_by = r_claimed_by;
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return t;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return std::nullopt;
}

std::vector<ActiveTicket> get_user_active_tickets(Database* db, uint64_t guild_id, uint64_t user_id) {
    std::vector<ActiveTicket> tickets;
    const char* q = "SELECT id, guild_id, channel_id, user_id, panel_id, status, claimed_by FROM guild_active_tickets WHERE guild_id = ? AND user_id = ? AND status != 'closed'";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("get_user_active_tickets prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return tickets;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&guild_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("get_user_active_tickets bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return tickets;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("get_user_active_tickets execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return tickets;
    }

    MYSQL_BIND res[7];
    memset(res, 0, sizeof(res));

    uint64_t r_id, r_guild_id, r_channel_id, r_user_id, r_panel_id, r_claimed_by;
    char r_status[50];
    unsigned long l_status;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &r_id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &r_guild_id;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = &r_channel_id;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG; res[3].buffer = &r_user_id;
    res[4].buffer_type = MYSQL_TYPE_LONGLONG; res[4].buffer = &r_panel_id;
    res[5].buffer_type = MYSQL_TYPE_STRING;   res[5].buffer = r_status; res[5].buffer_length = 50; res[5].length = &l_status;
    res[6].buffer_type = MYSQL_TYPE_LONGLONG; res[6].buffer = &r_claimed_by;

    if (mysql_stmt_bind_result(stmt, res)) {
        db->log_error("get_user_active_tickets bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return tickets;
    }

    while (!mysql_stmt_fetch(stmt)) {
        ActiveTicket t;
        t.id = r_id;
        t.guild_id = r_guild_id;
        t.channel_id = r_channel_id;
        t.user_id = r_user_id;
        t.panel_id = r_panel_id;
        t.status = std::string(r_status, l_status);
        t.claimed_by = r_claimed_by;
        tickets.push_back(t);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return tickets;
}

std::vector<TicketPanel> get_guild_ticket_panels(Database* db, uint64_t guild_id) {
    // Left unimplemented for brevity as it's not strictly needed for this pass,
    // but the stub is here for the header.
    return {};
}

} // namespace ticket_operations
} // namespace db
} // namespace bronx
