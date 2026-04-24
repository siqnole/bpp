-- ============================================================================
-- PRESTIGE VARIANT ITEMS MIGRATION
-- Adds variant rods and baits for P1-P20 (levels 7-26)
-- Each prestige tier now has 2 rods and 2 baits for more variety
-- Also updates existing bait unlocks to include new fish
-- Run this on an existing database to add the new items
-- ============================================================================

-- Update existing P1-P5 baits to include new fish unlocks
UPDATE shop_items SET metadata = '{"unlocks":["phoenix fish","inferno koi","molten eel"],"bonus":100,"multiplier":300,"prestige":1}' WHERE item_id = 'bait_prestige1';
UPDATE shop_items SET metadata = '{"unlocks":["void fish","wraith pike","nightmare angler"],"bonus":150,"multiplier":450,"prestige":2}' WHERE item_id = 'bait_prestige2';
UPDATE shop_items SET metadata = '{"unlocks":["nebula fish","spectral cod","galaxy grouper"],"bonus":200,"multiplier":600,"prestige":3}' WHERE item_id = 'bait_prestige3';
UPDATE shop_items SET metadata = '{"unlocks":["cosmic fish","astral pike","zodiac fish"],"bonus":250,"multiplier":800,"prestige":4}' WHERE item_id = 'bait_prestige4';
UPDATE shop_items SET metadata = '{"unlocks":["eternal fish","holy mackerel","divine dolphin"],"bonus":300,"multiplier":1000,"prestige":5}' WHERE item_id = 'bait_prestige5';

-- Update existing P6-P20 baits to include new fish unlocks
UPDATE shop_items SET metadata = '{"unlocks":["primordial eel","genesis whale","ancient kraken","dawn whale"],"bonus":350,"multiplier":1200,"prestige":6}' WHERE item_id = 'bait_prestige6';
UPDATE shop_items SET metadata = '{"unlocks":["mana fish","arcane leviathan","enchanted pike","grimoire ray"],"bonus":400,"multiplier":1500,"prestige":7}' WHERE item_id = 'bait_prestige7';
UPDATE shop_items SET metadata = '{"unlocks":["schrodinger fish","entangled pair","wave function","photon ray"],"bonus":450,"multiplier":1800,"prestige":8}' WHERE item_id = 'bait_prestige8';
UPDATE shop_items SET metadata = '{"unlocks":["time fish","paradox salmon","epoch bass","forever fish"],"bonus":500,"multiplier":2200,"prestige":9}' WHERE item_id = 'bait_prestige9';
UPDATE shop_items SET metadata = '{"unlocks":["multiverse carp","rift swimmer","portal fish","continuum ray"],"bonus":550,"multiplier":2600,"prestige":10}' WHERE item_id = 'bait_prestige10';
UPDATE shop_items SET metadata = '{"unlocks":["neutron fish","supernova ray","solar pike","dwarf star fish"],"bonus":600,"multiplier":3000,"prestige":11}' WHERE item_id = 'bait_prestige11';
UPDATE shop_items SET metadata = '{"unlocks":["black hole fish","quasar bass","nebula pike","stargate fish"],"bonus":650,"multiplier":3500,"prestige":12}' WHERE item_id = 'bait_prestige12';
UPDATE shop_items SET metadata = '{"unlocks":["entropy fish","big bang bass","void walker","terminus ray"],"bonus":700,"multiplier":4000,"prestige":13}' WHERE item_id = 'bait_prestige13';
UPDATE shop_items SET metadata = '{"unlocks":["infinity fish","omega whale","limitless pike","beyond fish"],"bonus":750,"multiplier":4500,"prestige":14}' WHERE item_id = 'bait_prestige14';
UPDATE shop_items SET metadata = '{"unlocks":["world serpent","elder god fish","chimera fish","legend bass"],"bonus":800,"multiplier":5000,"prestige":15}' WHERE item_id = 'bait_prestige15';
UPDATE shop_items SET metadata = '{"unlocks":["all-seeing fish","fate weaver","clairvoyant eel","seer ray"],"bonus":850,"multiplier":5500,"prestige":16}' WHERE item_id = 'bait_prestige16';
UPDATE shop_items SET metadata = '{"unlocks":["reality fish","creation koi","tyrant pike","crown fish"],"bonus":900,"multiplier":6000,"prestige":17}' WHERE item_id = 'bait_prestige17';
UPDATE shop_items SET metadata = '{"unlocks":["void emperor","concept fish","zenith pike","paradigm fish"],"bonus":950,"multiplier":6500,"prestige":18}' WHERE item_id = 'bait_prestige18';
UPDATE shop_items SET metadata = '{"unlocks":["axiom fish","singularity","supreme pike","apex leviathan"],"bonus":1000,"multiplier":7000,"prestige":19}' WHERE item_id = 'bait_prestige19';
UPDATE shop_items SET metadata = '{"unlocks":["the one fish","end of all","final pike","genesis breaker"],"bonus":1200,"multiplier":8000,"prestige":20}' WHERE item_id = 'bait_prestige20';

