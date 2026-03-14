#include "privacy_operations.h"
#include "../../core/database.h"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <iostream>
#include <array>

namespace bronx {
namespace db {

// ============================================================================
// PRIVACY & OPT-OUT OPERATIONS
// AES-256-CBC encryption for cached identity data
// ============================================================================

// ── Encryption helpers ──────────────────────────────────────────────────

// 32-byte key derived from a stable secret. in production this should come
// from an environment variable or HSM. for now we use a deterministic key
// seeded from a compile-time secret + the purpose string.
static std::array<unsigned char, 32> get_encryption_key() {
    // In a real deployment, load this from ENCRYPTION_KEY env var or vault.
    // This is a placeholder that should be replaced with proper key management.
    const char* env_key = std::getenv("BRONX_ENCRYPTION_KEY");
    std::array<unsigned char, 32> key{};
    
    if (env_key && strlen(env_key) >= 32) {
        std::memcpy(key.data(), env_key, 32);
    } else {
        // SECURITY: Refuse to start with insecure default key in production.
        // The deterministic fallback is only allowed if BRONX_ALLOW_DEFAULT_KEY=1
        // is explicitly set (for development environments).
        const char* allow_default = std::getenv("BRONX_ALLOW_DEFAULT_KEY");
        if (!allow_default || allow_default[0] != '1') {
            std::cerr << "\033[1;31m✘ FATAL: BRONX_ENCRYPTION_KEY is not set or too short (need >= 32 bytes).\033[0m\n";
            std::cerr << "  Set it via: export BRONX_ENCRYPTION_KEY=<32+ byte secret>\n";
            std::cerr << "  For development only: export BRONX_ALLOW_DEFAULT_KEY=1\n";
            std::abort();
        }
        // Development fallback: deterministic key (NOT for production)
        const std::string seed = "bronx-identity-encryption-2026-default";
        unsigned char hash[32];
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, seed.data(), seed.size());
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx, hash, &len);
        EVP_MD_CTX_free(ctx);
        std::memcpy(key.data(), hash, 32);
        
        static bool warned = false;
        if (!warned) {
            std::cerr << "\033[33m⚠ privacy: using DEFAULT encryption key — set BRONX_ENCRYPTION_KEY for production\033[0m\n";
            warned = true;
        }
    }
    return key;
}

// AES-256-CBC encrypt; returns ciphertext. iv must be 16 bytes (filled by this function).
static std::string aes_encrypt(const std::string& plaintext, unsigned char* iv_out) {
    auto key = get_encryption_key();
    RAND_bytes(iv_out, 16);
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv_out);
    
    // output can be at most plaintext.size() + block_size
    std::string ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int out_len1 = 0, out_len2 = 0;
    
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&ciphertext[0]),
                      &out_len1,
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      static_cast<int>(plaintext.size()));
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&ciphertext[out_len1]),
                        &out_len2);
    
    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(out_len1 + out_len2);
    return ciphertext;
}

// AES-256-CBC decrypt
static std::string aes_decrypt(const std::string& ciphertext, const unsigned char* iv) {
    auto key = get_encryption_key();
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv);
    
    std::string plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    int out_len1 = 0, out_len2 = 0;
    
    EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]),
                      &out_len1,
                      reinterpret_cast<const unsigned char*>(ciphertext.data()),
                      static_cast<int>(ciphertext.size()));
    int rc = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plaintext[out_len1]),
                                  &out_len2);
    
    EVP_CIPHER_CTX_free(ctx);
    
    if (rc != 1) {
        return ""; // decryption failed (corrupted or wrong key)
    }
    plaintext.resize(out_len1 + out_len2);
    return plaintext;
}

// ── Opt-out management ──────────────────────────────────────────────────

