#include "leaderboard_operations.h"
#include "../../core/database.h"
#include "../../../embed_style.h"
#include <sstream>

namespace bronx {
namespace db {

// Progressive rebirth emojis based on level
static inline std::string get_rebirth_emoji_for_level(int level) {
    switch (level) {
        case 1: return "<:rebirth:1481426459200327720>";
        case 2: return "<:rebirth2:1481426460517601340>";
        case 3: return "<:rebirth3:1481427415195451452>";
        case 4: return "<:rebirth4:1481427416038510622>";
        case 5: return "<:rebirth5:1481427838400856197>";
        default: return "<:rebirth:1481426459200327720>"; // fallback to level 1
    }
}

std::vector<LeaderboardEntry> Database::get_networth_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Always return global leaderboard - guild filtering done in command handler
    std::string query = "SELECT user_id, wallet + bank as networth "
                       "FROM users "
                       "WHERE wallet + bank > 0 "
                       "ORDER BY networth DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        pool_->release(conn);
        return entries;
    }
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    // Bind parameters
    MYSQL_BIND bind[1] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t networth;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &networth;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = ""; // Will be filled in by the command
            entry.value = networth;
            entry.rank = rank++;
            entry.extra_info = "";
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> Database::get_wallet_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Always return global leaderboard - guild filtering done in command handler
    std::string query = "SELECT user_id, wallet "
                       "FROM users "
                       "WHERE wallet > 0 "
                       "ORDER BY wallet DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        pool_->release(conn);
        return entries;
    }
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    MYSQL_BIND bind[1] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t wallet;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &wallet;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = "User#" + std::to_string(user_id);
            entry.value = wallet;
            entry.rank = rank++;
            entry.extra_info = "";
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> Database::get_bank_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Always return global leaderboard - guild filtering done in command handler
    std::string query = "SELECT user_id, bank "
                       "FROM users "
                       "WHERE bank > 0 "
                       "ORDER BY bank DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        pool_->release(conn);
        return entries;
    }
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    MYSQL_BIND bind[1] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t bank;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &bank;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = "User#" + std::to_string(user_id);
            entry.value = bank;
            entry.rank = rank++;
            entry.extra_info = "";
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> Database::get_inventory_value_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    // Calculate total inventory value per user
    // For now, return empty - can be implemented later with inventory valuation logic
    return entries;
}

std::vector<LeaderboardEntry> Database::get_fish_caught_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Count fish from inventory table - always global, guild filtering done in command handler
    std::string query = "SELECT user_id, COUNT(*) as fish_count "
                       "FROM user_inventory "
                       "WHERE item_type = 'collectible' "
                       "GROUP BY user_id "
                       "ORDER BY fish_count DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        pool_->release(conn);
        return entries;
    }
    
    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    MYSQL_BIND bind[1] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t fish_caught;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &fish_caught;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = "User#" + std::to_string(user_id);
            entry.value = fish_caught;
            entry.rank = rank++;
            entry.extra_info = "🐟";
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> Database::get_gambling_wins_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("gambling_wins", guild_id, limit, "🎰");
}

std::vector<LeaderboardEntry> Database::get_fish_sold_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("fish_sold", guild_id, limit, "💰");
}

std::vector<LeaderboardEntry> Database::get_most_valuable_fish_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Find the highest value fish each user owns - always global, guild filtering done in command handler
    std::string query = "SELECT i.user_id, "
                       "MAX(CAST(JSON_EXTRACT(i.metadata, '$.value') AS SIGNED)) as max_value "
                       "FROM user_inventory i "
                       "WHERE i.item_type = 'collectible' "
                       "GROUP BY i.user_id ORDER BY max_value DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    MYSQL_BIND bind[1] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t max_value;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &max_value;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = "User#" + std::to_string(user_id);
            entry.value = max_value;
            entry.rank = rank++;
            entry.extra_info = "💎";
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

std::vector<LeaderboardEntry> Database::get_fishing_profit_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("fish_profit", guild_id, limit, "🎣💰");
}

std::vector<LeaderboardEntry> Database::get_gambling_losses_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("gambling_losses", guild_id, limit, "💸");
}

std::vector<LeaderboardEntry> Database::get_gambling_profit_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("gambling_profit", guild_id, limit, "🎰💰");
}

std::vector<LeaderboardEntry> Database::get_slots_wins_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("slots_wins", guild_id, limit, "🎰");
}

std::vector<LeaderboardEntry> Database::get_coinflip_wins_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("coinflip_wins", guild_id, limit, "🪙");
}

std::vector<LeaderboardEntry> Database::get_commands_used_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("commands_used", guild_id, limit, "💻");
}

std::vector<LeaderboardEntry> Database::get_daily_streak_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("daily_streak", guild_id, limit, "🔥");
}

std::vector<LeaderboardEntry> Database::get_work_count_leaderboard(uint64_t guild_id, int limit) {
    return get_stats_leaderboard("work_count", guild_id, limit, "💼");
}

