-- Migration: Bronx Bot v2 Moderation Expansion
-- Description: Cases, Quiet Mode, and Config

CREATE TABLE IF NOT EXISTS guild_mod_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    log_channel BIGINT UNSIGNED DEFAULT NULL,
    quiet_global BOOLEAN NOT NULL DEFAULT TRUE,
    quiet_overrides JSON DEFAULT NULL, -- {action: bool}
    dm_on_action JSON DEFAULT NULL,    -- {action: bool}
    require_reason BOOLEAN NOT NULL DEFAULT FALSE,
    case_counter INT NOT NULL DEFAULT 1
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS mod_cases (
    case_id INT UNSIGNED NOT NULL, -- Per-guild ID
    guild_id BIGINT UNSIGNED NOT NULL,
    action ENUM('warn','mute','unmute','kick','ban','unban','timeout','untimeout','slowmode','lock','purge') NOT NULL,
    target_id BIGINT UNSIGNED NOT NULL,
    moderator_id BIGINT UNSIGNED NOT NULL,
    reason TEXT DEFAULT NULL,
    channel_id BIGINT UNSIGNED DEFAULT NULL,
    duration_s INT DEFAULT NULL,
    expires_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (guild_id, case_id),
    INDEX idx_target (target_id),
    INDEX idx_moderator (moderator_id)
) ENGINE=InnoDB;
