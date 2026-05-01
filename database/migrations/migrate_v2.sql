-- ============================================================================
-- Bronx Bot — Migration v2: Big-Bang Migration Script
-- ============================================================================
-- INSTRUCTIONS:
--   1. BACK UP the entire `bronxbot` database BEFORE running this script
--   2. Take the bot offline (stop the discord-bot process)
--   3. Run: mysql -u root -p bronxbot < database/migrations/migrate_v2.sql
--   4. Deploy the updated bot code
--   5. Start the bot
--
-- This script:
--   • Creates all new v2 tables
--   • Migrates data from old → new tables
--   • Flattens user_stats into users columns
--   • Deduplicates command_stats/command_usage
--   • Drops old tables
--   • Recreates views and stored procedures
-- ============================================================================

USE bronxbot;

-- Ensure proper charset for emoji support
SET NAMES utf8mb4;
SET CHARACTER SET utf8mb4;

-- Disable FK checks during migration
SET FOREIGN_KEY_CHECKS = 0;
SET @OLD_SQL_MODE = @@SQL_MODE;
SET SQL_MODE = 'NO_AUTO_VALUE_ON_ZERO';

-- ============================================================================
-- PHASE 1: CREATE NEW TABLES (idempotent — IF NOT EXISTS)
-- ============================================================================

-- We source the schema_v2.sql definitions inline here.
-- Only create tables that don't exist yet (new names).

