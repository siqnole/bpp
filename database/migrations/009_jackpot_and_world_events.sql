-- Migration 009: Progressive Jackpot & Random World Events
-- Created: 2026-03-04

-- ============================================================
-- Progressive Jackpot Pool
-- Single-row table tracking the global jackpot balance.
-- Every gambling loss contributes 1% to the pool.
-- Any gambling win has a 0.01% chance of triggering the jackpot.
-- ============================================================
CREATE TABLE IF NOT EXISTS progressive_jackpot (
    id              INT         NOT NULL DEFAULT 1,
    pool            BIGINT      NOT NULL DEFAULT 0,
    last_winner_id  BIGINT UNSIGNED NULL,
    last_won_amount BIGINT      NOT NULL DEFAULT 0,
    last_won_at     DATETIME    NULL,
    total_won_all_time BIGINT   NOT NULL DEFAULT 0,
    times_won       INT         NOT NULL DEFAULT 0,
    PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Seed the single row if it doesn't exist
INSERT IGNORE INTO progressive_jackpot (id, pool) VALUES (1, 0);

-- Jackpot win history for the /jackpot history display
CREATE TABLE IF NOT EXISTS jackpot_history (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id         BIGINT UNSIGNED NOT NULL,
    amount          BIGINT      NOT NULL,
    pool_before     BIGINT      NOT NULL DEFAULT 0,
    won_at          DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_jackpot_history_user (user_id),
    INDEX idx_jackpot_history_time (won_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================
-- Random World Events
-- Stores the currently active event (if any) and event history.
-- ============================================================
CREATE TABLE IF NOT EXISTS world_events (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    event_type      VARCHAR(64)   NOT NULL,
    event_name      VARCHAR(128)  NOT NULL,
    description     TEXT          NOT NULL,
    emoji           VARCHAR(32)   NOT NULL DEFAULT '🌍',
    bonus_type      VARCHAR(64)   NOT NULL,       -- e.g. 'fishing_rare', 'mining_gold', 'gambling_payout', 'no_rob'
    bonus_value     DOUBLE        NOT NULL DEFAULT 0.0,  -- multiplier or flat bonus
    started_at      DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ends_at         DATETIME      NOT NULL,
    active          BOOLEAN       NOT NULL DEFAULT TRUE,
    INDEX idx_world_events_active (active, ends_at),
    INDEX idx_world_events_type (event_type)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
