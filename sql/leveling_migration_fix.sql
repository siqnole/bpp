-- ============================================================================
-- Leveling system schema migration (fixed)
-- Fixes mismatch between old SQL schema names and current C++ code.
--
-- Run with:  mysql -u <user> -p bronxbot < sql/leveling_migration_fix.sql
-- ============================================================================

USE bronxbot;

-- ---------------------------------------------------------------------------
-- 1. user_xp — recreate with (user_id, guild_id) composite PK
-- ---------------------------------------------------------------------------
DROP TABLE IF EXISTS user_xp;

CREATE TABLE user_xp (
    user_id      BIGINT UNSIGNED NOT NULL,
    guild_id     BIGINT UNSIGNED NOT NULL DEFAULT 0,
    total_xp     BIGINT UNSIGNED NOT NULL DEFAULT 0,
    level        INT UNSIGNED    NOT NULL DEFAULT 1,
    last_xp_gain TIMESTAMP       NULL     DEFAULT NULL,

    PRIMARY KEY (user_id, guild_id),
    INDEX idx_guild_xp    (guild_id, total_xp DESC),
    INDEX idx_guild_level (guild_id, level DESC)
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- ---------------------------------------------------------------------------
-- 2. guild_leveling_config — what the C++ queries
-- ---------------------------------------------------------------------------
DROP TABLE IF EXISTS guild_leveling_config;

CREATE TABLE guild_leveling_config (
    guild_id           BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    enabled            BOOLEAN         NOT NULL DEFAULT FALSE,
    coin_rewards       BOOLEAN         NOT NULL DEFAULT FALSE,
    coins_per_message  INT             NOT NULL DEFAULT 5,
    min_xp             INT UNSIGNED    NOT NULL DEFAULT 15,
    max_xp             INT UNSIGNED    NOT NULL DEFAULT 25,
    min_message_length INT UNSIGNED    NOT NULL DEFAULT 5,
    xp_cooldown        INT UNSIGNED    NOT NULL DEFAULT 60,
    level_up_channel   BIGINT UNSIGNED NULL     DEFAULT NULL,
    level_up_message   VARCHAR(500)    NOT NULL DEFAULT 'Congratulations {name}! You reached level {level}!',

    INDEX idx_enabled (enabled)
) ENGINE=InnoDB;

-- ---------------------------------------------------------------------------
-- 3. guild_level_roles — what the C++ queries
-- ---------------------------------------------------------------------------
DROP TABLE IF EXISTS guild_level_roles;

CREATE TABLE guild_level_roles (
    id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    guild_id        BIGINT UNSIGNED NOT NULL,
    level           INT UNSIGNED    NOT NULL,
    role_id         BIGINT UNSIGNED NOT NULL,
    role_name       VARCHAR(100)    NOT NULL,
    description     VARCHAR(255)             DEFAULT '',
    remove_previous BOOLEAN         NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY unique_guild_level (guild_id, level),
    INDEX idx_guild (guild_id),
    INDEX idx_level (level)
) ENGINE=InnoDB;

SELECT 'Leveling migration complete.' AS status;
