-- Migration Script: Advanced Webhook Logging & Beta Gate

-- 1. Add beta_tester flag to guild_settings
ALTER TABLE guild_settings 
ADD COLUMN IF NOT EXISTS beta_tester BOOLEAN NOT NULL DEFAULT FALSE;

-- 2. Create guild_log_configs table for webhook logging
CREATE TABLE IF NOT EXISTS guild_log_configs (
    guild_id BIGINT UNSIGNED NOT NULL,
    log_type VARCHAR(50) NOT NULL, -- e.g., 'moderation', 'messages', 'members', 'economy', 'server'
    channel_id BIGINT UNSIGNED NOT NULL,
    webhook_url VARCHAR(512) NOT NULL,
    webhook_id BIGINT UNSIGNED NOT NULL,
    webhook_token VARCHAR(255) NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (guild_id, log_type),
    INDEX idx_guild (guild_id),
    INDEX idx_enabled (enabled)
) ENGINE=InnoDB;
