-- Populate dashboard data for Bronx Bot
-- This script adds sample data to make the dashboard functional

-- Add some command usage statistics
INSERT INTO command_stats (command_name, user_id, guild_id, used_at) VALUES
('balance', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 1 HOUR)),
('fish', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 2 HOUR)),
('rob', 270546179639607296, 987654321098765432, DATE_SUB(NOW(), INTERVAL 3 HOUR)),
('daily', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 4 HOUR)),
('work', 282681883249213440, 987654321098765432, DATE_SUB(NOW(), INTERVAL 5 HOUR)),
('gamble', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 6 HOUR)),
('shop', 270546179639607296, 987654321098765432, DATE_SUB(NOW(), INTERVAL 7 HOUR)),
('help', 446095261991829525, 987654321098765432, DATE_SUB(NOW(), INTERVAL 8 HOUR)),
('leaderboard', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 9 HOUR)),
('fish', 270546179639607296, 987654321098765432, DATE_SUB(NOW(), INTERVAL 10 HOUR)),
('balance', 282681883249213440, 987654321098765432, DATE_SUB(NOW(), INTERVAL 11 HOUR)),
('rob', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 12 HOUR));

-- Add fish catches for fishing statistics  
INSERT INTO fish_catches (user_id, fish_name, rarity, weight, value, caught_at, sold, rod_id, bait_id) VALUES
(144968658500714496, 'Bass', 'normal', 2.5, 150, DATE_SUB(NOW(), INTERVAL 1 HOUR), 0, 'basic_rod', 'worm'),
(270546179639607296, 'Salmon', 'rare', 8.2, 750, DATE_SUB(NOW(), INTERVAL 2 HOUR), 0, 'steel_rod', 'minnow'),
(282681883249213440, 'Legendary Tuna', 'legendary', 45.7, 15000, DATE_SUB(NOW(), INTERVAL 3 HOUR), 0, 'golden_rod', 'squid'),
(144968658500714496, 'Trout', 'normal', 3.1, 200, DATE_SUB(NOW(), INTERVAL 4 HOUR), 0, 'basic_rod', 'worm'),
(446095261991829525, 'Pike', 'epic', 12.4, 400, DATE_SUB(NOW(), INTERVAL 5 HOUR), 0, 'steel_rod', 'frog'),
(270546179639607296, 'Marlin', 'rare', 22.8, 1200, DATE_SUB(NOW(), INTERVAL 6 HOUR), 0, 'steel_rod', 'squid');

-- Add shop items if they don't exist
INSERT IGNORE INTO shop_items (item_id, name, description, category, price, level) VALUES
('fishing_rod_basic', 'Basic Fishing Rod', 'A simple rod for catching common fish', 'fishing', 500, 1),
('fishing_rod_steel', 'Steel Fishing Rod', 'Improved rod for better catches', 'fishing', 2500, 5),
('fishing_rod_golden', 'Golden Fishing Rod', 'Premium rod for rare fish', 'fishing', 10000, 15),
('bait_worm', 'Worm Bait', 'Basic bait for pond fishing', 'fishing', 25, 1),
('bait_minnow', 'Minnow Bait', 'Good bait for river fishing', 'fishing', 100, 3),
('bait_squid', 'Squid Bait', 'Premium bait for ocean fishing', 'fishing', 200, 8),
('bank_upgrade_1', 'Bank Upgrade Tier 1', 'Increases bank capacity by 10,000', 'upgrades', 5000, 1),
('bank_upgrade_2', 'Bank Upgrade Tier 2', 'Increases bank capacity by 25,000', 'upgrades', 15000, 5),
('multiplier_2x', '2x Money Multiplier', 'Double your earnings for 1 hour', 'boosts', 7500, 10);

-- Add daily deals
INSERT IGNORE INTO daily_deals (item_id, discount_percent, active_date, max_purchases) VALUES
('fishing_rod_steel', 25, CURDATE(), 50),
('multiplier_2x', 15, CURDATE(), 25);

-- Add guild settings for the main guild
INSERT INTO guild_settings (guild_id, prefix, logging_enabled, logging_channel) VALUES
(987654321098765432, 'b.', true, 1234567890123456789)
ON DUPLICATE KEY UPDATE prefix = VALUES(prefix);

-- Add module settings
INSERT INTO guild_module_settings (guild_id, module, enabled) VALUES
(987654321098765432, 'economy', true),
(987654321098765432, 'fishing', true),
(987654321098765432, 'gambling', true),
(987654321098765432, 'moderation', true),
(987654321098765432, 'fun', true),
(987654321098765432, 'utility', true)
ON DUPLICATE KEY UPDATE enabled = VALUES(enabled);

-- Add some ML settings
INSERT INTO ml_settings (`key`, `value`) VALUES
('auto_price_adjustment', 'true'),
('market_volatility', '0.15'),
('demand_factor', '1.2'),
('supply_factor', '0.8')
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);

-- Add some giveaway data
INSERT INTO giveaways (guild_id, channel_id, prize_amount, max_winners, ends_at, active, created_by) VALUES
(987654321098765432, 1234567890123456789, 50000, 3, DATE_ADD(NOW(), INTERVAL 2 DAY), true, 144968658500714496),
(987654321098765432, 1234567890123456789, 25000, 1, DATE_ADD(NOW(), INTERVAL 1 DAY), true, 144968658500714496);

-- Add giveaway entries
INSERT INTO giveaway_entries (giveaway_id, user_id, entered_at) VALUES
(1, 144968658500714496, NOW()),
(1, 270546179639607296, NOW()),
(1, 282681883249213440, NOW()),
(2, 144968658500714496, NOW()),
(2, 446095261991829525, NOW());

-- Add reaction roles
INSERT IGNORE INTO reaction_roles (message_id, channel_id, emoji_raw, role_id) VALUES
(9876543210987654321, 1234567890123456789, '🎮', 1111111111111111111),
(9876543210987654321, 1234567890123456789, '📚', 2222222222222222222),
(9876543210987654321, 1234567890123456789, '🎵', 3333333333333333333);

-- Add autopurge configuration
INSERT IGNORE INTO autopurges (guild_id, channel_id, max_age_hours, enabled) VALUES
(987654321098765432, 1234567890123456789, 24, true),
(987654321098765432, 9876543210987654321, 48, true);