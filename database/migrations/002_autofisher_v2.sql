-- Migration: Add Autofisher v2 columns
-- These columns enable dedicated rod/bait for the autofisher and auto-sell features

-- Add autofisher gear columns
ALTER TABLE autofishers
    ADD COLUMN IF NOT EXISTS af_rod_id VARCHAR(100) DEFAULT NULL AFTER active,
    ADD COLUMN IF NOT EXISTS af_bait_id VARCHAR(100) DEFAULT NULL AFTER af_rod_id,
    ADD COLUMN IF NOT EXISTS af_bait_qty INT NOT NULL DEFAULT 0 AFTER af_bait_id,
    ADD COLUMN IF NOT EXISTS af_bait_level INT NOT NULL DEFAULT 1 AFTER af_bait_qty,
    ADD COLUMN IF NOT EXISTS af_bait_meta TEXT DEFAULT NULL AFTER af_bait_level;

-- Add autofisher economy settings
ALTER TABLE autofishers
    ADD COLUMN IF NOT EXISTS max_bank_draw BIGINT NOT NULL DEFAULT 0 AFTER af_bait_meta,
    ADD COLUMN IF NOT EXISTS auto_sell BOOLEAN NOT NULL DEFAULT FALSE AFTER max_bank_draw,
    ADD COLUMN IF NOT EXISTS as_trigger VARCHAR(16) NOT NULL DEFAULT 'bag' AFTER auto_sell,
    ADD COLUMN IF NOT EXISTS as_threshold BIGINT NOT NULL DEFAULT 0 AFTER as_trigger;

-- Change default for active to FALSE (users must explicitly start)
ALTER TABLE autofishers MODIFY COLUMN active BOOLEAN NOT NULL DEFAULT FALSE;