-- 1.2 user_stats_ext (replaces user_stats EAV)
CREATE TABLE IF NOT EXISTS user_stats_ext (
    user_id BIGINT UNSIGNED NOT NULL,
    stat_name VARCHAR(64) NOT NULL,
    stat_value BIGINT NOT NULL DEFAULT 0,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, stat_name),
    INDEX idx_stat_name_value (stat_name, stat_value DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.3 user_inventory (unified)
CREATE TABLE IF NOT EXISTS user_inventory (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    item_id VARCHAR(100) NOT NULL,
    item_type ENUM('potion','upgrade','rod','bait','collectible','other',
                   'automation','boosts','title','tools','pickaxe','minecart',
                   'bag','crafted') NOT NULL,
    quantity INT NOT NULL DEFAULT 1,
    metadata JSON NULL,
    level INT NOT NULL DEFAULT 1,
    acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uq_user_guild_item (user_id, guild_id, item_id),
    INDEX idx_user (user_id),
    INDEX idx_guild (guild_id),
    INDEX idx_item_type (item_type),
    INDEX idx_item_id (item_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT chk_inv_quantity CHECK (quantity >= 0)
) ENGINE=InnoDB;

-- 1.4 Fishing
CREATE TABLE IF NOT EXISTS user_fish_catches (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    rarity ENUM('normal','rare','epic','legendary','event','mutated') NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    weight DECIMAL(10,2) NOT NULL,
    value BIGINT NOT NULL,
    caught_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    sold BOOLEAN NOT NULL DEFAULT FALSE,
    sold_at TIMESTAMP NULL DEFAULT NULL,
    rod_id VARCHAR(100) NULL,
    bait_id VARCHAR(100) NULL,
    INDEX idx_user (user_id),
    INDEX idx_guild_user (guild_id, user_id),
    INDEX idx_rarity (rarity),
    INDEX idx_value (value DESC),
    INDEX idx_user_unsold (user_id, guild_id, sold),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_fishing_gear (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    active_rod_id VARCHAR(100) NULL,
    active_bait_id VARCHAR(100) NULL,
    PRIMARY KEY (user_id, guild_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_autofishers (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    count INT NOT NULL DEFAULT 1,
    efficiency_level INT NOT NULL DEFAULT 1,
    efficiency_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    balance BIGINT NOT NULL DEFAULT 0,
    total_deposited BIGINT NOT NULL DEFAULT 0,
    bag_limit INT NOT NULL DEFAULT 10,
    last_claim TIMESTAMP NULL DEFAULT NULL,
    active BOOLEAN NOT NULL DEFAULT FALSE,
    af_rod_id VARCHAR(100) DEFAULT NULL,
    af_bait_id VARCHAR(100) DEFAULT NULL,
    af_bait_qty INT NOT NULL DEFAULT 0,
    af_bait_level INT NOT NULL DEFAULT 1,
    af_bait_meta TEXT DEFAULT NULL,
    max_bank_draw BIGINT NOT NULL DEFAULT 0,
    auto_sell BOOLEAN NOT NULL DEFAULT FALSE,
    as_trigger VARCHAR(16) NOT NULL DEFAULT 'bag',
    as_threshold BIGINT NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, guild_id),
    INDEX idx_active (active),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT chk_af_count CHECK (count >= 0 AND count <= 30),
    CONSTRAINT chk_af_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_autofish_storage (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    fish_catch_id BIGINT UNSIGNED NOT NULL,
    INDEX idx_user_guild (user_id, guild_id),
    FOREIGN KEY (user_id, guild_id) REFERENCES user_autofishers(user_id, guild_id) ON DELETE CASCADE,
    FOREIGN KEY (fish_catch_id) REFERENCES user_fish_catches(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.5 Cooldowns
CREATE TABLE IF NOT EXISTS user_cooldowns (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    command VARCHAR(50) NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    PRIMARY KEY (user_id, guild_id, command),
    INDEX idx_expires (expires_at),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.6 Gambling
CREATE TABLE IF NOT EXISTS user_gambling_stats (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    game_type VARCHAR(50) NOT NULL,
    games_played INT NOT NULL DEFAULT 0,
    total_bet BIGINT NOT NULL DEFAULT 0,
    total_won BIGINT NOT NULL DEFAULT 0,
    total_lost BIGINT NOT NULL DEFAULT 0,
    biggest_win BIGINT NOT NULL DEFAULT 0,
    biggest_loss BIGINT NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, guild_id, game_type),
    INDEX idx_game_type (game_type),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_gambling_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    game_type ENUM('slots','coinflip','blackjack','poker','roulette','crash','dice') NOT NULL,
    bet_amount BIGINT NOT NULL,
    result_amount BIGINT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user_game (user_id, game_type),
    INDEX idx_guild (guild_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.7 XP (unified — new table, old tables still exist during migration)
-- Note: user_xp already exists, we need to handle carefully
CREATE TABLE IF NOT EXISTS user_xp_v2 (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    total_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    level INT UNSIGNED NOT NULL DEFAULT 1,
    last_message_xp TIMESTAMP NULL DEFAULT NULL,
    PRIMARY KEY (user_id, guild_id),
    INDEX idx_guild_xp (guild_id, total_xp DESC),
    INDEX idx_guild_level (guild_id, level DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.8 Loans
CREATE TABLE IF NOT EXISTS user_loans (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    principal BIGINT NOT NULL DEFAULT 0,
    interest_rate DECIMAL(5,2) NOT NULL DEFAULT 5.00,
    remaining BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_payment_at TIMESTAMP NULL DEFAULT NULL,
    PRIMARY KEY (user_id, guild_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.9 Passive income
CREATE TABLE IF NOT EXISTS user_fish_ponds (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    pond_level INT NOT NULL DEFAULT 1,
    capacity INT NOT NULL DEFAULT 5,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_pond_fish (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    fish_emoji VARCHAR(32) NOT NULL DEFAULT '🐟',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',
    base_value INT NOT NULL DEFAULT 10,
    stocked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    FOREIGN KEY (user_id) REFERENCES user_fish_ponds(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_mining_claims (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    ore_name VARCHAR(100) NOT NULL,
    ore_emoji VARCHAR(32) NOT NULL DEFAULT '⛏️',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',
    yield_min INT NOT NULL DEFAULT 1,
    yield_max INT NOT NULL DEFAULT 3,
    ore_value INT NOT NULL DEFAULT 10,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_user (user_id),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB;

-- 1.10 Bazaar
CREATE TABLE IF NOT EXISTS user_bazaar_stock (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    shares INT NOT NULL DEFAULT 0,
    total_invested BIGINT NOT NULL DEFAULT 0,
    total_dividends BIGINT NOT NULL DEFAULT 0,
    last_purchase TIMESTAMP NULL DEFAULT NULL,
    last_dividend TIMESTAMP NULL DEFAULT NULL,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT chk_shares CHECK (shares >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_bazaar_visits (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    visited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    spent BIGINT NOT NULL DEFAULT 0,
    INDEX idx_user (user_id),
    INDEX idx_guild_recent (guild_id, visited_at),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_bazaar_purchases (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    item_name VARCHAR(255) NOT NULL,
    quantity INT NOT NULL DEFAULT 1,
    price_paid BIGINT NOT NULL,
    discount_percent DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 1.11 Misc user tables
CREATE TABLE IF NOT EXISTS user_afk (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    reason VARCHAR(500) NOT NULL,
    since TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_wishlists (
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, item_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_reminders (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message TEXT NOT NULL,
    remind_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed BOOLEAN NOT NULL DEFAULT FALSE,
    INDEX idx_remind_at (remind_at),
    INDEX idx_pending (completed, remind_at),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_command_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    entry_type ENUM('CMD','BAL','FSH','PAY','GAM','SHP') NOT NULL,
    description VARCHAR(500) NOT NULL,
    amount BIGINT DEFAULT NULL,
    balance_after BIGINT DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    INDEX idx_created (created_at DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 2.3 Guild bot staff (replaces server_bot_admins + server_bot_mods)
CREATE TABLE IF NOT EXISTS guild_bot_staff (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    role ENUM('admin','mod') NOT NULL,
    granted_by BIGINT UNSIGNED NOT NULL,
    granted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_user (user_id),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (granted_by) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- 2.4 Moderation config (NEW)
CREATE TABLE IF NOT EXISTS guild_moderation_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    antispam_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    text_filter_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    url_guard_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    reaction_filter_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    antispam_config JSON NULL,
    text_filter_config JSON NULL,
    url_guard_config JSON NULL,
    reaction_filter_config JSON NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 2.5 Renamed moderation tables
CREATE TABLE IF NOT EXISTS guild_reaction_roles (
    guild_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    emoji_raw VARCHAR(255) NOT NULL,
    emoji_id BIGINT UNSIGNED DEFAULT 0,
    role_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (message_id, emoji_raw),
    INDEX idx_guild (guild_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_autopurges (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    interval_seconds INT NOT NULL,
    message_limit INT NOT NULL,
    target_user_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    target_role_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    INDEX idx_guild (guild_id)
) ENGINE=InnoDB;

-- 2.6 Guild deleted messages (renamed)
CREATE TABLE IF NOT EXISTS guild_deleted_messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    author_id BIGINT UNSIGNED NOT NULL,
    author_tag VARCHAR(256) NOT NULL DEFAULT '',
    author_avatar VARCHAR(1024) NOT NULL DEFAULT '',
    content TEXT NULL,
    attachment_urls TEXT NULL,
    embeds_summary TEXT NULL,
    deleted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_channel (guild_id, channel_id),
    INDEX idx_deleted_at (deleted_at),
    INDEX idx_message_id (message_id)
) ENGINE=InnoDB;

-- 2.6 Analytics — fix column types from VARCHAR(20) to BIGINT UNSIGNED
-- guild_member_events, guild_message_events, guild_daily_stats,
-- guild_command_usage, guild_voice_events, guild_boost_events,
-- guild_user_activity_daily
-- These are created fresh if they don't exist; data migration below handles conversion

CREATE TABLE IF NOT EXISTS guild_user_activity_daily (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    stat_date DATE NOT NULL,
    messages INT NOT NULL DEFAULT 0,
    edits INT NOT NULL DEFAULT 0,
    deletes INT NOT NULL DEFAULT 0,
    voice_minutes INT NOT NULL DEFAULT 0,
    commands_used INT NOT NULL DEFAULT 0,
    PRIMARY KEY (guild_id, user_id, stat_date),
    INDEX idx_guild_date (guild_id, stat_date),
    INDEX idx_guild_user (guild_id, user_id)
) ENGINE=InnoDB;

-- 2.7 Server economy features (renamed)
CREATE TABLE IF NOT EXISTS guild_giveaways (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NULL,
    prize_amount BIGINT NOT NULL,
    max_winners INT NOT NULL DEFAULT 1,
    ends_at TIMESTAMP NOT NULL,
    winner_ids JSON NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_by BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_active_guild (active, guild_id),
    FOREIGN KEY (guild_id) REFERENCES guild_balances(guild_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_giveaway_entries (
    giveaway_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    entered_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (giveaway_id, user_id),
    FOREIGN KEY (giveaway_id) REFERENCES guild_giveaways(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_market_items (
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
    CONSTRAINT chk_market_price CHECK (price >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_trades (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NULL,
    initiator_id BIGINT UNSIGNED NOT NULL,
    recipient_id BIGINT UNSIGNED NOT NULL,
    initiator_cash BIGINT NOT NULL DEFAULT 0,
    initiator_items JSON NULL,
    recipient_cash BIGINT NOT NULL DEFAULT 0,
    recipient_items JSON NULL,
    status ENUM('pending','accepted','rejected','cancelled','completed') NOT NULL DEFAULT 'pending',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    completed_at TIMESTAMP NULL,
    INDEX idx_guild (guild_id),
    INDEX idx_initiator (initiator_id),
    INDEX idx_recipient (recipient_id),
    FOREIGN KEY (initiator_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_heists (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    channel_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    host_id BIGINT UNSIGNED NOT NULL,
    vault_name VARCHAR(100) NOT NULL DEFAULT 'Unknown Vault',
    vault_hp INT NOT NULL DEFAULT 100,
    vault_level INT NOT NULL DEFAULT 1,
    entry_fee BIGINT NOT NULL DEFAULT 5000,
    total_pool BIGINT NOT NULL DEFAULT 0,
    phase ENUM('lobby','active','completed','failed') NOT NULL DEFAULT 'lobby',
    current_round INT NOT NULL DEFAULT 0,
    max_rounds INT NOT NULL DEFAULT 3,
    started_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild (guild_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_heist_participants (
    heist_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    role ENUM('lockpicker','tunneler','hacker','muscle','lookout') NOT NULL DEFAULT 'muscle',
    contribution INT NOT NULL DEFAULT 0,
    alive TINYINT(1) NOT NULL DEFAULT 1,
    PRIMARY KEY (heist_id, user_id),
    FOREIGN KEY (heist_id) REFERENCES guild_heists(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- XP blacklist (consolidated)
CREATE TABLE IF NOT EXISTS guild_xp_blacklist (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    target_type ENUM('channel','role','user') NOT NULL,
    target_id BIGINT UNSIGNED NOT NULL,
    added_by BIGINT UNSIGNED NOT NULL,
    reason VARCHAR(255) NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uq_guild_target (guild_id, target_type, target_id),
    INDEX idx_guild (guild_id)
) ENGINE=InnoDB;

-- Leveling config (renamed from server_leveling_config)
CREATE TABLE IF NOT EXISTS guild_leveling_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    reward_coins BOOLEAN NOT NULL DEFAULT TRUE,
    coins_per_message INT NOT NULL DEFAULT 1,
    min_xp_per_message INT NOT NULL DEFAULT 15,
    max_xp_per_message INT NOT NULL DEFAULT 25,
    min_message_chars INT NOT NULL DEFAULT 5,
    xp_cooldown_seconds INT NOT NULL DEFAULT 60,
    announcement_channel BIGINT UNSIGNED NULL,
    announce_levelup BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Level roles (renamed)
CREATE TABLE IF NOT EXISTS guild_level_roles (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    level INT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    role_name VARCHAR(100) NOT NULL,
    description TEXT NULL,
    remove_previous BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uq_guild_level (guild_id, level)
) ENGINE=InnoDB;


-- ============================================================================
-- PHASE 2: ADD NEW COLUMNS TO USERS TABLE (flattened stats)
-- ============================================================================

-- Add flattened stat columns if they don't exist
ALTER TABLE users ADD COLUMN IF NOT EXISTS fish_caught BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS fish_sold BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS gambling_wins BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS gambling_losses BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS commands_used BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS daily_streak INT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS work_count BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS ores_mined BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS items_crafted BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS trades_completed BIGINT NOT NULL DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS passive BOOLEAN NOT NULL DEFAULT FALSE AFTER vip;

-- Indexes on new stat columns
CREATE INDEX IF NOT EXISTS idx_fish_caught ON users(fish_caught DESC);
CREATE INDEX IF NOT EXISTS idx_commands_used ON users(commands_used DESC);
CREATE INDEX IF NOT EXISTS idx_daily_streak ON users(daily_streak DESC);


-- ============================================================================
-- PHASE 3: MIGRATE DATA FROM OLD → NEW TABLES
-- ============================================================================

-- 3.1 Flatten user_stats into users columns
UPDATE users u
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'fish_caught') s1 ON u.user_id = s1.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'fish_sold') s2 ON u.user_id = s2.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'gambling_wins') s3 ON u.user_id = s3.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'gambling_losses') s4 ON u.user_id = s4.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'commands_used') s5 ON u.user_id = s5.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'daily_streak') s6 ON u.user_id = s6.user_id
LEFT JOIN (SELECT user_id, stat_value FROM user_stats WHERE stat_name = 'work_count') s7 ON u.user_id = s7.user_id
SET
    u.fish_caught = COALESCE(s1.stat_value, 0),
    u.fish_sold = COALESCE(s2.stat_value, 0),
    u.gambling_wins = COALESCE(s3.stat_value, 0),
    u.gambling_losses = COALESCE(s4.stat_value, 0),
    u.commands_used = COALESCE(s5.stat_value, 0),
    u.daily_streak = COALESCE(s6.stat_value, 0),
    u.work_count = COALESCE(s7.stat_value, 0);

-- Move non-standard stats to user_stats_ext
INSERT IGNORE INTO user_stats_ext (user_id, stat_name, stat_value, last_updated)
SELECT user_id, stat_name, stat_value, last_updated
FROM user_stats
WHERE stat_name NOT IN ('fish_caught','fish_sold','gambling_wins','gambling_losses',
                        'commands_used','daily_streak','work_count');

-- 3.2 Migrate inventory → user_inventory (global, guild_id = NULL)
INSERT IGNORE INTO user_inventory (user_id, guild_id, item_id, item_type, quantity, metadata, level, acquired_at)
SELECT user_id, NULL, item_id, item_type, quantity, metadata, level, acquired_at
FROM inventory;

-- 3.2b Migrate server_inventory (guild_id = actual guild_id)
INSERT IGNORE INTO user_inventory (user_id, guild_id, item_id, item_type, quantity, metadata, level, acquired_at)
SELECT user_id, guild_id, item_id, item_type, quantity, metadata, level, acquired_at
FROM server_inventory
WHERE EXISTS (SELECT 1 FROM server_inventory LIMIT 1);

-- 3.3 Migrate fish_catches → user_fish_catches (global)
INSERT INTO user_fish_catches (user_id, guild_id, rarity, fish_name, weight, value, caught_at, sold, sold_at, rod_id, bait_id)
SELECT user_id, NULL, rarity, fish_name, weight, value, caught_at, sold, sold_at, rod_id, bait_id
FROM fish_catches;

-- 3.3b Migrate server_fish_catches
INSERT INTO user_fish_catches (user_id, guild_id, rarity, fish_name, weight, value, caught_at, sold, sold_at, rod_id, bait_id)
SELECT user_id, guild_id, rarity, fish_name, weight, value, caught_at, sold, sold_at, rod_id, bait_id
FROM server_fish_catches
WHERE EXISTS (SELECT 1 FROM server_fish_catches LIMIT 1);

-- 3.4 Migrate active_fishing_gear → user_fishing_gear (guild_id = 0 for global)
INSERT IGNORE INTO user_fishing_gear (user_id, guild_id, active_rod_id, active_bait_id)
SELECT user_id, 0, active_rod_id, active_bait_id
FROM active_fishing_gear;

INSERT IGNORE INTO user_fishing_gear (user_id, guild_id, active_rod_id, active_bait_id)
SELECT user_id, guild_id, active_rod_id, active_bait_id
FROM server_active_fishing_gear
WHERE EXISTS (SELECT 1 FROM server_active_fishing_gear LIMIT 1);

-- 3.5 Migrate autofishers → user_autofishers (guild_id = 0 for global)
INSERT IGNORE INTO user_autofishers (user_id, guild_id, count, efficiency_level, efficiency_multiplier,
    balance, total_deposited, bag_limit, last_claim, active,
    af_rod_id, af_bait_id, af_bait_qty, af_bait_level, af_bait_meta,
    max_bank_draw, auto_sell, as_trigger, as_threshold)
SELECT user_id, 0, count, efficiency_level, efficiency_multiplier,
    balance, total_deposited, bag_limit, last_claim, active,
    af_rod_id, af_bait_id, af_bait_qty, af_bait_level, af_bait_meta,
    max_bank_draw, auto_sell, as_trigger, as_threshold
FROM autofishers;

INSERT IGNORE INTO user_autofishers (user_id, guild_id, count, efficiency_level, efficiency_multiplier,
    balance, total_deposited, bag_limit, last_claim, active,
    af_rod_id, af_bait_id, af_bait_qty, af_bait_level, af_bait_meta,
    max_bank_draw, auto_sell, as_trigger, as_threshold)
SELECT user_id, guild_id, count, efficiency_level, efficiency_multiplier,
    balance, total_deposited, bag_limit, last_claim, active,
    NULL, NULL, 0, 1, NULL,
    0, FALSE, 'bag', 0
FROM server_autofishers
WHERE EXISTS (SELECT 1 FROM server_autofishers LIMIT 1);

-- 3.6 Migrate cooldowns → user_cooldowns
INSERT IGNORE INTO user_cooldowns (user_id, guild_id, command, expires_at)
SELECT user_id, 0, command, expires_at
FROM cooldowns
WHERE expires_at > NOW();

INSERT IGNORE INTO user_cooldowns (user_id, guild_id, command, expires_at)
SELECT user_id, guild_id, command, expires_at
FROM server_cooldowns
WHERE expires_at > NOW()
AND EXISTS (SELECT 1 FROM server_cooldowns LIMIT 1);

-- 3.7 Migrate gambling_stats → user_gambling_stats
INSERT IGNORE INTO user_gambling_stats (user_id, guild_id, game_type, games_played, total_bet, total_won, total_lost, biggest_win, biggest_loss)
SELECT user_id, 0, game_type, games_played, total_bet, total_won, total_lost, biggest_win, biggest_loss
FROM gambling_stats;

INSERT IGNORE INTO user_gambling_stats (user_id, guild_id, game_type, games_played, total_bet, total_won, total_lost, biggest_win, biggest_loss)
SELECT user_id, guild_id, game_type, games_played, total_bet, total_won, total_lost, biggest_win, biggest_loss
FROM server_gambling_stats
WHERE EXISTS (SELECT 1 FROM server_gambling_stats LIMIT 1);

-- 3.8 Migrate gambling_history → user_gambling_history (table may not exist)
SET @_q = IF(
    (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='gambling_history') > 0,
    'INSERT INTO user_gambling_history (user_id, guild_id, game_type, bet_amount, result_amount, created_at) SELECT user_id, 0, game_type, bet_amount, result_amount, created_at FROM gambling_history',
    'DO 0'
);
PREPARE _s FROM @_q;
EXECUTE _s;
DEALLOCATE PREPARE _s;

-- 3.9 Migrate XP
-- Global XP (user_xp → user_xp_v2 with guild_id = 0)
INSERT IGNORE INTO user_xp_v2 (user_id, guild_id, total_xp, level, last_message_xp)
SELECT user_id, 0, total_xp, level, last_xp_gain
FROM user_xp;

-- Server XP (server_xp → user_xp_v2 with actual guild_id)
INSERT IGNORE INTO user_xp_v2 (user_id, guild_id, total_xp, level, last_message_xp)
SELECT user_id, guild_id, server_xp, server_level, last_server_xp_gain
FROM server_xp;

-- 3.10 Migrate loans
INSERT IGNORE INTO user_loans (user_id, guild_id, principal, interest_rate, remaining, created_at, last_payment_at)
SELECT user_id, 0, principal, interest, remaining, created_at, last_payment_at
FROM loans
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='loans');

-- 3.11 Migrate bazaar tables
INSERT IGNORE INTO user_bazaar_stock (user_id, shares, total_invested, total_dividends, last_purchase, last_dividend)
SELECT user_id, shares, total_invested, total_dividends, last_purchase, last_dividend
FROM bazaar_stock;

INSERT INTO user_bazaar_visits (user_id, guild_id, visited_at, spent)
SELECT user_id, guild_id, visited_at, spent
FROM bazaar_visitors;

INSERT INTO user_bazaar_purchases (user_id, item_id, item_name, quantity, price_paid, discount_percent, purchased_at)
SELECT user_id, item_id, item_name, quantity, price_paid, discount_percent, purchased_at
FROM bazaar_purchases;

-- 3.12 Migrate AFK
INSERT IGNORE INTO user_afk (user_id, reason, since)
SELECT user_id, reason, since
FROM afk_status;

-- 3.13 Migrate wishlists
INSERT IGNORE INTO user_wishlists (user_id, item_id, added_at)
SELECT user_id, item_id, added_at
FROM wishlists;

-- 3.14 Migrate reminders
INSERT INTO user_reminders (user_id, guild_id, channel_id, message, remind_at, created_at, completed)
SELECT user_id, guild_id, channel_id, message, remind_at, created_at, completed
FROM reminders;

-- 3.15 Migrate command_history
INSERT INTO user_command_history (user_id, entry_type, description, amount, balance_after, created_at)
SELECT user_id, entry_type, description, amount, balance_after, created_at
FROM command_history;

-- 3.16 Migrate reaction_roles → guild_reaction_roles
INSERT IGNORE INTO guild_reaction_roles (guild_id, message_id, channel_id, emoji_raw, emoji_id, role_id, created_at)
SELECT guild_id, message_id, channel_id, emoji_raw, emoji_id, role_id, created_at
FROM reaction_roles;

-- 3.17 Migrate autopurges → guild_autopurges (from FIRST definition which has target columns)
INSERT INTO guild_autopurges (user_id, guild_id, channel_id, interval_seconds, message_limit, target_user_id, target_role_id, created_at)
SELECT user_id, guild_id, channel_id, interval_seconds, message_limit,
       COALESCE(target_user_id, 0), COALESCE(target_role_id, 0), created_at
FROM autopurges;

-- 3.18 Migrate deleted_messages → guild_deleted_messages
INSERT INTO guild_deleted_messages (guild_id, channel_id, user_id, content, attachments, embeds_summary, deleted_at)
SELECT guild_id, channel_id, author_id, content, attachment_urls, embeds_summary, deleted_at
FROM deleted_messages
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='deleted_messages');

-- 3.19 Migrate bot permissions → guild_bot_staff
INSERT IGNORE INTO guild_bot_staff (guild_id, user_id, role, granted_by, granted_at)
SELECT guild_id, user_id, 'admin', granted_by, granted_at
FROM server_bot_admins;

INSERT IGNORE INTO guild_bot_staff (guild_id, user_id, role, granted_by, granted_at)
SELECT guild_id, user_id, 'mod', granted_by, granted_at
FROM server_bot_mods;

-- 3.20 Migrate guild_economy_settings → guild_settings
-- First ensure guild_settings has the economy columns
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS economy_enabled BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS economy_mode ENUM('global','server') NOT NULL DEFAULT 'global';
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS starting_wallet BIGINT NOT NULL DEFAULT 1000;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS starting_bank_limit BIGINT NOT NULL DEFAULT 10000;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS default_interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS daily_cooldown INT NOT NULL DEFAULT 86400;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS work_cooldown INT NOT NULL DEFAULT 3600;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS beg_cooldown INT NOT NULL DEFAULT 1800;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS rob_cooldown INT NOT NULL DEFAULT 7200;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS fish_cooldown INT NOT NULL DEFAULT 60;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS work_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS gambling_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS fishing_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS allow_gambling BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS allow_fishing BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS allow_trading BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS allow_robbery BOOLEAN NOT NULL DEFAULT TRUE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS max_wallet BIGINT NULL DEFAULT NULL;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS max_bank BIGINT NULL DEFAULT NULL;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS max_networth BIGINT NULL DEFAULT NULL;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS enable_tax BOOLEAN NOT NULL DEFAULT FALSE;
ALTER TABLE guild_settings ADD COLUMN IF NOT EXISTS transaction_tax_percent DECIMAL(5,2) NOT NULL DEFAULT 0.00;

-- Merge guild_economy_settings into guild_settings
-- Note: max_networth, enable_tax, transaction_tax_percent don't exist in old table; use defaults
INSERT INTO guild_settings (guild_id, economy_enabled, economy_mode,
    starting_wallet, starting_bank_limit, default_interest_rate,
    daily_cooldown, work_cooldown, beg_cooldown, rob_cooldown, fish_cooldown,
    work_multiplier, gambling_multiplier, fishing_multiplier,
    allow_gambling, allow_fishing, allow_trading, allow_robbery,
    max_wallet, max_bank, max_networth, enable_tax, transaction_tax_percent)
SELECT guild_id,
    CASE WHEN economy_mode = 'server' THEN TRUE ELSE FALSE END,
    economy_mode,
    starting_wallet, starting_bank_limit, default_interest_rate,
    daily_cooldown, work_cooldown, beg_cooldown, rob_cooldown, fish_cooldown,
    work_multiplier, gambling_multiplier, fishing_multiplier,
    allow_gambling, allow_fishing, allow_trading, allow_robbery,
    max_wallet, max_bank, NULL, FALSE, 0.00
FROM guild_economy_settings
ON DUPLICATE KEY UPDATE
    economy_enabled = VALUES(economy_enabled),
    economy_mode = VALUES(economy_mode),
    starting_wallet = VALUES(starting_wallet),
    starting_bank_limit = VALUES(starting_bank_limit),
    default_interest_rate = VALUES(default_interest_rate),
    daily_cooldown = VALUES(daily_cooldown),
    work_cooldown = VALUES(work_cooldown),
    beg_cooldown = VALUES(beg_cooldown),
    rob_cooldown = VALUES(rob_cooldown),
    fish_cooldown = VALUES(fish_cooldown),
    work_multiplier = VALUES(work_multiplier),
    gambling_multiplier = VALUES(gambling_multiplier),
    fishing_multiplier = VALUES(fishing_multiplier),
    allow_gambling = VALUES(allow_gambling),
    allow_fishing = VALUES(allow_fishing),
    allow_trading = VALUES(allow_trading),
    allow_robbery = VALUES(allow_robbery),
    max_wallet = VALUES(max_wallet),
    max_bank = VALUES(max_bank);

-- 3.21 Migrate XP blacklists → guild_xp_blacklist
INSERT IGNORE INTO guild_xp_blacklist (guild_id, target_type, target_id, added_by, reason, created_at)
SELECT guild_id, 'channel', channel_id, added_by, reason, created_at
FROM xp_blacklist_channels
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='xp_blacklist_channels');

INSERT IGNORE INTO guild_xp_blacklist (guild_id, target_type, target_id, added_by, reason, created_at)
SELECT guild_id, 'role', role_id, added_by, reason, created_at
FROM xp_blacklist_roles
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='xp_blacklist_roles');

INSERT IGNORE INTO guild_xp_blacklist (guild_id, target_type, target_id, added_by, reason, created_at)
SELECT guild_id, 'user', user_id, added_by, reason, created_at
FROM xp_blacklist_users
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='xp_blacklist_users');

-- 3.22 Migrate server_leveling_config → guild_leveling_config
-- Map old column names to new: min_xp→min_xp_per_message, max_xp→max_xp_per_message,
-- min_message_length→min_message_chars, xp_cooldown→xp_cooldown_seconds,
-- coin_rewards→reward_coins, level_up_channel→announcement_channel
INSERT IGNORE INTO guild_leveling_config (guild_id, enabled, reward_coins, coins_per_message,
    min_xp_per_message, max_xp_per_message, min_message_chars, xp_cooldown_seconds,
    announcement_channel, announce_levelup)
SELECT guild_id, enabled, coin_rewards, coins_per_message,
    min_xp, max_xp, min_message_length, xp_cooldown,
    level_up_channel, TRUE
FROM server_leveling_config;

-- 3.23 Migrate level_roles → guild_level_roles
INSERT IGNORE INTO guild_level_roles (guild_id, level, role_id, role_name, description, remove_previous, created_at)
SELECT guild_id, level, role_id, role_name, description, remove_previous, created_at
FROM level_roles;

-- 3.24 Migrate trades → guild_trades
INSERT INTO guild_trades (guild_id, initiator_id, recipient_id,
    initiator_cash, initiator_items, recipient_cash, recipient_items,
    status, created_at, expires_at, completed_at)
SELECT NULL, initiator_id, recipient_id,
    initiator_cash, initiator_items, recipient_cash, recipient_items,
    status, created_at, expires_at, completed_at
FROM trades;

INSERT INTO guild_trades (guild_id, initiator_id, recipient_id,
    initiator_cash, initiator_items, recipient_cash, recipient_items,
    status, created_at, expires_at, completed_at)
SELECT guild_id, initiator_id, recipient_id,
    initiator_cash, initiator_items, recipient_cash, recipient_items,
    status, created_at, expires_at, completed_at
FROM server_trades
WHERE EXISTS (SELECT 1 FROM server_trades LIMIT 1);

-- 3.25 Migrate giveaways → guild_giveaways
INSERT INTO guild_giveaways (guild_id, channel_id, message_id, prize_amount, max_winners,
    ends_at, winner_ids, active, created_by, created_at)
SELECT guild_id, channel_id, message_id, prize_amount, max_winners,
    ends_at, winner_ids, active, created_by, created_at
FROM giveaways;

INSERT IGNORE INTO guild_giveaway_entries (giveaway_id, user_id, entered_at)
SELECT giveaway_id, user_id, entered_at
FROM giveaway_entries;

-- 3.26 Migrate market_items → guild_market_items
INSERT IGNORE INTO guild_market_items (guild_id, item_id, name, description, category, price, max_quantity, metadata, expires_at)
SELECT guild_id, item_id, name, description, category, price, max_quantity, metadata, expires_at
FROM market_items;

-- 3.27 Migrate fish_ponds → user_fish_ponds
INSERT IGNORE INTO user_fish_ponds (user_id, pond_level, capacity, last_collect, created_at)
SELECT user_id, pond_level, capacity, last_collect, created_at
FROM fish_ponds
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='fish_ponds');

INSERT INTO user_pond_fish (user_id, fish_name, fish_emoji, rarity, base_value, stocked_at)
SELECT user_id, fish_name, fish_emoji, rarity, base_value, stocked_at
FROM pond_fish
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='pond_fish');

-- 3.28 Migrate mining_claims → user_mining_claims
INSERT INTO user_mining_claims (user_id, guild_id, ore_name, ore_emoji, rarity, yield_min, yield_max, ore_value, purchased_at, expires_at, last_collect)
SELECT user_id, 0, ore_name, ore_emoji, rarity, yield_min, yield_max, ore_value, purchased_at, expires_at, last_collect
FROM mining_claims
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='mining_claims');

-- 3.29 Migrate heists (skip if columns don't match — table was empty in practice)
SET @_q = IF(
    (SELECT COUNT(*) FROM information_schema.columns
     WHERE table_schema='bronxbot' AND table_name='heists' AND column_name='vault_hp') > 0,
    'INSERT INTO guild_heists (channel_id, guild_id, host_id, vault_name, vault_hp, vault_level, entry_fee, total_pool, phase, current_round, max_rounds, started_at, created_at) SELECT channel_id, guild_id, host_id, vault_name, vault_hp, vault_level, entry_fee, total_pool, phase, current_round, max_rounds, started_at, created_at FROM heists',
    'DO 0'
);
PREPARE _s FROM @_q;
EXECUTE _s;
DEALLOCATE PREPARE _s;

-- 3.30 Migrate guild analytics (fix VARCHAR→BIGINT for guild IDs)
-- guild_member_events, guild_message_events already have BIGINT in newer code
-- We need to handle the case where they have VARCHAR(20) columns from older migrations
-- The async_stat_writer already ensures BIGINT columns, so data should be fine

-- 3.31 Migrate user_activity_daily → guild_user_activity_daily
INSERT IGNORE INTO guild_user_activity_daily (guild_id, user_id, stat_date, messages, edits, deletes, voice_minutes, commands_used)
SELECT
    CAST(guild_id AS UNSIGNED),
    CAST(user_id AS UNSIGNED),
    stat_date, messages, edits, deletes, voice_minutes, commands_used
FROM user_activity_daily
WHERE EXISTS (SELECT 1 FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='user_activity_daily');

-- 3.32 Merge command_usage into command_stats (deduplication) — table may not exist
SET @_q = IF(
    (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='bronxbot' AND table_name='command_usage') > 0,
    'INSERT INTO command_stats (user_id, command_name, guild_id, used_at) SELECT user_id, command_name, guild_id, used_at FROM command_usage WHERE NOT EXISTS (SELECT 1 FROM command_stats cs WHERE cs.user_id = command_usage.user_id AND cs.command_name = command_usage.command_name AND cs.used_at = command_usage.used_at)',
    'DO 0'
);
PREPARE _s FROM @_q;
EXECUTE _s;
DEALLOCATE PREPARE _s;


-- ============================================================================
-- PHASE 4: REPLACE user_xp TABLE
-- ============================================================================

-- Drop the old user_xp and rename user_xp_v2
DROP TABLE IF EXISTS user_xp;
ALTER TABLE user_xp_v2 RENAME TO user_xp;


-- ============================================================================
-- PHASE 5: RECREATE VIEWS
-- ============================================================================

CREATE OR REPLACE VIEW v_user_networth AS
SELECT
    user_id,
    wallet,
    bank,
    (wallet + bank) AS networth,
    bank_limit,
    (bank_limit - bank) AS bank_space
FROM users;

CREATE OR REPLACE VIEW v_active_giveaways AS
SELECT
    g.*,
    gb.balance AS guild_balance,
    (SELECT COUNT(*) FROM guild_giveaway_entries WHERE giveaway_id = g.id) AS entry_count
FROM guild_giveaways g
JOIN guild_balances gb ON g.guild_id = gb.guild_id
WHERE g.active = TRUE AND g.ends_at > NOW();

CREATE OR REPLACE VIEW v_fish_statistics AS
SELECT
    user_id,
    guild_id,
    COUNT(*) AS total_caught,
    SUM(CASE WHEN sold THEN 0 ELSE 1 END) AS unsold_count,
    SUM(value) AS total_value,
    AVG(weight) AS avg_weight,
    MAX(weight) AS heaviest_fish,
    MAX(value) AS most_valuable,
    COUNT(CASE WHEN rarity = 'legendary' THEN 1 END) AS legendary_count,
    COUNT(CASE WHEN rarity = 'mutated' THEN 1 END) AS mutated_count
FROM user_fish_catches
GROUP BY user_id, guild_id;


-- ============================================================================
-- PHASE 6: RECREATE STORED PROCEDURES
-- ============================================================================

DROP PROCEDURE IF EXISTS sp_transfer_money;
DROP PROCEDURE IF EXISTS sp_server_transfer_money;
DROP PROCEDURE IF EXISTS sp_claim_interest;
DROP PROCEDURE IF EXISTS sp_server_claim_interest;
DROP PROCEDURE IF EXISTS sp_update_leaderboard_cache;

DELIMITER //

CREATE PROCEDURE sp_transfer_money(
    IN p_guild_id BIGINT UNSIGNED,
    IN from_user BIGINT UNSIGNED,
    IN to_user BIGINT UNSIGNED,
    IN amount BIGINT
)
BEGIN
    DECLARE from_balance BIGINT;
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    START TRANSACTION;

    -- Global economy transfer (guild_id = 0)
    IF p_guild_id = 0 THEN
        SELECT wallet INTO from_balance FROM users WHERE user_id = from_user FOR UPDATE;
        IF from_balance < amount THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Insufficient funds';
        END IF;
        UPDATE users SET wallet = wallet - amount WHERE user_id = from_user;
        UPDATE users SET wallet = wallet + amount WHERE user_id = to_user;
    END IF;

    COMMIT;
END //

CREATE PROCEDURE sp_claim_interest(
    IN p_user_id BIGINT UNSIGNED,
    OUT p_interest_amount BIGINT
)
BEGIN
    DECLARE v_bank BIGINT;
    DECLARE v_interest_rate DECIMAL(5,2);

    SELECT bank, interest_rate
    INTO v_bank, v_interest_rate
    FROM users
    WHERE user_id = p_user_id;

    SET p_interest_amount = FLOOR(v_bank * (v_interest_rate / 100.0));

    UPDATE users
    SET bank = bank + p_interest_amount,
        last_interest_claim = CURRENT_TIMESTAMP
    WHERE user_id = p_user_id;
END //

CREATE PROCEDURE sp_update_leaderboard_cache(
    IN p_rank_type VARCHAR(20),
    IN p_guild_id BIGINT UNSIGNED
)
BEGIN
    DELETE FROM leaderboard_cache
    WHERE rank_type = p_rank_type AND guild_id = p_guild_id;

    IF p_rank_type = 'networth' THEN
        INSERT INTO leaderboard_cache (rank_type, guild_id, user_id, rank_position, value)
        SELECT 'networth', p_guild_id, user_id,
               ROW_NUMBER() OVER (ORDER BY (wallet + bank) DESC),
               (wallet + bank)
        FROM users
        ORDER BY (wallet + bank) DESC
        LIMIT 1000;
    END IF;
END //

DELIMITER ;

-- Recreate cooldown cleanup event
DROP EVENT IF EXISTS cleanup_cooldowns;
CREATE EVENT cleanup_cooldowns
ON SCHEDULE EVERY 1 HOUR
DO
    DELETE FROM user_cooldowns WHERE expires_at < NOW();

-- Drop the old server cooldown cleanup event
DROP EVENT IF EXISTS cleanup_server_cooldowns;


-- ============================================================================
-- PHASE 7: DROP OLD TABLES
-- (Keep this last, after all data has been migrated)
-- ============================================================================

-- Server economy duplicates (now unified)
DROP TABLE IF EXISTS server_autofish_storage;
DROP TABLE IF EXISTS server_autofishers;
DROP TABLE IF EXISTS server_active_fishing_gear;
DROP TABLE IF EXISTS server_fish_catches;
DROP TABLE IF EXISTS server_inventory;
DROP TABLE IF EXISTS server_gambling_stats;
DROP TABLE IF EXISTS server_cooldowns;
DROP TABLE IF EXISTS server_trades;
DROP TABLE IF EXISTS server_command_stats;
DROP TABLE IF EXISTS server_leaderboard_cache;
DROP TABLE IF EXISTS server_users;
DROP TABLE IF EXISTS guild_economy_settings;

-- Deduplicated tables
DROP TABLE IF EXISTS command_usage;

-- Renamed tables (data migrated to new names)
DROP TABLE IF EXISTS autofish_storage;
DROP TABLE IF EXISTS autofishers;
DROP TABLE IF EXISTS active_fishing_gear;
DROP TABLE IF EXISTS fish_catches;
DROP TABLE IF EXISTS inventory;
DROP TABLE IF EXISTS cooldowns;
DROP TABLE IF EXISTS gambling_stats;
DROP TABLE IF EXISTS gambling_history;
DROP TABLE IF EXISTS user_stats;
DROP TABLE IF EXISTS server_xp;
DROP TABLE IF EXISTS afk_status;
DROP TABLE IF EXISTS wishlists;
DROP TABLE IF EXISTS reminders;
DROP TABLE IF EXISTS command_history;
DROP TABLE IF EXISTS reaction_roles;
DROP TABLE IF EXISTS autopurges;
DROP TABLE IF EXISTS bazaar_stock;
DROP TABLE IF EXISTS bazaar_visitors;
DROP TABLE IF EXISTS bazaar_purchases;
DROP TABLE IF EXISTS trades;
DROP TABLE IF EXISTS giveaways;
DROP TABLE IF EXISTS giveaway_entries;
DROP TABLE IF EXISTS market_items;
DROP TABLE IF EXISTS server_bot_admins;
DROP TABLE IF EXISTS server_bot_mods;
DROP TABLE IF EXISTS server_leveling_config;
DROP TABLE IF EXISTS level_roles;
DROP TABLE IF EXISTS xp_blacklist_channels;
DROP TABLE IF EXISTS xp_blacklist_roles;
DROP TABLE IF EXISTS xp_blacklist_users;
DROP TABLE IF EXISTS loans;
DROP TABLE IF EXISTS fish_ponds;
DROP TABLE IF EXISTS pond_fish;
DROP TABLE IF EXISTS mining_claims;
DROP TABLE IF EXISTS heists;
DROP TABLE IF EXISTS heist_participants;
DROP TABLE IF EXISTS deleted_messages;
DROP TABLE IF EXISTS user_activity_daily;

-- Old views (recreated above)
DROP VIEW IF EXISTS v_server_user_networth;
DROP VIEW IF EXISTS v_server_fish_statistics;


-- ============================================================================
-- PHASE 8: RE-ENABLE FK CHECKS & CLEANUP
-- ============================================================================

SET FOREIGN_KEY_CHECKS = 1;
SET SQL_MODE = @OLD_SQL_MODE;

-- Analyze new tables for query optimization
ANALYZE TABLE users, user_inventory, user_fish_catches, user_autofishers,
             user_cooldowns, user_gambling_stats, user_xp, user_loans,
             guild_settings, guild_bot_staff, guild_moderation_config,
             guild_trades, guild_giveaways, command_stats, leaderboard_cache;

-- ============================================================================
-- MIGRATION COMPLETE
-- ============================================================================
-- Verify row counts:
--   SELECT 'user_inventory' AS tbl, COUNT(*) AS cnt FROM user_inventory
--   UNION ALL SELECT 'user_fish_catches', COUNT(*) FROM user_fish_catches
--   UNION ALL SELECT 'user_gambling_stats', COUNT(*) FROM user_gambling_stats
--   UNION ALL SELECT 'user_xp', COUNT(*) FROM user_xp
--   UNION ALL SELECT 'guild_bot_staff', COUNT(*) FROM guild_bot_staff
--   UNION ALL SELECT 'guild_trades', COUNT(*) FROM guild_trades;
-- ============================================================================
