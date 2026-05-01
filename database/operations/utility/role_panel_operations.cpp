#include "role_panel_operations.h"
#include "../../core/database.h"
#include "../../../utils/logger.h"

namespace bronx {
namespace db {
namespace role_panel_operations {

uint64_t create_panel(Database* db, const InteractionPanel& panel) {
    const char* q = "INSERT INTO guild_interaction_panels (guild_id, channel_id, message_id, name, description, panel_type) VALUES (?,?,?,?,?,?)";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("create_panel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    MYSQL_BIND bind[6];
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

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)panel.description.c_str();
    bind[4].buffer_length = panel.description.length();

    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer = (char*)panel.panel_type.c_str();
    bind[5].buffer_length = panel.panel_type.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("create_panel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("create_panel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    uint64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return id;
}

uint64_t add_role_to_panel(Database* db, const InteractionRole& role) {
    const char* q = "INSERT INTO guild_interaction_roles (panel_id, role_id, label, emoji_raw, style) VALUES (?,?,?,?,?)";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("add_role_to_panel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&role.panel_id;

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&role.role_id;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)role.label.c_str();
    bind[2].buffer_length = role.label.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)role.emoji_raw.c_str();
    bind[3].buffer_length = role.emoji_raw.length();

    bind[4].buffer_type = MYSQL_TYPE_LONG;
    bind[4].buffer = (char*)&role.style;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("add_role_to_panel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("add_role_to_panel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return 0;
    }

    uint64_t id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return id;
}

std::vector<InteractionRole> get_panel_roles(Database* db, uint64_t panel_id) {
    std::vector<InteractionRole> roles;
    const char* q = "SELECT id, panel_id, role_id, label, emoji_raw, style FROM guild_interaction_roles WHERE panel_id = ?";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("get_panel_roles prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return roles;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&panel_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("get_panel_roles bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return roles;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("get_panel_roles execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return roles;
    }

    MYSQL_BIND res[6];
    memset(res, 0, sizeof(res));

    uint64_t r_id, r_panel_id, r_role_id;
    char r_label[256], r_emoji[256];
    unsigned long l_label, l_emoji;
    int r_style;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &r_id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &r_panel_id;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = &r_role_id;
    res[3].buffer_type = MYSQL_TYPE_STRING;   res[3].buffer = r_label; res[3].buffer_length = 256; res[3].length = &l_label;
    res[4].buffer_type = MYSQL_TYPE_STRING;   res[4].buffer = r_emoji; res[4].buffer_length = 256; res[4].length = &l_emoji;
    res[5].buffer_type = MYSQL_TYPE_LONG;     res[5].buffer = &r_style;

    if (mysql_stmt_bind_result(stmt, res)) {
        db->log_error("get_panel_roles bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return roles;
    }

    while (!mysql_stmt_fetch(stmt)) {
        InteractionRole r;
        r.id = r_id;
        r.panel_id = r_panel_id;
        r.role_id = r_role_id;
        r.label = std::string(r_label, l_label);
        r.emoji_raw = std::string(r_emoji, l_emoji);
        r.style = r_style;
        roles.push_back(r);
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return roles;
}

std::optional<InteractionPanel> get_panel(Database* db, uint64_t panel_id) {
    const char* q = "SELECT id, guild_id, channel_id, message_id, name, description, panel_type FROM guild_interaction_panels WHERE id = ?";
    auto conn = db->get_pool()->acquire();
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        db->log_error("get_panel prepare");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&panel_id;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db->log_error("get_panel bind");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt)) {
        db->log_error("get_panel execute");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    MYSQL_BIND res[7];
    memset(res, 0, sizeof(res));

    uint64_t r_id, r_guild_id, r_channel_id, r_message_id;
    char r_name[256], r_desc[2048], r_type[50];
    unsigned long l_name, l_desc, l_type;

    res[0].buffer_type = MYSQL_TYPE_LONGLONG; res[0].buffer = &r_id;
    res[1].buffer_type = MYSQL_TYPE_LONGLONG; res[1].buffer = &r_guild_id;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = &r_channel_id;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG; res[3].buffer = &r_message_id;
    res[4].buffer_type = MYSQL_TYPE_STRING;   res[4].buffer = r_name; res[4].buffer_length = 256; res[4].length = &l_name;
    res[5].buffer_type = MYSQL_TYPE_STRING;   res[5].buffer = r_desc; res[5].buffer_length = 2048; res[5].length = &l_desc;
    res[6].buffer_type = MYSQL_TYPE_STRING;   res[6].buffer = r_type; res[6].buffer_length = 50; res[6].length = &l_type;

    if (mysql_stmt_bind_result(stmt, res)) {
        db->log_error("get_panel bind result");
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return std::nullopt;
    }

    if (!mysql_stmt_fetch(stmt)) {
        InteractionPanel p;
        p.id = r_id;
        p.guild_id = r_guild_id;
        p.channel_id = r_channel_id;
        p.message_id = r_message_id;
        p.name = std::string(r_name, l_name);
        p.description = std::string(r_desc, l_desc);
        p.panel_type = std::string(r_type, l_type);
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return p;
    }

    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return std::nullopt;
}

} // namespace role_panel_operations
} // namespace db
} // namespace bronx
