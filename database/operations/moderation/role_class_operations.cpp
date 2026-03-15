#include "role_class_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <set>
#include <algorithm>

// Minimal JSON helpers — we only need to parse the restrictions object
// which has the shape: {"allowed_commands":["cmd1"],"denied_commands":["cmd2"]}
// We do minimal parsing to avoid pulling in a full JSON library.
namespace {

// Extract a JSON string array value by key.  Returns empty vector if not found.
std::vector<std::string> json_string_array(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return out;
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return out;
    std::string arr = json.substr(pos + 1, end - pos - 1);
    // Extract quoted strings
    size_t p = 0;
    while ((p = arr.find('"', p)) != std::string::npos) {
        size_t q = arr.find('"', p + 1);
        if (q == std::string::npos) break;
        out.push_back(arr.substr(p + 1, q - p - 1));
        p = q + 1;
    }
    return out;
}

} // anon namespace

namespace bronx {
namespace db {

// ============================================================================
// Role class CRUD
// ============================================================================

std::optional<RoleClass> Database::create_role_class(
    uint64_t guild_id, const std::string& name, int priority,
    bool inherit_lower, const std::string& restrictions_json)
{
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO guild_role_classes (guild_id, name, priority, inherit_lower, restrictions) "
        "VALUES (?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("create_role_class prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    my_bool inh = inherit_lower ? 1 : 0;
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    unsigned long n_len = name.size();
    bp[1].buffer_type = MYSQL_TYPE_STRING; bp[1].buffer = (char*)name.c_str();
    bp[1].buffer_length = name.size(); bp[1].length = &n_len;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&priority;
    bp[3].buffer_type = MYSQL_TYPE_TINY; bp[3].buffer = (char*)&inh;
    unsigned long r_len = restrictions_json.size();
    bp[4].buffer_type = MYSQL_TYPE_STRING; bp[4].buffer = (char*)restrictions_json.c_str();
    bp[4].buffer_length = restrictions_json.size(); bp[4].length = &r_len;
    mysql_stmt_bind_param(stmt, bp);

    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("create_role_class execute");
        mysql_stmt_close(stmt); pool_->release(conn); return std::nullopt;
    }
    uint32_t new_id = (uint32_t)mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt); pool_->release(conn);

    RoleClass rc;
    rc.id = new_id; rc.guild_id = guild_id; rc.name = name;
    rc.priority = priority; rc.inherit_lower = inherit_lower;
    rc.restrictions = restrictions_json;
    rc.created_at = std::chrono::system_clock::now();
    return rc;
}