bool Database::set_opted_out(uint64_t user_id, bool opted_out) {
    auto conn = pool_->acquire();
    const char* query = opted_out
        ? "INSERT INTO user_privacy (user_id, opted_out, opted_out_at) VALUES (?, TRUE, NOW()) "
          "ON DUPLICATE KEY UPDATE opted_out = TRUE, opted_out_at = NOW()"
        : "INSERT INTO user_privacy (user_id, opted_out, opted_out_at) VALUES (?, FALSE, NULL) "
          "ON DUPLICATE KEY UPDATE opted_out = FALSE, opted_out_at = NULL";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_opted_out prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("set_opted_out execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::is_opted_out(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query = "SELECT opted_out FROM user_privacy WHERE user_id = ? LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_opted_out prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("is_opted_out execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    mysql_stmt_store_result(stmt);
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    my_bool opted_out = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = (char*)&opted_out;
    mysql_stmt_bind_result(stmt, result_bind);
    
    bool is_out = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        is_out = (opted_out != 0);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return is_out;
}

// Delete all user data across every table when they opt out
bool Database::delete_all_user_data(uint64_t user_id) {
    auto conn = pool_->acquire();
    
    // List of all tables that contain user_id data (v2 names)
    const std::vector<std::string> tables = {
        "users",
        "user_loans", "user_inventory",
        "user_wishlists", "user_fish_catches", "user_fishing_gear",
        "user_autofishers", "user_autofish_storage",
        "user_fish_ponds", "user_pond_fish",
        "user_bazaar_stock", "user_bazaar_visits", "user_bazaar_purchases",
        "user_gambling_stats", "user_gambling_history", "lottery_entries",
        "user_xp", "user_stats_ext",
        "daily_challenges", "daily_stats", "daily_streaks",
        "user_pets", "user_mining_claims",
        "user_cooldowns",
        "user_afk", "user_command_history", "command_stats",
        "suggestions", "bug_reports",
        "user_prefixes", "user_reminders",
        "guild_bot_staff",
        "encrypted_identity_cache"
    };
    
    int deleted = 0;
    for (const auto& table : tables) {
        std::string query = "DELETE FROM " + table + " WHERE user_id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
            // table might not exist — that's fine, skip it
            mysql_stmt_close(stmt);
            continue;
        }
        
        MYSQL_BIND bind[1];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        
        if (mysql_stmt_execute(stmt) == 0) {
            deleted += static_cast<int>(mysql_stmt_affected_rows(stmt));
        }
        mysql_stmt_close(stmt);
    }
    
    // Also delete from tables that use initiator_id / recipient_id / created_by
    const std::vector<std::pair<std::string, std::string>> extra_cols = {
        {"guild_trades", "initiator_id"}, {"guild_trades", "recipient_id"},
        {"guild_giveaways", "created_by"},
        {"guild_giveaway_entries", "user_id"},
        {"guild_member_events", "user_id"},
        {"guild_message_events", "user_id"},
        {"guild_voice_events", "user_id"},
        {"guild_boost_events", "user_id"},
        {"command_stats", "user_id"},
    };
    
    for (const auto& [table, col] : extra_cols) {
        std::string query = "DELETE FROM " + table + " WHERE " + col + " = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, query.c_str(), query.size()) != 0) {
            mysql_stmt_close(stmt);
            continue;
        }
        
        MYSQL_BIND bind[1];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        
        if (mysql_stmt_execute(stmt) == 0) {
            deleted += static_cast<int>(mysql_stmt_affected_rows(stmt));
        }
        mysql_stmt_close(stmt);
    }
    
    // Update the privacy record to note deletion time
    {
        const char* q = "UPDATE user_privacy SET data_deleted_at = NOW() WHERE user_id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (mysql_stmt_prepare(stmt, q, strlen(q)) == 0) {
            MYSQL_BIND bind[1];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[0].buffer = (char*)&user_id;
            bind[0].is_unsigned = 1;
            mysql_stmt_bind_param(stmt, bind);
            mysql_stmt_execute(stmt);
        }
        mysql_stmt_close(stmt);
    }
    
    pool_->release(conn);
    std::cerr << "🔒 privacy: deleted " << deleted << " rows for user " << user_id << "\n";
    return deleted >= 0;
}

// ── Encrypted identity cache ────────────────────────────────────────────

