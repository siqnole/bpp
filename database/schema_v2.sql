-- ============================================================================
-- Bronx Bot — Schema v2: Unified Database Overhaul
-- ============================================================================
-- Reorganized into 3 clear domains:
--   Domain 1: USER   — all per-user data (guild_id NULL = global economy)
--   Domain 2: SERVER — all per-server config, moderation, analytics
--   Domain 3: GLOBAL — bot-wide catalogs, singletons, lookup tables
--
-- Key changes from v1:
--   • Server economy tables unified into main tables with optional guild_id
--   • 12 duplicated server_* tables eliminated
--   • user_stats EAV → hybrid (common stats as columns + overflow EAV)
--   • command_usage / command_stats deduplicated → command_stats
--   • 3 xp_blacklist tables → 1 guild_xp_blacklist
--   • server_bot_admins + server_bot_mods → guild_bot_staff
--   • Moderation configs now persisted (were in-memory only)
--   • autopurges duplicate definition resolved
--   • All IDs standardized to BIGINT UNSIGNED
--   • ~70 tables → ~47 well-organized tables
-- ============================================================================

CREATE DATABASE IF NOT EXISTS bronxbot
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE bronxbot;

-- ############################################################################
-- DOMAIN 1: USER MODEL
-- All per-user data. guild_id IS NULL = global economy, non-NULL = server.
-- ############################################################################