bool Database::update_role_class(uint32_t class_id, const std::string& name,
    int priority, bool inherit_lower, const std::string& restrictions_json)
{
    auto conn = pool_->acquire();
    const char* q = "UPDATE guild_role_classes SET name=?, priority=?, inherit_lower=?, restrictions=? WHERE id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("update_role_class prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[5]; memset(bp, 0, sizeof(bp));
    my_bool inh = inherit_lower ? 1 : 0;
    unsigned long n_len = name.size();
    bp[0].buffer_type = MYSQL_TYPE_STRING; bp[0].buffer = (char*)name.c_str();
    bp[0].buffer_length = name.size(); bp[0].length = &n_len;
    bp[1].buffer_type = MYSQL_TYPE_LONG; bp[1].buffer = (char*)&priority;
    bp[2].buffer_type = MYSQL_TYPE_TINY; bp[2].buffer = (char*)&inh;
    unsigned long r_len = restrictions_json.size();
    bp[3].buffer_type = MYSQL_TYPE_STRING; bp[3].buffer = (char*)restrictions_json.c_str();
    bp[3].buffer_length = restrictions_json.size(); bp[3].length = &r_len;
    bp[4].buffer_type = MYSQL_TYPE_LONG; bp[4].buffer = (char*)&class_id; bp[4].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("update_role_class execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

bool Database::delete_role_class(uint32_t class_id) {
    auto conn = pool_->acquire();
    const char* q = "DELETE FROM guild_role_classes WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("delete_role_class prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONG; bp[0].buffer = (char*)&class_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("delete_role_class execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

std::vector<RoleClass> Database::get_role_classes(uint64_t guild_id) {
    std::vector<RoleClass> out;
    auto conn = pool_->acquire();
    const char* q = "SELECT id, guild_id, name, priority, inherit_lower, "
        "COALESCE(restrictions, '{}'), created_at "
        "FROM guild_role_classes WHERE guild_id = ? ORDER BY priority DESC";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_role_classes prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_role_classes execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }

    uint32_t r_id; uint64_t r_guild;
    char r_name[64], r_rest[4096];
    unsigned long r_name_len, r_rest_len;
    int r_pri; my_bool r_inh;
    MYSQL_TIME r_created;

    MYSQL_BIND rb[7]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONG; rb[0].buffer = (char*)&r_id; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_guild; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_STRING; rb[2].buffer = r_name; rb[2].buffer_length = sizeof(r_name); rb[2].length = &r_name_len;
    rb[3].buffer_type = MYSQL_TYPE_LONG; rb[3].buffer = (char*)&r_pri;
    rb[4].buffer_type = MYSQL_TYPE_TINY; rb[4].buffer = (char*)&r_inh;
    rb[5].buffer_type = MYSQL_TYPE_STRING; rb[5].buffer = r_rest; rb[5].buffer_length = sizeof(r_rest); rb[5].length = &r_rest_len;
    rb[6].buffer_type = MYSQL_TYPE_TIMESTAMP; rb[6].buffer = (char*)&r_created;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        RoleClass rc;
        rc.id = r_id; rc.guild_id = r_guild;
        rc.name = std::string(r_name, r_name_len);
        rc.priority = r_pri; rc.inherit_lower = r_inh;
        rc.restrictions = std::string(r_rest, r_rest_len);
        struct tm t{}; t.tm_year = r_created.year - 1900; t.tm_mon = r_created.month - 1;
        t.tm_mday = r_created.day; t.tm_hour = r_created.hour;
        t.tm_min = r_created.minute; t.tm_sec = r_created.second;
        rc.created_at = std::chrono::system_clock::from_time_t(timegm(&t));
        out.push_back(rc);
    }
    mysql_stmt_close(stmt); pool_->release(conn);
    return out;
}

// ============================================================================
// Role ↔ Class membership
// ============================================================================

bool Database::assign_role_to_class(uint64_t guild_id, uint64_t role_id, uint32_t class_id) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO guild_role_class_members (guild_id, role_id, class_id) "
        "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE class_id = VALUES(class_id)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("assign_role_to_class prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[3]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&role_id; bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONG; bp[2].buffer = (char*)&class_id; bp[2].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("assign_role_to_class execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

bool Database::remove_role_from_class(uint64_t guild_id, uint64_t role_id) {
    auto conn = pool_->acquire();
    const char* q = "DELETE FROM guild_role_class_members WHERE guild_id = ? AND role_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("remove_role_from_class prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = (char*)&guild_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = (char*)&role_id; bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) { last_error_ = mysql_stmt_error(stmt); log_error("remove_role_from_class execute"); }
    mysql_stmt_close(stmt); pool_->release(conn);
    return ok;
}

std::vector<RoleClassMember> Database::get_class_members(uint32_t class_id) {
    std::vector<RoleClassMember> out;
    auto conn = pool_->acquire();
    const char* q = "SELECT guild_id, role_id, class_id FROM guild_role_class_members WHERE class_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_class_members prepare");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONG; bp[0].buffer = (char*)&class_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt); log_error("get_class_members execute");
        mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    uint64_t r_guild, r_role; uint32_t r_class;
    MYSQL_BIND rb[3]; memset(rb, 0, sizeof(rb));
    rb[0].buffer_type = MYSQL_TYPE_LONGLONG; rb[0].buffer = (char*)&r_guild; rb[0].is_unsigned = 1;
    rb[1].buffer_type = MYSQL_TYPE_LONGLONG; rb[1].buffer = (char*)&r_role; rb[1].is_unsigned = 1;
    rb[2].buffer_type = MYSQL_TYPE_LONG; rb[2].buffer = (char*)&r_class; rb[2].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, rb);
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.push_back({r_guild, r_role, r_class});
    }
    mysql_stmt_close(stmt); pool_->release(conn);
    return out;
}

