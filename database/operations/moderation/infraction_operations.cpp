#include "infraction_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>

namespace bronx {
namespace db {

// ============================================================================
// infraction_operations — Database methods on Database class
// Free-function wrappers at the end delegate to db->method().
// ============================================================================

// ---------------------------------------------------------------------------
// create_infraction
// ---------------------------------------------------------------------------
std::optional<InfractionRow> Database::create_infraction(
    uint64_t guild_id, uint64_t user_id, uint64_t moderator_id,
    const std::string& type, const std::string& reason,
    double points, uint32_t duration_seconds, const std::string& metadata_json)
{
    auto conn = pool_->acquire();

    // — 1. Get next case_number for this guild (atomic via FOR UPDATE) —
    const char* case_q = "SELECT COALESCE(MAX(case_number),0)+1 FROM guild_infractions WHERE guild_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, case_q, strlen(case_q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("create_infraction case_q prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    mysql_stmt_execute(stmt);

    uint32_t next_case = 1;
    MYSQL_BIND rb[1]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONG; rb[0].buffer = (char*)&next_case; rb[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);

    // — 2. Compute expires_at —
    // If duration_seconds > 0, record expires duration_seconds from now.
    // NULL otherwise (permanent record).
    bool has_expiry = (duration_seconds > 0);
    MYSQL_TIME expires_time; memset(&expires_time, 0, sizeof(expires_time));
    if (has_expiry) {
        time_t exp = time(nullptr) + duration_seconds;
        struct tm* t = gmtime(&exp);
        expires_time.year = t->tm_year + 1900; expires_time.month = t->tm_mon + 1;
        expires_time.day = t->tm_mday; expires_time.hour = t->tm_hour;
        expires_time.minute = t->tm_min; expires_time.second = t->tm_sec;
    }

    // — 3. INSERT —
    const char* ins = "INSERT INTO guild_infractions "
        "(guild_id, case_number, user_id, moderator_id, type, reason, points, "
        " duration_seconds, expires_at, metadata) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* istmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(istmt, ins, strlen(ins)) != 0) {
        last_error_ = mysql_stmt_error(istmt); log_error("create_infraction insert prepare");
        mysql_stmt_close(istmt); pool_->release(conn); return std::nullopt;
    }

    MYSQL_BIND ib[10]; memset(ib, 0, sizeof(ib));
    my_bool null_flag = 1;
    my_bool not_null  = 0;

    // 0: guild_id
    ib[0].buffer_type = MYSQL_TYPE_LONGLONG; ib[0].buffer = (char*)&guild_id; ib[0].is_unsigned = 1;
    // 1: case_number
    ib[1].buffer_type = MYSQL_TYPE_LONG; ib[1].buffer = (char*)&next_case; ib[1].is_unsigned = 1;
    // 2: user_id
    ib[2].buffer_type = MYSQL_TYPE_LONGLONG; ib[2].buffer = (char*)&user_id; ib[2].is_unsigned = 1;
    // 3: moderator_id
    ib[3].buffer_type = MYSQL_TYPE_LONGLONG; ib[3].buffer = (char*)&moderator_id; ib[3].is_unsigned = 1;
    // 4: type
    unsigned long type_len = type.size();
    ib[4].buffer_type = MYSQL_TYPE_STRING; ib[4].buffer = (char*)type.c_str();
    ib[4].buffer_length = type.size(); ib[4].length = &type_len;
    // 5: reason
    unsigned long reason_len = reason.size();
    ib[5].buffer_type = MYSQL_TYPE_STRING; ib[5].buffer = (char*)reason.c_str();
    ib[5].buffer_length = reason.size(); ib[5].length = &reason_len;
    // 6: points
    ib[6].buffer_type = MYSQL_TYPE_DOUBLE; ib[6].buffer = (char*)&points;
    // 7: duration_seconds
    ib[7].buffer_type = MYSQL_TYPE_LONG; ib[7].buffer = (char*)&duration_seconds; ib[7].is_unsigned = 1;
    // 8: expires_at
    ib[8].buffer_type = MYSQL_TYPE_TIMESTAMP; ib[8].buffer = (char*)&expires_time;
    ib[8].is_null = has_expiry ? &not_null : &null_flag;
    // 9: metadata
    unsigned long meta_len = metadata_json.size();
    ib[9].buffer_type = MYSQL_TYPE_STRING; ib[9].buffer = (char*)metadata_json.c_str();
    ib[9].buffer_length = metadata_json.size(); ib[9].length = &meta_len;

    mysql_stmt_bind_param(istmt, ib);
    bool ok = (mysql_stmt_execute(istmt) == 0);
    if (!ok) {
        last_error_ = mysql_stmt_error(istmt); log_error("create_infraction insert execute");
        mysql_stmt_close(istmt); pool_->release(conn); return std::nullopt;
    }

    uint64_t new_id = mysql_stmt_insert_id(istmt);
    mysql_stmt_close(istmt);
    pool_->release(conn);

