#include "guild_settings_operations.h"
#include "../../core/connection_pool.h"
#include <mariadb/mysql.h>
#include <iostream>

namespace bronx {
namespace db {

std::optional<GuildProfile> get_guild_profile_internal(Database& db, uint64_t guild_id) {
    auto conn = db.get_pool()->acquire();
    if (!conn) return std::nullopt;

    const char* query = "SELECT server_bio, server_website, server_banner_url, server_avatar_url "
                       "FROM guild_settings WHERE guild_id = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) return std::nullopt;

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        db.log_error("get_guild_profile prepare");
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    bind_in[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_in[0].buffer = &guild_id;
    bind_in[0].is_unsigned = true;

    if (mysql_stmt_bind_param(stmt, bind_in)) {
        db.log_error("get_guild_profile bind");
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt)) {
        db.log_error("get_guild_profile execute");
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    char bio[4096] = {0}, website[256] = {0}, banner[512] = {0}, avatar[512] = {0};
    unsigned long bio_len, web_len, banner_len, avatar_len;
    my_bool bio_null, web_null, banner_null, avatar_null;

    MYSQL_BIND bind_out[4];
    memset(bind_out, 0, sizeof(bind_out));

    bind_out[0].buffer_type = MYSQL_TYPE_BLOB; // TEXT
    bind_out[0].buffer = bio;
    bind_out[0].buffer_length = sizeof(bio);
    bind_out[0].length = &bio_len;
    bind_out[0].is_null = &bio_null;

    bind_out[1].buffer_type = MYSQL_TYPE_STRING;
    bind_out[1].buffer = website;
    bind_out[1].buffer_length = sizeof(website);
    bind_out[1].length = &web_len;
    bind_out[1].is_null = &web_null;

    bind_out[2].buffer_type = MYSQL_TYPE_STRING;
    bind_out[2].buffer = banner;
    bind_out[2].buffer_length = sizeof(banner);
    bind_out[2].length = &banner_len;
    bind_out[2].is_null = &banner_null;

    bind_out[3].buffer_type = MYSQL_TYPE_STRING;
    bind_out[3].buffer = avatar;
    bind_out[3].buffer_length = sizeof(avatar);
    bind_out[3].length = &avatar_len;
    bind_out[3].is_null = &avatar_null;

    if (mysql_stmt_bind_result(stmt, bind_out)) {
        db.log_error("get_guild_profile bind result");
        mysql_stmt_close(stmt);
        return std::nullopt;
    }

    if (mysql_stmt_fetch(stmt) == 0) {
        GuildProfile profile;
        profile.guild_id = guild_id;
        profile.bio = bio_null ? "" : std::string(bio, bio_len);
        profile.website = web_null ? "" : std::string(website, web_len);
        profile.banner_url = banner_null ? "" : std::string(banner, banner_len);
        profile.avatar_url = avatar_null ? "" : std::string(avatar, avatar_len);
        mysql_stmt_close(stmt);
        return profile;
    }

    mysql_stmt_close(stmt);
    return std::nullopt;
}

bool update_guild_profile_field_internal(Database& db, uint64_t guild_id, const std::string& field, const std::string& value) {
    auto conn = db.get_pool()->acquire();
    if (!conn) return false;

    // Sanitize field name to prevent injection even though it's internal
    if (field != "server_bio" && field != "server_website" && field != "server_banner_url" && field != "server_avatar_url") {
        return false;
    }

    std::string query_str = "INSERT INTO guild_settings (guild_id, " + field + ") VALUES (?, ?) "
                           "ON DUPLICATE KEY UPDATE " + field + " = VALUES(" + field + ")";
    const char* query = query_str.c_str();

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        db.log_error("update_guild_profile prepare");
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;

    unsigned long val_len = value.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)value.c_str();
    bind[1].buffer_length = val_len;
    bind[1].length = &val_len;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db.log_error("update_guild_profile bind");
        mysql_stmt_close(stmt);
        return false;
    }

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        db.log_error("update_guild_profile execute");
    }
    mysql_stmt_close(stmt);
    return success;
}

bool clear_guild_profile_field_internal(Database& db, uint64_t guild_id, const std::string& field) {
    auto conn = db.get_pool()->acquire();
    if (!conn) return false;

    if (field != "server_bio" && field != "server_website" && field != "server_banner_url" && field != "server_avatar_url") {
        return false;
    }

    std::string query_str = "UPDATE guild_settings SET " + field + " = NULL WHERE guild_id = ?";
    const char* query = query_str.c_str();

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        db.log_error("clear_guild_profile prepare");
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = true;

    if (mysql_stmt_bind_param(stmt, bind)) {
        db.log_error("clear_guild_profile bind");
        mysql_stmt_close(stmt);
        return false;
    }

    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        db.log_error("clear_guild_profile execute");
    }
    mysql_stmt_close(stmt);
    return success;
}

} // namespace db
} // namespace bronx