// ============================================================================
// Permission resolution: check_command_allowed
// ============================================================================
// Algorithm:
//   1. Get all role classes for the guild (ordered by priority DESC).
//   2. For each user role, look up its class_id in guild_role_class_members.
//   3. Collect the set of matched classes. If inherit_lower is set on any
//      matched class, also include all classes with lower priority.
//   4. Merge restrictions: denied wins over allowed.
//   5. If command is in denied_commands → blocked.
//      If allowed_commands is non-empty and command not in it → blocked.
//      Otherwise → allowed.

namespace role_class_operations {

bool check_command_allowed(Database* db, uint64_t guild_id,
    const std::vector<uint64_t>& user_role_ids, const std::string& command_name)
{
    if (user_role_ids.empty()) return true; // no roles → no restrictions

    auto classes = db->get_role_classes(guild_id);
    if (classes.empty()) return true; // no classes configured

    // Build role → class_id map by querying membership for each user role.
    // For efficiency we do a single query for all roles in the guild.
    // But since we don't have a bulk method, iterate (classes are small).
    std::set<uint32_t> matched_class_ids;
    int lowest_matched_priority = INT32_MAX;

    for (auto& rc : classes) {
        auto members = db->get_class_members(rc.id);
        for (auto& m : members) {
            for (auto rid : user_role_ids) {
                if (m.role_id == rid) {
                    matched_class_ids.insert(rc.id);
                    if (rc.inherit_lower && rc.priority < lowest_matched_priority) {
                        lowest_matched_priority = rc.priority;
                    }
                }
            }
        }
    }

    if (matched_class_ids.empty()) return true; // user has no class-assigned roles

    // If any matched class has inherit_lower, add all classes with lower priority
    bool any_inherit = false;
    int highest_inherit_priority = -1;
    for (auto& rc : classes) {
        if (matched_class_ids.count(rc.id) && rc.inherit_lower) {
            any_inherit = true;
            if (rc.priority > highest_inherit_priority) highest_inherit_priority = rc.priority;
        }
    }
    if (any_inherit) {
        for (auto& rc : classes) {
            if (rc.priority < highest_inherit_priority) {
                matched_class_ids.insert(rc.id);
            }
        }
    }

    // Merge restrictions from all matched classes
    std::set<std::string> allowed, denied;
    for (auto& rc : classes) {
        if (!matched_class_ids.count(rc.id)) continue;
        auto ac = json_string_array(rc.restrictions, "allowed_commands");
        auto dc = json_string_array(rc.restrictions, "denied_commands");
        for (auto& c : ac) allowed.insert(c);
        for (auto& c : dc) denied.insert(c);
    }

    // Denied takes precedence
    if (denied.count(command_name)) return false;
    // If there's an allow-list and this command isn't in it → blocked
    if (!allowed.empty() && !allowed.count(command_name)) return false;
    return true;
}

// Free-function wrappers
std::optional<RoleClass> create_role_class(Database* db, uint64_t guild_id,
    const std::string& name, int priority, bool inherit_lower, const std::string& restrictions_json) {
    return db->create_role_class(guild_id, name, priority, inherit_lower, restrictions_json);
}
bool update_role_class(Database* db, uint32_t class_id,
    const std::string& name, int priority, bool inherit_lower, const std::string& restrictions_json) {
    return db->update_role_class(class_id, name, priority, inherit_lower, restrictions_json);
}
bool delete_role_class(Database* db, uint32_t class_id) { return db->delete_role_class(class_id); }
std::vector<RoleClass> get_role_classes(Database* db, uint64_t guild_id) { return db->get_role_classes(guild_id); }
bool assign_role_to_class(Database* db, uint64_t guild_id, uint64_t role_id, uint32_t class_id) {
    return db->assign_role_to_class(guild_id, role_id, class_id);
}
bool remove_role_from_class(Database* db, uint64_t guild_id, uint64_t role_id) {
    return db->remove_role_from_class(guild_id, role_id);
}
std::vector<RoleClassMember> get_class_members(Database* db, uint32_t class_id) {
    return db->get_class_members(class_id);
}

} // namespace role_class_operations
} // namespace db
} // namespace bronx
