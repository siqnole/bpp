-- Migration: Add crafting and mastery system support
-- Date: 2026-03-04

-- Add 'crafted' to the inventory item_type ENUM
ALTER TABLE inventory 
    MODIFY COLUMN item_type ENUM('potion', 'upgrade', 'rod', 'bait', 'collectible', 'other', 'automation', 'boosts', 'title', 'tools', 'pickaxe', 'minecart', 'bag', 'crafted') NOT NULL;
