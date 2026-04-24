-- ============================================================================
-- Server-Specific Economy Migration
-- Enables guilds to have their own independent economy systems
-- ============================================================================

USE bronxbot;

-- ============================================================================
-- GUILD ECONOMY SETTINGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS guild_economy_settings (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    
    -- Economy mode toggle
    economy_mode ENUM('global', 'server') NOT NULL DEFAULT 'global',
    
    -- Server economy customization
    starting_wallet BIGINT NOT NULL DEFAULT 1000,
    starting_bank_limit BIGINT NOT NULL DEFAULT 10000,
    default_interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    
    -- Command cooldowns (in seconds, 0 = use default)
    daily_cooldown INT NOT NULL DEFAULT 86400,      -- 24 hours
    work_cooldown INT NOT NULL DEFAULT 3600,        -- 1 hour
    beg_cooldown INT NOT NULL DEFAULT 1800,         -- 30 minutes
    rob_cooldown INT NOT NULL DEFAULT 7200,         -- 2 hours
    fish_cooldown INT NOT NULL DEFAULT 60,          -- 1 minute
    
    -- Economy multipliers
    work_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    gambling_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    fishing_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    
    -- Feature toggles
    allow_gambling BOOLEAN NOT NULL DEFAULT TRUE,
    allow_fishing BOOLEAN NOT NULL DEFAULT TRUE,
    allow_trading BOOLEAN NOT NULL DEFAULT TRUE,
    allow_robbery BOOLEAN NOT NULL DEFAULT TRUE,
    
    -- Economy settings
    max_wallet BIGINT NULL DEFAULT NULL,            -- NULL = unlimited
    max_bank BIGINT NULL DEFAULT NULL,              -- NULL = unlimited
    max_networth BIGINT NULL DEFAULT NULL,          -- NULL = unlimited
    
    -- Tax system
    enable_tax BOOLEAN NOT NULL DEFAULT FALSE,
    transaction_tax_percent DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    
    -- Metadata
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    INDEX idx_economy_mode (economy_mode),
    
    CONSTRAINT chk_starting_wallet CHECK (starting_wallet >= 0),
    CONSTRAINT chk_starting_bank_limit CHECK (starting_bank_limit >= 0),
    CONSTRAINT chk_multipliers CHECK (work_multiplier >= 0 AND gambling_multiplier >= 0 AND fishing_multiplier >= 0),
    CONSTRAINT chk_tax CHECK (transaction_tax_percent >= 0 AND transaction_tax_percent <= 100)
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC USER ECONOMY DATA
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_users (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    
    -- Balances
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
    
    -- Metadata
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_active TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_guild_wallet (guild_id, wallet DESC),
    INDEX idx_guild_bank (guild_id, bank DESC),
    INDEX idx_user (user_id),
    INDEX idx_last_active (last_active),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB ROW_FORMAT=COMPRESSED;

-- Add computed column for server networth
ALTER TABLE server_users ADD COLUMN networth BIGINT AS (wallet + bank) STORED;
CREATE INDEX idx_guild_networth ON server_users(guild_id, networth DESC);

-- ============================================================================
-- SERVER-SPECIFIC INVENTORY SYSTEM
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_inventory (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    item_id VARCHAR(100) NOT NULL,
    item_type ENUM('potion', 'upgrade', 'rod', 'bait', 'collectible', 'other') NOT NULL,
    quantity INT NOT NULL DEFAULT 1,
    metadata JSON NULL,
    level INT NOT NULL DEFAULT 1,
    acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE KEY unique_guild_user_item (guild_id, user_id, item_id),
    INDEX idx_guild_user (guild_id, user_id),
    INDEX idx_item_type (item_type),
    INDEX idx_item_id (item_id),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    
    CONSTRAINT chk_server_quantity CHECK (quantity >= 0)
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC FISHING SYSTEM
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_fish_catches (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    rarity ENUM('normal', 'rare', 'epic', 'legendary', 'event', 'mutated') NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    weight DECIMAL(10,2) NOT NULL,
    value BIGINT NOT NULL,
    caught_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    sold BOOLEAN NOT NULL DEFAULT FALSE,
    sold_at TIMESTAMP NULL DEFAULT NULL,
    
    rod_id VARCHAR(100) NULL,
    bait_id VARCHAR(100) NULL,
    
    INDEX idx_guild_user (guild_id, user_id),
    INDEX idx_rarity (rarity),
    INDEX idx_weight (weight DESC),
    INDEX idx_value (value DESC),
    INDEX idx_sold (sold),
    INDEX idx_caught_at (caught_at),
    INDEX idx_guild_user_unsold (guild_id, user_id, sold),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS server_active_fishing_gear (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    active_rod_id VARCHAR(100) NULL,
    active_bait_id VARCHAR(100) NULL,
    
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_rod (active_rod_id),
    INDEX idx_bait (active_bait_id),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS server_autofishers (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    count INT NOT NULL DEFAULT 1,
    efficiency_level INT NOT NULL DEFAULT 1,
    efficiency_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    balance BIGINT NOT NULL DEFAULT 0,
    total_deposited BIGINT NOT NULL DEFAULT 0,
    bag_limit INT NOT NULL DEFAULT 10,
    last_claim TIMESTAMP NULL DEFAULT NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE,
    
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_active (active),
    INDEX idx_balance (balance DESC),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    
    CONSTRAINT chk_server_autofish_count CHECK (count >= 0 AND count <= 30),
    CONSTRAINT chk_server_autofish_balance CHECK (balance >= 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS server_autofish_storage (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    fish_catch_id BIGINT UNSIGNED NOT NULL,
    
    INDEX idx_guild_user (guild_id, user_id),
    
    FOREIGN KEY (guild_id, user_id) REFERENCES server_autofishers(guild_id, user_id) ON DELETE CASCADE,
    FOREIGN KEY (fish_catch_id) REFERENCES server_fish_catches(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC GAMBLING STATS
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_gambling_stats (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    game_type VARCHAR(50) NOT NULL,
    games_played INT NOT NULL DEFAULT 0,
    total_bet BIGINT NOT NULL DEFAULT 0,
    total_won BIGINT NOT NULL DEFAULT 0,
    total_lost BIGINT NOT NULL DEFAULT 0,
    biggest_win BIGINT NOT NULL DEFAULT 0,
    biggest_loss BIGINT NOT NULL DEFAULT 0,
    
    PRIMARY KEY (guild_id, user_id, game_type),
    INDEX idx_game_type (game_type),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC COOLDOWNS
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_cooldowns (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    command VARCHAR(50) NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    
    PRIMARY KEY (guild_id, user_id, command),
    INDEX idx_expires (expires_at),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC TRADING
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_trades (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    initiator_id BIGINT UNSIGNED NOT NULL,
    recipient_id BIGINT UNSIGNED NOT NULL,
    
    initiator_cash BIGINT NOT NULL DEFAULT 0,
    initiator_items JSON NULL,
    
    recipient_cash BIGINT NOT NULL DEFAULT 0,
    recipient_items JSON NULL,
    
    status ENUM('pending', 'accepted', 'rejected', 'cancelled', 'completed') NOT NULL DEFAULT 'pending',
    
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    completed_at TIMESTAMP NULL,
    
    INDEX idx_guild (guild_id),
    INDEX idx_initiator (initiator_id),
    INDEX idx_recipient (recipient_id),
    INDEX idx_status (status),
    INDEX idx_expires (expires_at),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (initiator_id) REFERENCES users(user_id) ON DELETE CASCADE,
    FOREIGN KEY (recipient_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER ECONOMY COMMAND STATS
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_command_stats (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    command_name VARCHAR(100) NOT NULL,
    used_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    INDEX idx_guild (guild_id),
    INDEX idx_user (user_id),
    INDEX idx_command (command_name),
    INDEX idx_used_at (used_at),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- SERVER-SPECIFIC LEADERBOARD CACHE
-- ============================================================================

CREATE TABLE IF NOT EXISTS server_leaderboard_cache (
    guild_id BIGINT UNSIGNED NOT NULL,
    rank_type ENUM('wallet', 'bank', 'networth', 'gambling', 'fishing') NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    rank_position INT NOT NULL,
    value BIGINT NOT NULL,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (guild_id, rank_type, user_id),
    INDEX idx_rank (guild_id, rank_type, rank_position),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- VIEWS FOR SERVER ECONOMY
-- ============================================================================

CREATE OR REPLACE VIEW v_server_user_networth AS
SELECT 
    guild_id,
    user_id,
    wallet,
    bank,
    (wallet + bank) AS networth,
    bank_limit,
    (bank_limit - bank) AS bank_space
FROM server_users;

CREATE OR REPLACE VIEW v_server_fish_statistics AS
SELECT 
    guild_id,
    user_id,
    COUNT(*) as total_caught,
    SUM(CASE WHEN sold THEN 0 ELSE 1 END) as unsold_count,
    SUM(value) as total_value,
    AVG(weight) as avg_weight,
    MAX(weight) as heaviest_fish,
    MAX(value) as most_valuable,
    COUNT(CASE WHEN rarity = 'legendary' THEN 1 END) as legendary_count,
    COUNT(CASE WHEN rarity = 'mutated' THEN 1 END) as mutated_count
FROM server_fish_catches
GROUP BY guild_id, user_id;

-- ============================================================================
-- STORED PROCEDURES FOR SERVER ECONOMY
-- ============================================================================

DELIMITER //

-- Transfer money between users in server economy
CREATE PROCEDURE IF NOT EXISTS sp_server_transfer_money(
    IN p_guild_id BIGINT UNSIGNED,
    IN from_user BIGINT UNSIGNED,
    IN to_user BIGINT UNSIGNED,
    IN amount BIGINT
)
BEGIN
    DECLARE from_balance BIGINT;
    DECLARE tax_amount BIGINT;
    DECLARE tax_enabled BOOLEAN;
    DECLARE tax_percent DECIMAL(5,2);
    
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;
    
    START TRANSACTION;
    
    -- Get tax settings
    SELECT enable_tax, transaction_tax_percent
    INTO tax_enabled, tax_percent
    FROM guild_economy_settings
    WHERE guild_id = p_guild_id;
    
    -- Lock the rows
    SELECT wallet INTO from_balance 
    FROM server_users 
    WHERE guild_id = p_guild_id AND user_id = from_user 
    FOR UPDATE;
    
    IF from_balance < amount THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Insufficient funds';
    END IF;
    
    -- Calculate tax
    IF tax_enabled THEN
        SET tax_amount = FLOOR(amount * (tax_percent / 100.0));
    ELSE
        SET tax_amount = 0;
    END IF;
    
    -- Perform transfer
    UPDATE server_users 
    SET wallet = wallet - amount 
    WHERE guild_id = p_guild_id AND user_id = from_user;
    
    UPDATE server_users 
    SET wallet = wallet + (amount - tax_amount)
    WHERE guild_id = p_guild_id AND user_id = to_user;
    
    COMMIT;
END //

-- Claim interest in server economy
CREATE PROCEDURE IF NOT EXISTS sp_server_claim_interest(
    IN p_guild_id BIGINT UNSIGNED,
    IN p_user_id BIGINT UNSIGNED,
    OUT p_interest_amount BIGINT
)
BEGIN
    DECLARE v_bank BIGINT;
    DECLARE v_interest_rate DECIMAL(5,2);
    
    SELECT bank, interest_rate
    INTO v_bank, v_interest_rate
    FROM server_users
    WHERE guild_id = p_guild_id AND user_id = p_user_id;
    
    SET p_interest_amount = FLOOR(v_bank * (v_interest_rate / 100.0));
    
    UPDATE server_users
    SET bank = bank + p_interest_amount,
        last_interest_claim = CURRENT_TIMESTAMP
    WHERE guild_id = p_guild_id AND user_id = p_user_id;
END //

DELIMITER ;

-- ============================================================================
-- SERVER-SPECIFIC PERMISSIONS
-- ============================================================================

-- Server-specific bot admins (can configure server economy)
CREATE TABLE IF NOT EXISTS server_bot_admins (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    granted_by BIGINT UNSIGNED NOT NULL,
    granted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_user (user_id),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Server-specific bot moderators (can use moderation features)
CREATE TABLE IF NOT EXISTS server_bot_mods (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    granted_by BIGINT UNSIGNED NOT NULL,
    granted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    
    PRIMARY KEY (guild_id, user_id),
    INDEX idx_user (user_id),
    
    FOREIGN KEY (guild_id) REFERENCES guild_economy_settings(guild_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ============================================================================
-- AUTO-CLEANUP EVENT FOR SERVER COOLDOWNS
-- ============================================================================

CREATE EVENT IF NOT EXISTS cleanup_server_cooldowns
ON SCHEDULE EVERY 1 HOUR
DO
    DELETE FROM server_cooldowns WHERE expires_at < NOW();

-- ============================================================================
-- END OF SERVER ECONOMY MIGRATION
-- ============================================================================
