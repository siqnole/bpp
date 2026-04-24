-- Add more comprehensive sample data for dashboard testing
-- Add varied command statistics with realistic usage patterns
INSERT INTO command_stats (command_name, user_id, guild_id, used_at) VALUES
-- Recent activity (last 24 hours)
('balance', 144968658500714496, 987654321098765432, NOW() - INTERVAL 30 MINUTE),
('shop', 270546179639607296, 987654321098765432, NOW() - INTERVAL 45 MINUTE),
('fish', 282681883249213440, 987654321098765432, NOW() - INTERVAL 1 HOUR),
('leaderboard', 446095261991829525, 987654321098765432, NOW() - INTERVAL 1.5 HOUR),
('gamble', 549699580820979735, 987654321098765432, NOW() - INTERVAL 2 HOUR),
('rob', 563149611137630213, 987654321098765432, NOW() - INTERVAL 2.5 HOUR),
('daily', 588952299808751617, 987654321098765432, NOW() - INTERVAL 3 HOUR),
('work', 597594241652228101, 987654321098765432, NOW() - INTERVAL 3.5 HOUR),
('help', 599736319618318377, 987654321098765432, NOW() - INTERVAL 4 HOUR),
('fish', 144968658500714496, 987654321098765432, NOW() - INTERVAL 4.5 HOUR),
('balance', 270546179639607296, 987654321098765432, NOW() - INTERVAL 5 HOUR),
('shop', 282681883249213440, 987654321098765432, NOW() - INTERVAL 5.5 HOUR),
('gamble', 446095261991829525, 987654321098765432, NOW() - INTERVAL 6 HOUR),
('rob', 549699580820979735, 987654321098765432, NOW() - INTERVAL 7 HOUR),
('leaderboard', 563149611137630213, 987654321098765432, NOW() - INTERVAL 8 HOUR);

-- Add more fish catches with variety
INSERT INTO fish_catches (user_id, fish_name, rarity, weight, value, caught_at, sold, rod_id, bait_id) VALUES
(599736319618318377, 'Goldfish', 'normal', 0.8, 75, NOW() - INTERVAL 30 MINUTE, 0, 'basic_rod', 'worm'),
(588952299808751617, 'Catfish', 'rare', 15.3, 890, NOW() - INTERVAL 1 HOUR, 0, 'steel_rod', 'minnow'),
(597594241652228101, 'Rainbow Trout', 'epic', 6.7, 1350, NOW() - INTERVAL 1.5 HOUR, 1, 'steel_rod', 'fly'),
(549699580820979735, 'Ancient Coelacanth', 'legendary', 78.2, 25000, NOW() - INTERVAL 2 HOUR, 0, 'golden_rod', 'rare_bait'),
(563149611137630213, 'Salmon', 'rare', 9.1, 650, NOW() - INTERVAL 3 HOUR, 1, 'steel_rod', 'minnow'),
(282681883249213440, 'Pufferfish', 'epic', 2.4, 2200, NOW() - INTERVAL 4 HOUR, 0, 'golden_rod', 'squid');

-- Add shop items for the shop management
INSERT IGNORE INTO shop_items (item_id, name, description, category, price, level) VALUES
('auto_fisher_1', 'Auto Fisher Tier 1', 'Automatically fish every 30 minutes', 'automation', 25000, 20),
('auto_fisher_2', 'Auto Fisher Tier 2', 'Automatically fish every 20 minutes', 'automation', 50000, 35),
('lucky_charm', 'Lucky Charm', 'Increases rare fish catch rate by 15%', 'boosts', 12000, 12),
('experience_boost', 'Experience Boost', 'Double XP for 24 hours', 'boosts', 8500, 1),
('fish_radar', 'Fish Radar', 'Shows fish locations for 1 hour', 'tools', 3500, 8),
('premium_bait_box', 'Premium Bait Box', 'Contains 10 random premium baits', 'fishing', 1500, 5);

-- Add some guild balances and settings
INSERT INTO guild_balances (guild_id, balance, total_donated, total_given) VALUES
(987654321098765432, 2500000, 450000, 1200000)
ON DUPLICATE KEY UPDATE 
balance = VALUES(balance),
total_donated = VALUES(total_donated),
total_given = VALUES(total_given);

-- Add autofisher data for fishing statistics
INSERT IGNORE INTO autofishers (user_id, tier, uses_remaining, last_used, active) VALUES
(144968658500714496, 1, 45, NOW() - INTERVAL 1 HOUR, 1),
(270546179639607296, 2, 78, NOW() - INTERVAL 30 MINUTE, 1),
(282681883249213440, 1, 23, NOW() - INTERVAL 2 HOUR, 1);

-- Add gambling statistics
INSERT IGNORE INTO gambling_stats (user_id, game_type, total_bet, total_won, games_played, last_played) VALUES
(144968658500714496, 'blackjack', 15000, 18500, 45, NOW() - INTERVAL 3 HOUR),
(270546179639607296, 'slots', 25000, 19000, 78, NOW() - INTERVAL 1 HOUR),
(282681883249213440, 'roulette', 8000, 12000, 23, NOW() - INTERVAL 2 HOUR),
(446095261991829525, 'coinflip', 5000, 4200, 34, NOW() - INTERVAL 4 HOUR);

-- Add some suggestions for the suggestions panel
INSERT IGNORE INTO suggestions (user_id, suggestion_text, status, submitted_at) VALUES
(144968658500714496, 'Add a lottery system with weekly drawings', 'pending', NOW() - INTERVAL 2 DAY),
(270546179639607296, 'Create fishing tournaments with leaderboards', 'approved', NOW() - INTERVAL 1 DAY),
(282681883249213440, 'Add pet system where pets can help with fishing', 'pending', NOW() - INTERVAL 12 HOUR),
(446095261991829525, 'More gambling games like poker and bingo', 'reviewing', NOW() - INTERVAL 8 HOUR);