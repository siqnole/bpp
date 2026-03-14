#pragma once
// ============================================================================
// snipe_operations.h — DB operations for the snipe (deleted messages) feature.
// Supports batch inserts from the snipe cache flush, querying with filters,
// and periodic cleanup of old entries.
// ============================================================================

#include "../../core/database.h"
#include "../../core/connection_pool.h"
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <sstream>

namespace bronx {
namespace db {
namespace snipe_operations {

struct DeletedMessageRow {
    uint64_t id = 0;
    uint64_t message_id = 0;
    uint64_t guild_id = 0;
    uint64_t channel_id = 0;
    uint64_t author_id = 0;
    std::string author_tag;
    std::string author_avatar;
    std::string content;
    std::string attachment_urls;  // newline-separated
    std::string embeds_summary;
    std::string deleted_at;       // ISO string from DB
    int64_t deleted_at_unix = 0;  // seconds since epoch
};

// Batch insert deleted messages into the database.
// Called by SnipeCache flush loop.
inline bool save_deleted_messages_batch(
    Database* db,
    const std::vector<DeletedMessageRow>& messages)
{
    if (!db || messages.empty()) return false;
    try {
        // Build multi-row INSERT
        std::ostringstream sql;
        sql << "INSERT INTO guild_deleted_messages "
            << "(message_id, guild_id, channel_id, author_id, author_tag, author_avatar, "
            << "content, attachment_urls, embeds_summary) VALUES ";

        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& m = messages[i];
            if (i > 0) sql << ", ";

            // Escape strings for SQL safety
            auto escape = [](const std::string& s) -> std::string {
                std::string out;
                out.reserve(s.size() + 10);
                for (char c : s) {
                    switch (c) {
                        case '\'': out += "\\'"; break;
                        case '\\': out += "\\\\"; break;
                        case '\0': out += "\\0"; break;
                        default: out += c;
                    }
                }
                return out;
            };

            sql << "('" << m.message_id << "', "
                << "'" << m.guild_id << "', "
                << "'" << m.channel_id << "', "
                << "'" << m.author_id << "', "
                << "'" << escape(m.author_tag) << "', "
                << "'" << escape(m.author_avatar) << "', "
                << "'" << escape(m.content) << "', "
                << "'" << escape(m.attachment_urls) << "', "
                << "'" << escape(m.embeds_summary) << "')";
        }

        return db->execute(sql.str());
    } catch (const std::exception& e) {
        std::cerr << "[snipe_ops] save_deleted_messages_batch failed: " << e.what() << "\n";
        return false;
    }
}

// Query deleted messages with optional filters.
// channel_id=0 means all channels in the guild.
// author_id=0 means all authors.
// since_unix=0 means all time.
// Returns ordered by deleted_at DESC.
inline std::vector<DeletedMessageRow> query_deleted_messages(
    Database* db,
    uint64_t guild_id,
    uint64_t channel_id = 0,
    uint64_t author_id = 0,
    int64_t since_unix = 0,
    int limit = 10,
    int offset = 0)
{
    std::vector<DeletedMessageRow> out;
    if (!db) return out;

    try {
        std::ostringstream sql;
        sql << "SELECT id, message_id, guild_id, channel_id, author_id, "
            << "author_tag, author_avatar, content, attachment_urls, embeds_summary, "
            << "deleted_at, UNIX_TIMESTAMP(deleted_at) "
            << "FROM guild_deleted_messages WHERE guild_id='" << guild_id << "'";

        if (channel_id != 0) {
            sql << " AND channel_id='" << channel_id << "'";
        }
        if (author_id != 0) {
            sql << " AND author_id='" << author_id << "'";
        }
        if (since_unix > 0) {
            sql << " AND deleted_at >= FROM_UNIXTIME(" << since_unix << ")";
        }
        sql << " ORDER BY deleted_at DESC LIMIT " << limit << " OFFSET " << offset;

        std::shared_ptr<Connection> conn = db->get_pool()->acquire();
        if (!conn) return out;

        if (mysql_real_query(conn->get(), sql.str().c_str(), sql.str().size()) != 0) {
            std::cerr << "[snipe_ops] query failed: " << mysql_error(conn->get()) << "\n";
            db->get_pool()->release(conn);
            return out;
        }

        MYSQL_RES* res = mysql_store_result(conn->get());
        if (!res) {
            db->get_pool()->release(conn);
            return out;
        }

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            DeletedMessageRow m;
            m.id             = row[0] ? std::stoull(row[0]) : 0;
            m.message_id     = row[1] ? std::stoull(row[1]) : 0;
            m.guild_id       = row[2] ? std::stoull(row[2]) : 0;
            m.channel_id     = row[3] ? std::stoull(row[3]) : 0;
            m.author_id      = row[4] ? std::stoull(row[4]) : 0;
            m.author_tag     = row[5] ? row[5] : "";
            m.author_avatar  = row[6] ? row[6] : "";
            m.content        = row[7] ? row[7] : "";
            m.attachment_urls = row[8] ? row[8] : "";
            m.embeds_summary = row[9] ? row[9] : "";
            m.deleted_at     = row[10] ? row[10] : "";
            m.deleted_at_unix = row[11] ? std::stoll(row[11]) : 0;
            out.push_back(std::move(m));
        }

        mysql_free_result(res);
        db->get_pool()->release(conn);
    } catch (const std::exception& e) {
        std::cerr << "[snipe_ops] query_deleted_messages failed: " << e.what() << "\n";
    }
    return out;
}

// Delete messages older than the given number of days.
// Returns true if the query executed successfully.
inline bool purge_old_deleted_messages(Database* db, int retention_days = 30) {
    if (!db) return false;
    try {
        std::string sql = "DELETE FROM guild_deleted_messages WHERE deleted_at < "
            "DATE_SUB(NOW(), INTERVAL " + std::to_string(retention_days) + " DAY)";
        return db->execute(sql);
    } catch (const std::exception& e) {
        std::cerr << "[snipe_ops] purge_old_deleted_messages failed: " << e.what() << "\n";
        return false;
    }
}

// Count deleted messages matching filters (for pagination total).
inline int count_deleted_messages(
    Database* db,
    uint64_t guild_id,
    uint64_t channel_id = 0,
    uint64_t author_id = 0,
    int64_t since_unix = 0)
{
    if (!db) return 0;
    try {
        std::ostringstream sql;
        sql << "SELECT COUNT(*) FROM guild_deleted_messages WHERE guild_id='" << guild_id << "'";
        if (channel_id != 0) sql << " AND channel_id='" << channel_id << "'";
        if (author_id != 0)  sql << " AND author_id='" << author_id << "'";
        if (since_unix > 0)  sql << " AND deleted_at >= FROM_UNIXTIME(" << since_unix << ")";

        std::shared_ptr<Connection> conn = db->get_pool()->acquire();
        if (!conn) return 0;

        if (mysql_real_query(conn->get(), sql.str().c_str(), sql.str().size()) != 0) {
            db->get_pool()->release(conn);
            return 0;
        }

        MYSQL_RES* res = mysql_store_result(conn->get());
        if (!res) { db->get_pool()->release(conn); return 0; }

        MYSQL_ROW row = mysql_fetch_row(res);
        int count = (row && row[0]) ? std::stoi(row[0]) : 0;
        mysql_free_result(res);
        db->get_pool()->release(conn);
        return count;
    } catch (const std::exception& e) {
        std::cerr << "[snipe_ops] count_deleted_messages failed: " << e.what() << "\n";
        return 0;
    }
}

} // namespace snipe_operations
} // namespace db
} // namespace bronx
