-- ============================================================================
-- Feature Flag System Migration
-- Run this against your production database to create the feature flag tables.
-- ============================================================================

CREATE TABLE IF NOT EXISTS feature_flags (
    feature_name VARCHAR(100) NOT NULL PRIMARY KEY,
    mode VARCHAR(20) NOT NULL DEFAULT 'enabled',  -- 'enabled', 'disabled', 'whitelist'
    reason VARCHAR(512) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS feature_flag_whitelist (
    feature_name VARCHAR(100) NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (feature_name, guild_id),
    INDEX idx_feature (feature_name),
    INDEX idx_guild (guild_id),

    FOREIGN KEY (feature_name) REFERENCES feature_flags(feature_name) ON DELETE CASCADE
) ENGINE=InnoDB;
