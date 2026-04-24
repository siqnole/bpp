-- ============================================================================
-- Migration 017: Infraction System
-- Adds guild infractions, infraction config, automod config, role classes.
-- ============================================================================

-- 1. Core infraction ledger
CREATE TABLE IF NOT EXISTS guild_infractions (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    case_number INT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    moderator_id BIGINT UNSIGNED NOT NULL,
    type ENUM(
        'warn','timeout','mute','jail','kick','ban',
        'auto_spam','auto_filter','auto_url','auto_reaction',
        'auto_account_age','auto_avatar','auto_mutual','auto_nickname'
    ) NOT NULL,
    reason TEXT NULL,
    points DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    duration_seconds INT UNSIGNED NULL,
    expires_at TIMESTAMP NULL DEFAULT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    pardoned BOOLEAN NOT NULL DEFAULT FALSE,
    pardoned_by BIGINT UNSIGNED NULL DEFAULT NULL,
    pardoned_at TIMESTAMP NULL DEFAULT NULL,
    pardoned_reason TEXT NULL,
    metadata JSON NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uq_guild_case (guild_id, case_number),
    INDEX idx_guild_user_active (guild_id, user_id, active),
    INDEX idx_guild_created (guild_id, created_at),
    INDEX idx_expires_active (expires_at, active),
    INDEX idx_guild_moderator (guild_id, moderator_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- 2. Per-guild infraction configuration
CREATE TABLE IF NOT EXISTS guild_infraction_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,

    -- Point weights per action type
    point_timeout DECIMAL(5,2) NOT NULL DEFAULT 0.25,
    point_mute DECIMAL(5,2) NOT NULL DEFAULT 0.50,
    point_kick DECIMAL(5,2) NOT NULL DEFAULT 2.00,
    point_ban DECIMAL(5,2) NOT NULL DEFAULT 5.00,
    point_warn DECIMAL(5,2) NOT NULL DEFAULT 0.10,

    -- Default infraction durations (how long the record stays active)
    default_duration_timeout INT UNSIGNED NOT NULL DEFAULT 259200,      -- 3 days
    default_duration_mute INT UNSIGNED NOT NULL DEFAULT 604800,         -- 7 days
    default_duration_kick INT UNSIGNED NOT NULL DEFAULT 1209600,        -- 14 days
    default_duration_ban INT UNSIGNED NOT NULL DEFAULT 15552000,        -- 180 days
    default_duration_warn INT UNSIGNED NOT NULL DEFAULT 604800,         -- 7 days

    -- Escalation rules: JSON array of {threshold_points, within_days, action, action_duration_seconds, reason_template}
    escalation_rules JSON NOT NULL DEFAULT ('[]'),

    -- Role IDs for mute/jail
    mute_role_id BIGINT UNSIGNED NULL DEFAULT NULL,
    jail_role_id BIGINT UNSIGNED NULL DEFAULT NULL,
    jail_channel_id BIGINT UNSIGNED NULL DEFAULT NULL,

    -- Logging
    log_channel_id BIGINT UNSIGNED NULL DEFAULT NULL,
    dm_on_action BOOLEAN NOT NULL DEFAULT TRUE,

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- 3. Extended auto-moderation config (new guards beyond existing 4 modules)
CREATE TABLE IF NOT EXISTS guild_automod_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,

    -- Account age guard
    account_age_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    account_age_days INT UNSIGNED NOT NULL DEFAULT 7,
    account_age_action VARCHAR(20) NOT NULL DEFAULT 'kick',

    -- Default avatar guard
    default_avatar_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    default_avatar_action VARCHAR(20) NOT NULL DEFAULT 'kick',

    -- Mutual servers guard
    mutual_servers_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    mutual_servers_min INT UNSIGNED NOT NULL DEFAULT 1,
    mutual_servers_action VARCHAR(20) NOT NULL DEFAULT 'kick',

    -- Nickname sanitizer
    nickname_sanitize_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    nickname_sanitize_format VARCHAR(100) NOT NULL DEFAULT 'Moderated Nickname {n}',
    nickname_bad_patterns JSON NOT NULL DEFAULT ('[]'),

    -- Infraction-based escalation toggle
    infraction_escalation_enabled BOOLEAN NOT NULL DEFAULT TRUE,

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- 4. Role-based permission classes
CREATE TABLE IF NOT EXISTS guild_role_classes (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    name VARCHAR(50) NOT NULL,
    priority INT NOT NULL DEFAULT 0,
    inherit_lower BOOLEAN NOT NULL DEFAULT FALSE,
    restrictions JSON NOT NULL DEFAULT ('{}'),
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uq_guild_class_name (guild_id, name),
    INDEX idx_guild_priority (guild_id, priority)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- 5. Maps Discord roles to permission classes
CREATE TABLE IF NOT EXISTS guild_role_class_members (
    guild_id BIGINT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    class_id INT UNSIGNED NOT NULL,

    PRIMARY KEY (guild_id, role_id),
    INDEX idx_class (class_id),
    CONSTRAINT fk_class_id FOREIGN KEY (class_id)
        REFERENCES guild_role_classes(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
