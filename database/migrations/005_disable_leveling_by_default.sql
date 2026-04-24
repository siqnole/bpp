-- Migration: Disable leveling by default for existing servers
-- This migration updates the default value for the enabled column in server_leveling_config
-- and sets existing configs to disabled (servers can opt-in to enable it)

-- First, alter the table to change the default value
ALTER TABLE server_leveling_config 
MODIFY COLUMN enabled BOOLEAN NOT NULL DEFAULT FALSE;

-- Optionally: Update existing records to be disabled (uncomment if you want this)
-- UPDATE server_leveling_config SET enabled = FALSE WHERE enabled = TRUE;

-- Note: If you want to preserve existing servers that have leveling enabled,
-- leave the UPDATE commented. Only new servers will have it disabled by default.
