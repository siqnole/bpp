#include "infraction_config_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ============================================================================
// InfractionConfig — guild_infraction_config
// ============================================================================

std::optional<InfractionConfig> Database::get_infraction_config(uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* q = "SELECT guild_id, point_timeout, point_mute, point_kick, point_ban, point_warn, "
        "default_duration_timeout, default_duration_mute, default_duration_kick, "
        "default_duration_ban, default_duration_warn, "
        "COALESCE(escalation_rules, '[]'), "
        "COALESCE(mute_role_id, 0), COALESCE(jail_role_id, 0), COALESCE(jail_channel_id, 0), "
        "COALESCE(log_channel_id, 0), dm_on_action, "
        "quiet_global, COALESCE(quiet_overrides, '{}'), case_counter, require_reason "
        "FROM guild_infraction_config WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_infraction_config prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_infraction_config execute");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }

    InfractionConfig c;
    char esc_buf[4096], q_ov_buf[4096];
    unsigned long esc_len, q_ov_len;
    my_bool dm, q_gl, req_r;
    uint64_t mute_r, jail_r, jail_c, log_c;
    uint32_t dur_t, dur_m, dur_k, dur_b, dur_w;
    int c_count;

    MYSQL_BIND rb[21]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&c.guild_id; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_DOUBLE; rb[1].buffer = (char*)&c.point_timeout;
    rb[2].buffer_type = MYSQL_TYPE_DOUBLE; rb[2].buffer = (char*)&c.point_mute;
    rb[3].buffer_type = MYSQL_TYPE_DOUBLE; rb[3].buffer = (char*)&c.point_kick;
    rb[4].buffer_type = MYSQL_TYPE_DOUBLE; rb[4].buffer = (char*)&c.point_ban;
    rb[5].buffer_type = MYSQL_TYPE_DOUBLE; rb[5].buffer = (char*)&c.point_warn;
    rb[6].buffer_type = MYSQL_TYPE_LONG; rb[6].buffer = (char*)&dur_t; rb[6].is_unsigned = 1;
    rb[7].buffer_type = MYSQL_TYPE_LONG; rb[7].buffer = (char*)&dur_m; rb[7].is_unsigned = 1;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&dur_k; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_LONG; rb[9].buffer = (char*)&dur_b; rb[9].is_unsigned = 1;
    rb[10].buffer_type = MYSQL_TYPE_LONG; rb[10].buffer = (char*)&dur_w; rb[10].is_unsigned = 1;
    rb[11].buffer_type = MYSQL_TYPE_STRING; rb[11].buffer = esc_buf; rb[11].buffer_length = sizeof(esc_buf); rb[11].length = &esc_len;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&mute_r; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_LONGLONG; rb[13].buffer = (char*)&jail_r; rb[13].is_unsigned = 1;
    rb[14].buffer_type = MYSQL_TYPE_LONGLONG; rb[14].buffer = (char*)&jail_c; rb[14].is_unsigned = 1;
    rb[15].buffer_type = MYSQL_TYPE_LONGLONG; rb[15].buffer = (char*)&log_c; rb[15].is_unsigned = 1;
    rb[16].buffer_type = MYSQL_TYPE_TINY; rb[16].buffer = (char*)&dm;
    rb[17].buffer_type = MYSQL_TYPE_TINY; rb[17].buffer = (char*)&q_gl;
    rb[18].buffer_type = MYSQL_TYPE_STRING; rb[18].buffer = q_ov_buf; rb[18].buffer_length = sizeof(q_ov_buf); rb[18].length = &q_ov_len;
    rb[19].buffer_type = MYSQL_TYPE_LONG; rb[19].buffer = (char*)&c_count;
    rb[20].buffer_type = MYSQL_TYPE_TINY; rb[20].buffer = (char*)&req_r;

    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    if (mysql_stmt_fetch(stmt) != 0) {
        // No row — return defaults
        mysql_stmt_close(stmt); pool_->release(conn);
        InfractionConfig def; def.guild_id = guild_id;
        return def;
    }

    c.default_duration_timeout = dur_t; c.default_duration_mute = dur_m;
    c.default_duration_kick = dur_k; c.default_duration_ban = dur_b;
    c.default_duration_warn = dur_w;
    c.escalation_rules = std::string(esc_buf, esc_len);
    c.mute_role_id = mute_r; c.jail_role_id = jail_r;
    c.jail_channel_id = jail_c; c.log_channel_id = log_c;
    c.dm_on_action = dm;
    c.quiet_global = q_gl;
    c.quiet_overrides = std::string(q_ov_buf, q_ov_len);
    c.case_counter = c_count;
    c.require_reason = req_r;

    mysql_stmt_close(stmt); pool_->release(conn);
    return c;
}

