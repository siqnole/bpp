-- Migration 015: Fix duplicate inventory rows caused by NULL guild_id in UNIQUE key
-- MySQL treats NULL != NULL, so ON DUPLICATE KEY UPDATE never triggered for
-- global economy items (guild_id IS NULL), creating duplicate rows.
--
-- This migration:
-- 1. Merges duplicate rows by summing quantities and keeping the highest level
-- 2. Deletes the extra duplicate rows
-- 3. Does NOT alter the schema (code-level fix handles the NULL upsert issue)

-- Step 1: Create a temp table with the correct merged data for duplicates
CREATE TEMPORARY TABLE _inv_merge AS
SELECT user_id, item_id,
       SUM(quantity) AS total_qty,
       MAX(level)    AS max_level,
       MIN(id)       AS keep_id
FROM user_inventory
WHERE guild_id IS NULL
GROUP BY user_id, item_id
HAVING COUNT(*) > 1;

-- Step 2: Update the row we're keeping with the merged totals
UPDATE user_inventory inv
JOIN _inv_merge m ON inv.id = m.keep_id
SET inv.quantity = m.total_qty,
    inv.level    = m.max_level;

-- Step 3: Delete all duplicate rows except the one we kept
DELETE inv FROM user_inventory inv
JOIN _inv_merge m ON inv.user_id = m.user_id
                  AND inv.item_id = m.item_id
                  AND inv.guild_id IS NULL
                  AND inv.id != m.keep_id;

-- Step 4: Clean up
DROP TEMPORARY TABLE _inv_merge;

-- Verify: This should return 0 rows after migration
-- SELECT user_id, item_id, COUNT(*) as cnt
-- FROM user_inventory WHERE guild_id IS NULL
-- GROUP BY user_id, item_id HAVING cnt > 1;