    // Build result row
    InfractionRow row;
    row.id = new_id;
    row.guild_id = guild_id;
    row.case_number = next_case;
    row.user_id = user_id;
    row.moderator_id = moderator_id;
    row.type = type;
    row.reason = reason;
    row.points = points;
    row.duration_seconds = duration_seconds;
    row.active = true;
    row.pardoned = false;
    row.pardoned_by = 0;
    row.metadata = metadata_json;
    row.created_at = std::chrono::system_clock::now();
    if (has_expiry) {
        row.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(duration_seconds);
    }
    return row;
}

// ---------------------------------------------------------------------------
// get_infraction
// ---------------------------------------------------------------------------
std::optional<InfractionRow> Database::get_infraction(uint64_t guild_id, uint32_t case_number) {
    auto conn = pool_->acquire();
    const char* q = "SELECT id, guild_id, case_number, user_id, moderator_id, type, "
        "COALESCE(reason,''), points, COALESCE(duration_seconds,0), expires_at, "
        "active, pardoned, COALESCE(pardoned_by,0), pardoned_at, COALESCE(pardoned_reason,''), "
        "COALESCE(metadata,'{}'), created_at "
        "FROM guild_infractions WHERE guild_id = ? AND case_number = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_infraction prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONG; bp[1].buffer = (char*)&case_number; bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_infraction execute");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }

    // Bind result columns
    uint64_t rid, r_guild, r_user, r_mod, r_pardoned_by;
    uint32_t r_case, r_dur;
    char r_type[32], r_reason[2048], r_pardon_reason[2048], r_meta[4096];
    unsigned long r_type_len, r_reason_len, r_pardon_reason_len, r_meta_len;
    double r_points;
    my_bool r_active, r_pardoned;
    MYSQL_TIME r_expires, r_pardoned_at, r_created;
    my_bool r_expires_null, r_pardoned_at_null;

    MYSQL_BIND rb[17]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&rid; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_case; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_LONGLONG; rb[3].buffer = (char*)&r_user; rb[3].is_unsigned = 1;
    rb[4].buffer_type = MYSQL_TYPE_LONGLONG; rb[4].buffer = (char*)&r_mod; rb[4].is_unsigned = 1;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_type; rb[5].buffer_length = sizeof(r_type); rb[5].length = &r_type_len;
    rb[6].buffer_type = MYSQL_TYPE_STRING; rb[6].buffer = r_reason; rb[6].buffer_length = sizeof(r_reason); rb[6].length = &r_reason_len;
    rb[7].buffer_type = MYSQL_TYPE_DOUBLE; rb[7].buffer = (char*)&r_points;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&r_dur; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[9].buffer = (char*)&r_expires; rb[9].is_null = &r_expires_null;
    rb[10].buffer_type = MYSQL_TYPE_TINY; rb[10].buffer = (char*)&r_active;
    rb[11].buffer_type = MYSQL_TYPE_TINY; rb[11].buffer = (char*)&r_pardoned;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&r_pardoned_by; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[13].buffer = (char*)&r_pardoned_at; rb[13].is_null = &r_pardoned_at_null;
    rb[14].buffer_type = MYSQL_TYPE_STRING; rb[14].buffer = r_pardon_reason; rb[14].buffer_length = sizeof(r_pardon_reason); rb[14].length = &r_pardon_reason_len;
    rb[15].buffer_type = MYSQL_TYPE_STRING; rb[15].buffer = r_meta; rb[15].buffer_length = sizeof(r_meta); rb[15].length = &r_meta_len;
    rb[16].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[16].buffer = (char*)&r_created;

    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }

    InfractionRow row;
    row.id = rid; row.guild_id = r_guild; row.case_number = r_case;
    row.user_id = r_user; row.moderator_id = r_mod;
    row.type = std::string(r_type, r_type_len);
    row.reason = std::string(r_reason, r_reason_len);
    row.points = r_points; row.duration_seconds = r_dur;
    row.active = r_active; row.pardoned = r_pardoned;
    row.pardoned_by = r_pardoned_by;
    row.pardoned_reason = std::string(r_pardon_reason, r_pardon_reason_len);
    row.metadata = std::string(r_meta, r_meta_len);

    // Convert MYSQL_TIME to time_point
    auto mysql_time_to_tp = [](const MYSQL_TIME& mt) -> std::chrono::system_clock::time_point {
        struct tm t{};
        t.tm_year = mt.year - 1900; t.tm_mon = mt.month - 1; t.tm_mday = mt.day;
        t.tm_hour = mt.hour; t.tm_min = mt.minute; t.tm_sec = mt.second;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    };

    if (!r_expires_null) row.expires_at = mysql_time_to_tp(r_expires);
    if (!r_pardoned_at_null) row.pardoned_at = mysql_time_to_tp(r_pardoned_at);
    row.created_at = mysql_time_to_tp(r_created);

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return row;
}