bool Database::upsert_infraction_config(const InfractionConfig& c) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO guild_infraction_config "
        "(guild_id, point_timeout, point_mute, point_kick, point_ban, point_warn, "
        " default_duration_timeout, default_duration_mute, default_duration_kick, "
        " default_duration_ban, default_duration_warn, escalation_rules, "
        " mute_role_id, jail_role_id, jail_channel_id, log_channel_id, dm_on_action, "
        " quiet_global, quiet_overrides, case_counter, require_reason) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE "
        " point_timeout=VALUES(point_timeout), point_mute=VALUES(point_mute), "
        " point_kick=VALUES(point_kick), point_ban=VALUES(point_ban), point_warn=VALUES(point_warn), "
        " default_duration_timeout=VALUES(default_duration_timeout), "
        " default_duration_mute=VALUES(default_duration_mute), "
        " default_duration_kick=VALUES(default_duration_kick), "
        " default_duration_ban=VALUES(default_duration_ban), "
        " default_duration_warn=VALUES(default_duration_warn), "
        " escalation_rules=VALUES(escalation_rules), "
        " mute_role_id=VALUES(mute_role_id), jail_role_id=VALUES(jail_role_id), "
        " jail_channel_id=VALUES(jail_channel_id), log_channel_id=VALUES(log_channel_id), "
        " dm_on_action=VALUES(dm_on_action), quiet_global=VALUES(quiet_global), "
        " quiet_overrides=VALUES(quiet_overrides), case_counter=VALUES(case_counter), "
        " require_reason=VALUES(require_reason)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("upsert_infraction_config prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }

    MYSQL_BIND bp[21]; memset(bp, 0, sizeof(bp));
    my_bool dm_val = c.dm_on_action ? 1 : 0;
    my_bool q_gl_val = c.quiet_global ? 1 : 0;
    my_bool req_r_val = c.require_reason ? 1 : 0;
    my_bool null_flag = 1, not_null = 0;

    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&c.guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_DOUBLE; bp[1].buffer = (char*)&c.point_timeout;
    bp[2].buffer_type = MYSQL_TYPE_DOUBLE; bp[2].buffer = (char*)&c.point_mute;
    bp[3].buffer_type = MYSQL_TYPE_DOUBLE; bp[3].buffer = (char*)&c.point_kick;
    bp[4].buffer_type = MYSQL_TYPE_DOUBLE; bp[4].buffer = (char*)&c.point_ban;
    bp[5].buffer_type = MYSQL_TYPE_DOUBLE; bp[5].buffer = (char*)&c.point_warn;
    bp[6].buffer_type = MYSQL_TYPE_LONG; bp[6].buffer = (char*)&c.default_duration_timeout; bp[6].is_unsigned = 1;
    bp[7].buffer_type = MYSQL_TYPE_LONG; bp[7].buffer = (char*)&c.default_duration_mute; bp[7].is_unsigned = 1;
    bp[8].buffer_type = MYSQL_TYPE_LONG; bp[8].buffer = (char*)&c.default_duration_kick; bp[8].is_unsigned = 1;
    bp[9].buffer_type = MYSQL_TYPE_LONG; bp[9].buffer = (char*)&c.default_duration_ban; bp[9].is_unsigned = 1;
    bp[10].buffer_type = MYSQL_TYPE_LONG; bp[10].buffer = (char*)&c.default_duration_warn; bp[10].is_unsigned = 1;
    unsigned long esc_len = c.escalation_rules.size();
    bp[11].buffer_type = MYSQL_TYPE_STRING; bp[11].buffer = (char*)c.escalation_rules.c_str();
    bp[11].buffer_length = c.escalation_rules.size(); bp[11].length = &esc_len;
    // mute_role_id — NULL if 0
    bp[12].buffer_type = MYSQL_TYPE_LONGLONG; bp[12].buffer = (char*)&c.mute_role_id;
    bp[12].is_unsigned = 1; bp[12].is_null = (c.mute_role_id == 0) ? &null_flag : &not_null;
    bp[13].buffer_type = MYSQL_TYPE_LONGLONG; bp[13].buffer = (char*)&c.jail_role_id;
    bp[13].is_unsigned = 1; bp[13].is_null = (c.jail_role_id == 0) ? &null_flag : &not_null;
    bp[14].buffer_type = MYSQL_TYPE_LONGLONG; bp[14].buffer = (char*)&c.jail_channel_id;
    bp[14].is_unsigned = 1; bp[14].is_null = (c.jail_channel_id == 0) ? &null_flag : &not_null;
    bp[15].buffer_type = MYSQL_TYPE_LONGLONG; bp[15].buffer = (char*)&c.log_channel_id;
    bp[15].is_unsigned = 1; bp[15].is_null = (c.log_channel_id == 0) ? &null_flag : &not_null;
    bp[16].buffer_type = MYSQL_TYPE_TINY; bp[16].buffer = (char*)&dm_val;
    bp[17].buffer_type = MYSQL_TYPE_TINY; bp[17].buffer = (char*)&q_gl_val;
    unsigned long q_ov_len = c.quiet_overrides.size();
    bp[18].buffer_type = MYSQL_TYPE_STRING; bp[18].buffer = (char*)c.quiet_overrides.c_str();
    bp[18].buffer_length = c.quiet_overrides.size(); bp[18].length = &q_ov_len;
    bp[19].buffer_type = MYSQL_TYPE_LONG; bp[19].buffer = (char*)&c.case_counter;
    bp[20].buffer_type = MYSQL_TYPE_TINY; bp[20].buffer = (char*)&req_r_val;

    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("upsert_infraction_config execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

// ============================================================================
// AutomodConfig — guild_automod_config
// ============================================================================

std::optional<AutomodConfig> Database::get_automod_config(uint64_t guild_id) {
    auto conn = pool_->acquire();
    const char* q = "SELECT guild_id, account_age_enabled, account_age_days, account_age_action, "
        "default_avatar_enabled, default_avatar_action, "
        "mutual_servers_enabled, mutual_servers_min, mutual_servers_action, "
        "nickname_sanitize_enabled, nickname_sanitize_format, "
        "COALESCE(nickname_bad_patterns, '[]'), infraction_escalation_enabled "
        "FROM guild_automod_config WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_automod_config prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_automod_config execute");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }

    AutomodConfig c;
    my_bool b_age, b_avatar, b_mutual, b_nick, b_esc;
    uint32_t age_days, mutual_min;
    char act_age[20], act_avatar[20], act_mutual[20], nick_fmt[100], nick_pat[4096];
    unsigned long act_age_len, act_avatar_len, act_mutual_len, nick_fmt_len, nick_pat_len;

    MYSQL_BIND rb[13]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&c.guild_id; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_TINY; rb[1].buffer = (char*)&b_age;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&age_days; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_STRING; rb[3].buffer = act_age; rb[3].buffer_length = sizeof(act_age); rb[3].length = &act_age_len;
    rb[4].buffer_type = MYSQL_TYPE_TINY; rb[4].buffer = (char*)&b_avatar;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = act_avatar; rb[5].buffer_length = sizeof(act_avatar); rb[5].length = &act_avatar_len;
    rb[6].buffer_type = MYSQL_TYPE_TINY; rb[6].buffer = (char*)&b_mutual;
    rb[7].buffer_type = MYSQL_TYPE_LONG; rb[7].buffer = (char*)&mutual_min; rb[7].is_unsigned = 1;
    rb[8].buffer_type = MYSQL_TYPE_STRING; rb[8].buffer = act_mutual; rb[8].buffer_length = sizeof(act_mutual); rb[8].length = &act_mutual_len;
    rb[9].buffer_type = MYSQL_TYPE_TINY; rb[9].buffer = (char*)&b_nick;
    rb[10].buffer_type = MYSQL_TYPE_STRING; rb[10].buffer = nick_fmt; rb[10].buffer_length = sizeof(nick_fmt); rb[10].length = &nick_fmt_len;
    rb[11].buffer_type = MYSQL_TYPE_STRING; rb[11].buffer = nick_pat; rb[11].buffer_length = sizeof(nick_pat); rb[11].length = &nick_pat_len;
    rb[12].buffer_type = MYSQL_TYPE_TINY; rb[12].buffer = (char*)&b_esc;

    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn);
        AutomodConfig def; def.guild_id = guild_id;
        return def;
    }

    c.account_age_enabled = b_age; c.account_age_days = age_days;
    c.account_age_action = std::string(act_age, act_age_len);
    c.default_avatar_enabled = b_avatar;
    c.default_avatar_action = std::string(act_avatar, act_avatar_len);
    c.mutual_servers_enabled = b_mutual; c.mutual_servers_min = mutual_min;
    c.mutual_servers_action = std::string(act_mutual, act_mutual_len);
    c.nickname_sanitize_enabled = b_nick;
    c.nickname_sanitize_format = std::string(nick_fmt, nick_fmt_len);
    c.nickname_bad_patterns = std::string(nick_pat, nick_pat_len);
    c.infraction_escalation_enabled = b_esc;

    mysql_stmt_close(stmt); pool_->release(conn);
    return c;
}

