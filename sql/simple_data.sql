-- Simplified data population for dashboard
-- Add some command usage statistics first
INSERT INTO command_stats (command_name, user_id, guild_id, used_at) VALUES
('balance', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 1 HOUR)),
('fish', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 2 HOUR)),
('rob', 270546179639607296, 987654321098765432, DATE_SUB(NOW(), INTERVAL 3 HOUR)),
('daily', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 4 HOUR)),
('work', 282681883249213440, 987654321098765432, DATE_SUB(NOW(), INTERVAL 5 HOUR)),
('gamble', 144968658500714496, 987654321098765432, DATE_SUB(NOW(), INTERVAL 6 HOUR)),
('shop', 270546179639607296, 987654321098765432, DATE_SUB(NOW(), INTERVAL 7 HOUR)),
('help', 446095261991829525, 987654321098765432, DATE_SUB(NOW(), INTERVAL 8 HOUR));

-- Add fish catches
INSERT INTO fish_catches (user_id, fish_name, rarity, weight, value, caught_at, sold, rod_id, bait_id) VALUES
(144968658500714496, 'Bass', 'normal', 2.5, 150, DATE_SUB(NOW(), INTERVAL 1 HOUR), 0, 'basic_rod', 'worm'),
(270546179639607296, 'Salmon', 'rare', 8.2, 750, DATE_SUB(NOW(), INTERVAL 2 HOUR), 0, 'steel_rod', 'minnow'),
(282681883249213440, 'Legendary Tuna', 'legendary', 45.7, 15000, DATE_SUB(NOW(), INTERVAL 3 HOUR), 0, 'golden_rod', 'squid'),
(144968658500714496, 'Trout', 'normal', 3.1, 200, DATE_SUB(NOW(), INTERVAL 4 HOUR), 0, 'basic_rod', 'worm');

-- Add daily deals with correct columns
INSERT IGNORE INTO daily_deals (item_id, discount_percent, stock_remaining, active_date) VALUES
('fishing_rod_steel', 25, 50, CURDATE()),
('multiplier_2x', 15, 25, CURDATE());

-- Add some ML settings
INSERT INTO ml_settings (`key`, `value`) VALUES
('auto_price_adjustment', 'true'),
('market_volatility', '0.15')
ON DUPLICATE KEY UPDATE `value` = VALUES(`value`);