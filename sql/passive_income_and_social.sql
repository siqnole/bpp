-- ============================================================================
-- PASSIVE INCOME & SOCIAL FEATURES MIGRATION
-- Run this to add: Fish Ponds, Mining Claims, Commodity Market, Bank Interest,
--                  Guild Heists, Trading Post improvements
-- ============================================================================

-- ── Fish Ponds ──────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS fish_ponds (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    pond_level INT NOT NULL DEFAULT 1,           -- 1-5, determines capacity
    capacity INT NOT NULL DEFAULT 5,             -- max fish that can be stocked
    last_collect TIMESTAMP NULL DEFAULT NULL,     -- last time coins were collected
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_last_collect (last_collect)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS pond_fish (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    fish_emoji VARCHAR(32) NOT NULL DEFAULT '🐟',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',  -- common/uncommon/rare/epic/legendary/prestige
    base_value INT NOT NULL DEFAULT 10,             -- passive income per cycle
    stocked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    FOREIGN KEY (user_id) REFERENCES fish_ponds(user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Mining Claims ───────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS mining_claims (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    ore_name VARCHAR(100) NOT NULL,
    ore_emoji VARCHAR(32) NOT NULL DEFAULT '⛏️',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',
    yield_min INT NOT NULL DEFAULT 1,               -- min ores per cycle
    yield_max INT NOT NULL DEFAULT 3,               -- max ores per cycle
    ore_value INT NOT NULL DEFAULT 10,               -- value per ore produced
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,                   -- 7 days from purchase
    last_collect TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_user (user_id),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Commodity Market (fluctuating prices for fish & ores) ───────────────────
CREATE TABLE IF NOT EXISTS commodity_prices (
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    base_price INT NOT NULL DEFAULT 100,
    current_price INT NOT NULL DEFAULT 100,
    price_modifier DECIMAL(5,2) NOT NULL DEFAULT 1.00,  -- multiplier (0.70 to 1.30)
    trend DECIMAL(5,2) NOT NULL DEFAULT 0.00,            -- recent direction (-1 to +1)
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (commodity_name, commodity_type),
    INDEX idx_type (commodity_type)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS commodity_price_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    price INT NOT NULL,
    recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_commodity (commodity_name, commodity_type),
    INDEX idx_recorded (recorded_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Guild Heists ────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS heists (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    channel_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    host_id BIGINT UNSIGNED NOT NULL,
    vault_name VARCHAR(100) NOT NULL DEFAULT 'Unknown Vault',
    vault_hp INT NOT NULL DEFAULT 100,
    vault_level INT NOT NULL DEFAULT 1,              -- 1-5 difficulty
    entry_fee BIGINT NOT NULL DEFAULT 5000,
    total_pool BIGINT NOT NULL DEFAULT 0,
    phase ENUM('lobby','active','completed','failed') NOT NULL DEFAULT 'lobby',
    current_round INT NOT NULL DEFAULT 0,
    max_rounds INT NOT NULL DEFAULT 3,
    started_at TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_channel (channel_id),
    INDEX idx_guild (guild_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS heist_participants (
    heist_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    role ENUM('lockpicker','tunneler','hacker','muscle','lookout') NOT NULL DEFAULT 'muscle',
    contribution INT NOT NULL DEFAULT 0,
    alive TINYINT(1) NOT NULL DEFAULT 1,
    PRIMARY KEY (heist_id, user_id),
    FOREIGN KEY (heist_id) REFERENCES heists(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Enhanced Boss Raids (already has boss_raids + raid_participants) ────────
-- Add action_log column if not exists (safe with IF NOT EXISTS pattern)
-- We'll handle this in code with ALTER TABLE IF NOT EXISTS pattern