bool Database::upsert_automod_config(const AutomodConfig& c) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO guild_automod_config "
        "(guild_id, account_age_enabled, account_age_days, account_age_action, "
        " default_avatar_enabled, default_avatar_action, "
        " mutual_servers_enabled, mutual_servers_min, mutual_servers_action, "
        " nickname_sanitize_enabled, nickname_sanitize_format, nickname_bad_patterns, "
        " infraction_escalation_enabled) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE "
        " account_age_enabled=VALUES(account_age_enabled), account_age_days=VALUES(account_age_days), "
        " account_age_action=VALUES(account_age_action), "
        " default_avatar_enabled=VALUES(default_avatar_enabled), default_avatar_action=VALUES(default_avatar_action), "
        " mutual_servers_enabled=VALUES(mutual_servers_enabled), mutual_servers_min=VALUES(mutual_servers_min), "
        " mutual_servers_action=VALUES(mutual_servers_action), "
        " nickname_sanitize_enabled=VALUES(nickname_sanitize_enabled), "
        " nickname_sanitize_format=VALUES(nickname_sanitize_format), "
        " nickname_bad_patterns=VALUES(nickname_bad_patterns), "
        " infraction_escalation_enabled=VALUES(infraction_escalation_enabled)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("upsert_automod_config prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }

    MYSQL_BIND bp[13]; memset(bp, 0, sizeof(bp));
    my_bool b_age = c.account_age_enabled, b_av = c.default_avatar_enabled;
    my_bool b_mut = c.mutual_servers_enabled, b_nick = c.nickname_sanitize_enabled;
    my_bool b_esc = c.infraction_escalation_enabled;

    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&c.guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_TINY; bp[1].buffer = (char*)&b_age;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&c.account_age_days; bp[2].is_unsigned = 1;
    unsigned long aa_len = c.account_age_action.size();
    bp[3].buffer_type = MYSQL_TYPE_STRING; bp[3].buffer = (char*)c.account_age_action.c_str();
    bp[3].buffer_length = c.account_age_action.size(); bp[3].length = &aa_len;
    bp[4].buffer_type = MYSQL_TYPE_TINY; bp[4].buffer = (char*)&b_av;
    unsigned long da_len = c.default_avatar_action.size();
    bp[5].buffer_type = MYSQL_TYPE_STRING; bp[5].buffer = (char*)c.default_avatar_action.c_str();
    bp[5].buffer_length = c.default_avatar_action.size(); bp[5].length = &da_len;
    bp[6].buffer_type = MYSQL_TYPE_TINY; bp[6].buffer = (char*)&b_mut;
    bp[7].buffer_type = MYSQL_TYPE_LONG; bp[7].buffer = (char*)&c.mutual_servers_min; bp[7].is_unsigned = 1;
    unsigned long ma_len = c.mutual_servers_action.size();
    bp[8].buffer_type = MYSQL_TYPE_STRING; bp[8].buffer = (char*)c.mutual_servers_action.c_str();
    bp[8].buffer_length = c.mutual_servers_action.size(); bp[8].length = &ma_len;
    bp[9].buffer_type = MYSQL_TYPE_TINY; bp[9].buffer = (char*)&b_nick;
    unsigned long nf_len = c.nickname_sanitize_format.size();
    bp[10].buffer_type = MYSQL_TYPE_STRING; bp[10].buffer = (char*)c.nickname_sanitize_format.c_str();
    bp[10].buffer_length = c.nickname_sanitize_format.size(); bp[10].length = &nf_len;
    unsigned long np_len = c.nickname_bad_patterns.size();
    bp[11].buffer_type = MYSQL_TYPE_STRING; bp[11].buffer = (char*)c.nickname_bad_patterns.c_str();
    bp[11].buffer_length = c.nickname_bad_patterns.size(); bp[11].length = &np_len;
    bp[12].buffer_type = MYSQL_TYPE_TINY; bp[12].buffer = (char*)&b_esc;

    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("upsert_automod_config execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

// ============================================================================
// Free-function wrappers
// ============================================================================
namespace infraction_config_operations {

std::optional<InfractionConfig> get_infraction_config(Database* db, uint64_t guild_id) {
    return db->get_infraction_config(guild_id);
}
bool upsert_infraction_config(Database* db, const InfractionConfig& config) {
    return db->upsert_infraction_config(config);
}
std::optional<AutomodConfig> get_automod_config(Database* db, uint64_t guild_id) {
    return db->get_automod_config(guild_id);
}
bool upsert_automod_config(Database* db, const AutomodConfig& config) {
    return db->upsert_automod_config(config);
}

} // namespace infraction_config_operations
} // namespace db
} // namespace bronx