-- ============================================================================
-- 1.1 USERS — master user profile
-- ============================================================================
CREATE TABLE IF NOT EXISTS users (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,

    -- Balances
    wallet BIGINT NOT NULL DEFAULT 0,
    bank BIGINT NOT NULL DEFAULT 0,
    bank_limit BIGINT NOT NULL DEFAULT 10000,

    -- Interest system
    interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    interest_level INT NOT NULL DEFAULT 0,
    last_interest_claim TIMESTAMP NULL DEFAULT NULL,

    -- Cooldowns (global economy only)
    last_daily TIMESTAMP NULL DEFAULT NULL,
    last_work TIMESTAMP NULL DEFAULT NULL,
    last_beg TIMESTAMP NULL DEFAULT NULL,
    last_rob TIMESTAMP NULL DEFAULT NULL,

    -- Gambling aggregates (global)
    total_gambled BIGINT NOT NULL DEFAULT 0,
    total_won BIGINT NOT NULL DEFAULT 0,
    total_lost BIGINT NOT NULL DEFAULT 0,

    -- Flattened common stats (hybrid model — hot-path stats as columns)
    fish_caught BIGINT NOT NULL DEFAULT 0,
    fish_sold BIGINT NOT NULL DEFAULT 0,
    gambling_wins BIGINT NOT NULL DEFAULT 0,
    gambling_losses BIGINT NOT NULL DEFAULT 0,
    commands_used BIGINT NOT NULL DEFAULT 0,
    daily_streak INT NOT NULL DEFAULT 0,
    work_count BIGINT NOT NULL DEFAULT 0,
    ores_mined BIGINT NOT NULL DEFAULT 0,
    items_crafted BIGINT NOT NULL DEFAULT 0,
    trades_completed BIGINT NOT NULL DEFAULT 0,

    -- Role flags
    dev BOOLEAN NOT NULL DEFAULT FALSE,
    admin BOOLEAN NOT NULL DEFAULT FALSE,
    is_mod BOOLEAN NOT NULL DEFAULT FALSE,
    maintainer BOOLEAN NOT NULL DEFAULT FALSE,
    contributor BOOLEAN NOT NULL DEFAULT FALSE,
    vip BOOLEAN NOT NULL DEFAULT FALSE,

    -- Prestige system
    prestige INT NOT NULL DEFAULT 0,

    -- Passive mode
    passive BOOLEAN NOT NULL DEFAULT FALSE,

    -- Metadata
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_active TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    -- Indexes
    INDEX idx_wallet (wallet DESC),
    INDEX idx_bank (bank DESC),
    INDEX idx_last_active (last_active),
    INDEX idx_badges (dev, admin, is_mod, vip),
    INDEX idx_prestige (prestige DESC),
    INDEX idx_fish_caught (fish_caught DESC),
    INDEX idx_commands_used (commands_used DESC),
    INDEX idx_daily_streak (daily_streak DESC)
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- Computed networth column
ALTER TABLE users ADD COLUMN IF NOT EXISTS networth BIGINT AS (wallet + bank) STORED;
CREATE INDEX IF NOT EXISTS idx_networth ON users(networth DESC);

-- ============================================================================
-- 1.2 USER_STATS_EXT — rare/overflow stats (EAV for niche metrics)
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_stats_ext (
    user_id BIGINT UNSIGNED NOT NULL,
    stat_name VARCHAR(64) NOT NULL,
    stat_value BIGINT NOT NULL DEFAULT 0,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (user_id, stat_name),
    INDEX idx_stat_name_value (stat_name, stat_value DESC),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.3 USER_INVENTORY — unified (replaces inventory + server_inventory)
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_inventory (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,  -- NULL = global economy
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

-- ============================================================================
-- 1.4 FISHING SYSTEM — unified
-- ============================================================================

-- Fish catches (replaces fish_catches + server_fish_catches)
CREATE TABLE IF NOT EXISTS user_fish_catches (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NULL,  -- NULL = global
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
    INDEX idx_weight (weight DESC),
    INDEX idx_value (value DESC),
    INDEX idx_sold (sold),
    INDEX idx_caught_at (caught_at),
    INDEX idx_user_unsold (user_id, guild_id, sold),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Active fishing gear (replaces active_fishing_gear + server_active_fishing_gear)
CREATE TABLE IF NOT EXISTS user_fishing_gear (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global (avoids NULL in PK)
    active_rod_id VARCHAR(100) NULL,
    active_bait_id VARCHAR(100) NULL,

    PRIMARY KEY (user_id, guild_id),
    INDEX idx_rod (active_rod_id),
    INDEX idx_bait (active_bait_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Autofishers (replaces autofishers + server_autofishers)
CREATE TABLE IF NOT EXISTS user_autofishers (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    count INT NOT NULL DEFAULT 1,
    efficiency_level INT NOT NULL DEFAULT 1,
    efficiency_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    balance BIGINT NOT NULL DEFAULT 0,
    total_deposited BIGINT NOT NULL DEFAULT 0,
    bag_limit INT NOT NULL DEFAULT 10,
    last_claim TIMESTAMP NULL DEFAULT NULL,
    active BOOLEAN NOT NULL DEFAULT FALSE,

    -- Autofisher v2 fields
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
    INDEX idx_balance (balance DESC),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,

    CONSTRAINT chk_af_count CHECK (count >= 0 AND count <= 30),
    CONSTRAINT chk_af_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

-- Autofish storage (replaces autofish_storage + server_autofish_storage)
CREATE TABLE IF NOT EXISTS user_autofish_storage (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    fish_catch_id BIGINT UNSIGNED NOT NULL,

    INDEX idx_user_guild (user_id, guild_id),

    FOREIGN KEY (user_id, guild_id) REFERENCES user_autofishers(user_id, guild_id) ON DELETE CASCADE,
    FOREIGN KEY (fish_catch_id) REFERENCES user_fish_catches(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.5 COOLDOWNS — unified (replaces cooldowns + server_cooldowns)
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_cooldowns (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    command VARCHAR(50) NOT NULL,
    expires_at TIMESTAMP NOT NULL,

    PRIMARY KEY (user_id, guild_id, command),
    INDEX idx_expires (expires_at),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Hourly cleanup events
CREATE EVENT IF NOT EXISTS cleanup_cooldowns
ON SCHEDULE EVERY 1 HOUR
DO
    DELETE FROM user_cooldowns WHERE expires_at < NOW();

-- ============================================================================
-- 1.6 GAMBLING — unified
-- ============================================================================

-- Per-game aggregate stats (replaces gambling_stats + server_gambling_stats)
CREATE TABLE IF NOT EXISTS user_gambling_stats (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
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

-- Individual bet history (replaces gambling_history, adds guild_id)
CREATE TABLE IF NOT EXISTS user_gambling_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    game_type ENUM('slots','coinflip','blackjack','poker','roulette','crash','dice') NOT NULL,
    bet_amount BIGINT NOT NULL,
    result_amount BIGINT NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_user_game (user_id, game_type),
    INDEX idx_user_result (user_id, result_amount),
    INDEX idx_game_date (game_type, created_at),
    INDEX idx_guild (guild_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.7 XP / LEVELING — unified (replaces user_xp + server_xp)
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_xp (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global XP
    total_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,
    level INT UNSIGNED NOT NULL DEFAULT 1,
    last_message_xp TIMESTAMP NULL DEFAULT NULL,

    PRIMARY KEY (user_id, guild_id),
    INDEX idx_guild_xp (guild_id, total_xp DESC),
    INDEX idx_guild_level (guild_id, level DESC),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.8 LOANS
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_loans (
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    principal BIGINT NOT NULL DEFAULT 0,
    interest_rate DECIMAL(5,2) NOT NULL DEFAULT 5.00,
    remaining BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_payment_at TIMESTAMP NULL DEFAULT NULL,

    PRIMARY KEY (user_id, guild_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.9 PASSIVE INCOME — fish ponds, mining claims
-- ============================================================================

CREATE TABLE IF NOT EXISTS user_fish_ponds (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    pond_level INT NOT NULL DEFAULT 1,
    capacity INT NOT NULL DEFAULT 5,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_last_collect (last_collect),

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
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
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

-- ============================================================================
-- 1.10 BAZAAR — per-user ownership
-- ============================================================================
CREATE TABLE IF NOT EXISTS user_bazaar_stock (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    shares INT NOT NULL DEFAULT 0,
    total_invested BIGINT NOT NULL DEFAULT 0,
    total_dividends BIGINT NOT NULL DEFAULT 0,
    last_purchase TIMESTAMP NULL DEFAULT NULL,
    last_dividend TIMESTAMP NULL DEFAULT NULL,

    INDEX idx_shares (shares DESC),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,

    CONSTRAINT chk_shares CHECK (shares >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_bazaar_visits (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    visited_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    spent BIGINT NOT NULL DEFAULT 0,

    INDEX idx_visited (visited_at),
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
    INDEX idx_item (item_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 1.11 MISC USER TABLES
-- ============================================================================

CREATE TABLE IF NOT EXISTS user_afk (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    reason VARCHAR(500) NOT NULL,
    since TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_since (since),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_prefixes (
    user_id BIGINT UNSIGNED NOT NULL,
    prefix VARCHAR(50) NOT NULL,
    PRIMARY KEY (user_id, prefix),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_wishlists (
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (user_id, item_id),
    INDEX idx_item (item_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (item_id) REFERENCES shop_items(item_id) ON DELETE CASCADE
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

    INDEX idx_user (user_id),
    INDEX idx_remind_at (remind_at),
    INDEX idx_pending (completed, remind_at),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS user_privacy (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    opted_out BOOLEAN NOT NULL DEFAULT FALSE,
    opted_out_at TIMESTAMP NULL DEFAULT NULL,
    data_deleted_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Command audit log
CREATE TABLE IF NOT EXISTS user_command_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    entry_type ENUM('CMD','BAL','FSH','PAY','GAM','SHP') NOT NULL,
    description VARCHAR(500) NOT NULL,
    amount BIGINT DEFAULT NULL,
    balance_after BIGINT DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_user (user_id),
    INDEX idx_type (entry_type),
    INDEX idx_created (created_at DESC),
    INDEX idx_user_time (user_id, created_at DESC),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;


-- ############################################################################
-- DOMAIN 2: SERVER MODEL
-- All per-server data keyed by guild_id.
-- ############################################################################

-- ============================================================================
-- 2.1 GUILD CONFIG
-- ============================================================================

-- Main guild settings (absorbs guild_economy_settings)
CREATE TABLE IF NOT EXISTS guild_settings (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,

    -- General settings
    prefix VARCHAR(10) NOT NULL DEFAULT 'bb ',
    blocked_channels JSON NULL,
    blocked_commands JSON NULL,
    logging_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    logging_channel BIGINT UNSIGNED NULL,

    -- Economy mode toggle
    economy_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    economy_mode ENUM('global','server') NOT NULL DEFAULT 'global',

    -- Beta Access Flag
    beta_tester BOOLEAN NOT NULL DEFAULT FALSE,

    -- Server economy customization
    starting_wallet BIGINT NOT NULL DEFAULT 1000,
    starting_bank_limit BIGINT NOT NULL DEFAULT 10000,
    default_interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00,

    -- Command cooldowns (seconds, 0 = use default)
    daily_cooldown INT NOT NULL DEFAULT 86400,
    work_cooldown INT NOT NULL DEFAULT 3600,
    beg_cooldown INT NOT NULL DEFAULT 1800,
    rob_cooldown INT NOT NULL DEFAULT 7200,
    fish_cooldown INT NOT NULL DEFAULT 60,

    -- Economy multipliers
    work_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    gambling_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    fishing_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,

    -- Feature toggles
    allow_gambling BOOLEAN NOT NULL DEFAULT TRUE,
    allow_fishing BOOLEAN NOT NULL DEFAULT TRUE,
    allow_trading BOOLEAN NOT NULL DEFAULT TRUE,
    allow_robbery BOOLEAN NOT NULL DEFAULT TRUE,

    -- Economy limits
    max_wallet BIGINT NULL DEFAULT NULL,
    max_bank BIGINT NULL DEFAULT NULL,
    max_networth BIGINT NULL DEFAULT NULL,

    -- Tax system
    enable_tax BOOLEAN NOT NULL DEFAULT FALSE,
    transaction_tax_percent DECIMAL(5,2) NOT NULL DEFAULT 0.00,

    -- Metadata
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    INDEX idx_economy_mode (economy_mode),

    CONSTRAINT chk_gs_multipliers CHECK (work_multiplier >= 0 AND gambling_multiplier >= 0 AND fishing_multiplier >= 0),
    CONSTRAINT chk_gs_tax CHECK (transaction_tax_percent >= 0 AND transaction_tax_percent <= 100)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_prefixes (
    guild_id BIGINT UNSIGNED NOT NULL,
    prefix VARCHAR(50) NOT NULL,
    PRIMARY KEY (guild_id, prefix)
) ENGINE=InnoDB;

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

-- ============================================================================
-- 2.2 LEVELING CONFIG
-- ============================================================================

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
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    CONSTRAINT chk_xp_range CHECK (min_xp_per_message <= max_xp_per_message),
    CONSTRAINT chk_xp_positive CHECK (min_xp_per_message > 0 AND max_xp_per_message > 0),
    CONSTRAINT chk_coins_positive CHECK (coins_per_message >= 0),
    CONSTRAINT chk_min_chars CHECK (min_message_chars >= 0),
    CONSTRAINT chk_cooldown CHECK (xp_cooldown_seconds >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_level_roles (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    level INT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    role_name VARCHAR(100) NOT NULL,
    description TEXT NULL,
    remove_previous BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uq_guild_level (guild_id, level),
    INDEX idx_guild (guild_id),
    INDEX idx_level (level),

    CONSTRAINT chk_level_positive CHECK (level > 0)
) ENGINE=InnoDB;

-- Consolidated XP blacklist (replaces 3 separate tables)
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

-- ============================================================================
-- 2.3 GUILD PERMISSIONS (replaces server_bot_admins + server_bot_mods)
-- ============================================================================

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

-- ============================================================================
-- 2.4 MODERATION CONFIG (NEW — persists configs that were in-memory only)
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_moderation_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,

    -- Feature toggles (fast boolean checks)
    antispam_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    text_filter_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    url_guard_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    reaction_filter_enabled BOOLEAN NOT NULL DEFAULT FALSE,

    -- Full configs stored as JSON for flexibility
    antispam_config JSON NULL,       -- rate limits, actions, thresholds
    text_filter_config JSON NULL,    -- regex patterns, blocked words, actions
    url_guard_config JSON NULL,      -- allowed/blocked domains, actions
    reaction_filter_config JSON NULL, -- reaction filtering rules

    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ============================================================================
-- 2.4.1 LOGGING CONFIG (NEW — Server-specific webhook log categories)
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_log_configs (
    guild_id BIGINT UNSIGNED NOT NULL,
    log_type VARCHAR(50) NOT NULL, -- 'moderation', 'messages', 'members', 'economy', 'server'
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

-- ============================================================================
-- 2.4.2 FEATURE FLAGS (runtime kill-switch / beta gating system)
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

-- ============================================================================
-- 2.5 MODERATION FEATURES
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_reaction_roles (
    guild_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    emoji_raw VARCHAR(255) NOT NULL,
    emoji_id BIGINT UNSIGNED DEFAULT 0,
    role_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (message_id, emoji_raw),
    INDEX idx_guild (guild_id),
    INDEX idx_channel (channel_id),
    INDEX idx_role (role_id)
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
    INDEX idx_guild (guild_id),
    INDEX idx_channel (channel_id),
    INDEX idx_target_user (target_user_id),
    INDEX idx_target_role (target_role_id)
) ENGINE=InnoDB;

-- ============================================================================
-- 2.6 GUILD MESSAGES & ANALYTICS
-- ============================================================================

-- Snipe cache for deleted messages
CREATE TABLE IF NOT EXISTS guild_deleted_messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    author_id BIGINT UNSIGNED NOT NULL,
    author_tag VARCHAR(128) NOT NULL DEFAULT '',
    author_avatar VARCHAR(512) NOT NULL DEFAULT '',
    content TEXT NULL,
    attachment_urls TEXT NULL,
    embeds_summary TEXT NULL,
    deleted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_guild_channel (guild_id, channel_id),
    INDEX idx_deleted_at (deleted_at),
    INDEX idx_author (author_id),
    INDEX idx_message (message_id)
) ENGINE=InnoDB;

-- Member join/leave events
CREATE TABLE IF NOT EXISTS guild_member_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('join','leave') NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_guild_time (guild_id, created_at),
    INDEX idx_guild_type_time (guild_id, event_type, created_at)
) ENGINE=InnoDB;

-- Message/edit/delete events (purged after 7 days)
CREATE TABLE IF NOT EXISTS guild_message_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    channel_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('message','edit','delete') NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_guild_time (guild_id, created_at),
    INDEX idx_guild_chan_time (guild_id, channel_id, created_at),
    INDEX idx_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB;

-- Daily rollup stats
CREATE TABLE IF NOT EXISTS guild_daily_stats (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    stat_date DATE NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = guild-wide aggregate
    messages_count INT NOT NULL DEFAULT 0,
    edits_count INT NOT NULL DEFAULT 0,
    deletes_count INT NOT NULL DEFAULT 0,
    joins_count INT NOT NULL DEFAULT 0,
    leaves_count INT NOT NULL DEFAULT 0,
    commands_count INT NOT NULL DEFAULT 0,
    active_users INT NOT NULL DEFAULT 0,

    UNIQUE KEY uq_guild_date_chan (guild_id, stat_date, channel_id),
    INDEX idx_guild_date (guild_id, stat_date)
) ENGINE=InnoDB;

-- Per-command per-channel daily usage
CREATE TABLE IF NOT EXISTS guild_command_usage (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    command_name VARCHAR(64) NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    usage_date DATE NOT NULL,
    use_count INT NOT NULL DEFAULT 1,

    UNIQUE KEY uq_guild_cmd_chan_date (guild_id, command_name, channel_id, usage_date),
    INDEX idx_guild_date (guild_id, usage_date),
    INDEX idx_guild_cmd_date (guild_id, command_name, usage_date)
) ENGINE=InnoDB;

-- Voice channel events
CREATE TABLE IF NOT EXISTS guild_voice_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('join','leave') NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_guild_time (guild_id, created_at),
    INDEX idx_guild_chan_time (guild_id, channel_id, created_at),
    INDEX idx_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB;

-- Boost events
CREATE TABLE IF NOT EXISTS guild_boost_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('boost','unboost') NOT NULL,
    boost_id VARCHAR(32) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_guild_time (guild_id, created_at),
    INDEX idx_guild_user_time (guild_id, user_id, created_at)
) ENGINE=InnoDB;

-- Pre-aggregated per-user daily activity
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
    INDEX idx_guild_user (guild_id, user_id),
    INDEX idx_guild_date_msgs (guild_id, stat_date, messages DESC),
    INDEX idx_guild_date_vc (guild_id, stat_date, voice_minutes DESC),
    INDEX idx_guild_date_cmds (guild_id, stat_date, commands_used DESC)
) ENGINE=InnoDB;

-- ============================================================================
-- 2.7 SERVER ECONOMY FEATURES
-- ============================================================================

-- Server treasury
CREATE TABLE IF NOT EXISTS guild_balances (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    balance BIGINT NOT NULL DEFAULT 0,
    total_donated BIGINT NOT NULL DEFAULT 0,
    total_given BIGINT NOT NULL DEFAULT 0,

    INDEX idx_balance (balance DESC),

    CONSTRAINT chk_guild_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

-- Giveaways
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

    INDEX idx_guild (guild_id),
    INDEX idx_active (active),
    INDEX idx_ends_at (ends_at),
    INDEX idx_active_guild (active, guild_id),

    FOREIGN KEY (guild_id) REFERENCES guild_balances(guild_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS guild_giveaway_entries (
    giveaway_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    entered_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (giveaway_id, user_id),
    INDEX idx_user (user_id),

    FOREIGN KEY (giveaway_id) REFERENCES guild_giveaways(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Server-specific market items
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
    INDEX idx_guild (guild_id),
    INDEX idx_price (price),

    CONSTRAINT chk_market_price CHECK (price >= 0)
) ENGINE=InnoDB;

-- Trades (unified — replaces trades + server_trades)
CREATE TABLE IF NOT EXISTS guild_trades (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NULL,  -- NULL = global trade
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
    INDEX idx_status (status),
    INDEX idx_expires (expires_at),

    FOREIGN KEY (initiator_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Heists
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

    INDEX idx_channel (channel_id),
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

-- Guild membership for leaderboards
CREATE TABLE IF NOT EXISTS guild_members (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    joined_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uq_guild_user (guild_id, user_id),
    INDEX idx_guild (guild_id),
    INDEX idx_user (user_id),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;


-- ############################################################################
-- DOMAIN 3: GLOBAL / BOT-WIDE
-- Catalogs, singletons, lookup tables, and bot-level data
-- ############################################################################

-- ============================================================================
-- 3.1 SHOP & ITEM CATALOGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS shop_items (
    item_id VARCHAR(100) NOT NULL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    category VARCHAR(50) NOT NULL,
    price BIGINT NOT NULL,
    max_quantity INT NULL,
    required_level INT NOT NULL DEFAULT 0,
    level INT NOT NULL DEFAULT 1,
    usable BOOLEAN NOT NULL DEFAULT TRUE,
    metadata JSON NULL,

    INDEX idx_category (category),
    INDEX idx_price (price),

    CONSTRAINT chk_price CHECK (price >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS daily_deals (
    id INT AUTO_INCREMENT PRIMARY KEY,
    item_id VARCHAR(100) NOT NULL,
    discount_percent DECIMAL(5,2) NOT NULL,
    stock_remaining INT NULL,
    active_date DATE NOT NULL,

    UNIQUE KEY uq_item_date (item_id, active_date),
    INDEX idx_active_date (active_date),

    FOREIGN KEY (item_id) REFERENCES shop_items(item_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 3.2 MINING CATALOGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS ore_types (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL UNIQUE,
    emoji VARCHAR(32) NOT NULL,
    weight INT NOT NULL,
    min_value BIGINT NOT NULL,
    max_value BIGINT NOT NULL,
    effect ENUM('None','Flat','Exponential','Logarithmic','NLogN','Wacky',
                'Jackpot','Critical','Volatile','Surge','Diminishing',
                'Cascading','Wealthy','Banker','Miner','Merchant',
                'Ascended','Collector','Persistent') NOT NULL DEFAULT 'None',
    effect_chance DOUBLE NOT NULL DEFAULT 0.0,
    min_pickaxe_level INT NOT NULL DEFAULT 0,
    max_pickaxe_level INT NOT NULL DEFAULT 0,
    description VARCHAR(255) NOT NULL DEFAULT '',
    tier ENUM('common','uncommon','rare','epic','legendary','prestige') NOT NULL DEFAULT 'common',

    INDEX idx_tier (tier),
    INDEX idx_pickaxe (min_pickaxe_level, max_pickaxe_level),
    INDEX idx_weight (weight DESC)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS mining_gear (
    item_id VARCHAR(100) NOT NULL PRIMARY KEY,
    gear_type ENUM('pickaxe','minecart','bag') NOT NULL,
    name VARCHAR(255) NOT NULL,
    level INT NOT NULL DEFAULT 1,
    price BIGINT NOT NULL DEFAULT 0,
    prestige_required INT NOT NULL DEFAULT 0,
    metadata JSON NULL,

    INDEX idx_type_level (gear_type, level)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.3 COMMODITY MARKET
-- ============================================================================

CREATE TABLE IF NOT EXISTS commodity_prices (
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    base_price INT NOT NULL DEFAULT 100,
    current_price INT NOT NULL DEFAULT 100,
    price_modifier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    trend DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (commodity_name, commodity_type),
    INDEX idx_type (commodity_type)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS commodity_price_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    price INT NOT NULL,
    recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_commodity (commodity_name, commodity_type),
    INDEX idx_recorded (recorded_at)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.4 BAZAAR GLOBAL STATE
-- ============================================================================

CREATE TABLE IF NOT EXISTS bazaar_state (
    id INT NOT NULL PRIMARY KEY DEFAULT 1,
    stock_base_price DECIMAL(10,2) NOT NULL DEFAULT 100.00,
    last_refresh TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    active_items JSON NULL,
    CHECK (id = 1)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.5 GAMBLING SINGLETONS
-- ============================================================================

CREATE TABLE IF NOT EXISTS progressive_jackpot (
    id INT NOT NULL DEFAULT 1,
    pool BIGINT NOT NULL DEFAULT 0,
    last_winner_id BIGINT UNSIGNED NULL,
    last_won_amount BIGINT NOT NULL DEFAULT 0,
    last_won_at DATETIME NULL,
    total_won_all_time BIGINT NOT NULL DEFAULT 0,
    times_won INT NOT NULL DEFAULT 0,
    PRIMARY KEY (id)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS jackpot_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    amount BIGINT NOT NULL,
    pool_before BIGINT NOT NULL DEFAULT 0,
    won_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_user (user_id),
    INDEX idx_time (won_at)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.6 WORLD EVENTS
-- ============================================================================

CREATE TABLE IF NOT EXISTS world_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    event_type VARCHAR(64) NOT NULL,
    event_name VARCHAR(128) NOT NULL,
    description TEXT NOT NULL,
    emoji VARCHAR(32) NOT NULL DEFAULT '🌍',
    bonus_type VARCHAR(64) NOT NULL,
    bonus_value DOUBLE NOT NULL DEFAULT 0.0,
    started_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ends_at DATETIME NOT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,

    INDEX idx_active (active, ends_at),
    INDEX idx_type (event_type)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.7 COMMAND STATS (deduplicated — replaces command_stats + command_usage)
-- ============================================================================

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
    INDEX idx_user_command (user_id, command_name),
    INDEX idx_guild_usage (guild_id, used_at),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 3.8 LEADERBOARD CACHE
-- ============================================================================

CREATE TABLE IF NOT EXISTS leaderboard_cache (
    rank_type ENUM('wallet','bank','networth','gambling','fishing') NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL DEFAULT 0,  -- 0 = global
    user_id BIGINT UNSIGNED NOT NULL,
    rank_position INT NOT NULL,
    value BIGINT NOT NULL,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (rank_type, guild_id, user_id),
    INDEX idx_rank (rank_type, guild_id, rank_position),

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- 3.9 ML / PRICING
-- ============================================================================

CREATE TABLE IF NOT EXISTS ml_settings (
    `key` VARCHAR(64) NOT NULL PRIMARY KEY,
    `value` TEXT NOT NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS ml_price_changes (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    bait_level INT NOT NULL,
    adjust BIGINT NOT NULL,
    changed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_bait_level (bait_level),
    INDEX idx_changed_at (changed_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS fishing_logs (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    rod_level INT NOT NULL,
    bait_level INT NOT NULL,
    net_profit BIGINT NOT NULL,
    logged_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    INDEX idx_rod (rod_level),
    INDEX idx_bait (bait_level)
) ENGINE=InnoDB;

-- ============================================================================
-- 3.10 MISC GLOBAL TABLES
-- ============================================================================

CREATE TABLE IF NOT EXISTS patch_notes (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    version VARCHAR(20) NOT NULL,
    notes TEXT NOT NULL,
    author_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    UNIQUE KEY uq_version (version),
    INDEX idx_created (created_at DESC)
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

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

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

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

    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

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

CREATE TABLE IF NOT EXISTS encrypted_identity_cache (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    encrypted_username VARBINARY(512) DEFAULT NULL,
    encrypted_nickname VARBINARY(512) DEFAULT NULL,
    encrypted_avatar VARBINARY(1024) DEFAULT NULL,
    encryption_iv VARBINARY(16) NOT NULL,
    cached_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS encryption_keys (
    key_id INT AUTO_INCREMENT PRIMARY KEY,
    key_purpose VARCHAR(64) NOT NULL,
    encrypted_key VARBINARY(512) NOT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    rotated_at TIMESTAMP NULL DEFAULT NULL,
    UNIQUE KEY uq_purpose_active (key_purpose, active)
) ENGINE=InnoDB;


-- ############################################################################
-- VIEWS
-- ############################################################################

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


-- ############################################################################
-- STORED PROCEDURES
-- ############################################################################

DELIMITER //

-- Unified transfer (handles both global and server economy)
CREATE PROCEDURE IF NOT EXISTS sp_transfer_money(
    IN p_guild_id BIGINT UNSIGNED,  -- 0 = global
    IN from_user BIGINT UNSIGNED,
    IN to_user BIGINT UNSIGNED,
    IN amount BIGINT
)
BEGIN
    DECLARE from_balance BIGINT;
    DECLARE tax_amount BIGINT DEFAULT 0;
    DECLARE tax_enabled BOOLEAN DEFAULT FALSE;
    DECLARE tax_percent DECIMAL(5,2) DEFAULT 0.00;

    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    START TRANSACTION;

    IF p_guild_id = 0 THEN
        -- Global economy transfer
        SELECT wallet INTO from_balance FROM users WHERE user_id = from_user FOR UPDATE;
        IF from_balance < amount THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Insufficient funds';
        END IF;
        UPDATE users SET wallet = wallet - amount WHERE user_id = from_user;
        UPDATE users SET wallet = wallet + amount WHERE user_id = to_user;
    ELSE
        -- Server economy transfer (check tax)
        SELECT enable_tax, transaction_tax_percent
        INTO tax_enabled, tax_percent
        FROM guild_settings
        WHERE guild_id = p_guild_id;

        -- For server economy, user balances are tracked per-guild
        -- The application layer handles server-specific wallet lookups
        -- This procedure handles the global case; server transfers are
        -- handled at the application level with guild_id context
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Server transfers handled at application layer';
    END IF;

    COMMIT;
END //

-- Claim interest (unified)
CREATE PROCEDURE IF NOT EXISTS sp_claim_interest(
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

-- Update leaderboard cache
CREATE PROCEDURE IF NOT EXISTS sp_update_leaderboard_cache(
    IN p_rank_type VARCHAR(20),
    IN p_guild_id BIGINT UNSIGNED  -- 0 = global
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


-- ============================================================================
-- 2.8 INFRACTION SYSTEM
-- ============================================================================

-- Core infraction ledger
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

-- Per-guild infraction configuration
CREATE TABLE IF NOT EXISTS guild_infraction_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    point_timeout DECIMAL(5,2) NOT NULL DEFAULT 0.25,
    point_mute DECIMAL(5,2) NOT NULL DEFAULT 0.50,
    point_kick DECIMAL(5,2) NOT NULL DEFAULT 2.00,
    point_ban DECIMAL(5,2) NOT NULL DEFAULT 5.00,
    point_warn DECIMAL(5,2) NOT NULL DEFAULT 0.10,
    default_duration_timeout INT UNSIGNED NOT NULL DEFAULT 259200,
    default_duration_mute INT UNSIGNED NOT NULL DEFAULT 604800,
    default_duration_kick INT UNSIGNED NOT NULL DEFAULT 1209600,
    default_duration_ban INT UNSIGNED NOT NULL DEFAULT 15552000,
    default_duration_warn INT UNSIGNED NOT NULL DEFAULT 604800,
    escalation_rules JSON NOT NULL DEFAULT ('[]'),
    mute_role_id BIGINT UNSIGNED NULL DEFAULT NULL,
    jail_role_id BIGINT UNSIGNED NULL DEFAULT NULL,
    jail_channel_id BIGINT UNSIGNED NULL DEFAULT NULL,
    log_channel_id BIGINT UNSIGNED NULL DEFAULT NULL,
    dm_on_action BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Extended auto-moderation config
CREATE TABLE IF NOT EXISTS guild_automod_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    account_age_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    account_age_days INT UNSIGNED NOT NULL DEFAULT 7,
    account_age_action VARCHAR(20) NOT NULL DEFAULT 'kick',
    default_avatar_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    default_avatar_action VARCHAR(20) NOT NULL DEFAULT 'kick',
    mutual_servers_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    mutual_servers_min INT UNSIGNED NOT NULL DEFAULT 1,
    mutual_servers_action VARCHAR(20) NOT NULL DEFAULT 'kick',
    nickname_sanitize_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    nickname_sanitize_format VARCHAR(100) NOT NULL DEFAULT 'Moderated Nickname {n}',
    nickname_bad_patterns JSON NOT NULL DEFAULT ('[]'),
    infraction_escalation_enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Role-based permission classes
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

-- Maps Discord roles to permission classes
CREATE TABLE IF NOT EXISTS guild_role_class_members (
    guild_id BIGINT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    class_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (guild_id, role_id),
    INDEX idx_class (class_id),
    CONSTRAINT fk_class_id FOREIGN KEY (class_id)
        REFERENCES guild_role_classes(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- ############################################################################
-- SEED DATA
-- ############################################################################

-- Initialize bazaar state singleton
INSERT INTO bazaar_state (id, stock_base_price) VALUES (1, 100.00)
    ON DUPLICATE KEY UPDATE stock_base_price = stock_base_price;

-- Initialize jackpot singleton
INSERT INTO progressive_jackpot (id, pool) VALUES (1, 0)
    ON DUPLICATE KEY UPDATE pool = pool;

-- Shop items will be migrated from the old schema
-- (see migrate_v2.sql for data migration)


-- ############################################################################
-- PERFORMANCE
-- ############################################################################

ANALYZE TABLE users, user_inventory, user_fish_catches, user_bazaar_stock,
             guild_giveaways, guild_settings, command_stats;


-- ============================================================================
-- END OF SCHEMA V2
-- ============================================================================
