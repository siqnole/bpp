-- ============================================================================
-- HIGH PRESTIGE ITEMS (P6-P20) - Rods and Bait
-- Values scale up to 100 billion for endgame content
-- These items are hidden from shop until user reaches required prestige level
-- ============================================================================

-- Insert high-prestige rods and bait (levels 12-26, P6-P20)
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata)
VALUES
    -- P6 - Primordial tier (level 12)
    ('rod_prestige6',   'Primordial Rod',      'existed before time itself',         'rod',  5000000000,    NULL, 0, 12, TRUE, '{"luck":175,"capacity":28,"prestige":6}'),
    ('bait_prestige6',  'Primordial Bait',     'essence of the first waters',        'bait', 100000000,     NULL, 0, 12, TRUE, '{"unlocks":["primordial eel","genesis whale","ancient kraken","dawn whale"],"bonus":350,"multiplier":1200,"prestige":6}'),
    ('rod_prestige6b',  'Ancient Rod',         'carved from petrified world tree',   'rod',  4000000000,    NULL, 0, 12, TRUE, '{"luck":160,"capacity":32,"prestige":6}'),
    ('bait_prestige6b', 'Fossil Bait',         'amber-encased organisms',            'bait', 80000000,      NULL, 0, 12, TRUE, '{"unlocks":["origin serpent","fossil fish","primeval shark"],"bonus":360,"multiplier":1150,"prestige":6}'),
    
    -- P7 - Arcane tier (level 13)
    ('rod_prestige7',   'Arcane Rod',          'infused with pure magic',            'rod',  15000000000,   NULL, 0, 13, TRUE, '{"luck":200,"capacity":32,"prestige":7}'),
    ('bait_prestige7',  'Arcane Bait',         'crystallized mana essence',          'bait', 300000000,     NULL, 0, 13, TRUE, '{"unlocks":["mana fish","arcane leviathan","enchanted pike","grimoire ray"],"bonus":400,"multiplier":1500,"prestige":7}'),
    ('rod_prestige7b',  'Mystic Rod',          'channels ley line energy',           'rod',  12000000000,   NULL, 0, 13, TRUE, '{"luck":185,"capacity":36,"prestige":7}'),
    ('bait_prestige7b', 'Rune Bait',           'etched with power glyphs',           'bait', 250000000,     NULL, 0, 13, TRUE, '{"unlocks":["spell weaver","rune bass","spell shark"],"bonus":410,"multiplier":1450,"prestige":7}'),
    
    -- P8 - Quantum tier (level 14)
    ('rod_prestige8',   'Quantum Rod',         'exists in superposition',            'rod',  40000000000,   NULL, 0, 14, TRUE, '{"luck":225,"capacity":36,"prestige":8}'),
    ('bait_prestige8',  'Quantum Bait',        'probabilistic fish attractor',       'bait', 800000000,     NULL, 0, 14, TRUE, '{"unlocks":["schrodinger fish","entangled pair","wave function","photon ray"],"bonus":450,"multiplier":1800,"prestige":8}'),
    ('rod_prestige8b',  'Particle Rod',        'built from a particle collider',     'rod',  35000000000,   NULL, 0, 14, TRUE, '{"luck":210,"capacity":40,"prestige":8}'),
    ('bait_prestige8b', 'Quark Bait',          'subatomic fish attractor',           'bait', 650000000,     NULL, 0, 14, TRUE, '{"unlocks":["quantum ghost","quark pike","neutrino bass"],"bonus":460,"multiplier":1750,"prestige":8}'),
    
    -- P9 - Temporal tier (level 15)
    ('rod_prestige9',   'Temporal Rod',        'catches fish across timelines',      'rod',  100000000000,  NULL, 0, 15, TRUE, '{"luck":250,"capacity":40,"prestige":9}'),
    ('bait_prestige9',  'Temporal Bait',       'lure from the end of time',          'bait', 2000000000,    NULL, 0, 15, TRUE, '{"unlocks":["time fish","paradox salmon","epoch bass","forever fish"],"bonus":500,"multiplier":2200,"prestige":9}'),
    ('rod_prestige9b',  'Chrono Rod',          'bends time around its hook',         'rod',  80000000000,   NULL, 0, 15, TRUE, '{"luck":235,"capacity":44,"prestige":9}'),
    ('bait_prestige9b', 'Epoch Bait',          'distilled from ancient eras',        'bait', 1600000000,    NULL, 0, 15, TRUE, '{"unlocks":["temporal anomaly","millennium pike","aeon ray"],"bonus":510,"multiplier":2150,"prestige":9}'),
    
    -- P10 - Dimensional tier (level 16)
    ('rod_prestige10',  'Dimensional Rod',     'fishes across realities',            'rod',  250000000000,  NULL, 0, 16, TRUE, '{"luck":275,"capacity":45,"prestige":10}'),
    ('bait_prestige10', 'Dimensional Bait',    'attracts multiverse entities',       'bait', 5000000000,    NULL, 0, 16, TRUE, '{"unlocks":["multiverse carp","rift swimmer","portal fish","continuum ray"],"bonus":550,"multiplier":2600,"prestige":10}'),
    ('rod_prestige10b', 'Planar Rod',          'reaches between planes',             'rod',  200000000000,  NULL, 0, 16, TRUE, '{"luck":260,"capacity":50,"prestige":10}'),
    ('bait_prestige10b','Rift Bait',           'harvested from dimensional tears',   'bait', 4000000000,    NULL, 0, 16, TRUE, '{"unlocks":["dimensional echo","warp pike","realm bass"],"bonus":560,"multiplier":2550,"prestige":10}'),
    
    -- P11 - Stellar tier (level 17)
    ('rod_prestige11',  'Stellar Rod',         'forged in a dying star',             'rod',  600000000000,  NULL, 0, 17, TRUE, '{"luck":300,"capacity":50,"prestige":11}'),
    ('bait_prestige11', 'Stellar Bait',        'compressed stellar matter',          'bait', 12000000000,   NULL, 0, 17, TRUE, '{"unlocks":["neutron fish","supernova ray","solar pike","dwarf star fish"],"bonus":600,"multiplier":3000,"prestige":11}'),
    ('rod_prestige11b', 'Nova Rod',            'born from a stellar explosion',      'rod',  500000000000,  NULL, 0, 17, TRUE, '{"luck":285,"capacity":55,"prestige":11}'),
    ('bait_prestige11b','Plasma Bait',         'superheated star material',          'bait', 10000000000,   NULL, 0, 17, TRUE, '{"unlocks":["pulsar pike","magnetar bass","cosmic dust ray"],"bonus":610,"multiplier":2950,"prestige":11}'),
    
    -- P12 - Galactic tier (level 18)
    ('rod_prestige12',  'Galactic Rod',        'spans entire star systems',          'rod',  1500000000000, NULL, 0, 18, TRUE, '{"luck":325,"capacity":55,"prestige":12}'),
    ('bait_prestige12', 'Galactic Bait',       'distilled dark matter',              'bait', 30000000000,   NULL, 0, 18, TRUE, '{"unlocks":["black hole fish","quasar bass","nebula pike","stargate fish"],"bonus":650,"multiplier":3500,"prestige":12}'),
    ('rod_prestige12b', 'Cosmic Rod',          'woven from cosmic strings',          'rod',  1200000000000, NULL, 0, 18, TRUE, '{"luck":310,"capacity":60,"prestige":12}'),
    ('bait_prestige12b','Dark Matter Bait',    'invisible yet irresistible',         'bait', 25000000000,   NULL, 0, 18, TRUE, '{"unlocks":["gravity well","dark energy bass","event horizon ray"],"bonus":660,"multiplier":3450,"prestige":12}'),
    
    -- P13 - Universal tier (level 19)
    ('rod_prestige13',  'Universal Rod',       'reaches beyond the cosmic horizon',  'rod',  4000000000000, NULL, 0, 19, TRUE, '{"luck":350,"capacity":60,"prestige":13}'),
    ('bait_prestige13', 'Universal Bait',      'entropy made tangible',              'bait', 80000000000,   NULL, 0, 19, TRUE, '{"unlocks":["entropy fish","big bang bass","void walker","terminus ray"],"bonus":700,"multiplier":4000,"prestige":13}'),
    ('rod_prestige13b', 'Entropy Rod',         'harnesses universal decay',          'rod',  3500000000000, NULL, 0, 19, TRUE, '{"luck":335,"capacity":65,"prestige":13}'),
    ('bait_prestige13b','Chaos Bait',          'pure disorder as attraction',        'bait', 65000000000,   NULL, 0, 19, TRUE, '{"unlocks":["chaos breeder","chaos pike","oblivion bass"],"bonus":710,"multiplier":3950,"prestige":13}'),
    
    -- P14 - Infinite tier (level 20)
    ('rod_prestige14',  'Infinite Rod',        'transcends all limits',              'rod',  10000000000000, NULL, 0, 20, TRUE, '{"luck":375,"capacity":65,"prestige":14}'),
    ('bait_prestige14', 'Infinite Bait',       'boundless attraction',               'bait', 200000000000,  NULL, 0, 20, TRUE, '{"unlocks":["infinity fish","omega whale","limitless pike","beyond fish"],"bonus":750,"multiplier":4500,"prestige":14}'),
    ('rod_prestige14b', 'Boundless Rod',       'extends past all horizons',          'rod',  8000000000000, NULL, 0, 20, TRUE, '{"luck":360,"capacity":70,"prestige":14}'),
    ('bait_prestige14b','Eternity Bait',       'lure that exists forever',           'bait', 160000000000,  NULL, 0, 20, TRUE, '{"unlocks":["limit breaker","eternal bass","perpetual ray"],"bonus":760,"multiplier":4450,"prestige":14}'),
    
    -- P15 - Mythical tier (level 21)
    ('rod_prestige15',  'Mythical Rod',        'woven from legend itself',           'rod',  25000000000000, NULL, 0, 21, TRUE, '{"luck":400,"capacity":70,"prestige":15}'),
    ('bait_prestige15', 'Mythical Bait',       'fragments of elder stories',         'bait', 500000000000,  NULL, 0, 21, TRUE, '{"unlocks":["world serpent","elder god fish","chimera fish","legend bass"],"bonus":800,"multiplier":5000,"prestige":15}'),
    ('rod_prestige15b', 'Fable Rod',           'crafted from fairy tales',           'rod',  20000000000000, NULL, 0, 21, TRUE, '{"luck":385,"capacity":75,"prestige":15}'),
    ('bait_prestige15b','Myth Bait',           'condensed folklore essence',         'bait', 400000000000,  NULL, 0, 21, TRUE, '{"unlocks":["ancient one","phoenix whale","hydra pike"],"bonus":810,"multiplier":4950,"prestige":15}'),
    
    -- P16 - Omniscient tier (level 22)
    ('rod_prestige16',  'Omniscient Rod',      'knows where all fish are',           'rod',  60000000000000, NULL, 0, 22, TRUE, '{"luck":425,"capacity":75,"prestige":16}'),
    ('bait_prestige16', 'Omniscient Bait',     'all-knowing lure',                   'bait', 1200000000000, NULL, 0, 22, TRUE, '{"unlocks":["all-seeing fish","fate weaver","clairvoyant eel","seer ray"],"bonus":850,"multiplier":5500,"prestige":16}'),
    ('rod_prestige16b', 'Prophet Rod',         'sees every fish before it bites',    'rod',  50000000000000, NULL, 0, 22, TRUE, '{"luck":410,"capacity":80,"prestige":16}'),
    ('bait_prestige16b','Vision Bait',         'reveals hidden fish',                'bait', 1000000000000, NULL, 0, 22, TRUE, '{"unlocks":["oracle oracle","prophet pike","visionary tuna","prescient bass"],"bonus":860,"multiplier":5450,"prestige":16}'),
    
    -- P17 - Omnipotent tier (level 23)
    ('rod_prestige17',  'Omnipotent Rod',      'commands all waters',                'rod',  150000000000000, NULL, 0, 23, TRUE, '{"luck":450,"capacity":80,"prestige":17}'),
    ('bait_prestige17', 'Omnipotent Bait',     'bends reality to attract',           'bait', 3000000000000, NULL, 0, 23, TRUE, '{"unlocks":["reality fish","creation koi","tyrant pike","crown fish"],"bonus":900,"multiplier":6000,"prestige":17}'),
    ('rod_prestige17b', 'Sovereign Rod',       'rules over all that swims',          'rod',  120000000000000, NULL, 0, 23, TRUE, '{"luck":435,"capacity":85,"prestige":17}'),
    ('bait_prestige17b','Authority Bait',      'fish cannot resist its command',     'bait', 2500000000000, NULL, 0, 23, TRUE, '{"unlocks":["world builder","genesis leviathan","emperor bass","dominion ray"],"bonus":910,"multiplier":5950,"prestige":17}'),
    
    -- P18 - Transcendent II tier (level 24)
    ('rod_prestige18',  'Ascendant Rod',       'beyond mortal comprehension',        'rod',  400000000000000, NULL, 0, 24, TRUE, '{"luck":475,"capacity":85,"prestige":18}'),
    ('bait_prestige18', 'Ascendant Bait',      'pure conceptual attraction',         'bait', 8000000000000, NULL, 0, 24, TRUE, '{"unlocks":["void emperor","concept fish","zenith pike","paradigm fish"],"bonus":950,"multiplier":6500,"prestige":18}'),
    ('rod_prestige18b', 'Apex Rod',            'the peak of rod engineering',        'rod',  350000000000000, NULL, 0, 24, TRUE, '{"luck":460,"capacity":90,"prestige":18}'),
    ('bait_prestige18b','Summit Bait',         'crafted at the highest peak',        'bait', 6500000000000, NULL, 0, 24, TRUE, '{"unlocks":["void sovereign","abstract entity","summit bass","pinnacle ray"],"bonus":960,"multiplier":6450,"prestige":18}'),
    
    -- P19 - Absolute tier (level 25)
    ('rod_prestige19',  'Absolute Rod',        'the rod that ends all rods',         'rod',  1000000000000000, NULL, 0, 25, TRUE, '{"luck":500,"capacity":90,"prestige":19}'),
    ('bait_prestige19', 'Absolute Bait',       'fundamental truth as lure',          'bait', 20000000000000, NULL, 0, 25, TRUE, '{"unlocks":["axiom fish","singularity","supreme pike","apex leviathan"],"bonus":1000,"multiplier":7000,"prestige":19}'),
    ('rod_prestige19b', 'Paramount Rod',       'surpasses all expectations',         'rod',  800000000000000, NULL, 0, 25, TRUE, '{"luck":485,"capacity":95,"prestige":19}'),
    ('bait_prestige19b','Supreme Bait',        'nothing ranks higher',               'bait', 16000000000000, NULL, 0, 25, TRUE, '{"unlocks":["absolute truth","perfect form","ultimate eel","paramount ray"],"bonus":1010,"multiplier":6950,"prestige":19}'),
    
    -- P20 - Ultimate tier (level 26)
    ('rod_prestige20',  'Ultimate Rod',        'THE fishing rod',                    'rod',  2500000000000000, NULL, 0, 26, TRUE, '{"luck":600,"capacity":100,"prestige":20}'),
    ('bait_prestige20', 'Ultimate Bait',       'the final lure ever needed',         'bait', 50000000000000, NULL, 0, 26, TRUE, '{"unlocks":["the one fish","end of all","final pike","genesis breaker"],"bonus":1200,"multiplier":8000,"prestige":20}'),
    ('rod_prestige20b', 'Zenith Rod',          'pinnacle of all creation',           'rod',  2000000000000000, NULL, 0, 26, TRUE, '{"luck":550,"capacity":95,"prestige":20}'),
    ('bait_prestige20b','Zenith Bait',         'the bait beyond all baits',          'bait', 40000000000000, NULL, 0, 26, TRUE, '{"unlocks":["alpha omega","eternal paradox","omega bass","last ray"],"bonus":1210,"multiplier":7950,"prestige":20}')

ON DUPLICATE KEY UPDATE
    price = VALUES(price),
    name = VALUES(name),
    description = VALUES(description),
    metadata = VALUES(metadata),
    level = VALUES(level);
