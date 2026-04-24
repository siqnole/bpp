-- Migration 016: Guild Activity Log
-- Tracks all setting changes from both Dashboard (DB) and Discord (DC)
-- for the "Recent Activity" feature on the dashboard overview

CREATE TABLE IF NOT EXISTS guild_activity_log (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NULL,
    user_name VARCHAR(100) NULL,
    user_avatar VARCHAR(255) NULL,
    source ENUM('DB', 'DC') NOT NULL DEFAULT 'DB' COMMENT 'DB=Dashboard, DC=Discord',
    action VARCHAR(500) NOT NULL,
    old_value VARCHAR(255) NULL,
    new_value VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_guild_time (guild_id, created_at DESC),
    INDEX idx_source (source),
    INDEX idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Add comment for documentation
ALTER TABLE guild_activity_log COMMENT = 'Audit log for guild settings changes - Dashboard and Discord bot';
