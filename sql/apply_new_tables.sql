-- Migration script to add leveling system and patch notes tables
-- Run with: mysql -u your_user -p bronxbot < apply_new_tables.sql

USE bronxbot;

-- ============================================================================
-- LEVELING SYSTEM TABLES
-- ============================================================================

-- User XP Table (Global XP across all servers)
CREATE TABLE IF NOT EXISTS user_xp (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    total_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    level INT UNSIGNED NOT NULL DEFAULT 0,
    last_xp_gain TIMESTAMP NULL DEFAULT NULL,
    
    INDEX idx_level (level DESC),
    INDEX idx_xp (total_xp DESC),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- Server XP Table (Per-server XP tracking)
CREATE TABLE IF NOT EXISTS server_xp (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    server_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    server_level INT UNSIGNED NOT NULL DEFAULT 0,
    last_server_xp_gain TIMESTAMP NULL DEFAULT NULL,
    
    PRIMARY KEY (user_id, guild_id),
    INDEX idx_guild_level (guild_id, server_level DESC),
    INDEX idx_guild_xp (guild_id, server_xp DESC),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- Server Leveling Configuration
CREATE TABLE IF NOT EXISTS server_leveling_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    min_xp INT UNSIGNED NOT NULL DEFAULT 15,
    max_xp INT UNSIGNED NOT NULL DEFAULT 25,
    xp_cooldown INT UNSIGNED NOT NULL DEFAULT 60,
    min_message_length INT UNSIGNED NOT NULL DEFAULT 5,
    coin_rewards BOOLEAN NOT NULL DEFAULT FALSE,
    coins_per_message INT NOT NULL DEFAULT 5,
    level_up_channel BIGINT UNSIGNED NULL DEFAULT NULL,
    level_up_message VARCHAR(500) DEFAULT 'Congratulations {user}! You reached level {level}!',
    
    INDEX idx_enabled (enabled),
    
    CONSTRAINT chk_xp_range CHECK (min_xp <= max_xp),
    CONSTRAINT chk_cooldown CHECK (xp_cooldown >= 0)
) ENGINE=InnoDB;

-- Level Role Rewards
CREATE TABLE IF NOT EXISTS level_roles (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    level INT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    role_name VARCHAR(100) NOT NULL,
    description VARCHAR(255) DEFAULT '',
    remove_previous BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_level (guild_id, level),
    INDEX idx_guild (guild_id),
    INDEX idx_level (level),
    
    CONSTRAINT chk_level_positive CHECK (level > 0)
) ENGINE=InnoDB;

-- ============================================================================
-- PATCH NOTES TABLE
-- ============================================================================

-- Patch Notes Table
CREATE TABLE IF NOT EXISTS patch_notes (
    id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    version VARCHAR(20) NOT NULL,
    notes TEXT NOT NULL,
    author_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_version (version),
    INDEX idx_created (created_at DESC)
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- ============================================================================
-- INITIAL DATA - First patch note for leveling system
-- ============================================================================

INSERT IGNORE INTO patch_notes (version, notes, author_id)
VALUES (
    '1.0.0',
    '**🎉 New Leveling System**

• **XP Tracking**: Earn XP from chatting in servers
• **Dual Leaderboards**: Global XP and per-server rankings
• **Level Roles**: Unlock roles as you level up
• **Coin Rewards**: Optionally earn coins with each message (configurable)
• **Admin Controls**: Fully customizable with `levelconfig` command
  - Set XP range per message
  - Configure cooldown between rewards
  - Set minimum message length
  - Toggle coin rewards on/off
  - Reset server XP (preserves global XP)
• **Role Rewards**: Admins can set up role rewards at specific levels with `levelroles`
• **Progress Tracking**: Use `/rank` to view your XP, level, and progress

**Commands:**
• `/rank` or `rank` - View your XP and level progress
• `levelconfig` - Admin command to configure leveling settings
• `levelroles` - Admin command to manage level-based role rewards
• `/leaderboard global-xp` - View global XP leaderboard
• `/leaderboard server-xp` - View server XP leaderboard

**📝 Patch Notes System**
• `/patch` or `patch` - View the latest bot updates
• `/patch history` - Browse all previous updates with pagination
• `.patchadd <notes>` - Owner command to add new patch notes',
    814226043924643880
);

SELECT 'Migration completed successfully!' AS status;
