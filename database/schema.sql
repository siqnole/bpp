-- ============================================================================
-- South Bronx Bot - MariaDB Database Schema
-- High-performance economy system with proper indexing and constraints
-- ============================================================================

-- Drop existing database if exists (development only)
-- DROP DATABASE IF EXISTS bronxbot;

CREATE DATABASE bronxbot;
GO

USE bronxbot;
GO

-- ============================================================================
-- CORE ECONOMY TABLES
-- ============================================================================

-- Users table - Primary economy data
-- Optimized for fast balance lookups and leaderboard queries
CREATE TABLE users (
    user_id BIGINT NOT NULL PRIMARY KEY,
    wallet BIGINT NOT NULL DEFAULT 0,
    bank BIGINT NOT NULL DEFAULT 0,
    bank_limit BIGINT NOT NULL DEFAULT 10000,
    
    -- Interest system
    interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    interest_level INT NOT NULL DEFAULT 0,
    last_interest_claim TIMESTAMP NULL DEFAULT NULL,
    
    -- Cooldowns
    last_daily TIMESTAMP NULL DEFAULT NULL,
    last_work TIMESTAMP NULL DEFAULT NULL,
    last_beg TIMESTAMP NULL DEFAULT NULL,
    last_rob TIMESTAMP NULL DEFAULT NULL,
    
    -- Statistics
    total_gambled BIGINT NOT NULL DEFAULT 0,
    total_won BIGINT NOT NULL DEFAULT 0,
    total_lost BIGINT NOT NULL DEFAULT 0,
    commands_used INT NOT NULL DEFAULT 0,
    
    -- Badges
    dev BOOLEAN NOT NULL DEFAULT FALSE,
    admin BOOLEAN NOT NULL DEFAULT FALSE,
    is_mod BOOLEAN NOT NULL DEFAULT FALSE,
    maintainer BOOLEAN NOT NULL DEFAULT FALSE,
    contributor BOOLEAN NOT NULL DEFAULT FALSE,
    vip BOOLEAN NOT NULL DEFAULT FALSE,
    
    -- Prestige system
    prestige INT NOT NULL DEFAULT 0,
    
    -- Metadata
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_active TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    -- Indexes for fast queries
    INDEX idx_wallet (wallet DESC),
    INDEX idx_bank (bank DESC),
    INDEX idx_last_active (last_active),
    INDEX idx_badges (dev, admin, is_mod, vip),
    INDEX idx_prestige (prestige DESC)
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- Add computed column for networth (MariaDB 10.2+)
ALTER TABLE users ADD COLUMN networth BIGINT AS (wallet + bank) STORED;
CREATE INDEX idx_networth ON users(networth DESC);

-- AFK status table - Separated for better performance
CREATE TABLE IF NOT EXISTS afk_status (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    reason VARCHAR(500) NOT NULL,
    since TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_since (since),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- INVENTORY SYSTEM
-- ============================================================================

-- Unified inventory table for all item types
-- Uses JSON for flexible item-specific metadata
CREATE TABLE IF NOT EXISTS inventory (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    item_type ENUM('potion', 'upgrade', 'rod', 'bait', 'collectible', 'other', 'automation', 'boosts', 'title', 'tools', 'pickaxe', 'minecart', 'bag', 'crafted') NOT NULL,
    quantity INT NOT NULL DEFAULT 1,
    metadata JSON NULL, -- Custom data per item (durability, enchantments, etc)
    level INT NOT NULL DEFAULT 1, -- item level for rods/bait/upgrades
    acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_user_item (user_id, item_id),
    INDEX idx_user (user_id),
    INDEX idx_item_type (item_type),
    INDEX idx_item_id (item_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    
    -- Ensure quantities are positive
    CONSTRAINT chk_quantity CHECK (quantity >= 0)
) ENGINE=InnoDB;

-- ============================================================================
-- FISHING SYSTEM
-- ============================================================================

-- Fish catches table - Detailed tracking of all catches
CREATE TABLE IF NOT EXISTS fish_catches (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    rarity ENUM('normal', 'rare', 'epic', 'legendary', 'event', 'mutated') NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    weight DECIMAL(10,2) NOT NULL, -- In pounds/kg
    value BIGINT NOT NULL, -- Sell value
    caught_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    sold BOOLEAN NOT NULL DEFAULT FALSE,
    sold_at TIMESTAMP NULL DEFAULT NULL,
    
    -- Fishing gear used
    rod_id VARCHAR(100) NULL,
    bait_id VARCHAR(100) NULL,
    
    INDEX idx_user (user_id),
    INDEX idx_rarity (rarity),
    INDEX idx_weight (weight DESC),
    INDEX idx_value (value DESC),
    INDEX idx_sold (sold),
    INDEX idx_caught_at (caught_at),
    INDEX idx_user_unsold (user_id, sold),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Machine learning logging for fishing results. Stores anonymous net-profit samples along with gear levels.
CREATE TABLE IF NOT EXISTS fishing_logs (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    rod_level INT NOT NULL,
    bait_level INT NOT NULL,
    net_profit BIGINT NOT NULL,
    logged_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_rod (rod_level),
    INDEX idx_bait (bait_level),
    INDEX idx_profit (net_profit)
) ENGINE=InnoDB;

-- Active fishing gear - What the user currently has equipped
CREATE TABLE IF NOT EXISTS active_fishing_gear (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    active_rod_id VARCHAR(100) NULL,
    active_bait_id VARCHAR(100) NULL,
    
    INDEX idx_rod (active_rod_id),
    INDEX idx_bait (active_bait_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Autofishing system
CREATE TABLE IF NOT EXISTS autofishers (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    count INT NOT NULL DEFAULT 1, -- Number of autofishers owned
    efficiency_level INT NOT NULL DEFAULT 1,
    efficiency_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    balance BIGINT NOT NULL DEFAULT 0, -- Dedicated autofishing balance
    total_deposited BIGINT NOT NULL DEFAULT 0, -- Total ever deposited (for bag limits)
    bag_limit INT NOT NULL DEFAULT 10, -- Max fish that can be stored
    last_claim TIMESTAMP NULL DEFAULT NULL,
    active BOOLEAN NOT NULL DEFAULT FALSE,
    
    -- Autofisher v2: dedicated gear and bait hopper
    af_rod_id VARCHAR(100) DEFAULT NULL,        -- Rod equipped to the autofisher
    af_bait_id VARCHAR(100) DEFAULT NULL,       -- Bait type the autofisher uses
    af_bait_qty INT NOT NULL DEFAULT 0,         -- Bait quantity in hopper
    af_bait_level INT NOT NULL DEFAULT 1,       -- Bait level
    af_bait_meta TEXT DEFAULT NULL,             -- Bait metadata (JSON)
    
    -- Autofisher v2: economy settings
    max_bank_draw BIGINT NOT NULL DEFAULT 0,    -- 0 = disabled; max to spend from bank per bait buy
    auto_sell BOOLEAN NOT NULL DEFAULT FALSE,   -- Whether to auto-sell fish
    as_trigger VARCHAR(16) NOT NULL DEFAULT 'bag', -- "bag" | "count" | "balance"
    as_threshold BIGINT NOT NULL DEFAULT 0,     -- Threshold for count/balance triggers
    
    INDEX idx_active (active),
    INDEX idx_balance (balance DESC),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    
    CONSTRAINT chk_autofish_count CHECK (count >= 0 AND count <= 30),
    CONSTRAINT chk_autofish_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

-- Stored fish from autofisher
CREATE TABLE IF NOT EXISTS autofish_storage (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    fish_catch_id BIGINT UNSIGNED NOT NULL, -- References fish_catches
    
    INDEX idx_user (user_id),
    
    FOREIGN KEY (user_id) REFERENCES autofishers(user_id) ON DELETE CASCADE,
    FOREIGN KEY (fish_catch_id) REFERENCES fish_catches(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- BAZAAR / MARKETPLACE SYSTEM
-- ============================================================================

-- Bazaar stock ownership per user
CREATE TABLE IF NOT EXISTS bazaar_stock (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    shares INT NOT NULL DEFAULT 0,
    total_invested BIGINT NOT NULL DEFAULT 0,
    total_dividends BIGINT NOT NULL DEFAULT 0,
    last_purchase TIMESTAMP NULL DEFAULT NULL,
    last_dividend TIMESTAMP NULL DEFAULT NULL,
    
    INDEX idx_shares (shares DESC),
    INDEX idx_invested (total_invested DESC),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    
    CONSTRAINT chk_shares CHECK (shares >= 0)
) ENGINE=InnoDB;

-- Bazaar visitor tracking for dynamic pricing
CREATE TABLE IF NOT EXISTS bazaar_visitors (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    visited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    spent BIGINT NOT NULL DEFAULT 0,
    
    INDEX idx_visited (visited_at),
    INDEX idx_user (user_id),
    INDEX idx_guild (guild_id),
    INDEX idx_guild_recent (guild_id, visited_at),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Bazaar purchase history
CREATE TABLE IF NOT EXISTS bazaar_purchases (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    item_name VARCHAR(255) NOT NULL,
    quantity INT NOT NULL DEFAULT 1,
    price_paid BIGINT NOT NULL,
    discount_percent DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_user (user_id),
    INDEX idx_item (item_id),
    INDEX idx_purchased_at (purchased_at),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Global bazaar state (current items, prices)
CREATE TABLE IF NOT EXISTS bazaar_state (
    id INT NOT NULL PRIMARY KEY DEFAULT 1, -- Singleton
    stock_base_price DECIMAL(10,2) NOT NULL DEFAULT 100.00,
    last_refresh TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    active_items JSON NULL, -- Current items in bazaar
    CHECK (id = 1)
) ENGINE=InnoDB;

-- Reaction roles persistence (message -> emoji -> role)
CREATE TABLE IF NOT EXISTS reaction_roles (
    guild_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    emoji_raw VARCHAR(255) NOT NULL,
    emoji_id BIGINT UNSIGNED DEFAULT 0,
    role_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (message_id, emoji_raw),
    INDEX idx_guild (guild_id),
    INDEX idx_message (message_id),
    INDEX idx_channel (channel_id),
    INDEX idx_role (role_id)
) ENGINE=InnoDB;

-- Machine learning configuration values (owner-editable)
CREATE TABLE IF NOT EXISTS ml_settings (
    `key` VARCHAR(64) NOT NULL PRIMARY KEY,
    `value` TEXT NOT NULL
) ENGINE=InnoDB;

-- history of price adjustments performed by ML tuning
CREATE TABLE IF NOT EXISTS ml_price_changes (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    bait_level INT NOT NULL,
    adjust BIGINT NOT NULL,
    changed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_bait_level (bait_level),
    INDEX idx_changed_at (changed_at)
) ENGINE=InnoDB;

-- Autopurge schedules for auto-deleting user messages
CREATE TABLE IF NOT EXISTS autopurges (
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
    INDEX idx_target_user (target_user_id),
    INDEX idx_target_role (target_role_id),
    INDEX idx_guild (guild_id),
    INDEX idx_channel (channel_id)
) ENGINE=InnoDB;

-- ============================================================================
-- GLOBAL BLACKLIST / WHITELIST (for command abuse prevention)
-- ============================================================================

CREATE TABLE IF NOT EXISTS global_blacklist (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    reason VARCHAR(512) DEFAULT NULL,
    added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS global_whitelist (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    reason VARCHAR(512) DEFAULT NULL,
    added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SUGGESTIONS TABLE (feedback from users)
-- Stores user suggestions along with networth snapshot and timestamps
-- ============================================================================

CREATE TABLE IF NOT EXISTS suggestions (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    suggestion TEXT NOT NULL,
    networth BIGINT NOT NULL,
    submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `read` BOOLEAN NOT NULL DEFAULT FALSE,
    INDEX idx_user (user_id),
    INDEX idx_read (`read`),
    INDEX idx_submitted (submitted_at),
    INDEX idx_networth (networth DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- BUG REPORTS TABLE (user-submitted bug reports)
-- Stores detailed bug reports with command, reproduction steps, etc.
-- ============================================================================

CREATE TABLE IF NOT EXISTS bug_reports (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    command_or_feature VARCHAR(100) NOT NULL,
    reproduction_steps TEXT NOT NULL,
    expected_behavior TEXT NOT NULL,
    actual_behavior TEXT NOT NULL,
    networth BIGINT NOT NULL DEFAULT 0,
    submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `read` BOOLEAN NOT NULL DEFAULT FALSE,
    resolved BOOLEAN NOT NULL DEFAULT FALSE,
    INDEX idx_user (user_id),
    INDEX idx_read (`read`),
    INDEX idx_resolved (resolved),
    INDEX idx_submitted (submitted_at),
    INDEX idx_command (command_or_feature),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- COMMAND HISTORY (owner activity tracking)
-- Logs user commands and balance changes for auditing
-- ============================================================================

CREATE TABLE IF NOT EXISTS command_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    entry_type ENUM('CMD', 'BAL', 'FSH', 'PAY', 'GAM', 'SHP') NOT NULL,
    description VARCHAR(500) NOT NULL,
    amount BIGINT DEFAULT NULL,          -- balance change if applicable
    balance_after BIGINT DEFAULT NULL,   -- wallet balance after action
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    INDEX idx_type (entry_type),
    INDEX idx_created (created_at DESC),
    INDEX idx_user_time (user_id, created_at DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- CUSTOM PREFIXES
-- prefixes may be registered per-guild or per-user; users can have multiple.
-- Guild prefixes apply to everyone in the guild, user prefixes only to that user.
-- This allows users to define their own triggers without affecting others.
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_prefixes (
    guild_id BIGINT UNSIGNED NOT NULL,
    prefix VARCHAR(50) NOT NULL,
    PRIMARY KEY (guild_id, prefix)
) ENGINE=InnoDB;

-- per-guild command & module toggle tables for enabling/disabling functionality
CREATE TABLE IF NOT EXISTS guild_command_settings (
    guild_id BIGINT UNSIGNED NOT NULL,
    command VARCHAR(100) NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    PRIMARY KEY (guild_id, command)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_module_settings (
    guild_id BIGINT UNSIGNED NOT NULL,
    module VARCHAR(100) NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    PRIMARY KEY (guild_id, module)
) ENGINE=InnoDB;

-- scoped override tables for commands/modules (per-channel/role/user)
CREATE TABLE IF NOT EXISTS guild_command_scope_settings (
    guild_id BIGINT UNSIGNED NOT NULL,
    command VARCHAR(100) NOT NULL,
    scope_type ENUM('channel','role','user') NOT NULL,
    scope_id BIGINT UNSIGNED NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    exclusive BOOLEAN NOT NULL DEFAULT FALSE,
    PRIMARY KEY (guild_id, command, scope_type, scope_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_module_scope_settings (
    guild_id BIGINT UNSIGNED NOT NULL,
    module VARCHAR(100) NOT NULL,
    scope_type ENUM('channel','role','user') NOT NULL,
    scope_id BIGINT UNSIGNED NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    exclusive BOOLEAN NOT NULL DEFAULT FALSE,
    PRIMARY KEY (guild_id, module, scope_type, scope_id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_prefixes (
    user_id BIGINT UNSIGNED NOT NULL,
    prefix VARCHAR(50) NOT NULL,
    PRIMARY KEY (user_id, prefix),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Default shop catalog entries (rods and bait with varying levels/prices)
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- rods, level increases with price; metadata defines luck (%) and bait capacity
    ('rod_wood',      'Wooden Rod',    'entry-level fishing rod',       'rod',  500,   NULL, 0, 1, TRUE, '{"luck":0,"capacity":1}'),
    ('rod_iron',      'Iron Rod',      'sturdy iron rod',               'rod', 2000,   NULL, 0, 2, TRUE, '{"luck":5,"capacity":2}'),
    ('rod_steel',     'Steel Rod',     'durable steel rod',             'rod', 60000,   NULL, 0, 3, TRUE, '{"luck":10,"capacity":3}'),
    ('rod_gold',      'Golden Rod',    'luxurious gold plated rod',     'rod',  200000,  NULL, 0, 4, TRUE, '{"luck":20,"capacity":5}'),
    ('rod_diamond',   'Diamond Rod',   'exceptionally sharp rod',       'rod',1000000,  NULL, 0, 5, TRUE, '{"luck":50,"capacity":10}'),
    
    -- Themed specialty rods (level 6+)
    ('rod_infinity',  'Infinity Rod',  'transcends mathematical limits', 'rod', 15707960, NULL, 0, 6, TRUE, '{"luck":75,"capacity":15}'),
    ('rod_dev',       'Dev Rod',       'debugged to perfection',        'rod', 10485760, NULL, 0, 6, TRUE, '{"luck":60,"capacity":12}'),
    ('rod_shrek',     'Shrek Rod',     'ogre-sized fishing power',       'rod', 1337420, NULL, 0, 6, TRUE, '{"luck":80,"capacity":8}'),
    
    -- bait, prices range from 50 to 100k, levels correspond; metadata lists unlocked fish names
    ('bait_common',     'Common Bait',    'cheap general purpose bait',   'bait',     50,   NULL, 0, 1, TRUE, '{"unlocks":["common fish","salmon","bass","perch","bluegill"],"bonus":50,"multiplier":100}'),
    ('bait_uncommon',   'Uncommon Bait',  'better than common bait',      'bait',    200,   NULL, 0, 2, TRUE, '{"unlocks":["common fish","salmon","tropical fish","tuna","mackerel","flounder","cod"],"bonus":75,"multiplier":150}'),
    ('bait_rare',       'Rare Bait',      'rare bait that attracts better fish','bait',2000, NULL, 0, 3, TRUE, '{"unlocks":["salmon","tropical fish","octopus","shark","giant squid","anglerfish","barracuda","swordfish","marlin","stingray"],"bonus":100,"multiplier":200}'),
    ('bait_epic',       'Epic Bait',      'highly sought bait',           'bait',   50000,   NULL, 0, 4, TRUE, '{"unlocks":["whale","golden fish","abyssal leviathan","great white shark","giant octopus","colossal squid","blue whale"],"bonus":10,"multiplier":400}'),
    ('bait_legendary',  'Legendary Bait', 'legendary bait of myths',      'bait', 500000,  NULL, 0, 5, TRUE, '{"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40}'),
    
    -- Themed specialty baits (level 6+)
    ('bait_pi',         'π',              'transcendental mathematical constant', 'bait', 1570795,  NULL, 0, 6, TRUE, '{"unlocks":["rational fish","irrational fish","rooted fish","exponential fish","imaginary fish","prime fish","fibonacci fish"],"bonus":31,"multiplier":415}'),
    ('bait_segfault',   'Segmentation Fault', 'causes core dumps in fish', 'bait', 327680,   NULL, 0, 6, TRUE, '{"unlocks":["null pointer","stack overflow","memory leak","race condition","buffer overflow","deadlock","segfault"],"bonus":64,"multiplier":256}'),
    ('bait_swamp',      'Swamp Water',    'murky water from Far Far Away', 'bait', 133742,  NULL, 0, 6, TRUE, '{"unlocks":["donkey","fiona","puss in boots","lord farquaad","dragon","gingerbread man","shrek"],"bonus":42,"multiplier":420}'),
    
    -- Prestige rods (require prestige to purchase, levels 7-11)
    ('rod_prestige1',   'Ascended Rod',       'forged by those who reset',         'rod', 5000000,   NULL, 0, 7, TRUE, '{"luck":60,"capacity":12,"prestige":1}'),
    ('rod_prestige2',   'Transcendent Rod',   'twice reborn power',                'rod', 25000000,  NULL, 0, 8, TRUE, '{"luck":80,"capacity":15,"prestige":2}'),
    ('rod_prestige3',   'Ethereal Rod',       'thrice purified fishing',           'rod', 100000000, NULL, 0, 9, TRUE, '{"luck":100,"capacity":18,"prestige":3}'),
    ('rod_prestige4',   'Celestial Rod',      'heavenly fishing might',            'rod', 10000000000, NULL, 0, 10, TRUE, '{"luck":125,"capacity":22,"prestige":4}'),
    ('rod_prestige5',   'Divine Rod',         'ultimate prestige power',           'rod', 1500000000,NULL, 0, 11, TRUE, '{"luck":150,"capacity":25,"prestige":5}'),
    
    -- Variant prestige rods (alternative stats, levels 7-11)
    ('rod_prestige1b',  'Rebirth Rod',        'reforged from broken dreams',       'rod', 3500000,   NULL, 0, 7, TRUE, '{"luck":45,"capacity":16,"prestige":1}'),
    ('rod_prestige2b',  'Phantom Rod',        'woven from shadows',                'rod', 20000000,  NULL, 0, 8, TRUE, '{"luck":90,"capacity":12,"prestige":2}'),
    ('rod_prestige3b',  'Spectral Rod',       'shimmers between planes',           'rod', 80000000,  NULL, 0, 9, TRUE, '{"luck":85,"capacity":22,"prestige":3}'),
    ('rod_prestige4b',  'Astral Rod',         'reaches beyond the stars',          'rod', 8000000000,NULL, 0, 10, TRUE, '{"luck":110,"capacity":28,"prestige":4}'),
    ('rod_prestige5b',  'Sacred Rod',         'blessed by ocean spirits',          'rod', 1200000000,NULL, 0, 11, TRUE, '{"luck":165,"capacity":20,"prestige":5}'),
    
    -- Prestige baits (require prestige to purchase, levels 7-11)
    ('bait_prestige1',  'Ascended Bait',      'bait for the reborn',               'bait', 100000,   NULL, 0, 7, TRUE, '{"unlocks":["phoenix fish","inferno koi","molten eel"],"bonus":100,"multiplier":300,"prestige":1}'),
    ('bait_prestige2',  'Transcendent Bait',  'twice refined lure',                'bait', 500000,   NULL, 0, 8, TRUE, '{"unlocks":["void fish","wraith pike","nightmare angler"],"bonus":150,"multiplier":450,"prestige":2}'),
    ('bait_prestige3',  'Ethereal Bait',      'thrice blessed attraction',         'bait', 2000000,  NULL, 0, 9, TRUE, '{"unlocks":["nebula fish","spectral cod","galaxy grouper"],"bonus":200,"multiplier":600,"prestige":3}'),
    ('bait_prestige4',  'Celestial Bait',     'heavenly fish allure',              'bait', 120000000,  NULL, 0, 10, TRUE, '{"unlocks":["cosmic fish","astral pike","zodiac fish"],"bonus":250,"multiplier":800,"prestige":4}'),
    ('bait_prestige5',  'Divine Bait',        'ultimate prestige lure',            'bait', 30000000, NULL, 0, 11, TRUE, '{"unlocks":["eternal fish","holy mackerel","divine dolphin"],"bonus":300,"multiplier":1000,"prestige":5}'),
    
    -- Variant prestige baits (alternative unlocks, levels 7-11)
    ('bait_prestige1b', 'Ember Bait',         'smoldering lure of rebirth',        'bait', 80000,    NULL, 0, 7, TRUE, '{"unlocks":["firebird","ash marlin","cinder whale"],"bonus":110,"multiplier":280,"prestige":1}'),
    ('bait_prestige2b', 'Twilight Bait',      'dusk-infused attraction',           'bait', 400000,   NULL, 0, 8, TRUE, '{"unlocks":["abyssal","umbral tuna","dark tide bass"],"bonus":160,"multiplier":420,"prestige":2}'),
    ('bait_prestige3b', 'Starlight Bait',     'shimmering cosmic lure',            'bait', 1800000,  NULL, 0, 9, TRUE, '{"unlocks":["stardust sprite","cosmic ray","plasma fish"],"bonus":210,"multiplier":580,"prestige":3}'),
    ('bait_prestige4b', 'Comet Bait',         'trails stardust behind it',         'bait', 100000000,NULL, 0, 10, TRUE, '{"unlocks":["stellar phantom","orbit bass","constellation eel"],"bonus":260,"multiplier":780,"prestige":4}'),
    ('bait_prestige5b', 'Sacred Bait',        'purified by holy waters',           'bait', 25000000, NULL, 0, 11, TRUE, '{"unlocks":["ascendant angel","seraphim ray","blessed marlin"],"bonus":310,"multiplier":980,"prestige":5}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price),
    name = VALUES(name),
    description = VALUES(description),
    metadata = VALUES(metadata),
    level = VALUES(level);
    level = VALUES(level);

-- Initialize bazaar state
INSERT INTO bazaar_state (id, stock_base_price) VALUES (1, 100.00)
    ON DUPLICATE KEY UPDATE stock_base_price = stock_base_price;

-- ============================================================================
-- COOLDOWN SYSTEM
-- ============================================================================

-- Fast cooldown lookup table
-- Automatically cleaned by TTL
CREATE TABLE IF NOT EXISTS cooldowns (
    user_id BIGINT UNSIGNED NOT NULL,
    command VARCHAR(50) NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    
    PRIMARY KEY (user_id, command),
    INDEX idx_expires (expires_at),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Event to automatically clean expired cooldowns (runs every hour)
CREATE EVENT IF NOT EXISTS cleanup_cooldowns
ON SCHEDULE EVERY 1 HOUR
DO
    DELETE FROM cooldowns WHERE expires_at < NOW();

-- ============================================================================
-- GIVEAWAY SYSTEM
-- ============================================================================

-- Guild giveaway balance
CREATE TABLE IF NOT EXISTS guild_balances (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    balance BIGINT NOT NULL DEFAULT 0,
    total_donated BIGINT NOT NULL DEFAULT 0,
    total_given BIGINT NOT NULL DEFAULT 0,
    
    INDEX idx_balance (balance DESC),
    
    CONSTRAINT chk_guild_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

-- Active giveaways
CREATE TABLE IF NOT EXISTS giveaways (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NULL,
    prize_amount BIGINT NOT NULL,
    max_winners INT NOT NULL DEFAULT 1,
    ends_at TIMESTAMP NOT NULL,
    winner_ids JSON NULL, -- Array of winner user IDs
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_by BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_guild (guild_id),
    INDEX idx_active (active),
    INDEX idx_ends_at (ends_at),
    INDEX idx_active_guild (active, guild_id),
    
    FOREIGN KEY (guild_id) REFERENCES guild_balances(guild_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Giveaway entries
CREATE TABLE IF NOT EXISTS giveaway_entries (
    giveaway_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    entered_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (giveaway_id, user_id),
    INDEX idx_user (user_id),
    
    FOREIGN KEY (giveaway_id) REFERENCES giveaways(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SHOP SYSTEM
-- ============================================================================

-- Shop items catalog (static)
CREATE TABLE IF NOT EXISTS shop_items (
    item_id VARCHAR(100) NOT NULL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    category VARCHAR(50) NOT NULL,
    price BIGINT NOT NULL,
    max_quantity INT NULL, -- NULL = unlimited
    required_level INT NOT NULL DEFAULT 0,
    level INT NOT NULL DEFAULT 1, -- general level of the item (for rods/bait compatibility)
    usable BOOLEAN NOT NULL DEFAULT TRUE,
    metadata JSON NULL, -- Custom item properties
    
    INDEX idx_category (category),
    INDEX idx_price (price),
    
    CONSTRAINT chk_price CHECK (price >= 0)
) ENGINE=InnoDB;

-- Daily deals / special offers
CREATE TABLE IF NOT EXISTS daily_deals (
    id INT AUTO_INCREMENT PRIMARY KEY,
    item_id VARCHAR(100) NOT NULL,
    discount_percent DECIMAL(5,2) NOT NULL,
    stock_remaining INT NULL, -- NULL = unlimited
    active_date DATE NOT NULL,
    
    UNIQUE KEY unique_item_date (item_id, active_date),
    INDEX idx_active_date (active_date),
    
    FOREIGN KEY (item_id) REFERENCES shop_items(item_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ==========================================================================
-- SERVER-SPECIFIC MARKET
-- ============================================================================

-- Items available for purchase in a particular guild.  Admins can add/modify
-- entries, and each row may have a limited quantity or expiration timestamp.
CREATE TABLE IF NOT EXISTS market_items (
    guild_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    name VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    category VARCHAR(50) NOT NULL,
    price BIGINT NOT NULL,
    max_quantity INT NULL, -- NULL = unlimited
    metadata JSON NULL,
    expires_at TIMESTAMP NULL,

    PRIMARY KEY (guild_id, item_id),
    INDEX idx_guild (guild_id),
    INDEX idx_price (price),

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


-- User wishlists
CREATE TABLE IF NOT EXISTS wishlists (
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (user_id, item_id),
    INDEX idx_item (item_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (item_id) REFERENCES shop_items(item_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- TRADING SYSTEM
-- ============================================================================

-- Trade offers
CREATE TABLE IF NOT EXISTS trades (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    initiator_id BIGINT UNSIGNED NOT NULL,
    recipient_id BIGINT UNSIGNED NOT NULL,
    
    -- Initiator offers
    initiator_cash BIGINT NOT NULL DEFAULT 0,
    initiator_items JSON NULL, -- Array of {item_id, quantity}
    
    -- Recipient offers
    recipient_cash BIGINT NOT NULL DEFAULT 0,
    recipient_items JSON NULL,
    
    status ENUM('pending', 'accepted', 'rejected', 'cancelled', 'completed') NOT NULL DEFAULT 'pending',
    
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    completed_at TIMESTAMP NULL,
    
    INDEX idx_initiator (initiator_id),
    INDEX idx_recipient (recipient_id),
    INDEX idx_status (status),
    INDEX idx_expires (expires_at),
    
    FOREIGN KEY (initiator_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- STATISTICS AND ANALYTICS
-- ============================================================================

-- Command usage statistics
CREATE TABLE IF NOT EXISTS command_stats (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    command_name VARCHAR(100) NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    used_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_user (user_id),
    INDEX idx_command (command_name),
    INDEX idx_used_at (used_at),
    INDEX idx_guild (guild_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Gambling statistics per user
CREATE TABLE IF NOT EXISTS gambling_stats (
    user_id BIGINT UNSIGNED NOT NULL,
    game_type VARCHAR(50) NOT NULL,
    games_played INT NOT NULL DEFAULT 0,
    total_bet BIGINT NOT NULL DEFAULT 0,
    total_won BIGINT NOT NULL DEFAULT 0,
    total_lost BIGINT NOT NULL DEFAULT 0,
    biggest_win BIGINT NOT NULL DEFAULT 0,
    biggest_loss BIGINT NOT NULL DEFAULT 0,
    
    PRIMARY KEY (user_id, game_type),
    INDEX idx_game_type (game_type),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Leaderboard cache (updated periodically)
CREATE TABLE IF NOT EXISTS leaderboard_cache (
    rank_type ENUM('wallet', 'bank', 'networth', 'gambling', 'fishing') NOT NULL,
    guild_id BIGINT UNSIGNED NULL, -- NULL = global
    user_id BIGINT UNSIGNED NOT NULL,
    rank_position INT NOT NULL,
    value BIGINT NOT NULL,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (rank_type, guild_id, user_id),
    INDEX idx_rank (rank_type, guild_id, rank_position),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- GUILD SETTINGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_settings (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    prefix VARCHAR(10) NOT NULL DEFAULT 'bb ',
    blocked_channels JSON NULL, -- Array of channel IDs
    blocked_commands JSON NULL, -- Array of command names
    logging_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    logging_channel BIGINT UNSIGNED NULL,
    public_stats BOOLEAN NOT NULL DEFAULT FALSE,
    
    -- Custom Server Metadata
    server_bio TEXT NULL,
    server_website VARCHAR(255) NULL,
    server_banner_url VARCHAR(512) NULL,
    server_avatar_url VARCHAR(512) NULL,
    
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ============================================================================
-- REMINDERS
-- ============================================================================

CREATE TABLE IF NOT EXISTS reminders (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message TEXT NOT NULL,
    remind_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed BOOLEAN NOT NULL DEFAULT FALSE,
    
    INDEX idx_user (user_id),
    INDEX idx_remind_at (remind_at),
    INDEX idx_pending (completed, remind_at),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- AUTOPURGE SCHEDULES
-- ============================================================================

CREATE TABLE IF NOT EXISTS autopurges (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    interval_seconds INT NOT NULL,
    message_limit INT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_user (user_id),
    INDEX idx_guild (guild_id),
    INDEX idx_channel (channel_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- VIEWS FOR COMMON QUERIES
-- ============================================================================

-- User net worth view
CREATE OR REPLACE VIEW v_user_networth AS
SELECT 
    user_id,
    wallet,
    bank,
    (wallet + bank) AS networth,
    bank_limit,
    (bank_limit - bank) AS bank_space
FROM users;

-- Active giveaways view
CREATE OR REPLACE VIEW v_active_giveaways AS
SELECT 
    g.*,
    gb.balance as guild_balance,
    (SELECT COUNT(*) FROM giveaway_entries WHERE giveaway_id = g.id) as entry_count
FROM giveaways g
JOIN guild_balances gb ON g.guild_id = gb.guild_id
WHERE g.active = TRUE AND g.ends_at > NOW();

-- Fish statistics view
CREATE OR REPLACE VIEW v_fish_statistics AS
SELECT 
    user_id,
    COUNT(*) as total_caught,
    SUM(CASE WHEN sold THEN 0 ELSE 1 END) as unsold_count,
    SUM(value) as total_value,
    AVG(weight) as avg_weight,
    MAX(weight) as heaviest_fish,
    MAX(value) as most_valuable,
    COUNT(CASE WHEN rarity = 'legendary' THEN 1 END) as legendary_count,
    COUNT(CASE WHEN rarity = 'mutated' THEN 1 END) as mutated_count
FROM fish_catches
GROUP BY user_id;

-- ============================================================================
-- STORED PROCEDURES
-- ============================================================================

DELIMITER //

-- Transfer money between users atomically
CREATE PROCEDURE IF NOT EXISTS sp_transfer_money(
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
    
    -- Lock the rows
    SELECT wallet INTO from_balance FROM users WHERE user_id = from_user FOR UPDATE;
    
    IF from_balance < amount THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Insufficient funds';
    END IF;
    
    -- Perform transfer
    UPDATE users SET wallet = wallet - amount WHERE user_id = from_user;
    UPDATE users SET wallet = wallet + amount WHERE user_id = to_user;
    
    COMMIT;
END //

-- Claim interest
CREATE PROCEDURE IF NOT EXISTS sp_claim_interest(
    IN p_user_id BIGINT UNSIGNED,
    OUT p_interest_amount BIGINT
)
BEGIN
    DECLARE v_bank BIGINT;
    DECLARE v_interest_rate DECIMAL(5,2);
    DECLARE v_last_claim TIMESTAMP;
    
    SELECT bank, interest_rate, last_interest_claim
    INTO v_bank, v_interest_rate, v_last_claim
    FROM users
    WHERE user_id = p_user_id;
    
    -- Calculate interest (daily compound)
    SET p_interest_amount = FLOOR(v_bank * (v_interest_rate / 100.0));
    
    -- Apply interest
    UPDATE users
    SET bank = bank + p_interest_amount,
        last_interest_claim = CURRENT_TIMESTAMP
    WHERE user_id = p_user_id;
END //

-- Update leaderboard cache
CREATE PROCEDURE IF NOT EXISTS sp_update_leaderboard_cache(
    IN p_rank_type VARCHAR(20),
    IN p_guild_id BIGINT UNSIGNED
)
BEGIN
    DELETE FROM leaderboard_cache 
    WHERE rank_type = p_rank_type AND (guild_id = p_guild_id OR (guild_id IS NULL AND p_guild_id IS NULL));
    
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

-- ============================================================================
-- PERFORMANCE OPTIMIZATION
-- ============================================================================

-- Analyze tables for query optimization
ANALYZE TABLE users, inventory, fish_catches, bazaar_stock, giveaways;

-- ============================================================================
-- INDEXES SUMMARY
-- ============================================================================
-- users: 5 indexes (wallet, bank, networth, last_active, badges)
-- inventory: 3 indexes (user, item_type, item_id) + unique constraint
-- fish_catches: 7 indexes (user, rarity, weight, value, sold, caught_at, user_unsold)
-- autofishers: 2 indexes (active, balance)
-- bazaar_visitors: 4 indexes (visited, user, guild, guild_recent)
-- cooldowns: 1 index (expires)
-- giveaways: 4 indexes (guild, active, ends_at, active_guild)
-- Total: Heavily indexed for read performance, optimized for economy queries

-- ============================================================================
-- LEVELING SYSTEM
-- ============================================================================

-- Global user XP tracking
CREATE TABLE IF NOT EXISTS user_xp (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    total_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    level INT UNSIGNED NOT NULL DEFAULT 1,
    last_message_xp TIMESTAMP NULL DEFAULT NULL,
    
    INDEX idx_total_xp (total_xp DESC),
    INDEX idx_level (level DESC),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Per-server XP tracking (can be reset by admins without affecting global xp)
CREATE TABLE IF NOT EXISTS server_xp (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    server_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    server_level INT UNSIGNED NOT NULL DEFAULT 1,
    last_message_xp TIMESTAMP NULL DEFAULT NULL,
    
    PRIMARY KEY (user_id, guild_id),
    INDEX idx_guild_xp (guild_id, server_xp DESC),
    INDEX idx_guild_level (guild_id, server_level DESC),
    INDEX idx_user (user_id),
    
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Server leveling configuration
CREATE TABLE IF NOT EXISTS server_leveling_config (
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
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    CONSTRAINT chk_xp_range CHECK (min_xp_per_message <= max_xp_per_message),
    CONSTRAINT chk_xp_positive CHECK (min_xp_per_message > 0 AND max_xp_per_message > 0),
    CONSTRAINT chk_coins_positive CHECK (coins_per_message >= 0),
    CONSTRAINT chk_min_chars CHECK (min_message_chars >= 0),
    CONSTRAINT chk_cooldown CHECK (xp_cooldown_seconds >= 0)
) ENGINE=InnoDB;

-- Level role rewards (similar to market items but for levels)
CREATE TABLE IF NOT EXISTS level_roles (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    level INT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    role_name VARCHAR(100) NOT NULL,
    description TEXT NULL,
    remove_previous BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_level (guild_id, level),
    INDEX idx_guild (guild_id),
    INDEX idx_level (level),
    
    CONSTRAINT chk_level_positive CHECK (level > 0)
) ENGINE=InnoDB;

-- Patch Notes Table
-- Stores bot update patch notes with automatic versioning
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
-- PRIVACY & OPT-OUT TABLES
-- ============================================================================

-- User privacy preferences and opt-out status
CREATE TABLE IF NOT EXISTS user_privacy (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    opted_out BOOLEAN NOT NULL DEFAULT FALSE,
    opted_out_at TIMESTAMP NULL DEFAULT NULL,
    data_deleted_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Encrypted identity cache (username, nickname, avatar)
-- Data is AES-256 encrypted at rest and expires after 30 days
CREATE TABLE IF NOT EXISTS encrypted_identity_cache (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    encrypted_username VARBINARY(512) DEFAULT NULL,
    encrypted_nickname VARBINARY(512) DEFAULT NULL,
    encrypted_avatar VARBINARY(1024) DEFAULT NULL,
    encryption_iv VARBINARY(16) NOT NULL,
    cached_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================================
-- END OF SCHEMA
-- ============================================================================
