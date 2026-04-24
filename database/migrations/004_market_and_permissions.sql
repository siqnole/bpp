-- Migration: Add market_items and server permission tables
-- Run with: mysql -u root -p bronxbot < 004_market_and_permissions.sql

-- ==========================================================================
-- SERVER-SPECIFIC MARKET
-- ==========================================================================

CREATE TABLE IF NOT EXISTS market_items (
    guild_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    name VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    category VARCHAR(50) NOT NULL,
    price BIGINT NOT NULL,
    max_quantity INT NULL,
    metadata JSON NULL,
    expires_at TIMESTAMP NULL,

    PRIMARY KEY (guild_id, item_id),
    INDEX idx_market_guild (guild_id),
    INDEX idx_market_price (price),

    CONSTRAINT chk_market_price CHECK (price >= 0)
) ENGINE=InnoDB;

-- ==========================================================================
-- SERVER-SPECIFIC BOT PERMISSIONS
-- ==========================================================================

-- Server-specific bot administrators (can use admin commands in that server)
CREATE TABLE IF NOT EXISTS server_bot_admins (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    granted_by BIGINT UNSIGNED NOT NULL,
    granted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (guild_id, user_id),
    INDEX idx_sba_user (user_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (granted_by) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Server-specific bot moderators (can use mod commands in that server)
CREATE TABLE IF NOT EXISTS server_bot_mods (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    granted_by BIGINT UNSIGNED NOT NULL,
    granted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (guild_id, user_id),
    INDEX idx_sbm_user (user_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (granted_by) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;
