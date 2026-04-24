-- ============================================================================
-- Mining System - Full Migration
-- Adds ore_types + mining_gear lookup tables, shop items, and mining_claims
-- Run on the Aiven (servered) database after base schema migration
-- ============================================================================

SET NAMES utf8mb4;
SET CHARACTER SET utf8mb4;

-- ----------------------------------------------------------------------------
-- 1. Add 'crafted' to inventory item_type ENUM if missing
-- ----------------------------------------------------------------------------
ALTER TABLE inventory MODIFY COLUMN item_type
    ENUM('potion','upgrade','rod','bait','collectible','other',
         'automation','boosts','title','tools','pickaxe','minecart','bag','crafted') NOT NULL;

-- ----------------------------------------------------------------------------
-- 2. Ore Types Lookup Table  (mirrors mining_helpers.h ore_types vector)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ore_types (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name        VARCHAR(100) NOT NULL UNIQUE,
    emoji       VARCHAR(32)  NOT NULL,
    weight      INT          NOT NULL COMMENT 'spawn weight (higher = more common)',
    min_value   BIGINT       NOT NULL,
    max_value   BIGINT       NOT NULL,
    effect      ENUM('None','Flat','Exponential','Logarithmic','NLogN','Wacky',
                     'Jackpot','Critical','Volatile','Surge','Diminishing',
                     'Cascading','Wealthy','Banker','Miner','Merchant',
                     'Ascended','Collector','Persistent') NOT NULL DEFAULT 'None',
    effect_chance    DOUBLE  NOT NULL DEFAULT 0.0,
    min_pickaxe_level INT   NOT NULL DEFAULT 0,
    max_pickaxe_level INT   NOT NULL DEFAULT 0 COMMENT '0 = no upper limit',
    description VARCHAR(255) NOT NULL DEFAULT '',
    tier        ENUM('common','uncommon','rare','epic','legendary','prestige') NOT NULL DEFAULT 'common',

    INDEX idx_tier (tier),
    INDEX idx_pickaxe (min_pickaxe_level, max_pickaxe_level),
    INDEX idx_weight (weight DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Common tier (pickaxe level 0) ──────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('stone',           '🪨', 250,  5,      20,    'None',        0.0,  0, 10, 'plain old rock',              'common'),
('coal',            '⬛', 220,  10,     35,    'None',        0.03, 0, 10, 'fuel for the furnace',        'common'),
('copper ore',      '🟤', 200,  15,     50,    'Flat',        0.08, 0, 10, 'ductile orange metal',        'common'),
('tin ore',         '⬜', 190,  12,     40,    'None',        0.04, 0, 10, 'soft silvery metal',          'common'),
('clay',            '🟫', 210,  8,      25,    'None',        0.02, 0, 10, 'good for pottery',            'common'),
('flint',           '🔘', 180,  18,     55,    'Cascading',   0.06, 0, 10, 'sharp when chipped',          'common'),
('sandstone',       '🟡', 195,  10,     30,    'None',        0.03, 0, 10, 'layered sedimentary rock',    'common'),
('quartz',          '💎', 160,  25,     70,    'Flat',        0.09, 0, 10, 'crystalline silica',          'common'),
('iron ore',        '🔩', 150,  30,     90,    'Flat',        0.1,  0, 10, 'backbone of civilization',    'common'),
('mica',            '✨', 175,  14,     42,    'Cascading',   0.07, 0, 10, 'shimmering mineral flakes',   'common'),
('salt crystal',    '🧊', 185,  12,     38,    'None',        0.04, 0, 10, 'crystallized NaCl',           'common'),
('limestone',       '⬜', 170,  20,     60,    'None',        0.05, 0, 10, 'ancient sea floors',          'common'),
('granite',         '🪨', 165,  22,     65,    'Flat',        0.06, 0, 10, 'tough igneous rock',          'common'),
('obsidian shard',  '🖤', 140,  35,     100,   'Critical',    0.1,  0, 10, 'volcanic glass fragment',     'common'),
('pyrite',          '🌟', 155,  28,     80,    'Jackpot',     0.12, 0, 10, 'fool''s gold',                'common')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Uncommon tier (pickaxe level 1) ────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('silver ore',      '🥈', 95,   80,     250,   'Flat',        0.1,  1, 10, 'precious shiny metal',        'uncommon'),
('gold ore',        '🥇', 75,   120,    380,   'Logarithmic', 0.12, 1, 10, 'the ultimate currency',       'uncommon'),
('lead ore',        '⬛', 105,  60,     180,   'None',        0.06, 1, 10, 'dense heavy metal',           'uncommon'),
('zinc ore',        '🔘', 100,  65,     200,   'Cascading',   0.09, 1, 10, 'galvanizing mineral',         'uncommon'),
('nickel ore',      '⚪', 90,   90,     275,   'NLogN',       0.1,  1, 10, 'magnetic silvery metal',      'uncommon'),
('cobalt ore',      '🔵', 80,   100,    320,   'Flat',        0.11, 1, 10, 'brilliant blue tint',         'uncommon'),
('lapis lazuli',    '💙', 70,   130,    400,   'NLogN',       0.12, 1, 0,  'deep blue gemstone',          'uncommon'),
('topaz',           '🟡', 65,   150,    450,   'Wacky',       0.13, 1, 0,  'golden brilliance',           'uncommon'),
('garnet',          '🔴', 60,   160,    500,   'Volatile',    0.14, 1, 0,  'deep crimson crystal',        'uncommon'),
('amethyst',        '🟣', 55,   180,    550,   'Exponential', 0.12, 1, 0,  'royal purple quartz',         'uncommon'),
('jade',            '🟢', 68,   140,    420,   'Logarithmic', 0.1,  1, 10, 'eastern treasure stone',      'uncommon'),
('marble slab',     '🏛️', 88,   95,     290,   'Flat',        0.08, 1, 10, 'smooth architectural stone',  'uncommon')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Rare tier (pickaxe level 2) ────────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('platinum ore',    '⚪', 42,   350,    900,   'NLogN',       0.14, 2, 0,  'rarer than gold',             'rare'),
('titanium ore',    '🛡️', 38,   400,    1000,  'Logarithmic', 0.12, 2, 0,  'aerospace-grade metal',       'rare'),
('ruby',            '❤️', 30,   500,    1400,  'Critical',    0.16, 2, 0,  'blood-red gemstone',          'rare'),
('sapphire',        '💙', 28,   550,    1500,  'Wacky',       0.15, 2, 0,  'cornflower blue brilliance',  'rare'),
('emerald',         '💚', 25,   600,    1800,  'Exponential', 0.14, 2, 0,  'vivid green treasure',        'rare'),
('opal',            '🌈', 32,   450,    1200,  'Volatile',    0.17, 2, 0,  'plays with every color',      'rare'),
('aquamarine',      '🩵', 35,   380,    1050,  'Flat',        0.11, 2, 0,  'sea-blue crystal',            'rare'),
('tungsten ore',    '⚙️', 40,   320,    850,   'Surge',       0.13, 2, 0,  'incredibly dense metal',      'rare'),
('meteorite shard', '☄️', 22,   650,    1900,  'Jackpot',     0.2,  2, 0,  'fell from the sky',           'rare'),
('ancient fossil',  '🦴', 36,   380,    1000,  'Diminishing', 0.12, 2, 10, 'millions of years old',       'rare')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Epic tier (pickaxe level 3) ────────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('diamond',         '💎', 15,   1200,   4000,  'Critical',    0.2,  3, 0,  'hardest natural material',    'epic'),
('alexandrite',     '💜', 12,   1500,   5000,  'Wacky',       0.18, 3, 0,  'color-changing marvel',       'epic'),
('black opal',      '🖤', 10,   1800,   6000,  'Volatile',    0.2,  3, 0,  'rarest opal variety',         'epic'),
('palladium ore',   '🪙', 14,   1300,   4500,  'NLogN',       0.16, 3, 0,  'catalyst metal',              'epic'),
('iridium ore',     '🌌', 8,    2000,   7000,  'Exponential', 0.18, 3, 0,  'densest natural element',     'epic'),
('mithril ore',     '🔮', 6,    2500,   8000,  'Surge',       0.15, 3, 0,  'lighter than silk, harder than steel', 'epic'),
('osmium ore',      '⚫', 9,    1900,   6500,  'Logarithmic', 0.14, 3, 0,  'densest element known',       'epic'),
('star sapphire',   '⭐', 7,    2200,   7500,  'Cascading',   0.19, 3, 0,  'displays a six-rayed star',   'epic'),
('dragon stone',    '🐉', 5,    3000,   10000, 'Jackpot',     0.22, 3, 0,  'formed in volcanic hearts',   'epic')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Legendary tier (pickaxe level 4-5) ─────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('philosopher''s stone','🔴', 3,  5000,   15000, 'Exponential', 0.25, 4, 0,  'transmutes base metals',      'legendary'),
('void crystal',    '🕳️', 2,   8000,   25000, 'Wacky',       0.22, 4, 0,  'contains nothingness',        'legendary'),
('unobtanium',      '💫', 2,   10000,  30000, 'Jackpot',     0.28, 4, 0,  'theoretically impossible',    'legendary'),
('celestial ore',   '🌠', 1,   15000,  50000, 'Cascading',   0.3,  4, 0,  'fallen from the heavens',     'legendary'),
('adamantite',      '🛡️', 2,   12000,  35000, 'NLogN',       0.24, 4, 0,  'indestructible alloy',        'legendary'),
('world core shard','🌍', 1,   20000,  60000, 'Surge',       0.26, 5, 0,  'fragment of earth''s heart',  'legendary'),
('stardust ore',    '✨', 1,   25000,  80000, 'Exponential', 0.3,  5, 0,  'cosmic particles condensed',  'legendary'),
('eternity gem',    '♾️', 1,   30000,  100000,'Wacky',       0.35, 5, 0,  'time frozen in crystal',      'legendary')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P1 (level 7) ────────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('molten core',     '🔥', 4,   15000,  50000,  'Exponential', 0.2,  7, 0,  'birthed in a prestige furnace',  'prestige'),
('phoenix ember',   '🕊️', 3,   18000,  60000,  'Wacky',       0.22, 7, 0,  'ashes of rebirth',               'prestige'),
('infernal ruby',   '♦️', 5,   16000,  55000,  'NLogN',       0.21, 7, 0,  'forged in the reset fire',       'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P2 (level 8) ────────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('shadow ore',      '🌑', 3,   30000,  100000, 'Wacky',       0.25, 8, 0,  'absorbs all light',              'prestige'),
('void shard',      '⬛', 2,   35000,  120000, 'Exponential', 0.27, 8, 0,  'fragment of the abyss',          'prestige'),
('dark matter',     '🔮', 4,   32000,  110000, 'NLogN',       0.26, 8, 0,  'mysterious cosmic substance',    'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P3 (level 9) ────────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('nebula crystal',  '🌌', 3,   60000,  200000, 'Miner',       0.25, 9, 0,  'stardust compressed',            'prestige'),
('astral shard',    '✨', 2,   70000,  240000, 'Collector',   0.26, 9, 0,  'galactic energy condensed',      'prestige'),
('cosmic topaz',    '🌈', 4,   65000,  220000, 'Wealthy',     0.24, 9, 0,  'shimmers with starlight',        'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P4 (level 10) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('stellar diamond', '🌠', 3,   120000, 400000, 'Exponential', 0.3,  10, 0, 'harder than spacetime',          'prestige'),
('gravity gem',     '⚫', 2,   140000, 480000, 'NLogN',       0.32, 10, 0, 'bends light around itself',      'prestige'),
('solar topaz',     '☀️', 4,   130000, 440000, 'Wacky',       0.31, 10, 0, 'contains a tiny sun',            'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P5 (level 11) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('eternal ore',     '💫', 2,   250000, 1000000, 'Ascended',   0.4,  11, 0, 'transcends time itself',         'prestige'),
('immortal gem',    '👼', 1,   300000, 1200000, 'Persistent', 0.38, 11, 0, 'blessed with endless life',      'prestige'),
('divine crystal',  '🏨', 3,   275000, 1100000, 'Banker',     0.36, 11, 0, 'forged by the gods',             'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P6 (level 12) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('primordial ore',  '🌊', 3,   500000,  2500000, 'Exponential', 0.25, 12, 0, 'predates the universe',        'prestige'),
('genesis stone',   '🐋', 1,   750000,  3000000, 'NLogN',       0.22, 12, 0, 'first mineral ever formed',    'prestige'),
('origin crystal',  '🐍', 2,   600000,  2800000, 'Wacky',       0.27, 12, 0, 'seed of all gemstones',        'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P7 (level 13) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('mana ore',        '🔮', 3,   1000000, 5000000, 'Wacky',       0.28, 13, 0, 'pure magical energy',          'prestige'),
('arcane crystal',  '⚗️', 1,   1500000, 6000000, 'Exponential', 0.25, 13, 0, 'woven from ancient spells',    'prestige'),
('spell stone',     '📜', 2,   1200000, 5500000, 'NLogN',       0.26, 13, 0, 'enchanted mineral',            'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P8 (level 14) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('quantum ore',     '📦', 2,   2000000, 10000000, 'Volatile',   0.4,  14, 0, 'exists in superposition',      'prestige'),
('entangled gem',   '🔗', 1,   2500000, 12000000, 'Cascading',  0.35, 14, 0, 'two crystals linked',          'prestige'),
('superposition',   '👻', 3,   2200000, 11000000, 'Jackpot',    0.38, 14, 0, 'both here and not here',       'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P9 (level 15) ───────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('time ore',        '⏰', 3,   5000000, 25000000, 'NLogN',      0.3,  15, 0, 'frozen temporal energy',       'prestige'),
('paradox gem',     '🔄', 1,   6000000, 30000000, 'Wacky',      0.32, 15, 0, 'defies causality',             'prestige'),
('temporal shard',  '⚙️', 2,   5500000, 28000000, 'Exponential',0.31, 15, 0, 'warps spacetime',              'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ── Prestige tier P10 (level 16) ──────────────────────────────────────────
INSERT INTO ore_types (name, emoji, weight, min_value, max_value, effect, effect_chance, min_pickaxe_level, max_pickaxe_level, description, tier) VALUES
('infinity ore',    '♾️', 2,   10000000,50000000,  'Ascended',   0.5,  16, 0, 'beyond all boundaries',        'prestige'),
('omega crystal',   '🔱', 1,   12000000,60000000,  'Persistent', 0.48, 16, 0, 'the final mineral',            'prestige'),
('limit breaker gem','🚀',3,   11000000,55000000,  'Collector',  0.46, 16, 0, 'shatters constraints',         'prestige')
ON DUPLICATE KEY UPDATE weight=VALUES(weight), min_value=VALUES(min_value), max_value=VALUES(max_value),
    effect=VALUES(effect), effect_chance=VALUES(effect_chance), min_pickaxe_level=VALUES(min_pickaxe_level),
    max_pickaxe_level=VALUES(max_pickaxe_level), description=VALUES(description), tier=VALUES(tier);

-- ----------------------------------------------------------------------------
-- 3. Mining Gear Lookup Table (pickaxes, minecarts, bags)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS mining_gear (
    item_id     VARCHAR(100) NOT NULL PRIMARY KEY,
    gear_type   ENUM('pickaxe','minecart','bag') NOT NULL,
    name        VARCHAR(255) NOT NULL,
    description VARCHAR(500) NOT NULL DEFAULT '',
    level       INT          NOT NULL DEFAULT 1,
    price       BIGINT       NOT NULL DEFAULT 0,
    prestige    INT          NOT NULL DEFAULT 0 COMMENT '0 = no prestige requirement',
    metadata    JSON         NOT NULL COMMENT 'pickaxe:{luck,duration,multimine} / minecart:{speed,spawn_rates} / bag:{capacity,rip_chance}',

    INDEX idx_type_level (gear_type, level),
    INDEX idx_prestige (prestige)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Pickaxes ───────────────────────────────────────────────────────────────
INSERT INTO mining_gear (item_id, gear_type, name, description, level, price, prestige, metadata) VALUES
('pickaxe_wood',     'pickaxe', 'Wooden Pickaxe',       'basic mining tool',                    1,    500,           0, '{"luck":0,"duration":20,"multimine":0}'),
('pickaxe_stone',    'pickaxe', 'Stone Pickaxe',        'slightly better than wood',            2,    2000,          0, '{"luck":5,"duration":25,"multimine":1}'),
('pickaxe_iron',     'pickaxe', 'Iron Pickaxe',         'sturdy iron construction',             3,    15000,         0, '{"luck":10,"duration":30,"multimine":2}'),
('pickaxe_gold',     'pickaxe', 'Golden Pickaxe',       'luxurious and effective',              4,    100000,        0, '{"luck":20,"duration":35,"multimine":3}'),
('pickaxe_diamond',  'pickaxe', 'Diamond Pickaxe',      'cuts through anything',                5,    750000,        0, '{"luck":40,"duration":40,"multimine":4}'),
('pickaxe_p1',       'pickaxe', 'Emberstrike Pickaxe',  'forged in prestige flames',            7,    5000000,       1, '{"luck":50,"duration":40,"multimine":5,"prestige":1}'),
('pickaxe_p2',       'pickaxe', 'Voidbreaker Pickaxe',  'mines through dimensions',             8,    25000000,      2, '{"luck":65,"duration":45,"multimine":7,"prestige":2}'),
('pickaxe_p3',       'pickaxe', 'Phantomsteel Pickaxe', 'phases through stone',                 9,    100000000,     3, '{"luck":80,"duration":45,"multimine":9,"prestige":3}'),
('pickaxe_p4',       'pickaxe', 'Starforged Pickaxe',   'starforged mining power',              10,   500000000,     4, '{"luck":100,"duration":50,"multimine":11,"prestige":4}'),
('pickaxe_p5',       'pickaxe', 'Worldsplitter Pickaxe','ultimate mining instrument',           11,   2000000000,    5, '{"luck":120,"duration":50,"multimine":13,"prestige":5}'),
('pickaxe_p6',       'pickaxe', 'Abyssal Pickaxe',      'existed before stone itself',          12,   5000000000,    6, '{"luck":140,"duration":55,"multimine":15,"prestige":6}'),
('pickaxe_p7',       'pickaxe', 'Runegraven Pickaxe',   'enchanted with ancient magic',         13,   10000000000,   7, '{"luck":160,"duration":55,"multimine":16,"prestige":7}'),
('pickaxe_p8',       'pickaxe', 'Paradox Pickaxe',      'mines in superposition',               14,   25000000000,   8, '{"luck":180,"duration":60,"multimine":18,"prestige":8}'),
('pickaxe_p9',       'pickaxe', 'Epoch Pickaxe',        'mines across timelines',               15,   60000000000,   9, '{"luck":200,"duration":60,"multimine":19,"prestige":9}'),
('pickaxe_p10',      'pickaxe', 'Oblivion Pickaxe',     'breaks the boundaries of mining',      16,   150000000000, 10, '{"luck":250,"duration":65,"multimine":20,"prestige":10}')
ON DUPLICATE KEY UPDATE name=VALUES(name), description=VALUES(description), level=VALUES(level),
    price=VALUES(price), prestige=VALUES(prestige), metadata=VALUES(metadata);

-- ── Minecarts ──────────────────────────────────────────────────────────────
INSERT INTO mining_gear (item_id, gear_type, name, description, level, price, prestige, metadata) VALUES
('minecart_rusty',   'minecart', 'Rusty Minecart',      'barely rolls',                         1,    400,           0, '{"speed":10000,"spawn_rates":{"coal":10}}'),
('minecart_wood',    'minecart', 'Wooden Minecart',     'creaky but functional',                2,    1800,          0, '{"speed":8000,"spawn_rates":{"coal":10,"copper ore":8,"iron ore":5}}'),
('minecart_iron',    'minecart', 'Iron Minecart',       'solid and reliable',                   3,    12000,         0, '{"speed":6500,"spawn_rates":{"silver ore":10,"gold ore":8,"lapis lazuli":5}}'),
('minecart_steel',   'minecart', 'Steel Minecart',      'high-speed ore delivery',              4,    80000,         0, '{"speed":5000,"spawn_rates":{"ruby":10,"sapphire":8,"emerald":6,"platinum ore":5}}'),
('minecart_diamond', 'minecart', 'Diamond Minecart',    'frictionless perfection',              5,    600000,        0, '{"speed":4000,"spawn_rates":{"diamond":10,"mithril ore":8,"iridium ore":6}}'),
('minecart_p1',      'minecart', 'Blazerail Minecart',  'runs on prestige energy',              7,    4000000,       1, '{"speed":3500,"spawn_rates":{"molten core":10,"phoenix ember":8},"prestige":1}'),
('minecart_p2',      'minecart', 'Wraith Minecart',     'transcends friction',                  8,    20000000,      2, '{"speed":3000,"spawn_rates":{"shadow ore":10,"void shard":8},"prestige":2}'),
('minecart_p3',      'minecart', 'Mirage Minecart',     'phases through obstacles',             9,    80000000,      3, '{"speed":2800,"spawn_rates":{"nebula crystal":10,"astral shard":8},"prestige":3}'),
('minecart_p4',      'minecart', 'Comet Minecart',      'starlight-powered rails',              10,   400000000,     4, '{"speed":2500,"spawn_rates":{"stellar diamond":10,"gravity gem":8},"prestige":4}'),
('minecart_p5',      'minecart', 'Sanctified Minecart', 'holy momentum',                        11,   1500000000,    5, '{"speed":2200,"spawn_rates":{"eternal ore":10,"immortal gem":8},"prestige":5}'),
('minecart_p6',      'minecart', 'Genesis Minecart',    'predates physics',                     12,   4000000000,    6, '{"speed":2000,"spawn_rates":{"primordial ore":10,"genesis stone":8},"prestige":6}'),
('minecart_p7',      'minecart', 'Spellbound Minecart', 'enchanted wheels',                     13,   8000000000,    7, '{"speed":1800,"spawn_rates":{"mana ore":10,"arcane crystal":8},"prestige":7}'),
('minecart_p8',      'minecart', 'Flux Minecart',       'in multiple places at once',           14,   20000000000,   8, '{"speed":1600,"spawn_rates":{"quantum ore":10,"entangled gem":8},"prestige":8}'),
('minecart_p9',      'minecart', 'Chrono Minecart',     'arrives before departure',             15,   50000000000,   9, '{"speed":1400,"spawn_rates":{"time ore":10,"paradox gem":8},"prestige":9}'),
('minecart_p10',     'minecart', 'Eternal Minecart',    'unlimited velocity',                   16,   120000000000, 10, '{"speed":1200,"spawn_rates":{"infinity ore":10,"omega crystal":8},"prestige":10}')
ON DUPLICATE KEY UPDATE name=VALUES(name), description=VALUES(description), level=VALUES(level),
    price=VALUES(price), prestige=VALUES(prestige), metadata=VALUES(metadata);

-- ── Bags ───────────────────────────────────────────────────────────────────
INSERT INTO mining_gear (item_id, gear_type, name, description, level, price, prestige, metadata) VALUES
('bag_cloth',        'bag', 'Cloth Bag',         'flimsy but functional',                  1,    300,           0, '{"capacity":5,"rip_chance":0.60}'),
('bag_leather',      'bag', 'Leather Bag',       'holds a decent amount',                  2,    1500,          0, '{"capacity":8,"rip_chance":0.45}'),
('bag_canvas',       'bag', 'Canvas Bag',        'sturdy woven canvas',                    3,    10000,         0, '{"capacity":12,"rip_chance":0.30}'),
('bag_iron',         'bag', 'Iron Bag',          'reinforced metal ore bag',               4,    70000,         0, '{"capacity":18,"rip_chance":0.18}'),
('bag_diamond',      'bag', 'Diamond Bag',       'unrippable crystalline bag',             5,    500000,        0, '{"capacity":25,"rip_chance":0.08}'),
('bag_p1',           'bag', 'Emberweave Bag',    'stitched with prestige thread',          7,    3500000,       1, '{"capacity":30,"rip_chance":0.06,"prestige":1}'),
('bag_p2',           'bag', 'Voidpouch Bag',     'defies material limits',                 8,    18000000,      2, '{"capacity":35,"rip_chance":0.04,"prestige":2}'),
('bag_p3',           'bag', 'Spectral Bag',      'weightless infinite space',              9,    70000000,      3, '{"capacity":42,"rip_chance":0.03,"prestige":3}'),
('bag_p4',           'bag', 'Nebula Bag',        'holds cosmic treasures',                 10,   350000000,     4, '{"capacity":50,"rip_chance":0.02,"prestige":4}'),
('bag_p5',           'bag', 'Hallowed Bag',      'blessed container',                      11,   1200000000,    5, '{"capacity":60,"rip_chance":0.01,"prestige":5}'),
('bag_p6',           'bag', 'Ancient Bag',       'existed before matter',                  12,   3500000000,    6, '{"capacity":70,"rip_chance":0.008,"prestige":6}'),
('bag_p7',           'bag', 'Grimoire Bag',      'enchanted dimensional pocket',           13,   7000000000,    7, '{"capacity":80,"rip_chance":0.005,"prestige":7}'),
('bag_p8',           'bag', 'Tesseract Bag',     'larger on the inside',                   14,   18000000000,   8, '{"capacity":95,"rip_chance":0.003,"prestige":8}'),
('bag_p9',           'bag', 'Riftweave Bag',     'stores across time',                     15,   45000000000,   9, '{"capacity":110,"rip_chance":0.001,"prestige":9}'),
('bag_p10',          'bag', 'Boundless Bag',     'truly unlimited storage',                16,   100000000000, 10, '{"capacity":130,"rip_chance":0.0005,"prestige":10}')
ON DUPLICATE KEY UPDATE name=VALUES(name), description=VALUES(description), level=VALUES(level),
    price=VALUES(price), prestige=VALUES(prestige), metadata=VALUES(metadata);

-- ----------------------------------------------------------------------------
-- 4. Populate shop_items with mining gear (so /shop shows them)
-- ----------------------------------------------------------------------------
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- Pickaxes
    ('pickaxe_wood',     'Wooden Pickaxe',    'basic mining tool',                    'pickaxe',    500,    NULL, 0, 1, TRUE, '{"luck":0,"duration":20,"multimine":0}'),
    ('pickaxe_stone',    'Stone Pickaxe',     'slightly better than wood',            'pickaxe',   2000,    NULL, 0, 2, TRUE, '{"luck":5,"duration":25,"multimine":1}'),
    ('pickaxe_iron',     'Iron Pickaxe',      'sturdy iron construction',             'pickaxe',  15000,    NULL, 0, 3, TRUE, '{"luck":10,"duration":30,"multimine":2}'),
    ('pickaxe_gold',     'Golden Pickaxe',    'luxurious and effective',              'pickaxe', 100000,    NULL, 0, 4, TRUE, '{"luck":20,"duration":35,"multimine":3}'),
    ('pickaxe_diamond',  'Diamond Pickaxe',   'cuts through anything',               'pickaxe', 750000,    NULL, 0, 5, TRUE, '{"luck":40,"duration":40,"multimine":4}'),
    ('pickaxe_p1',  'Emberstrike Pickaxe',    'forged in prestige flames',           'pickaxe',   5000000, NULL, 0, 7,  TRUE, '{"luck":50,"duration":40,"multimine":5,"prestige":1}'),
    ('pickaxe_p2',  'Voidbreaker Pickaxe',    'mines through dimensions',            'pickaxe',  25000000, NULL, 0, 8,  TRUE, '{"luck":65,"duration":45,"multimine":7,"prestige":2}'),
    ('pickaxe_p3',  'Phantomsteel Pickaxe',   'phases through stone',                'pickaxe', 100000000, NULL, 0, 9,  TRUE, '{"luck":80,"duration":45,"multimine":9,"prestige":3}'),
    ('pickaxe_p4',  'Starforged Pickaxe',     'starforged mining power',             'pickaxe', 500000000, NULL, 0, 10, TRUE, '{"luck":100,"duration":50,"multimine":11,"prestige":4}'),
    ('pickaxe_p5',  'Worldsplitter Pickaxe',  'ultimate mining instrument',          'pickaxe',2000000000, NULL, 0, 11, TRUE, '{"luck":120,"duration":50,"multimine":13,"prestige":5}'),
    ('pickaxe_p6',  'Abyssal Pickaxe',        'existed before stone itself',         'pickaxe',5000000000, NULL, 0, 12, TRUE, '{"luck":140,"duration":55,"multimine":15,"prestige":6}'),
    ('pickaxe_p7',  'Runegraven Pickaxe',     'enchanted with ancient magic',        'pickaxe',10000000000,NULL, 0, 13, TRUE, '{"luck":160,"duration":55,"multimine":16,"prestige":7}'),
    ('pickaxe_p8',  'Paradox Pickaxe',        'mines in superposition',              'pickaxe',25000000000,NULL, 0, 14, TRUE, '{"luck":180,"duration":60,"multimine":18,"prestige":8}'),
    ('pickaxe_p9',  'Epoch Pickaxe',          'mines across timelines',              'pickaxe',60000000000,NULL, 0, 15, TRUE, '{"luck":200,"duration":60,"multimine":19,"prestige":9}'),
    ('pickaxe_p10', 'Oblivion Pickaxe',       'breaks the boundaries of mining',     'pickaxe',150000000000,NULL,0, 16, TRUE, '{"luck":250,"duration":65,"multimine":20,"prestige":10}'),
    -- Minecarts
    ('minecart_rusty',    'Rusty Minecart',     'barely rolls',                       'minecart',    400,   NULL, 0, 1, TRUE, '{"speed":10000,"spawn_rates":{"coal":10}}'),
    ('minecart_wood',     'Wooden Minecart',    'creaky but functional',              'minecart',   1800,   NULL, 0, 2, TRUE, '{"speed":8000,"spawn_rates":{"coal":10,"copper ore":8,"iron ore":5}}'),
    ('minecart_iron',     'Iron Minecart',      'solid and reliable',                 'minecart',  12000,   NULL, 0, 3, TRUE, '{"speed":6500,"spawn_rates":{"silver ore":10,"gold ore":8,"lapis lazuli":5}}'),
    ('minecart_steel',    'Steel Minecart',     'high-speed ore delivery',            'minecart',  80000,   NULL, 0, 4, TRUE, '{"speed":5000,"spawn_rates":{"ruby":10,"sapphire":8,"emerald":6,"platinum ore":5}}'),
    ('minecart_diamond',  'Diamond Minecart',   'frictionless perfection',            'minecart', 600000,   NULL, 0, 5, TRUE, '{"speed":4000,"spawn_rates":{"diamond":10,"mithril ore":8,"iridium ore":6}}'),
    ('minecart_p1',  'Blazerail Minecart',     'runs on prestige energy',            'minecart',   4000000, NULL, 0, 7,  TRUE, '{"speed":3500,"spawn_rates":{"molten core":10,"phoenix ember":8},"prestige":1}'),
    ('minecart_p2',  'Wraith Minecart',        'transcends friction',                'minecart',  20000000, NULL, 0, 8,  TRUE, '{"speed":3000,"spawn_rates":{"shadow ore":10,"void shard":8},"prestige":2}'),
    ('minecart_p3',  'Mirage Minecart',        'phases through obstacles',           'minecart',  80000000, NULL, 0, 9,  TRUE, '{"speed":2800,"spawn_rates":{"nebula crystal":10,"astral shard":8},"prestige":3}'),
    ('minecart_p4',  'Comet Minecart',         'starlight-powered rails',            'minecart', 400000000, NULL, 0, 10, TRUE, '{"speed":2500,"spawn_rates":{"stellar diamond":10,"gravity gem":8},"prestige":4}'),
    ('minecart_p5',  'Sanctified Minecart',    'holy momentum',                      'minecart',1500000000, NULL, 0, 11, TRUE, '{"speed":2200,"spawn_rates":{"eternal ore":10,"immortal gem":8},"prestige":5}'),
    ('minecart_p6',  'Genesis Minecart',       'predates physics',                   'minecart',4000000000, NULL, 0, 12, TRUE, '{"speed":2000,"spawn_rates":{"primordial ore":10,"genesis stone":8},"prestige":6}'),
    ('minecart_p7',  'Spellbound Minecart',    'enchanted wheels',                   'minecart',8000000000, NULL, 0, 13, TRUE, '{"speed":1800,"spawn_rates":{"mana ore":10,"arcane crystal":8},"prestige":7}'),
    ('minecart_p8',  'Flux Minecart',          'in multiple places at once',         'minecart',20000000000,NULL, 0, 14, TRUE, '{"speed":1600,"spawn_rates":{"quantum ore":10,"entangled gem":8},"prestige":8}'),
    ('minecart_p9',  'Chrono Minecart',        'arrives before departure',           'minecart',50000000000,NULL, 0, 15, TRUE, '{"speed":1400,"spawn_rates":{"time ore":10,"paradox gem":8},"prestige":9}'),
    ('minecart_p10', 'Eternal Minecart',       'unlimited velocity',                 'minecart',120000000000,NULL,0, 16, TRUE, '{"speed":1200,"spawn_rates":{"infinity ore":10,"omega crystal":8},"prestige":10}'),
    -- Bags
    ('bag_cloth',     'Cloth Bag',       'flimsy but functional',                 'bag',    300,    NULL, 0, 1, TRUE, '{"capacity":5,"rip_chance":0.60}'),
    ('bag_leather',   'Leather Bag',     'holds a decent amount',                 'bag',   1500,    NULL, 0, 2, TRUE, '{"capacity":8,"rip_chance":0.45}'),
    ('bag_canvas',    'Canvas Bag',      'sturdy woven canvas',                   'bag',  10000,    NULL, 0, 3, TRUE, '{"capacity":12,"rip_chance":0.30}'),
    ('bag_iron',      'Iron Bag',        'reinforced metal ore bag',              'bag',  70000,    NULL, 0, 4, TRUE, '{"capacity":18,"rip_chance":0.18}'),
    ('bag_diamond',   'Diamond Bag',     'unrippable crystalline bag',            'bag', 500000,    NULL, 0, 5, TRUE, '{"capacity":25,"rip_chance":0.08}'),
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

-- ----------------------------------------------------------------------------
-- 5. Ensure mining_claims table exists (normally created at runtime)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS mining_claims (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id     BIGINT UNSIGNED NOT NULL,
    ore_name    VARCHAR(100) NOT NULL,
    ore_emoji   VARCHAR(32) NOT NULL DEFAULT '⛏️',
    rarity      VARCHAR(20) NOT NULL DEFAULT 'common',
    yield_min   INT NOT NULL DEFAULT 1,
    yield_max   INT NOT NULL DEFAULT 3,
    ore_value   INT NOT NULL DEFAULT 10,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at  TIMESTAMP NOT NULL,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_user (user_id),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- 6. Ensure user_stats table exists (used for ores_mined, ores_sold, etc.)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user_stats (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id     BIGINT UNSIGNED NOT NULL,
    stat_name   VARCHAR(50) NOT NULL,
    stat_value  BIGINT SIGNED DEFAULT 0,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY unique_user_stat (user_id, stat_name),
    INDEX idx_user_stat (user_id, stat_name),
    INDEX idx_stat_value (stat_name, stat_value DESC),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Done!
-- Verify with:
--   SELECT COUNT(*) FROM ore_types;          -- should be 68
--   SELECT COUNT(*) FROM mining_gear;        -- should be 45
--   SELECT COUNT(*) FROM shop_items WHERE category IN ('pickaxe','minecart','bag');  -- should be 45
