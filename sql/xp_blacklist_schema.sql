-- XP Blacklist System
-- Allows admins to prevent XP gain in specific channels, for specific roles, or for specific users

USE bronxbot;

-- Blacklisted channels (no XP gained in these channels)
CREATE TABLE IF NOT EXISTS xp_blacklist_channels (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    added_by BIGINT UNSIGNED NOT NULL,
    reason VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_channel (guild_id, channel_id),
    INDEX idx_guild (guild_id),
    INDEX idx_channel (channel_id)
) ENGINE=InnoDB;

-- Blacklisted roles (users with these roles don't gain XP)
CREATE TABLE IF NOT EXISTS xp_blacklist_roles (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    added_by BIGINT UNSIGNED NOT NULL,
    reason VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_role (guild_id, role_id),
    INDEX idx_guild (guild_id),
    INDEX idx_role (role_id)
) ENGINE=InnoDB;

-- Blacklisted users (specific users don't gain XP in this server)
CREATE TABLE IF NOT EXISTS xp_blacklist_users (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    added_by BIGINT UNSIGNED NOT NULL,
    reason VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_user (guild_id, user_id),
    INDEX idx_guild (guild_id),
    INDEX idx_user (user_id)
) ENGINE=InnoDB;
