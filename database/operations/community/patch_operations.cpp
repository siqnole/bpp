#include "patch_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <iostream>
#include <sstream>

namespace bronx {
namespace db {

std::string patch_operations::get_next_version(Database* db) {
    auto latest = get_latest_patch(db);
    if (!latest.has_value()) {
        return "1.0.0"; // First version
    }
    
    // Parse current version (format: major.minor.patch)
    std::string current = latest->version;
    int major = 1, minor = 0, patch = 0;
    
    size_t first_dot = current.find('.');
    size_t second_dot = current.find('.', first_dot + 1);
    
    if (first_dot != std::string::npos && second_dot != std::string::npos) {
        major = std::stoi(current.substr(0, first_dot));
        minor = std::stoi(current.substr(first_dot + 1, second_dot - first_dot - 1));
        patch = std::stoi(current.substr(second_dot + 1));
    }
    
    // Increment patch version
    patch++;
    
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    return oss.str();
}

bool patch_operations::create_patch_note(Database* db, const std::string& notes, uint64_t author_id) {
    std::string version = get_next_version(db);
    return db->add_patch_note(version, notes, author_id);
}

std::optional<PatchNote> patch_operations::get_latest_patch(Database* db) {
    return db->get_latest_patch();
}

std::vector<PatchNote> patch_operations::get_all_patches(Database* db, int limit, int offset) {
    return db->get_all_patches(limit, offset);
}

int patch_operations::get_patch_count(Database* db) {
    return db->get_patch_count();
}

bool patch_operations::delete_patch_by_id(Database* db, uint32_t patch_id) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM patch_notes WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&patch_id;
    bind[0].is_unsigned = 1;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

bool patch_operations::delete_patch_by_version(Database* db, const std::string& version) {
    auto conn = db->get_pool()->acquire();
    if (!conn) return false;
    
    const char* query = "DELETE FROM patch_notes WHERE version = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        db->get_pool()->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)version.c_str();
    bind[0].buffer_length = version.size();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0;
    
    mysql_stmt_close(stmt);
    db->get_pool()->release(conn);
    return success;
}

// Database class implementations

bool Database::add_patch_note(const std::string& version, const std::string& notes, uint64_t author_id) {
    auto conn = pool_->acquire();
    const char* query = "INSERT INTO patch_notes (version, notes, author_id) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("add_patch_note prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)version.c_str();
    bind[0].buffer_length = version.size();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)notes.c_str();
    bind[1].buffer_length = notes.size();

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&author_id;
    bind[2].is_unsigned = 1;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("add_patch_note bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }

    bool success = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<PatchNote> Database::get_latest_patch() {
    auto conn = pool_->acquire();
    const char* query = "SELECT id, version, notes, author_id, UNIX_TIMESTAMP(created_at) FROM patch_notes ORDER BY created_at DESC LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_latest_patch prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_latest_patch execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    PatchNote patch;
    static char version_buf[20];
    static char notes_buf[16384];
    unsigned long version_len = 0;
    unsigned long notes_len = 0;
    long long ts = 0;

    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = (char*)&patch.id;
    result[0].is_unsigned = 1;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = version_buf;
    result[1].buffer_length = sizeof(version_buf);
    result[1].length = &version_len;

    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = notes_buf;
    result[2].buffer_length = sizeof(notes_buf);
    result[2].length = &notes_len;

    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = (char*)&patch.author_id;
    result[3].is_unsigned = 1;

    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = (char*)&ts;

    if (mysql_stmt_bind_result(stmt, result) != 0) {
        log_error("get_latest_patch bind_result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }

    int fetch_status = mysql_stmt_fetch(stmt);
    if (fetch_status == 0) {
        patch.version = std::string(version_buf, version_len);
        patch.notes = std::string(notes_buf, notes_len);
        patch.created_at = std::chrono::system_clock::from_time_t(ts);
        
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return patch;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return std::nullopt;
}

std::vector<PatchNote> Database::get_all_patches(int limit, int offset) {
    std::vector<PatchNote> results;
    auto conn = pool_->acquire();
    const char* query = "SELECT id, version, notes, author_id, UNIX_TIMESTAMP(created_at) FROM patch_notes ORDER BY created_at DESC LIMIT ? OFFSET ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_all_patches prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&limit;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&offset;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        log_error("get_all_patches bind");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_patches execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    PatchNote patch;
    static char version_buf[20];
    static char notes_buf[16384];
    unsigned long version_len = 0;
    unsigned long notes_len = 0;
    long long ts = 0;

    MYSQL_BIND result[5];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = (char*)&patch.id;
    result[0].is_unsigned = 1;

    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = version_buf;
    result[1].buffer_length = sizeof(version_buf);
    result[1].length = &version_len;

    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = notes_buf;
    result[2].buffer_length = sizeof(notes_buf);
    result[2].length = &notes_len;

    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = (char*)&patch.author_id;
    result[3].is_unsigned = 1;

    result[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result[4].buffer = (char*)&ts;

    if (mysql_stmt_bind_result(stmt, result) != 0) {
        log_error("get_all_patches bind_result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return results;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        patch.version = std::string(version_buf, version_len);
        patch.notes = std::string(notes_buf, notes_len);
        patch.created_at = std::chrono::system_clock::from_time_t(ts);
        results.push_back(patch);
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return results;
}

int Database::get_patch_count() {
    auto conn = pool_->acquire();
    const char* query = "SELECT COUNT(*) FROM patch_notes";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        log_error("get_patch_count prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_patch_count execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    int count = 0;
    MYSQL_BIND result[1];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = (char*)&count;

    if (mysql_stmt_bind_result(stmt, result) != 0) {
        log_error("get_patch_count bind_result");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }

    if (mysql_stmt_fetch(stmt) == 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return count;
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return 0;
}

} // namespace db
} // namespace bronx