std::vector<LeaderboardEntry> Database::get_prestige_leaderboard(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Always return global leaderboard - guild filtering done in command handler
    // Try rebirth-aware query first: includes users who rebirthed (prestige reset to 0)
    // Effective prestige = rebirth_level * 100 + prestige for ranking
    std::string rb_query =
        "SELECT u.user_id, u.prestige, COALESCE(r.rebirth_level, 0) "
        "FROM users u "
        "LEFT JOIN user_rebirths r ON u.user_id = r.user_id "
        "WHERE u.prestige > 0 OR r.rebirth_level > 0 "
        "ORDER BY (COALESCE(r.rebirth_level, 0) * 100 + u.prestige) DESC, u.user_id ASC "
        "LIMIT " + std::to_string(limit);
    
    bool ok = false;
    if (mysql_query(conn->get(), rb_query.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (res) {
            ok = true;
            int rank = 1;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                LeaderboardEntry entry;
                entry.user_id = std::stoull(row[0]);
                entry.username = "";
                int prestige = row[1] ? std::stoi(row[1]) : 0;
                int rebirth = row[2] ? std::stoi(row[2]) : 0;
                entry.value = rebirth * 100 + prestige;
                entry.rank = rank++;
                if (rebirth > 0) {
                    entry.extra_info = get_rebirth_emoji_for_level(rebirth) + " R" + std::to_string(rebirth) + " \xC2\xB7 P" + std::to_string(prestige) + " " + bronx::EMOJI_STAR;
                } else {
                    entry.extra_info = std::to_string(prestige) + " " + bronx::EMOJI_STAR;
                }
                entries.push_back(entry);
            }
            mysql_free_result(res);
        }
    }
    
    // Fallback: original prestige-only query (user_rebirths table may not exist yet)
    if (!ok) {
        std::string query = "SELECT user_id, prestige "
                           "FROM users "
                           "WHERE prestige > 0 "
                           "ORDER BY prestige DESC, user_id ASC "
                           "LIMIT " + std::to_string(limit);
        if (mysql_query(conn->get(), query.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn->get());
            if (res) {
                int rank = 1;
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res)) != nullptr) {
                    LeaderboardEntry entry;
                    entry.user_id = std::stoull(row[0]);
                    entry.username = "";
                    entry.value = std::stoi(row[1]);
                    entry.rank = rank++;
                    entry.extra_info = std::to_string(entry.value) + " " + bronx::EMOJI_STAR;
                    entries.push_back(entry);
                }
                mysql_free_result(res);
            }
        }
    }
    
    pool_->release(conn);
    return entries;
}

// Helper method for stats-based leaderboards
std::vector<LeaderboardEntry> Database::get_stats_leaderboard(const std::string& stat_name, uint64_t guild_id, int limit, const std::string& emoji) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    
    // Always return global results - guild filtering done in command handler
    std::string query = "SELECT us.user_id, us.stat_value "
                       "FROM user_stats_ext us "
                       "WHERE us.stat_name = ? "
                       "ORDER BY us.stat_value DESC LIMIT ?";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }
    
    MYSQL_BIND bind[2] = {};
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)stat_name.c_str();
    bind[0].buffer_length = stat_name.length();
    
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &limit;
    
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t stat_value;
        
        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &stat_value;
        
        mysql_stmt_bind_result(stmt, result_bind);
        
        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.username = "User#" + std::to_string(user_id);
            entry.value = stat_value;
            entry.rank = rank++;
            entry.extra_info = emoji;
            entries.push_back(entry);
        }
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

// Placeholder methods
std::vector<LeaderboardEntry> Database::get_leaderboard(const std::string& type, int limit) {
    std::vector<LeaderboardEntry> entries;
    return entries;
}

int Database::get_user_rank(uint64_t user_id, const std::string& type) {
    return 0; // Placeholder
}

void Database::update_leaderboard_cache() {
    // Placeholder for cache update
}

std::vector<LeaderboardEntry> Database::get_guild_top_wealthy_users(uint64_t guild_id, int limit) {
    std::vector<LeaderboardEntry> entries;
    auto conn = pool_->acquire();
    if (!conn) return entries;

    // Join with server_xp to verify guild membership. 
    // We only tax users with at least 1,000,000 networth.
    std::string query = "SELECT u.user_id, (u.wallet + u.bank) as networth "
                       "FROM users u "
                       "JOIN server_xp s ON u.user_id = s.user_id "
                       "WHERE s.guild_id = ? AND (u.wallet + u.bank) >= 1000000 "
                       "GROUP BY u.user_id "
                       "ORDER BY networth DESC LIMIT ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        pool_->release(conn);
        return entries;
    }

    if (mysql_stmt_prepare(stmt, query.c_str(), query.length()) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return entries;
    }

    MYSQL_BIND bind[2] = {};
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &guild_id;
    bind[0].is_unsigned = 1;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &limit;

    mysql_stmt_bind_param(stmt, bind);

    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result_bind[2] = {};
        uint64_t user_id;
        int64_t networth;

        result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[0].buffer = &user_id;
        result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
        result_bind[1].buffer = &networth;

        mysql_stmt_bind_result(stmt, result_bind);

        int rank = 1;
        while (mysql_stmt_fetch(stmt) == 0) {
            LeaderboardEntry entry;
            entry.user_id = user_id;
            entry.value = networth;
            entry.rank = rank++;
            entries.push_back(entry);
        }
    }

    mysql_stmt_close(stmt);
    pool_->release(conn);
    return entries;
}

} // namespace db
} // namespace bronx