// ---------------------------------------------------------------------------
// get_user_infractions
// ---------------------------------------------------------------------------
std::vector<InfractionRow> Database::get_user_infractions(
    uint64_t guild_id, uint64_t user_id,
    bool active_only, int limit, int offset)
{
    std::vector<InfractionRow> out;
    auto conn = pool_->acquire();

    std::string q = "SELECT id, guild_id, case_number, user_id, moderator_id, type, "
        "COALESCE(reason,''), points, COALESCE(duration_seconds,0), expires_at, "
        "active, pardoned, COALESCE(pardoned_by,0), pardoned_at, COALESCE(pardoned_reason,''), "
        "COALESCE(metadata,'{}'), created_at "
        "FROM guild_infractions WHERE guild_id = ? AND user_id = ?";
    if (active_only) q += " AND active = 1 AND pardoned = 0";
    q += " ORDER BY case_number DESC LIMIT ? OFFSET ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q.c_str(), q.size()) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_user_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&user_id; bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&limit;
    bp[3].buffer_type = MYSQL_TYPE_LONG; bp[3].buffer = (char*)&offset;
    mysql_stmt_bind_param(stmt, bp);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_user_infractions execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    // Reuse same result-binding pattern as get_infraction
    uint64_t rid, r_guild, r_user, r_mod, r_pardoned_by;
    uint32_t r_case, r_dur;
    char r_type[32], r_reason[2048], r_pardon_reason[2048], r_meta[4096];
    unsigned long r_type_len, r_reason_len, r_pardon_reason_len, r_meta_len;
    double r_points;
    my_bool r_active, r_pardoned;
    MYSQL_TIME r_expires, r_pardoned_at, r_created;
    my_bool r_expires_null, r_pardoned_at_null;

    MYSQL_BIND rb[17]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&rid; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_case; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_LONGLONG; rb[3].buffer = (char*)&r_user; rb[3].is_unsigned = 1;
    rb[4].buffer_type = MYSQL_TYPE_LONGLONG; rb[4].buffer = (char*)&r_mod; rb[4].is_unsigned = 1;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_type; rb[5].buffer_length = sizeof(r_type); rb[5].length = &r_type_len;
    rb[6].buffer_type = MYSQL_TYPE_STRING; rb[6].buffer = r_reason; rb[6].buffer_length = sizeof(r_reason); rb[6].length = &r_reason_len;
    rb[7].buffer_type = MYSQL_TYPE_DOUBLE; rb[7].buffer = (char*)&r_points;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&r_dur; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[9].buffer = (char*)&r_expires; rb[9].is_null = &r_expires_null;
    rb[10].buffer_type = MYSQL_TYPE_TINY; rb[10].buffer = (char*)&r_active;
    rb[11].buffer_type = MYSQL_TYPE_TINY; rb[11].buffer = (char*)&r_pardoned;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&r_pardoned_by; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[13].buffer = (char*)&r_pardoned_at; rb[13].is_null = &r_pardoned_at_null;
    rb[14].buffer_type = MYSQL_TYPE_STRING; rb[14].buffer = r_pardon_reason; rb[14].buffer_length = sizeof(r_pardon_reason); rb[14].length = &r_pardon_reason_len;
    rb[15].buffer_type = MYSQL_TYPE_STRING; rb[15].buffer = r_meta; rb[15].buffer_length = sizeof(r_meta); rb[15].length = &r_meta_len;
    rb[16].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[16].buffer = (char*)&r_created;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    auto mysql_time_to_tp = [](const MYSQL_TIME& mt) -> std::chrono::system_clock::time_point {
        struct tm t{}; t.tm_year = mt.year - 1900; t.tm_mon = mt.month - 1; t.tm_mday = mt.day;
        t.tm_hour = mt.hour; t.tm_min = mt.minute; t.tm_sec = mt.second;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    };

    while (mysql_stmt_fetch(stmt) == 0) {
        InfractionRow row;
        row.id = rid; row.guild_id = r_guild; row.case_number = r_case;
        row.user_id = r_user; row.moderator_id = r_mod;
        row.type = std::string(r_type, r_type_len);
        row.reason = std::string(r_reason, r_reason_len);
        row.points = r_points; row.duration_seconds = r_dur;
        row.active = r_active; row.pardoned = r_pardoned;
        row.pardoned_by = r_pardoned_by;
        row.pardoned_reason = std::string(r_pardon_reason, r_pardon_reason_len);
        row.metadata = std::string(r_meta, r_meta_len);
        if (!r_expires_null) row.expires_at = mysql_time_to_tp(r_expires);
        if (!r_pardoned_at_null) row.pardoned_at = mysql_time_to_tp(r_pardoned_at);
        row.created_at = mysql_time_to_tp(r_created);
        out.push_back(row);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// ---------------------------------------------------------------------------
// get_user_active_points
// ---------------------------------------------------------------------------
double Database::get_user_active_points(uint64_t guild_id, uint64_t user_id, int within_days) {
    auto conn = pool_->acquire();
    std::string q = "SELECT COALESCE(SUM(points),0) FROM guild_infractions "
        "WHERE guild_id = ? AND user_id = ? AND active = 1 AND pardoned = 0";
    if (within_days > 0) {
        q += " AND created_at >= DATE_SUB(NOW(), INTERVAL ? DAY)";
    }
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q.c_str(), q.size()) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_user_active_points prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    int n = 2;
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&user_id; bp[1].is_unsigned = 1;
    if (within_days > 0) {
        bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&within_days;
        n = 3;
    }
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("get_user_active_points execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    double total = 0;
    MYSQL_BIND rb[1]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_DOUBLE; rb[0].buffer = (char*)&total;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return total;
}

// ---------------------------------------------------------------------------
// get_recent_infractions
// ---------------------------------------------------------------------------
std::vector<InfractionRow> Database::get_recent_infractions(
    uint64_t guild_id, int limit, int offset, const std::string& type_filter)
{
    std::vector<InfractionRow> out;
    auto conn = pool_->acquire();

    std::string q = "SELECT id, guild_id, case_number, user_id, moderator_id, type, "
        "COALESCE(reason,''), points, COALESCE(duration_seconds,0), expires_at, "
        "active, pardoned, COALESCE(pardoned_by,0), pardoned_at, COALESCE(pardoned_reason,''), "
        "COALESCE(metadata,'{}'), created_at "
        "FROM guild_infractions WHERE guild_id = ?";
    if (!type_filter.empty()) q += " AND type = ?";
    q += " ORDER BY created_at DESC LIMIT ? OFFSET ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q.c_str(), q.size()) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_recent_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    int idx = 0;
    bp[idx].buffer_type = MYSQL_TYPE_LONGLONG; bp[idx].buffer = (char*)&guild_id; bp[idx].is_unsigned = 1; idx++;
    unsigned long tf_len = type_filter.size();
    if (!type_filter.empty()) {
        bp[idx].buffer_type = MYSQL_TYPE_STRING; bp[idx].buffer = (char*)type_filter.c_str();
        bp[idx].buffer_length = type_filter.size(); bp[idx].length = &tf_len; idx++;
    }
    bp[idx].buffer_type = MYSQL_TYPE_LONG; bp[idx].buffer = (char*)&limit; idx++;
    bp[idx].buffer_type = MYSQL_TYPE_LONG; bp[idx].buffer = (char*)&offset; idx++;
    mysql_stmt_bind_param(stmt, bp);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_recent_infractions execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    // Same result pattern
    uint64_t rid, r_guild, r_user, r_mod, r_pardoned_by;
    uint32_t r_case, r_dur;
    char r_type[32], r_reason[2048], r_pardon_reason[2048], r_meta[4096];
    unsigned long r_type_len, r_reason_len, r_pardon_reason_len, r_meta_len;
    double r_points;
    my_bool r_active, r_pardoned;
    MYSQL_TIME r_expires, r_pardoned_at, r_created;
    my_bool r_expires_null, r_pardoned_at_null;

    MYSQL_BIND rb[17]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&rid; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_case; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_LONGLONG; rb[3].buffer = (char*)&r_user; rb[3].is_unsigned = 1;
    rb[4].buffer_type = MYSQL_TYPE_LONGLONG; rb[4].buffer = (char*)&r_mod; rb[4].is_unsigned = 1;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_type; rb[5].buffer_length = sizeof(r_type); rb[5].length = &r_type_len;
    rb[6].buffer_type = MYSQL_TYPE_STRING; rb[6].buffer = r_reason; rb[6].buffer_length = sizeof(r_reason); rb[6].length = &r_reason_len;
    rb[7].buffer_type = MYSQL_TYPE_DOUBLE; rb[7].buffer = (char*)&r_points;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&r_dur; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[9].buffer = (char*)&r_expires; rb[9].is_null = &r_expires_null;
    rb[10].buffer_type = MYSQL_TYPE_TINY; rb[10].buffer = (char*)&r_active;
    rb[11].buffer_type = MYSQL_TYPE_TINY; rb[11].buffer = (char*)&r_pardoned;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&r_pardoned_by; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[13].buffer = (char*)&r_pardoned_at; rb[13].is_null = &r_pardoned_at_null;
    rb[14].buffer_type = MYSQL_TYPE_STRING; rb[14].buffer = r_pardon_reason; rb[14].buffer_length = sizeof(r_pardon_reason); rb[14].length = &r_pardon_reason_len;
    rb[15].buffer_type = MYSQL_TYPE_STRING; rb[15].buffer = r_meta; rb[15].buffer_length = sizeof(r_meta); rb[15].length = &r_meta_len;
    rb[16].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[16].buffer = (char*)&r_created;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    auto mysql_time_to_tp = [](const MYSQL_TIME& mt) -> std::chrono::system_clock::time_point {
        struct tm t{}; t.tm_year = mt.year - 1900; t.tm_mon = mt.month - 1; t.tm_mday = mt.day;
        t.tm_hour = mt.hour; t.tm_min = mt.minute; t.tm_sec = mt.second;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    };

    while (mysql_stmt_fetch(stmt) == 0) {
        InfractionRow row;
        row.id = rid; row.guild_id = r_guild; row.case_number = r_case;
        row.user_id = r_user; row.moderator_id = r_mod;
        row.type = std::string(r_type, r_type_len);
        row.reason = std::string(r_reason, r_reason_len);
        row.points = r_points; row.duration_seconds = r_dur;
        row.active = r_active; row.pardoned = r_pardoned;
        row.pardoned_by = r_pardoned_by;
        row.pardoned_reason = std::string(r_pardon_reason, r_pardon_reason_len);
        row.metadata = std::string(r_meta, r_meta_len);
        if (!r_expires_null) row.expires_at = mysql_time_to_tp(r_expires);
        if (!r_pardoned_at_null) row.pardoned_at = mysql_time_to_tp(r_pardoned_at);
        row.created_at = mysql_time_to_tp(r_created);
        out.push_back(row);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// ---------------------------------------------------------------------------
// get_moderator_actions
// ---------------------------------------------------------------------------
std::vector<InfractionRow> Database::get_moderator_actions(
    uint64_t guild_id, uint64_t moderator_id, int limit, int offset)
{
    // Re-use get_recent_infractions logic but filter by moderator_id
    std::vector<InfractionRow> out;
    auto conn = pool_->acquire();

    const char* q = "SELECT id, guild_id, case_number, user_id, moderator_id, type, "
        "COALESCE(reason,''), points, COALESCE(duration_seconds,0), expires_at, "
        "active, pardoned, COALESCE(pardoned_by,0), pardoned_at, COALESCE(pardoned_reason,''), "
        "COALESCE(metadata,'{}'), created_at "
        "FROM guild_infractions WHERE guild_id = ? AND moderator_id = ? "
        "ORDER BY created_at DESC LIMIT ? OFFSET ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_moderator_actions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&moderator_id; bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&limit;
    bp[3].buffer_type = MYSQL_TYPE_LONG; bp[3].buffer = (char*)&offset;
    mysql_stmt_bind_param(stmt, bp);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_moderator_actions execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    uint64_t rid, r_guild, r_user, r_mod, r_pardoned_by;
    uint32_t r_case, r_dur;
    char r_type[32], r_reason[2048], r_pardon_reason[2048], r_meta[4096];
    unsigned long r_type_len, r_reason_len, r_pardon_reason_len, r_meta_len;
    double r_points;
    my_bool r_active, r_pardoned;
    MYSQL_TIME r_expires, r_pardoned_at, r_created;
    my_bool r_expires_null, r_pardoned_at_null;

    MYSQL_BIND rb[17]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&rid; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_case; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_LONGLONG; rb[3].buffer = (char*)&r_user; rb[3].is_unsigned = 1;
    rb[4].buffer_type = MYSQL_TYPE_LONGLONG; rb[4].buffer = (char*)&r_mod; rb[4].is_unsigned = 1;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_type; rb[5].buffer_length = sizeof(r_type); rb[5].length = &r_type_len;
    rb[6].buffer_type = MYSQL_TYPE_STRING; rb[6].buffer = r_reason; rb[6].buffer_length = sizeof(r_reason); rb[6].length = &r_reason_len;
    rb[7].buffer_type = MYSQL_TYPE_DOUBLE; rb[7].buffer = (char*)&r_points;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&r_dur; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[9].buffer = (char*)&r_expires; rb[9].is_null = &r_expires_null;
    rb[10].buffer_type = MYSQL_TYPE_TINY; rb[10].buffer = (char*)&r_active;
    rb[11].buffer_type = MYSQL_TYPE_TINY; rb[11].buffer = (char*)&r_pardoned;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&r_pardoned_by; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[13].buffer = (char*)&r_pardoned_at; rb[13].is_null = &r_pardoned_at_null;
    rb[14].buffer_type = MYSQL_TYPE_STRING; rb[14].buffer = r_pardon_reason; rb[14].buffer_length = sizeof(r_pardon_reason); rb[14].length = &r_pardon_reason_len;
    rb[15].buffer_type = MYSQL_TYPE_STRING; rb[15].buffer = r_meta; rb[15].buffer_length = sizeof(r_meta); rb[15].length = &r_meta_len;
    rb[16].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[16].buffer = (char*)&r_created;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    auto mysql_time_to_tp = [](const MYSQL_TIME& mt) -> std::chrono::system_clock::time_point {
        struct tm t{}; t.tm_year = mt.year - 1900; t.tm_mon = mt.month - 1; t.tm_mday = mt.day;
        t.tm_hour = mt.hour; t.tm_min = mt.minute; t.tm_sec = mt.second;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    };

    while (mysql_stmt_fetch(stmt) == 0) {
        InfractionRow row;
        row.id = rid; row.guild_id = r_guild; row.case_number = r_case;
        row.user_id = r_user; row.moderator_id = r_mod;
        row.type = std::string(r_type, r_type_len);
        row.reason = std::string(r_reason, r_reason_len);
        row.points = r_points; row.duration_seconds = r_dur;
        row.active = r_active; row.pardoned = r_pardoned;
        row.pardoned_by = r_pardoned_by;
        row.pardoned_reason = std::string(r_pardon_reason, r_pardon_reason_len);
        row.metadata = std::string(r_meta, r_meta_len);
        if (!r_expires_null) row.expires_at = mysql_time_to_tp(r_expires);
        if (!r_pardoned_at_null) row.pardoned_at = mysql_time_to_tp(r_pardoned_at);
        row.created_at = mysql_time_to_tp(r_created);
        out.push_back(row);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// ---------------------------------------------------------------------------
// count_infractions
// ---------------------------------------------------------------------------
InfractionCounts Database::count_infractions(uint64_t guild_id, uint64_t user_id) {
    InfractionCounts c{0, 0, 0};
    auto conn = pool_->acquire();
    const char* q = "SELECT "
        "COUNT(*), "
        "SUM(active = 1 AND pardoned = 0), "
        "SUM(pardoned = 1) "
        "FROM guild_infractions WHERE guild_id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("count_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return c;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&user_id; bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("count_infractions execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return c;
    }

    int64_t r_total = 0, r_active = 0, r_pardoned = 0;
    MYSQL_BIND rb[3]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&r_total;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_active;
    rb[2].buffer_type = MYSQL_TYPE_LONGLONG; rb[2].buffer = (char*)&r_pardoned;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    c.total = (int)r_total; c.active = (int)r_active; c.pardoned = (int)r_pardoned;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return c;
}

// ---------------------------------------------------------------------------
// count_guild_infractions
// ---------------------------------------------------------------------------
int Database::count_guild_infractions(uint64_t guild_id, const std::string& type_filter, uint64_t user_id) {
    auto conn = pool_->acquire();
    std::string q = "SELECT COUNT(*) FROM guild_infractions WHERE guild_id = ?";
    if (!type_filter.empty()) q += " AND type = ?";
    if (user_id > 0) q += " AND user_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q.c_str(), q.size()) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("count_guild_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    int idx = 0;
    bp[idx].buffer_type = MYSQL_TYPE_LONGLONG; bp[idx].buffer = (char*)&guild_id; bp[idx].is_unsigned = 1; idx++;
    unsigned long tf_len = type_filter.size();
    if (!type_filter.empty()) {
        bp[idx].buffer_type = MYSQL_TYPE_STRING; bp[idx].buffer = (char*)type_filter.c_str();
        bp[idx].buffer_length = type_filter.size(); bp[idx].length = &tf_len; idx++;
    }
    if (user_id > 0) {
        bp[idx].buffer_type = MYSQL_TYPE_LONGLONG; bp[idx].buffer = (char*)&user_id; bp[idx].is_unsigned = 1; idx++;
    }
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("count_guild_infractions execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    int64_t total = 0;
    MYSQL_BIND rb[1]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&total;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return (int)total;
}

// ---------------------------------------------------------------------------
// pardon_infraction
// ---------------------------------------------------------------------------
bool Database::pardon_infraction(uint64_t guild_id, uint32_t case_number, uint64_t pardoned_by, const std::string& reason) {
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_infractions SET pardoned = 1, active = 0, "
        "pardoned_by = ?, pardoned_at = NOW(), pardoned_reason = ? "
        "WHERE guild_id = ? AND case_number = ? AND pardoned = 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("pardon_infraction prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&pardoned_by; bp[0].is_unsigned = 1;
    unsigned long r_len = reason.size();
    bp[1].buffer_type = MYSQL_TYPE_STRING; bp[1].buffer = (char*)reason.c_str();
    bp[1].buffer_length = reason.size(); bp[1].length = &r_len;
    bp[2].buffer_type = MYSQL_TYPE_LONGLONG; bp[2].buffer = (char*)&guild_id; bp[2].is_unsigned = 1;
    bp[3].buffer_type = MYSQL_TYPE_LONG; bp[3].buffer = (char*)&case_number; bp[3].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("pardon_infraction execute"); }
    uint64_t affected = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok && affected > 0;
}

// ---------------------------------------------------------------------------
// bulk_pardon_user
// ---------------------------------------------------------------------------
int Database::bulk_pardon_user(uint64_t guild_id, uint64_t user_id, uint64_t pardoned_by, const std::string& reason) {
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_infractions SET pardoned = 1, active = 0, "
        "pardoned_by = ?, pardoned_at = NOW(), pardoned_reason = ? "
        "WHERE guild_id = ? AND user_id = ? AND pardoned = 0 AND active = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("bulk_pardon_user prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&pardoned_by; bp[0].is_unsigned = 1;
    unsigned long r_len = reason.size();
    bp[1].buffer_type = MYSQL_TYPE_STRING; bp[1].buffer = (char*)reason.c_str();
    bp[1].buffer_length = reason.size(); bp[1].length = &r_len;
    bp[2].buffer_type = MYSQL_TYPE_LONGLONG; bp[2].buffer = (char*)&guild_id; bp[2].is_unsigned = 1;
    bp[3].buffer_type = MYSQL_TYPE_LONGLONG; bp[3].buffer = (char*)&user_id; bp[3].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("bulk_pardon_user execute");
    }
    int affected = (int)mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return affected;
}

// ---------------------------------------------------------------------------
// pardon_user_type
// ---------------------------------------------------------------------------
int Database::pardon_user_type(uint64_t guild_id, uint64_t user_id, const std::string& type, uint64_t pardoned_by, const std::string& reason) {
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_infractions SET pardoned = 1, active = 0, "
        "pardoned_by = ?, pardoned_at = NOW(), pardoned_reason = ? "
        "WHERE guild_id = ? AND user_id = ? AND type = ? AND pardoned = 0 AND active = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("pardon_user_type prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&pardoned_by; bp[0].is_unsigned = 1;
    unsigned long r_len = reason.size();
    bp[1].buffer_type = MYSQL_TYPE_STRING; bp[1].buffer = (char*)reason.c_str();
    bp[1].buffer_length = reason.size(); bp[1].length = &r_len;
    bp[2].buffer_type = MYSQL_TYPE_LONGLONG; bp[2].buffer = (char*)&guild_id; bp[2].is_unsigned = 1;
    bp[3].buffer_type = MYSQL_TYPE_LONGLONG; bp[3].buffer = (char*)&user_id; bp[3].is_unsigned = 1;
    unsigned long t_len = type.size();
    bp[4].buffer_type = MYSQL_TYPE_STRING; bp[4].buffer = (char*)type.c_str();
    bp[4].buffer_length = type.size(); bp[4].length = &t_len;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("pardon_user_type execute");
    }
    int affected = (int)mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return affected;
}

// ---------------------------------------------------------------------------
// get_active_timed_infractions
// ---------------------------------------------------------------------------
std::vector<InfractionRow> Database::get_active_timed_infractions() {
    std::vector<InfractionRow> out;
    auto conn = pool_->acquire();
    const char* q = "SELECT id, guild_id, case_number, user_id, moderator_id, type, "
        "COALESCE(reason,''), points, COALESCE(duration_seconds,0), expires_at, "
        "active, pardoned, COALESCE(pardoned_by,0), pardoned_at, COALESCE(pardoned_reason,''), "
        "COALESCE(metadata,'{}'), created_at "
        "FROM guild_infractions WHERE active = 1 AND expires_at > NOW()";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_active_timed_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_active_timed_infractions execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    uint64_t rid, r_guild, r_user, r_mod, r_pardoned_by;
    uint32_t r_case, r_dur;
    char r_type[32], r_reason[2048], r_pardon_reason[2048], r_meta[4096];
    unsigned long r_type_len, r_reason_len, r_pardon_reason_len, r_meta_len;
    double r_points;
    my_bool r_active, r_pardoned;
    MYSQL_TIME r_expires, r_pardoned_at, r_created;
    my_bool r_expires_null, r_pardoned_at_null;

    MYSQL_BIND rb[17]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&rid; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_case; rb[2].is_unsigned = 1;
    rb[3].buffer_type = MYSQL_TYPE_LONGLONG; rb[3].buffer = (char*)&r_user; rb[3].is_unsigned = 1;
    rb[4].buffer_type = MYSQL_TYPE_LONGLONG; rb[4].buffer = (char*)&r_mod; rb[4].is_unsigned = 1;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_type; rb[5].buffer_length = sizeof(r_type); rb[5].length = &r_type_len;
    rb[6].buffer_type = MYSQL_TYPE_STRING; rb[6].buffer = r_reason; rb[6].buffer_length = sizeof(r_reason); rb[6].length = &r_reason_len;
    rb[7].buffer_type = MYSQL_TYPE_DOUBLE; rb[7].buffer = (char*)&r_points;
    rb[8].buffer_type = MYSQL_TYPE_LONG; rb[8].buffer = (char*)&r_dur; rb[8].is_unsigned = 1;
    rb[9].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[9].buffer = (char*)&r_expires; rb[9].is_null = &r_expires_null;
    rb[10].buffer_type = MYSQL_TYPE_TINY; rb[10].buffer = (char*)&r_active;
    rb[11].buffer_type = MYSQL_TYPE_TINY; rb[11].buffer = (char*)&r_pardoned;
    rb[12].buffer_type = MYSQL_TYPE_LONGLONG; rb[12].buffer = (char*)&r_pardoned_by; rb[12].is_unsigned = 1;
    rb[13].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[13].buffer = (char*)&r_pardoned_at; rb[13].is_null = &r_pardoned_at_null;
    rb[14].buffer_type = MYSQL_TYPE_STRING; rb[14].buffer = r_pardon_reason; rb[14].buffer_length = sizeof(r_pardon_reason); rb[14].length = &r_pardon_reason_len;
    rb[15].buffer_type = MYSQL_TYPE_STRING; rb[15].buffer = r_meta; rb[15].buffer_length = sizeof(r_meta); rb[15].length = &r_meta_len;
    rb[16].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[16].buffer = (char*)&r_created;

    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    auto mysql_time_to_tp = [](const MYSQL_TIME& mt) -> std::chrono::system_clock::time_point {
        struct tm t{};
        t.tm_year = mt.year - 1900; t.tm_mon = mt.month - 1; t.tm_mday = mt.day;
        t.tm_hour = mt.hour; t.tm_min = mt.minute; t.tm_sec = mt.second;
        return std::chrono::system_clock::from_time_t(timegm(&t));
    };

    while (mysql_stmt_fetch(stmt) == 0) {
        InfractionRow row;
        row.id = rid; row.guild_id = r_guild; row.case_number = r_case;
        row.user_id = r_user; row.moderator_id = r_mod;
        row.type = std::string(r_type, r_type_len);
        row.reason = std::string(r_reason, r_reason_len);
        row.points = r_points; row.duration_seconds = r_dur;
        row.active = r_active; row.pardoned = r_pardoned;
        row.pardoned_by = r_pardoned_by;
        row.pardoned_reason = std::string(r_pardon_reason, r_pardon_reason_len);
        row.metadata = std::string(r_meta, r_meta_len);
        if (!r_expires_null) row.expires_at = mysql_time_to_tp(r_expires);
        if (!r_pardoned_at_null) row.pardoned_at = mysql_time_to_tp(r_pardoned_at);
        row.created_at = mysql_time_to_tp(r_created);
        out.push_back(row);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// ---------------------------------------------------------------------------
// update_infraction_reason
// ---------------------------------------------------------------------------
bool Database::update_infraction_reason(uint64_t guild_id, uint32_t case_number, const std::string& reason) {
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_infractions SET reason = ? WHERE guild_id = ? AND case_number = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("update_infraction_reason prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }

    unsigned long reason_len = (unsigned long)reason.size();
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_STRING; bp[0].buffer = (char*)reason.c_str(); bp[0].buffer_length = reason.size(); bp[0].length = &reason_len;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&guild_id; bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&case_number; bp[2].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);

    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// update_infraction_duration
// ---------------------------------------------------------------------------
bool Database::update_infraction_duration(uint64_t guild_id, uint32_t case_number, uint32_t new_duration_seconds) {
    auto conn = pool_->acquire();
    
    // We update duration_seconds and recalculate expires_at from created_at
    // If duration is 0, expires_at becomes NULL (permanent)
    const char* q = "UPDATE guild_infractions SET "
                    "duration_seconds = ?, "
                    "expires_at = IF(? > 0, DATE_ADD(created_at, INTERVAL ? SECOND), NULL) "
                    "WHERE guild_id = ? AND case_number = ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("update_infraction_duration prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }

    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    // 0: duration_seconds
    bp[0].buffer_type = MYSQL_TYPE_LONG; bp[0].buffer = (char*)&new_duration_seconds; bp[0].is_unsigned = 1;
    // 1: duration_seconds (for IF test)
    bp[1].buffer_type = MYSQL_TYPE_LONG; bp[1].buffer = (char*)&new_duration_seconds; bp[1].is_unsigned = 1;
    // 2: duration_seconds (for DATE_ADD)
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&new_duration_seconds; bp[2].is_unsigned = 1;
    // 3: guild_id
    bp[3].buffer_type = MYSQL_TYPE_LONGLONG; bp[3].buffer = (char*)&guild_id; bp[3].is_unsigned = 1;
    // 4: case_number
    bp[4].buffer_type = MYSQL_TYPE_LONG; bp[4].buffer = (char*)&case_number; bp[4].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) {
        last_error_ = mysql_stmt_error(stmt); log_error("update_infraction_duration execute");
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

// ---------------------------------------------------------------------------
// expire_infractions
// ---------------------------------------------------------------------------
int Database::expire_infractions() {
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_infractions SET active = 0 "
        "WHERE expires_at IS NOT NULL AND expires_at <= NOW() AND active = 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("expire_infractions prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("expire_infractions execute");
    }
    int affected = (int)mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return affected;
}

// ============================================================================
// Free-function wrappers
// ============================================================================
namespace infraction_operations {

std::optional<InfractionRow> create_infraction(
    Database* db, uint64_t guild_id, uint64_t user_id, uint64_t moderator_id,
    const std::string& type, const std::string& reason,
    double points, uint32_t duration_seconds, const std::string& metadata_json)
{
    return db->create_infraction(guild_id, user_id, moderator_id, type, reason, points, duration_seconds, metadata_json);
}

std::optional<InfractionRow> get_infraction(Database* db, uint64_t guild_id, uint32_t case_number) {
    return db->get_infraction(guild_id, case_number);
}

std::vector<InfractionRow> get_user_infractions(Database* db, uint64_t guild_id, uint64_t user_id,
    bool active_only, int limit, int offset) {
    return db->get_user_infractions(guild_id, user_id, active_only, limit, offset);
}

double get_user_active_points(Database* db, uint64_t guild_id, uint64_t user_id, int within_days) {
    return db->get_user_active_points(guild_id, user_id, within_days);
}

std::vector<InfractionRow> get_recent_infractions(Database* db, uint64_t guild_id,
    int limit, int offset, const std::string& type_filter) {
    return db->get_recent_infractions(guild_id, limit, offset, type_filter);
}

std::vector<InfractionRow> get_moderator_actions(Database* db, uint64_t guild_id,
    uint64_t moderator_id, int limit, int offset) {
    return db->get_moderator_actions(guild_id, moderator_id, limit, offset);
}

InfractionCounts count_infractions(Database* db, uint64_t guild_id, uint64_t user_id) {
    return db->count_infractions(guild_id, user_id);
}

int count_guild_infractions(Database* db, uint64_t guild_id, const std::string& type_filter, uint64_t user_id) {
    return db->count_guild_infractions(guild_id, type_filter, user_id);
}

bool pardon_infraction(Database* db, uint64_t guild_id, uint32_t case_number, uint64_t pardoned_by, const std::string& reason) {
    return db->pardon_infraction(guild_id, case_number, pardoned_by, reason);
}

int bulk_pardon_user(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t pardoned_by, const std::string& reason) {
    return db->bulk_pardon_user(guild_id, user_id, pardoned_by, reason);
}

int pardon_user_type(Database* db, uint64_t guild_id, uint64_t user_id, const std::string& type,
    uint64_t pardoned_by, const std::string& reason) {
    return db->pardon_user_type(guild_id, user_id, type, pardoned_by, reason);
}

int expire_infractions(Database* db) {
    return db->expire_infractions();
}

std::vector<InfractionRow> get_active_timed_infractions(Database* db) {
    return db->get_active_timed_infractions();
}

bool update_infraction_reason(Database* db, uint64_t guild_id, uint32_t case_number, const std::string& reason) {
    return db->update_infraction_reason(guild_id, case_number, reason);
}

bool update_infraction_duration(Database* db, uint64_t guild_id, uint32_t case_number, uint32_t new_duration_seconds) {
    return db->update_infraction_duration(guild_id, case_number, new_duration_seconds);
}

} // namespace infraction_operations
} // namespace db
} // namespace bronx
