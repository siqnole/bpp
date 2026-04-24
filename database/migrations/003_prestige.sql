-- Add prestige column to users table
-- When a user prestiges, their prestige level increases and economy resets

ALTER TABLE users ADD COLUMN prestige INT NOT NULL DEFAULT 0;
CREATE INDEX idx_prestige ON users(prestige DESC);