-- ============================================================================
-- NEW VARIANT RODS AND BAITS (P1-P5)
-- ============================================================================
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- Variant rods P1-P5 (alternative stats: trade luck for capacity or vice versa)
    ('rod_prestige1b',  'Rebirth Rod',        'reforged from broken dreams',       'rod', 3500000,   NULL, 0, 7, TRUE, '{"luck":45,"capacity":16,"prestige":1}'),
    ('rod_prestige2b',  'Phantom Rod',        'woven from shadows',                'rod', 20000000,  NULL, 0, 8, TRUE, '{"luck":90,"capacity":12,"prestige":2}'),
    ('rod_prestige3b',  'Spectral Rod',       'shimmers between planes',           'rod', 80000000,  NULL, 0, 9, TRUE, '{"luck":85,"capacity":22,"prestige":3}'),
    ('rod_prestige4b',  'Astral Rod',         'reaches beyond the stars',          'rod', 8000000000,NULL, 0, 10, TRUE, '{"luck":110,"capacity":28,"prestige":4}'),
    ('rod_prestige5b',  'Sacred Rod',         'blessed by ocean spirits',          'rod', 1200000000,NULL, 0, 11, TRUE, '{"luck":165,"capacity":20,"prestige":5}'),

    -- Variant baits P1-P5 (unlock different fish from the same tier)
    ('bait_prestige1b', 'Ember Bait',         'smoldering lure of rebirth',        'bait', 80000,    NULL, 0, 7, TRUE, '{"unlocks":["firebird","ash marlin","cinder whale"],"bonus":110,"multiplier":280,"prestige":1}'),
    ('bait_prestige2b', 'Twilight Bait',      'dusk-infused attraction',           'bait', 400000,   NULL, 0, 8, TRUE, '{"unlocks":["abyssal","umbral tuna","dark tide bass"],"bonus":160,"multiplier":420,"prestige":2}'),
    ('bait_prestige3b', 'Starlight Bait',     'shimmering cosmic lure',            'bait', 1800000,  NULL, 0, 9, TRUE, '{"unlocks":["stardust sprite","cosmic ray","plasma fish"],"bonus":210,"multiplier":580,"prestige":3}'),
    ('bait_prestige4b', 'Comet Bait',         'trails stardust behind it',         'bait', 100000000,NULL, 0, 10, TRUE, '{"unlocks":["stellar phantom","orbit bass","constellation eel"],"bonus":260,"multiplier":780,"prestige":4}'),
    ('bait_prestige5b', 'Sacred Bait',        'purified by holy waters',           'bait', 25000000, NULL, 0, 11, TRUE, '{"unlocks":["ascendant angel","seraphim ray","blessed marlin"],"bonus":310,"multiplier":980,"prestige":5}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price),
    name = VALUES(name),
    description = VALUES(description),
    metadata = VALUES(metadata),
    level = VALUES(level);

-- ============================================================================
-- NEW VARIANT RODS AND BAITS (P6-P20)
-- ============================================================================
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- P6 variants
    ('rod_prestige6b',  'Ancient Rod',         'carved from petrified world tree',   'rod',  4000000000,    NULL, 0, 12, TRUE, '{"luck":160,"capacity":32,"prestige":6}'),
    ('bait_prestige6b', 'Fossil Bait',         'amber-encased organisms',            'bait', 80000000,      NULL, 0, 12, TRUE, '{"unlocks":["origin serpent","fossil fish","primeval shark"],"bonus":360,"multiplier":1150,"prestige":6}'),

    -- P7 variants
    ('rod_prestige7b',  'Mystic Rod',          'channels ley line energy',           'rod',  12000000000,   NULL, 0, 13, TRUE, '{"luck":185,"capacity":36,"prestige":7}'),
    ('bait_prestige7b', 'Rune Bait',           'etched with power glyphs',           'bait', 250000000,     NULL, 0, 13, TRUE, '{"unlocks":["spell weaver","rune bass","spell shark"],"bonus":410,"multiplier":1450,"prestige":7}'),

    -- P8 variants
    ('rod_prestige8b',  'Particle Rod',        'built from a particle collider',     'rod',  35000000000,   NULL, 0, 14, TRUE, '{"luck":210,"capacity":40,"prestige":8}'),
    ('bait_prestige8b', 'Quark Bait',          'subatomic fish attractor',           'bait', 650000000,     NULL, 0, 14, TRUE, '{"unlocks":["quantum ghost","quark pike","neutrino bass"],"bonus":460,"multiplier":1750,"prestige":8}'),

    -- P9 variants
    ('rod_prestige9b',  'Chrono Rod',          'bends time around its hook',         'rod',  80000000000,   NULL, 0, 15, TRUE, '{"luck":235,"capacity":44,"prestige":9}'),
    ('bait_prestige9b', 'Epoch Bait',          'distilled from ancient eras',        'bait', 1600000000,    NULL, 0, 15, TRUE, '{"unlocks":["temporal anomaly","millennium pike","aeon ray"],"bonus":510,"multiplier":2150,"prestige":9}'),

    -- P10 variants
    ('rod_prestige10b', 'Planar Rod',          'reaches between planes',             'rod',  200000000000,  NULL, 0, 16, TRUE, '{"luck":260,"capacity":50,"prestige":10}'),
    ('bait_prestige10b','Rift Bait',           'harvested from dimensional tears',   'bait', 4000000000,    NULL, 0, 16, TRUE, '{"unlocks":["dimensional echo","warp pike","realm bass"],"bonus":560,"multiplier":2550,"prestige":10}'),

    -- P11 variants
    ('rod_prestige11b', 'Nova Rod',            'born from a stellar explosion',      'rod',  500000000000,  NULL, 0, 17, TRUE, '{"luck":285,"capacity":55,"prestige":11}'),
    ('bait_prestige11b','Plasma Bait',         'superheated star material',          'bait', 10000000000,   NULL, 0, 17, TRUE, '{"unlocks":["pulsar pike","magnetar bass","cosmic dust ray"],"bonus":610,"multiplier":2950,"prestige":11}'),

    -- P12 variants
    ('rod_prestige12b', 'Cosmic Rod',          'woven from cosmic strings',          'rod',  1200000000000, NULL, 0, 18, TRUE, '{"luck":310,"capacity":60,"prestige":12}'),
    ('bait_prestige12b','Dark Matter Bait',    'invisible yet irresistible',         'bait', 25000000000,   NULL, 0, 18, TRUE, '{"unlocks":["gravity well","dark energy bass","event horizon ray"],"bonus":660,"multiplier":3450,"prestige":12}'),

    -- P13 variants
    ('rod_prestige13b', 'Entropy Rod',         'harnesses universal decay',          'rod',  3500000000000, NULL, 0, 19, TRUE, '{"luck":335,"capacity":65,"prestige":13}'),
    ('bait_prestige13b','Chaos Bait',          'pure disorder as attraction',        'bait', 65000000000,   NULL, 0, 19, TRUE, '{"unlocks":["chaos breeder","chaos pike","oblivion bass"],"bonus":710,"multiplier":3950,"prestige":13}'),

    -- P14 variants
    ('rod_prestige14b', 'Boundless Rod',       'extends past all horizons',          'rod',  8000000000000, NULL, 0, 20, TRUE, '{"luck":360,"capacity":70,"prestige":14}'),
    ('bait_prestige14b','Eternity Bait',       'lure that exists forever',           'bait', 160000000000,  NULL, 0, 20, TRUE, '{"unlocks":["limit breaker","eternal bass","perpetual ray"],"bonus":760,"multiplier":4450,"prestige":14}'),

    -- P15 variants
    ('rod_prestige15b', 'Fable Rod',           'crafted from fairy tales',           'rod',  20000000000000, NULL, 0, 21, TRUE, '{"luck":385,"capacity":75,"prestige":15}'),
    ('bait_prestige15b','Myth Bait',           'condensed folklore essence',         'bait', 400000000000,  NULL, 0, 21, TRUE, '{"unlocks":["ancient one","phoenix whale","hydra pike"],"bonus":810,"multiplier":4950,"prestige":15}'),

    -- P16 variants
    ('rod_prestige16b', 'Prophet Rod',         'sees every fish before it bites',    'rod',  50000000000000, NULL, 0, 22, TRUE, '{"luck":410,"capacity":80,"prestige":16}'),
    ('bait_prestige16b','Vision Bait',         'reveals hidden fish',                'bait', 1000000000000, NULL, 0, 22, TRUE, '{"unlocks":["oracle oracle","prophet pike","visionary tuna","prescient bass"],"bonus":860,"multiplier":5450,"prestige":16}'),

    -- P17 variants
    ('rod_prestige17b', 'Sovereign Rod',       'rules over all that swims',          'rod',  120000000000000, NULL, 0, 23, TRUE, '{"luck":435,"capacity":85,"prestige":17}'),
    ('bait_prestige17b','Authority Bait',      'fish cannot resist its command',     'bait', 2500000000000, NULL, 0, 23, TRUE, '{"unlocks":["world builder","genesis leviathan","emperor bass","dominion ray"],"bonus":910,"multiplier":5950,"prestige":17}'),

    -- P18 variants
    ('rod_prestige18b', 'Apex Rod',            'the peak of rod engineering',        'rod',  350000000000000, NULL, 0, 24, TRUE, '{"luck":460,"capacity":90,"prestige":18}'),
    ('bait_prestige18b','Summit Bait',         'crafted at the highest peak',        'bait', 6500000000000, NULL, 0, 24, TRUE, '{"unlocks":["void sovereign","abstract entity","summit bass","pinnacle ray"],"bonus":960,"multiplier":6450,"prestige":18}'),

    -- P19 variants
    ('rod_prestige19b', 'Paramount Rod',       'surpasses all expectations',         'rod',  800000000000000, NULL, 0, 25, TRUE, '{"luck":485,"capacity":95,"prestige":19}'),
    ('bait_prestige19b','Supreme Bait',        'nothing ranks higher',               'bait', 16000000000000, NULL, 0, 25, TRUE, '{"unlocks":["absolute truth","perfect form","ultimate eel","paramount ray"],"bonus":1010,"multiplier":6950,"prestige":19}'),

    -- P20 variants
    ('rod_prestige20b', 'Zenith Rod',          'pinnacle of all creation',           'rod',  2000000000000000, NULL, 0, 26, TRUE, '{"luck":550,"capacity":95,"prestige":20}'),
    ('bait_prestige20b','Zenith Bait',         'the bait beyond all baits',          'bait', 40000000000000, NULL, 0, 26, TRUE, '{"unlocks":["alpha omega","eternal paradox","omega bass","last ray"],"bonus":1210,"multiplier":7950,"prestige":20}')
ON DUPLICATE KEY UPDATE
    price = VALUES(price),
    name = VALUES(name),
    description = VALUES(description),
    metadata = VALUES(metadata),
    level = VALUES(level);
