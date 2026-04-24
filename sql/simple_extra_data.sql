-- Simple additional data for dashboard
-- Add more command statistics
INSERT INTO command_stats (command_name, user_id, guild_id, used_at) VALUES
('balance', 144968658500714496, 987654321098765432, NOW() - INTERVAL 30 MINUTE),
('shop', 270546179639607296, 987654321098765432, NOW() - INTERVAL 45 MINUTE),
('fish', 282681883249213440, 987654321098765432, NOW() - INTERVAL 1 HOUR),
('leaderboard', 446095261991829525, 987654321098765432, NOW() - INTERVAL 1.5 HOUR),
('help', 549699580820979735, 987654321098765432, NOW() - INTERVAL 2 HOUR);

-- Add more fish catches
INSERT INTO fish_catches (user_id, fish_name, rarity, weight, value, caught_at, sold, rod_id, bait_id) VALUES
(549699580820979735, 'Goldfish', 'normal', 0.8, 75, NOW() - INTERVAL 30 MINUTE, 0, 'basic_rod', 'worm'),
(563149611137630213, 'Catfish', 'rare', 15.3, 890, NOW() - INTERVAL 1 HOUR, 0, 'steel_rod', 'minnow'),
(597594241652228101, 'Rainbow Trout', 'epic', 6.7, 1350, NOW() - INTERVAL 1.5 HOUR, 1, 'steel_rod', 'fly');

-- Add guild balance
INSERT INTO guild_balances (guild_id, balance, total_donated, total_given) VALUES
(987654321098765432, 2500000, 450000, 1200000)
ON DUPLICATE KEY UPDATE 
balance = VALUES(balance),
total_donated = VALUES(total_donated),
total_given = VALUES(total_given);