bool Database::cache_encrypted_identity(uint64_t user_id,
                                         const std::string& username,
                                         const std::string& nickname,
                                         const std::string& avatar_hash) {
    unsigned char iv[16];
    std::string enc_username = aes_encrypt(username, iv);
    // reuse the same IV for all three fields in this row (they're stored together)
    std::string enc_nickname = aes_encrypt(nickname, iv);
    std::string enc_avatar = aes_encrypt(avatar_hash, iv);
    
    auto conn = pool_->acquire();
    const char* query =
        "INSERT INTO encrypted_identity_cache "
        "(user_id, encrypted_username, encrypted_nickname, encrypted_avatar, encryption_iv, cached_at, expires_at) "
        "VALUES (?, ?, ?, ?, ?, NOW(), DATE_ADD(NOW(), INTERVAL 30 DAY)) "
        "ON DUPLICATE KEY UPDATE "
        "encrypted_username = VALUES(encrypted_username), "
        "encrypted_nickname = VALUES(encrypted_nickname), "
        "encrypted_avatar = VALUES(encrypted_avatar), "
        "encryption_iv = VALUES(encryption_iv), "
        "cached_at = NOW(), "
        "expires_at = DATE_ADD(NOW(), INTERVAL 30 DAY)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("cache_encrypted_identity prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    
    unsigned long enc_username_len = enc_username.size();
    bind[1].buffer_type = MYSQL_TYPE_BLOB;
    bind[1].buffer = (char*)enc_username.data();
    bind[1].buffer_length = enc_username.size();
    bind[1].length = &enc_username_len;
    
    unsigned long enc_nickname_len = enc_nickname.size();
    bind[2].buffer_type = MYSQL_TYPE_BLOB;
    bind[2].buffer = (char*)enc_nickname.data();
    bind[2].buffer_length = enc_nickname.size();
    bind[2].length = &enc_nickname_len;
    
    unsigned long enc_avatar_len = enc_avatar.size();
    bind[3].buffer_type = MYSQL_TYPE_BLOB;
    bind[3].buffer = (char*)enc_avatar.data();
    bind[3].buffer_length = enc_avatar.size();
    bind[3].length = &enc_avatar_len;
    
    unsigned long iv_len = 16;
    bind[4].buffer_type = MYSQL_TYPE_BLOB;
    bind[4].buffer = (char*)iv;
    bind[4].buffer_length = 16;
    bind[4].length = &iv_len;
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) {
        last_error_ = mysql_stmt_error(stmt);
        log_error("cache_encrypted_identity execute");
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<Database::DecryptedIdentityResult> Database::get_cached_identity(uint64_t user_id) {
    auto conn = pool_->acquire();
    const char* query =
        "SELECT encrypted_username, encrypted_nickname, encrypted_avatar, encryption_iv "
        "FROM encrypted_identity_cache WHERE user_id = ? AND expires_at > NOW() LIMIT 1";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    mysql_stmt_store_result(stmt);
    
    char enc_username_buf[512] = {};
    char enc_nickname_buf[512] = {};
    char enc_avatar_buf[1024] = {};
    unsigned char iv_buf[16] = {};
    unsigned long len_u = 0, len_n = 0, len_a = 0, len_iv = 0;
    
    MYSQL_BIND result_bind[4];
    memset(result_bind, 0, sizeof(result_bind));
    
    result_bind[0].buffer_type = MYSQL_TYPE_BLOB;
    result_bind[0].buffer = enc_username_buf;
    result_bind[0].buffer_length = sizeof(enc_username_buf);
    result_bind[0].length = &len_u;
    
    result_bind[1].buffer_type = MYSQL_TYPE_BLOB;
    result_bind[1].buffer = enc_nickname_buf;
    result_bind[1].buffer_length = sizeof(enc_nickname_buf);
    result_bind[1].length = &len_n;
    
    result_bind[2].buffer_type = MYSQL_TYPE_BLOB;
    result_bind[2].buffer = enc_avatar_buf;
    result_bind[2].buffer_length = sizeof(enc_avatar_buf);
    result_bind[2].length = &len_a;
    
    result_bind[3].buffer_type = MYSQL_TYPE_BLOB;
    result_bind[3].buffer = iv_buf;
    result_bind[3].buffer_length = sizeof(iv_buf);
    result_bind[3].length = &len_iv;
    
    mysql_stmt_bind_result(stmt, result_bind);
    
    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    // decrypt
    std::string enc_u(enc_username_buf, len_u);
    std::string enc_n(enc_nickname_buf, len_n);
    std::string enc_a(enc_avatar_buf, len_a);
    
    DecryptedIdentityResult identity;
    identity.username = aes_decrypt(enc_u, iv_buf);
    identity.nickname = aes_decrypt(enc_n, iv_buf);
    identity.avatar_hash = aes_decrypt(enc_a, iv_buf);
    
    return identity;
}

int Database::purge_expired_identities() {
    auto conn = pool_->acquire();
    const char* query = "DELETE FROM encrypted_identity_cache WHERE expires_at <= NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    int deleted = 0;
    if (mysql_stmt_execute(stmt) == 0) {
        deleted = static_cast<int>(mysql_stmt_affected_rows(stmt));
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    
    if (deleted > 0) {
        std::cerr << "🔒 privacy: purged " << deleted << " expired identity cache entries\n";
    }
    return deleted;
}

std::vector<uint64_t> Database::get_all_opted_out_users() {
    std::vector<uint64_t> users;
    auto conn = pool_->acquire();
    const char* query = "SELECT user_id FROM user_privacy WHERE opted_out = TRUE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return users;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return users;
    }
    
    mysql_stmt_store_result(stmt);
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    uint64_t uid = 0;
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = (char*)&uid;
    result_bind[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, result_bind);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        users.push_back(uid);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return users;
}

// ── Namespace wrappers ──────────────────────────────────────────────────

namespace privacy_operations {

bool set_opted_out(Database* db, uint64_t user_id, bool opted_out) {
    return db->set_opted_out(user_id, opted_out);
}

bool is_opted_out(Database* db, uint64_t user_id) {
    return db->is_opted_out(user_id);
}

bool delete_all_user_data(Database* db, uint64_t user_id) {
    return db->delete_all_user_data(user_id);
}

bool cache_encrypted_identity(Database* db, uint64_t user_id,
                               const std::string& username,
                               const std::string& nickname,
                               const std::string& avatar_hash) {
    return db->cache_encrypted_identity(user_id, username, nickname, avatar_hash);
}

std::optional<DecryptedIdentity> get_cached_identity(Database* db, uint64_t user_id) {
    auto result = db->get_cached_identity(user_id);
    if (!result) return std::nullopt;
    DecryptedIdentity identity;
    identity.username = result->username;
    identity.nickname = result->nickname;
    identity.avatar_hash = result->avatar_hash;
    return identity;
}

int purge_expired_identities(Database* db) {
    return db->purge_expired_identities();
}

std::vector<uint64_t> get_all_opted_out_users(Database* db) {
    return db->get_all_opted_out_users();
}

} // namespace privacy_operations

} // namespace db
} // namespace bronx
