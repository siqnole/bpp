-- ============================================================================
-- Migration 013: Privacy, Opt-Out, and Identity Encryption
-- Adds user_privacy table for opt-out tracking and encrypted identity cache
-- ============================================================================

-- User privacy preferences and opt-out status
CREATE TABLE IF NOT EXISTS user_privacy (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    opted_out BOOLEAN NOT NULL DEFAULT FALSE,
    opted_out_at TIMESTAMP NULL DEFAULT NULL,
    data_deleted_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Encrypted identity cache (username, nickname, avatar)
-- Data is AES-256 encrypted at rest and expires after 30 days
CREATE TABLE IF NOT EXISTS encrypted_identity_cache (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    encrypted_username VARBINARY(512) DEFAULT NULL,
    encrypted_nickname VARBINARY(512) DEFAULT NULL,
    encrypted_avatar VARBINARY(1024) DEFAULT NULL,
    encryption_iv VARBINARY(16) NOT NULL,
    cached_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Encryption key store (rotatable)
CREATE TABLE IF NOT EXISTS encryption_keys (
    key_id INT AUTO_INCREMENT PRIMARY KEY,
    key_purpose VARCHAR(64) NOT NULL,
    encrypted_key VARBINARY(512) NOT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    rotated_at TIMESTAMP NULL DEFAULT NULL,
    UNIQUE KEY uq_purpose_active (key_purpose, active)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Index for fast opt-out lookups in command handler hot path
CREATE INDEX IF NOT EXISTS idx_privacy_opted_out ON user_privacy (opted_out) WHERE opted_out = TRUE;
