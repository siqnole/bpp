-- Migration: Add exclusive column to scope settings tables
-- Date: 2026-02-23
-- Description: Adds support for exclusive channel mode where commands/modules
--              can only be used in specific channels when -e flag is set

USE bronxbot;

-- Add exclusive column to command scope settings
ALTER TABLE guild_command_scope_settings 
ADD COLUMN IF NOT EXISTS exclusive BOOLEAN NOT NULL DEFAULT FALSE;

-- Add exclusive column to module scope settings
ALTER TABLE guild_module_scope_settings 
ADD COLUMN IF NOT EXISTS exclusive BOOLEAN NOT NULL DEFAULT FALSE;

-- Verify changes
SELECT 'Migration completed: exclusive column added to scope settings tables' AS status;
