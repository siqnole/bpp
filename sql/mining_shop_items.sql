-- ============================================================================
-- Mining System - Shop Items & Schema Additions
-- Run this after the base schema to add mining gear to the shop
-- ============================================================================

USE bronxbot;

-- Add mining item types to inventory ENUM if not already present
-- (MariaDB allows extending ENUMs by ALTERing the column)
ALTER TABLE inventory MODIFY COLUMN item_type 
    ENUM('potion', 'upgrade', 'rod', 'bait', 'collectible', 'other', 'automation', 'boosts', 'title', 'tools', 'pickaxe', 'minecart', 'bag') NOT NULL;

-- ============================================================================
-- PICKAXES - determines ore types and mining pace (interval)
-- metadata: luck (%), duration (seconds of mining session), multimine (max extra ores per hit)
-- ============================================================================
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- Base pickaxes (levels 1-5)
    ('pickaxe_wood',     'Wooden Pickaxe',    'basic mining tool',                    'pickaxe',    500,    NULL, 0, 1, TRUE, '{"luck":0,"duration":20,"multimine":0}'),
    ('pickaxe_stone',    'Stone Pickaxe',     'slightly better than wood',            'pickaxe',   2000,    NULL, 0, 2, TRUE, '{"luck":5,"duration":25,"multimine":1}'),
    ('pickaxe_iron',     'Iron Pickaxe',      'sturdy iron construction',             'pickaxe',  15000,    NULL, 0, 3, TRUE, '{"luck":10,"duration":30,"multimine":2}'),
    ('pickaxe_gold',     'Golden Pickaxe',    'luxurious and effective',              'pickaxe', 100000,    NULL, 0, 4, TRUE, '{"luck":20,"duration":35,"multimine":3}'),
    ('pickaxe_diamond',  'Diamond Pickaxe',   'cuts through anything',               'pickaxe', 750000,    NULL, 0, 5, TRUE, '{"luck":40,"duration":40,"multimine":4}'),

    -- Prestige pickaxes (P1-P10, levels 7-16)
    ('pickaxe_p1',  'Emberstrike Pickaxe',    'forged in prestige flames',           'pickaxe',   5000000, NULL, 0, 7,  TRUE, '{"luck":50,"duration":40,"multimine":5,"prestige":1}'),
    ('pickaxe_p2',  'Voidbreaker Pickaxe',    'mines through dimensions',            'pickaxe',  25000000, NULL, 0, 8,  TRUE, '{"luck":65,"duration":45,"multimine":7,"prestige":2}'),
    ('pickaxe_p3',  'Phantomsteel Pickaxe',   'phases through stone',                'pickaxe', 100000000, NULL, 0, 9,  TRUE, '{"luck":80,"duration":45,"multimine":9,"prestige":3}'),
    ('pickaxe_p4',  'Starforged Pickaxe',     'starforged mining power',             'pickaxe', 500000000, NULL, 0, 10, TRUE, '{"luck":100,"duration":50,"multimine":11,"prestige":4}'),
    ('pickaxe_p5',  'Worldsplitter Pickaxe',  'ultimate mining instrument',          'pickaxe',2000000000, NULL, 0, 11, TRUE, '{"luck":120,"duration":50,"multimine":13,"prestige":5}'),
    ('pickaxe_p6',  'Abyssal Pickaxe',        'existed before stone itself',         'pickaxe',5000000000, NULL, 0, 12, TRUE, '{"luck":140,"duration":55,"multimine":15,"prestige":6}'),
    ('pickaxe_p7',  'Runegraven Pickaxe',     'enchanted with ancient magic',        'pickaxe',10000000000,NULL, 0, 13, TRUE, '{"luck":160,"duration":55,"multimine":16,"prestige":7}'),
    ('pickaxe_p8',  'Paradox Pickaxe',        'mines in superposition',              'pickaxe',25000000000,NULL, 0, 14, TRUE, '{"luck":180,"duration":60,"multimine":18,"prestige":8}'),
    ('pickaxe_p9',  'Epoch Pickaxe',          'mines across timelines',              'pickaxe',60000000000,NULL, 0, 15, TRUE, '{"luck":200,"duration":60,"multimine":19,"prestige":9}'),
    ('pickaxe_p10', 'Oblivion Pickaxe',       'breaks the boundaries of mining',     'pickaxe',150000000000,NULL,0, 16, TRUE, '{"luck":250,"duration":65,"multimine":20,"prestige":10}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price), name = VALUES(name), description = VALUES(description),
    metadata = VALUES(metadata), level = VALUES(level);

-- ============================================================================
-- MINECARTS - speed (how fast ores fly by) + spawn_rates (ore weight boosts)
-- metadata: speed (ms between ore spawns, lower = faster), spawn_rates {ore:bonus_weight}
-- ============================================================================
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- Base minecarts (levels 1-5)
    ('minecart_rusty',    'Rusty Minecart',     'barely rolls',                       'minecart',    400,   NULL, 0, 1, TRUE, '{"speed":10000,"spawn_rates":{"coal":10}}'),
    ('minecart_wood',     'Wooden Minecart',    'creaky but functional',              'minecart',   1800,   NULL, 0, 2, TRUE, '{"speed":8000,"spawn_rates":{"coal":10,"copper ore":8,"iron ore":5}}'),
    ('minecart_iron',     'Iron Minecart',      'solid and reliable',                 'minecart',  12000,   NULL, 0, 3, TRUE, '{"speed":6500,"spawn_rates":{"silver ore":10,"gold ore":8,"lapis lazuli":5}}'),
    ('minecart_steel',    'Steel Minecart',     'high-speed ore delivery',            'minecart',  80000,   NULL, 0, 4, TRUE, '{"speed":5000,"spawn_rates":{"ruby":10,"sapphire":8,"emerald":6,"platinum ore":5}}'),
    ('minecart_diamond',  'Diamond Minecart',   'frictionless perfection',            'minecart', 600000,   NULL, 0, 5, TRUE, '{"speed":4000,"spawn_rates":{"diamond":10,"mithril ore":8,"iridium ore":6}}'),

    -- Prestige minecarts (P1-P10, levels 7-16)
    ('minecart_p1',  'Blazerail Minecart',     'runs on prestige energy',            'minecart',   4000000, NULL, 0, 7,  TRUE, '{"speed":3500,"spawn_rates":{"molten core":10,"phoenix ember":8},"prestige":1}'),
    ('minecart_p2',  'Wraith Minecart',        'transcends friction',                'minecart',  20000000, NULL, 0, 8,  TRUE, '{"speed":3000,"spawn_rates":{"shadow ore":10,"void shard":8},"prestige":2}'),
    ('minecart_p3',  'Mirage Minecart',        'phases through obstacles',           'minecart',  80000000, NULL, 0, 9,  TRUE, '{"speed":2800,"spawn_rates":{"nebula crystal":10,"astral shard":8},"prestige":3}'),
    ('minecart_p4',  'Comet Minecart',         'starlight-powered rails',            'minecart', 400000000, NULL, 0, 10, TRUE, '{"speed":2500,"spawn_rates":{"stellar diamond":10,"gravity gem":8},"prestige":4}'),
    ('minecart_p5',  'Sanctified Minecart',    'holy momentum',                      'minecart',1500000000, NULL, 0, 11, TRUE, '{"speed":2200,"spawn_rates":{"eternal ore":10,"immortal gem":8},"prestige":5}'),
    ('minecart_p6',  'Genesis Minecart',       'predates physics',                   'minecart',4000000000, NULL, 0, 12, TRUE, '{"speed":2000,"spawn_rates":{"primordial ore":10,"genesis stone":8},"prestige":6}'),
    ('minecart_p7',  'Spellbound Minecart',    'enchanted wheels',                   'minecart',8000000000, NULL, 0, 13, TRUE, '{"speed":1800,"spawn_rates":{"mana ore":10,"arcane crystal":8},"prestige":7}'),
    ('minecart_p8',  'Flux Minecart',          'in multiple places at once',         'minecart',20000000000,NULL, 0, 14, TRUE, '{"speed":1600,"spawn_rates":{"quantum ore":10,"entangled gem":8},"prestige":8}'),
    ('minecart_p9',  'Chrono Minecart',        'arrives before departure',           'minecart',50000000000,NULL, 0, 15, TRUE, '{"speed":1400,"spawn_rates":{"time ore":10,"paradox gem":8},"prestige":9}'),
    ('minecart_p10', 'Eternal Minecart',       'unlimited velocity',                 'minecart',120000000000,NULL,0, 16, TRUE, '{"speed":1200,"spawn_rates":{"infinity ore":10,"omega crystal":8},"prestige":10}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price), name = VALUES(name), description = VALUES(description),
    metadata = VALUES(metadata), level = VALUES(level);

-- ============================================================================
-- BAGS - capacity (max ores per session) + rip_chance (lower = safer)
-- metadata: capacity (int), rip_chance (0.0-1.0, chance bag rips on timeout)
-- ============================================================================
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- Base bags (levels 1-5)
    ('bag_cloth',     'Cloth Bag',       'flimsy but functional',                 'bag',    300,    NULL, 0, 1, TRUE, '{"capacity":5,"rip_chance":0.60}'),
    ('bag_leather',   'Leather Bag',     'holds a decent amount',                 'bag',   1500,    NULL, 0, 2, TRUE, '{"capacity":8,"rip_chance":0.45}'),
    ('bag_canvas',    'Canvas Bag',      'sturdy woven canvas',                   'bag',  10000,    NULL, 0, 3, TRUE, '{"capacity":12,"rip_chance":0.30}'),
    ('bag_iron',      'Iron Bag',        'reinforced metal ore bag',              'bag',  70000,    NULL, 0, 4, TRUE, '{"capacity":18,"rip_chance":0.18}'),
    ('bag_diamond',   'Diamond Bag',     'unrippable crystalline bag',            'bag', 500000,    NULL, 0, 5, TRUE, '{"capacity":25,"rip_chance":0.08}'),

    -- Prestige bags (P1-P10, levels 7-16)
    ('bag_p1',  'Emberweave Bag',       'stitched with prestige thread',          'bag',   3500000, NULL, 0, 7,  TRUE, '{"capacity":30,"rip_chance":0.06,"prestige":1}'),
    ('bag_p2',  'Voidpouch Bag',        'defies material limits',                 'bag',  18000000, NULL, 0, 8,  TRUE, '{"capacity":35,"rip_chance":0.04,"prestige":2}'),
    ('bag_p3',  'Spectral Bag',         'weightless infinite space',              'bag',  70000000, NULL, 0, 9,  TRUE, '{"capacity":42,"rip_chance":0.03,"prestige":3}'),
    ('bag_p4',  'Nebula Bag',           'holds cosmic treasures',                 'bag', 350000000, NULL, 0, 10, TRUE, '{"capacity":50,"rip_chance":0.02,"prestige":4}'),
    ('bag_p5',  'Hallowed Bag',         'blessed container',                      'bag',1200000000, NULL, 0, 11, TRUE, '{"capacity":60,"rip_chance":0.01,"prestige":5}'),
    ('bag_p6',  'Ancient Bag',          'existed before matter',                  'bag',3500000000, NULL, 0, 12, TRUE, '{"capacity":70,"rip_chance":0.008,"prestige":6}'),
    ('bag_p7',  'Grimoire Bag',         'enchanted dimensional pocket',           'bag',7000000000, NULL, 0, 13, TRUE, '{"capacity":80,"rip_chance":0.005,"prestige":7}'),
    ('bag_p8',  'Tesseract Bag',        'larger on the inside',                   'bag',18000000000,NULL, 0, 14, TRUE, '{"capacity":95,"rip_chance":0.003,"prestige":8}'),
    ('bag_p9',  'Riftweave Bag',        'stores across time',                     'bag',45000000000,NULL, 0, 15, TRUE, '{"capacity":110,"rip_chance":0.001,"prestige":9}'),
    ('bag_p10', 'Boundless Bag',        'truly unlimited storage',                'bag',100000000000,NULL,0, 16, TRUE, '{"capacity":130,"rip_chance":0.0005,"prestige":10}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price), name = VALUES(name), description = VALUES(description),
    metadata = VALUES(metadata), level = VALUES(level);
