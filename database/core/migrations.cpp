#include "migrations.h"

namespace bronx {
namespace db {

std::vector<std::string> get_schema_migrations() {
    std::vector<std::string> migrations;

    // Helper: MySQL-compatible "ADD COLUMN IF NOT EXISTS" via stored procedure
    migrations.push_back("DROP PROCEDURE IF EXISTS _add_col_if_missing");
    migrations.push_back(
        "CREATE PROCEDURE _add_col_if_missing("
        "  IN p_table VARCHAR(64), IN p_column VARCHAR(64), IN p_definition VARCHAR(512))"
        "BEGIN "
        "  IF NOT EXISTS ("
        "    SELECT 1 FROM information_schema.columns "
        "    WHERE table_schema = DATABASE() AND table_name = p_table AND column_name = p_column"
        "  ) THEN "
        "    SET @ddl = CONCAT('ALTER TABLE `', p_table, '` ADD COLUMN `', p_column, '` ', p_definition); "
        "    PREPARE stmt FROM @ddl; "
        "    EXECUTE stmt; "
        "    DEALLOCATE PREPARE stmt; "
        "  END IF; "
        "END");
    
    migrations.push_back("DROP PROCEDURE IF EXISTS _drop_fk_if_exists");
    migrations.push_back(
        "CREATE PROCEDURE _drop_fk_if_exists("
        "  IN p_table VARCHAR(64), IN p_constraint VARCHAR(64))"
        "BEGIN "
        "  IF EXISTS ("
        "    SELECT 1 FROM information_schema.TABLE_CONSTRAINTS "
        "    WHERE CONSTRAINT_SCHEMA = DATABASE() AND TABLE_NAME = p_table "
        "      AND CONSTRAINT_NAME = p_constraint AND CONSTRAINT_TYPE = 'FOREIGN KEY'"
        "  ) THEN "
        "    SET @ddl = CONCAT('ALTER TABLE `', p_table, '` DROP FOREIGN KEY `', p_constraint, '`'); "
        "    PREPARE stmt FROM @ddl; "
        "    EXECUTE stmt; "
        "    DEALLOCATE PREPARE stmt; "
        "  END IF; "
        "END");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS users ("
        "user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "wallet BIGINT NOT NULL DEFAULT 0,"
        "bank BIGINT NOT NULL DEFAULT 0,"
        "bank_limit BIGINT NOT NULL DEFAULT 10000,"
        "interest_rate DECIMAL(5,2) NOT NULL DEFAULT 1.00,"
        "interest_level INT NOT NULL DEFAULT 0,"
        "last_interest_claim TIMESTAMP NULL DEFAULT NULL,"
        "last_daily TIMESTAMP NULL DEFAULT NULL,"
        "last_work TIMESTAMP NULL DEFAULT NULL,"
        "last_beg TIMESTAMP NULL DEFAULT NULL,"
        "last_rob TIMESTAMP NULL DEFAULT NULL,"
        "total_gambled BIGINT NOT NULL DEFAULT 0,"
        "total_won BIGINT NOT NULL DEFAULT 0,"
        "total_lost BIGINT NOT NULL DEFAULT 0,"
        "fish_caught BIGINT NOT NULL DEFAULT 0,"
        "fish_sold BIGINT NOT NULL DEFAULT 0,"
        "gambling_wins BIGINT NOT NULL DEFAULT 0,"
        "gambling_losses BIGINT NOT NULL DEFAULT 0,"
        "commands_used BIGINT NOT NULL DEFAULT 0,"
        "daily_streak INT NOT NULL DEFAULT 0,"
        "work_count BIGINT NOT NULL DEFAULT 0,"
        "ores_mined BIGINT NOT NULL DEFAULT 0,"
        "items_crafted BIGINT NOT NULL DEFAULT 0,"
        "trades_completed BIGINT NOT NULL DEFAULT 0,"
        "dev BOOLEAN NOT NULL DEFAULT FALSE,"
        "admin BOOLEAN NOT NULL DEFAULT FALSE,"
        "is_mod BOOLEAN NOT NULL DEFAULT FALSE,"
        "maintainer BOOLEAN NOT NULL DEFAULT FALSE,"
        "contributor BOOLEAN NOT NULL DEFAULT FALSE,"
        "vip BOOLEAN NOT NULL DEFAULT FALSE,"
        "passive BOOLEAN NOT NULL DEFAULT FALSE,"
        "prestige INT NOT NULL DEFAULT 0,"
        "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "last_active TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "INDEX idx_wallet (wallet DESC),"
        "INDEX idx_bank (bank DESC),"
        "INDEX idx_last_active (last_active),"
        "INDEX idx_prestige (prestige DESC)"
        ") ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS user_stats_ext ("
        "user_id BIGINT UNSIGNED NOT NULL,"
        "stat_name VARCHAR(64) NOT NULL,"
        "stat_value BIGINT NOT NULL DEFAULT 0,"
        "last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "PRIMARY KEY (user_id, stat_name),"
        "INDEX idx_stat_name_value (stat_name, stat_value DESC),"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS user_skill_points ("
        "user_id BIGINT UNSIGNED NOT NULL,"
        "skill_id VARCHAR(64) NOT NULL,"
        "rank INT NOT NULL DEFAULT 0,"
        "PRIMARY KEY (user_id, skill_id),"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_balances ("
        "guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "balance BIGINT NOT NULL DEFAULT 0,"
        "total_donated BIGINT NOT NULL DEFAULT 0,"
        "total_given BIGINT NOT NULL DEFAULT 0,"
        "INDEX idx_balance (balance DESC)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS global_blacklist ("
        "user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "reason VARCHAR(512) DEFAULT NULL,"
        "added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");
    
    migrations.push_back("CALL _add_col_if_missing('global_blacklist','reason','VARCHAR(512) DEFAULT NULL')");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS global_whitelist ("
        "user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "reason VARCHAR(512) DEFAULT NULL,"
        "added_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");
    
    migrations.push_back("CALL _add_col_if_missing('global_whitelist','reason','VARCHAR(512) DEFAULT NULL')");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_prefixes ("
        "guild_id BIGINT UNSIGNED NOT NULL,"
        "prefix VARCHAR(50) NOT NULL,"
        "PRIMARY KEY (guild_id, prefix)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS user_prefixes ("
        "user_id BIGINT UNSIGNED NOT NULL,"
        "prefix VARCHAR(50) NOT NULL,"
        "PRIMARY KEY (user_id, prefix),"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_command_settings ("
        "guild_id BIGINT UNSIGNED NOT NULL,"
        "command VARCHAR(100) NOT NULL,"
        "enabled BOOLEAN NOT NULL DEFAULT TRUE,"
        "PRIMARY KEY (guild_id, command)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_module_settings ("
        "guild_id BIGINT UNSIGNED NOT NULL,"
        "module VARCHAR(100) NOT NULL,"
        "enabled BOOLEAN NOT NULL DEFAULT TRUE,"
        "PRIMARY KEY (guild_id, module)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_command_scope_settings ("
        "guild_id BIGINT UNSIGNED NOT NULL,"
        "command VARCHAR(100) NOT NULL,"
        "scope_type ENUM('channel','role','user') NOT NULL,"
        "scope_id BIGINT UNSIGNED NOT NULL,"
        "enabled BOOLEAN NOT NULL DEFAULT TRUE,"
        "PRIMARY KEY (guild_id, command, scope_type, scope_id)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS guild_module_scope_settings ("
        "guild_id BIGINT UNSIGNED NOT NULL,"
        "module VARCHAR(100) NOT NULL,"
        "scope_type ENUM('channel','role','user') NOT NULL,"
        "scope_id BIGINT UNSIGNED NOT NULL,"
        "enabled BOOLEAN NOT NULL DEFAULT TRUE,"
        "PRIMARY KEY (guild_id, module, scope_type, scope_id)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS suggestions ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "user_id BIGINT UNSIGNED NOT NULL,"
        "suggestion TEXT NOT NULL,"
        "networth BIGINT NOT NULL,"
        "submitted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "`read` BOOLEAN NOT NULL DEFAULT FALSE,"
        "INDEX idx_user (user_id),"
        "INDEX idx_read (`read`),"
        "INDEX idx_submitted (submitted_at),"
        "INDEX idx_networth (networth DESC),"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE"
        ") ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS fishing_logs ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "rod_level INT NOT NULL,"
        "bait_level INT NOT NULL,"
        "net_profit BIGINT NOT NULL,"
        "logged_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_rod (rod_level),"
        "INDEX idx_bait (bait_level),"
        "INDEX idx_profit (net_profit)"
        ") ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS ml_settings ("
        "`key` VARCHAR(64) NOT NULL PRIMARY KEY,"
        "`value` TEXT NOT NULL"
        ") ENGINE=InnoDB");

    migrations.push_back("INSERT IGNORE INTO ml_settings (`key`,`value`) VALUES ('lottery_pool','30000000')");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS lottery_entries ("
        "user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,"
        "tickets BIGINT NOT NULL DEFAULT 0,"
        "FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS ml_price_changes ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "bait_level INT NOT NULL,"
        "adjust BIGINT NOT NULL,"
        "changed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_bait_level (bait_level),"
        "INDEX idx_changed_at (changed_at)"
        ") ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS progressive_jackpot ("
        "id INT NOT NULL DEFAULT 1,"
        "pool BIGINT NOT NULL DEFAULT 0,"
        "last_winner_id BIGINT UNSIGNED NULL,"
        "last_won_amount BIGINT NOT NULL DEFAULT 0,"
        "last_won_at DATETIME NULL,"
        "total_won_all_time BIGINT NOT NULL DEFAULT 0,"
        "times_won INT NOT NULL DEFAULT 0,"
        "PRIMARY KEY (id)) ENGINE=InnoDB");

    migrations.push_back("INSERT IGNORE INTO progressive_jackpot (id, pool) VALUES (1, 0)");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS jackpot_history ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "user_id BIGINT UNSIGNED NOT NULL,"
        "amount BIGINT NOT NULL,"
        "pool_before BIGINT NOT NULL DEFAULT 0,"
        "won_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "INDEX idx_jackpot_history_user (user_id),"
        "INDEX idx_jackpot_history_time (won_at)) ENGINE=InnoDB");

    migrations.push_back(
        "CREATE TABLE IF NOT EXISTS world_events ("
        "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "event_type VARCHAR(64) NOT NULL,"
        "event_name VARCHAR(128) NOT NULL,"
        "description TEXT NOT NULL,"
        "emoji VARCHAR(32) NOT NULL DEFAULT '\xF0\x9F\x8C\x8D',"
        "bonus_type VARCHAR(64) NOT NULL,"
        "bonus_value DOUBLE NOT NULL DEFAULT 0.0,"
        "started_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "ends_at DATETIME NOT NULL,"
        "active BOOLEAN NOT NULL DEFAULT TRUE,"
        "INDEX idx_world_events_active (active, ends_at),"
        "INDEX idx_world_events_type (event_type)) ENGINE=InnoDB");

    migrations.push_back("CALL _add_col_if_missing('user_inventory','level','INT NOT NULL DEFAULT 1')");
    migrations.push_back("CALL _add_col_if_missing('user_inventory','metadata','TEXT')");
    migrations.push_back("CALL _add_col_if_missing('shop_items','required_level','INT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('shop_items','level','INT NOT NULL DEFAULT 1')");
    migrations.push_back("CALL _add_col_if_missing('shop_items','usable','BOOLEAN NOT NULL DEFAULT FALSE')");
    migrations.push_back("CALL _add_col_if_missing('shop_items','metadata','TEXT')");
    migrations.push_back("CALL _add_col_if_missing('guild_settings','server_bio','TEXT NULL')");
    migrations.push_back("CALL _add_col_if_missing('guild_settings','server_website','VARCHAR(255) NULL')");
    migrations.push_back("CALL _add_col_if_missing('guild_settings','server_banner_url','VARCHAR(512) NULL')");
    migrations.push_back("CALL _add_col_if_missing('guild_settings','server_avatar_url','VARCHAR(512) NULL')");

    migrations.push_back(
        "UPDATE user_inventory SET acquired_at = NOW() "
        "WHERE acquired_at = '0000-00-00 00:00:00' OR acquired_at IS NULL");
    
    migrations.push_back(
        "ALTER TABLE user_inventory MODIFY COLUMN item_type "
        "ENUM('potion','upgrade','rod','bait','collectible','other',"
        "'automation','boosts','title','tools','pickaxe','minecart','bag') NOT NULL");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id = s.item_id "
        "SET i.item_type = s.category");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_SET(COALESCE(metadata,'{}'), '$.bonus', 0) "
        "WHERE category = 'bait' AND (metadata IS NULL OR JSON_EXTRACT(metadata,'$.bonus') IS NULL)");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_SET(COALESCE(metadata,'{}'), '$.multiplier', 0) "
        "WHERE category = 'bait' AND (metadata IS NULL OR JSON_EXTRACT(metadata,'$.multiplier') IS NULL)");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id = s.item_id "
        "SET i.metadata = JSON_SET(COALESCE(i.metadata, '{}'), "
        "'$.bonus', COALESCE(JSON_EXTRACT(i.metadata,'$.bonus'), JSON_EXTRACT(s.metadata,'$.bonus'),0), "
        "'$.multiplier', COALESCE(JSON_EXTRACT(i.metadata,'$.multiplier'), JSON_EXTRACT(s.metadata,'$.multiplier'),0)) "
        "WHERE s.category = 'bait' AND (i.metadata IS NULL OR JSON_EXTRACT(i.metadata,'$.bonus') IS NULL OR JSON_EXTRACT(i.metadata,'$.multiplier') IS NULL)");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_SET(metadata,'$.multiplier', CASE item_id WHEN 'bait_rare' THEN 200 WHEN 'bait_epic' THEN 400 ELSE JSON_EXTRACT(metadata,'$.multiplier') END) "
        "WHERE item_id IN ('bait_rare','bait_epic')");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_REMOVE(metadata, '$.unlocks[0]') "
        "WHERE item_id = 'bait_rare' AND JSON_CONTAINS(metadata,'\"common fish\"','$.unlocks')");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id = s.item_id "
        "SET i.metadata = JSON_REMOVE(i.metadata, '$.unlocks[0]') "
        "WHERE s.item_id = 'bait_rare' AND JSON_CONTAINS(i.metadata,'\"common fish\"','$.unlocks')");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_ARRAY_APPEND(metadata,'$.unlocks','\"abyssal leviathan\"') "
        "WHERE item_id='bait_epic' AND NOT JSON_CONTAINS(metadata,'\"abyssal leviathan\"','$.unlocks')");
    
    migrations.push_back(
        "UPDATE shop_items SET metadata = JSON_ARRAY_APPEND(metadata,'$.unlocks','\"celestial kraken\"') "
        "WHERE item_id='bait_legendary' AND NOT JSON_CONTAINS(metadata,'\"celestial kraken\"','$.unlocks')");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id=s.item_id "
        "SET i.metadata = JSON_ARRAY_APPEND(i.metadata,'$.unlocks','\"abyssal leviathan\"') "
        "WHERE s.item_id='bait_epic' AND NOT JSON_CONTAINS(i.metadata,'\"abyssal leviathan\"','$.unlocks')");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id=s.item_id "
        "SET i.metadata = JSON_ARRAY_APPEND(i.metadata,'$.unlocks','\"celestial kraken\"') "
        "WHERE s.item_id='bait_legendary' AND NOT JSON_CONTAINS(i.metadata,'\"celestial kraken\"','$.unlocks')");

    migrations.push_back("UPDATE user_inventory SET item_id='rod_wood', level=1 WHERE item_id='fishing_rod'");
    migrations.push_back("UPDATE user_inventory SET item_id='bait_common', level=1 WHERE item_id='bait'");
    migrations.push_back("UPDATE user_fish_catches SET rod_id='rod_wood' WHERE rod_id='fishing_rod'");
    migrations.push_back("UPDATE user_fish_catches SET bait_id='bait_common' WHERE bait_id='bait'");
    migrations.push_back("UPDATE user_fishing_gear SET active_rod_id='rod_wood' WHERE active_rod_id='fishing_rod'");
    migrations.push_back("UPDATE user_fishing_gear SET active_bait_id='bait_common' WHERE active_bait_id='bait'");
    migrations.push_back("DELETE FROM shop_items WHERE item_id IN ('fishing_rod','bait')");

    migrations.push_back(R"SQL(
INSERT INTO shop_items (item_id,name,description,category,price,max_quantity,required_level,level,usable,metadata) VALUES
('rod_wood','Wooden Rod','entry-level fishing rod','rod',500,NULL,0,1,TRUE,'{"luck":0,"capacity":1}'),
('rod_iron','Iron Rod','sturdy iron rod','rod',2000,NULL,0,2,TRUE,'{"luck":5,"capacity":2}'),
('rod_steel','Steel Rod','durable steel rod','rod',60000,NULL,0,3,TRUE,'{"luck":10,"capacity":3}'),
('rod_gold','Golden Rod','luxurious gold plated rod','rod',200000,NULL,0,4,TRUE,'{"luck":20,"capacity":5}'),
('rod_diamond','Diamond Rod','exceptionally sharp rod','rod',1000000,NULL,0,5,TRUE,'{"luck":50,"capacity":10}'),
('rod_infinity','Infinity Rod','transcends mathematical limits','rod',15707960,NULL,0,6,TRUE,'{"luck":75,"capacity":15}'),
('rod_dev','Dev Rod','debugged to perfection','rod',10485760,NULL,0,6,TRUE,'{"luck":60,"capacity":12}'),
('rod_shrek','Shrek Rod','ogre-sized fishing power','rod',1337420,NULL,0,6,TRUE,'{"luck":80,"capacity":8}'),
('bait_common','Common Bait','cheap general purpose bait','bait',50,NULL,0,1,TRUE,'{"unlocks":["common fish","salmon","bass","perch","bluegill"],"bonus":50,"multiplier":100}'),
('bait_uncommon','Uncommon Bait','better than common bait','bait',200,NULL,0,2,TRUE,'{"unlocks":["common fish","salmon","tropical fish","tuna","mackerel","flounder","cod"],"bonus":75,"multiplier":150}'),
('bait_rare','Rare Bait','rare bait that attracts better fish','bait',2000,NULL,0,3,TRUE,'{"unlocks":["salmon","tropical fish","octopus","shark","giant squid","anglerfish","barracuda","swordfish","marlin","stingray"],"bonus":100,"multiplier":200}'),
('bait_epic','Epic Bait','highly sought bait','bait',50000,NULL,0,4,TRUE,'{"unlocks":["whale","golden fish","abyssal leviathan","great white shark","giant octopus","colossal squid","blue whale"],"bonus":10,"multiplier":400}'),
('bait_legendary','Legendary Bait','legendary bait of myths','bait',500000,NULL,0,5,TRUE,'{"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40}'),
('bait_pi','π','transcendental mathematical constant','bait',1570795,NULL,0,6,TRUE,'{"unlocks":["rational fish","irrational fish","rooted fish","exponential fish","imaginary fish","prime fish","fibonacci fish"],"bonus":31,"multiplier":415}'),
('bait_segfault','Segmentation Fault','causes core dumps in fish','bait',327680,NULL,0,6,TRUE,'{"unlocks":["null pointer","stack overflow","memory leak","race condition","buffer overflow","deadlock","segfault"],"bonus":64,"multiplier":256}'),
('bait_swamp','Swamp Water','murky water from Far Far Away','bait',133742,NULL,0,6,TRUE,'{"unlocks":["donkey","fiona","puss in boots","lord farquaad","dragon","gingerbread man","shrek"],"bonus":42,"multiplier":420}')
ON DUPLICATE KEY UPDATE price=VALUES(price),name=VALUES(name),description=VALUES(description),metadata=VALUES(metadata),level=VALUES(level)
)SQL");

    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id=s.item_id "
        "SET i.level = s.level "
        "WHERE i.item_id LIKE 'rod_%' OR i.item_id LIKE 'bait_%'");
    
    migrations.push_back(
        "UPDATE user_inventory i JOIN shop_items s ON i.item_id = s.item_id "
        "SET i.metadata = s.metadata "
        "WHERE s.category = 'bait'");

    migrations.push_back(R"SQL(
UPDATE shop_items SET level = CASE item_id
    WHEN 'rod_wood' THEN 1
    WHEN 'rod_iron' THEN 2
    WHEN 'rod_steel' THEN 3
    WHEN 'rod_gold' THEN 4
    WHEN 'rod_diamond' THEN 5
    WHEN 'rod_infinity' THEN 6
    WHEN 'rod_dev' THEN 6
    WHEN 'rod_shrek' THEN 6
    WHEN 'bait_common' THEN 1
    WHEN 'bait_uncommon' THEN 2
    WHEN 'bait_rare' THEN 3
    WHEN 'bait_epic' THEN 4
    WHEN 'bait_legendary' THEN 5
    WHEN 'bait_pi' THEN 6
    WHEN 'bait_segfault' THEN 6
    WHEN 'bait_swamp' THEN 6
    ELSE level END
)SQL");

    migrations.push_back(R"SQL(
UPDATE shop_items SET metadata = CASE item_id
    WHEN 'bait_common' THEN '{"unlocks":["common fish","salmon","bass","perch","bluegill"],"bonus":50,"multiplier":100}'
    WHEN 'bait_uncommon' THEN '{"unlocks":["common fish","salmon","tropical fish","tuna","mackerel","flounder","cod"],"bonus":75,"multiplier":150}'
    WHEN 'bait_rare' THEN '{"unlocks":["salmon","tropical fish","octopus","shark","giant squid","anglerfish","barracuda","swordfish","marlin","stingray"],"bonus":100,"multiplier":200}'
    WHEN 'bait_epic' THEN '{"unlocks":["whale","golden fish","abyssal leviathan","great white shark","giant octopus","colossal squid","blue whale"],"bonus":10,"multiplier":400}'
    WHEN 'bait_legendary' THEN '{"unlocks":["legendary fish","celestial kraken","leviathan","sea serpent","ancient turtle"],"bonus":20,"multiplier":40}'
    WHEN 'bait_pi' THEN '{"unlocks":["rational fish","irrational fish","rooted fish","exponential fish","imaginary fish","prime fish","fibonacci fish"],"bonus":31,"multiplier":415}'
    WHEN 'bait_segfault' THEN '{"unlocks":["null pointer","stack overflow","memory leak","race condition","buffer overflow","deadlock","segfault"],"bonus":64,"multiplier":256}'
    WHEN 'bait_swamp' THEN '{"unlocks":["donkey","fiona","puss in boots","lord farquaad","dragon","gingerbread man","shrek"],"bonus":42,"multiplier":420}'
    ELSE metadata END
)SQL");

    migrations.push_back(
        "UPDATE user_inventory SET level = CASE item_id "
        "WHEN 'bait_common' THEN 1 "
        "WHEN 'bait_uncommon' THEN 2 "
        "WHEN 'bait_rare' THEN 3 "
        "WHEN 'bait_epic' THEN 4 "
        "WHEN 'bait_legendary' THEN 5 "
        "WHEN 'bait_pi' THEN 6 "
        "WHEN 'bait_segfault' THEN 6 "
        "WHEN 'bait_swamp' THEN 6 "
        "ELSE level END"
        " WHERE item_type='bait'");

    migrations.push_back(R"SQL(
UPDATE user_inventory SET level = CASE item_id
    WHEN 'rod_wood' THEN 1
    WHEN 'rod_iron' THEN 2
    WHEN 'rod_steel' THEN 3
    WHEN 'rod_gold' THEN 4
    WHEN 'rod_diamond' THEN 5
    WHEN 'rod_infinity' THEN 6
    WHEN 'rod_dev' THEN 6
    WHEN 'rod_shrek' THEN 6
    WHEN 'bait_common' THEN 1
    WHEN 'bait_uncommon' THEN 2
    WHEN 'bait_rare' THEN 3
    WHEN 'bait_epic' THEN 4
    WHEN 'bait_legendary' THEN 5
    WHEN 'bait_pi' THEN 6
    WHEN 'bait_segfault' THEN 6
    WHEN 'bait_swamp' THEN 6
    ELSE level END
WHERE item_id IN ('rod_wood','rod_iron','rod_steel','rod_gold','rod_diamond','rod_infinity','rod_dev','rod_shrek',
                    'bait_common','bait_uncommon','bait_rare','bait_epic','bait_legendary','bait_pi','bait_segfault','bait_swamp')
)SQL");

    migrations.push_back(R"SQL(
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata) VALUES
('pickaxe_wood','Wooden Pickaxe','basic mining tool','pickaxe',500,NULL,0,1,TRUE,'{"luck":0,"duration":20,"multimine":0}'),
('pickaxe_stone','Stone Pickaxe','slightly better than wood','pickaxe',2000,NULL,0,2,TRUE,'{"luck":5,"duration":25,"multimine":1}'),
('pickaxe_iron','Iron Pickaxe','sturdy iron construction','pickaxe',15000,NULL,0,3,TRUE,'{"luck":10,"duration":30,"multimine":2}'),
('pickaxe_gold','Golden Pickaxe','luxurious and effective','pickaxe',100000,NULL,0,4,TRUE,'{"luck":20,"duration":35,"multimine":3}'),
('pickaxe_diamond','Diamond Pickaxe','cuts through anything','pickaxe',750000,NULL,0,5,TRUE,'{"luck":40,"duration":40,"multimine":4,"multiore_chance":0.10,"multiore_max":1}'),
('pickaxe_p1','Emberstrike Pickaxe','forged in prestige flames','pickaxe',5000000,NULL,0,7,TRUE,'{"luck":50,"duration":40,"multimine":5,"prestige":1,"multiore_chance":0.15,"multiore_max":2}'),
('pickaxe_p2','Voidbreaker Pickaxe','mines through dimensions','pickaxe',25000000,NULL,0,8,TRUE,'{"luck":65,"duration":45,"multimine":7,"prestige":2,"multiore_chance":0.20,"multiore_max":2}'),
('pickaxe_p3','Phantomsteel Pickaxe','phases through stone','pickaxe',100000000,NULL,0,9,TRUE,'{"luck":80,"duration":45,"multimine":9,"prestige":3,"multiore_chance":0.25,"multiore_max":3}'),
('pickaxe_p4','Starforged Pickaxe','starforged mining power','pickaxe',500000000,NULL,0,10,TRUE,'{"luck":100,"duration":50,"multimine":11,"prestige":4,"multiore_chance":0.30,"multiore_max":4}'),
('pickaxe_p5','Worldsplitter Pickaxe','ultimate mining instrument','pickaxe',2000000000,NULL,0,11,TRUE,'{"luck":120,"duration":50,"multimine":13,"prestige":5,"multiore_chance":0.35,"multiore_max":5}'),
('minecart_rusty','Rusty Minecart','barely rolls','minecart',400,NULL,0,1,TRUE,'{"speed":10000,"spawn_rates":{"coal":10}}'),
('minecart_wood','Wooden Minecart','creaky but functional','minecart',1800,NULL,0,2,TRUE,'{"speed":8000,"spawn_rates":{"coal":10,"copper ore":8,"iron ore":5}}'),
('minecart_iron','Iron Minecart','solid and reliable','minecart',12000,NULL,0,3,TRUE,'{"speed":6500,"spawn_rates":{"silver ore":10,"gold ore":8,"lapis lazuli":5}}'),
('minecart_steel','Steel Minecart','high-speed ore delivery','minecart',80000,NULL,0,4,TRUE,'{"speed":5000,"spawn_rates":{"ruby":10,"sapphire":8,"emerald":6,"platinum ore":5}}'),
('minecart_diamond','Diamond Minecart','frictionless perfection','minecart',600000,NULL,0,5,TRUE,'{"speed":4000,"spawn_rates":{"diamond":10,"mithril ore":8,"iridium ore":6}}'),
('bag_cloth','Cloth Bag','flimsy but functional','bag',300,NULL,0,1,TRUE,'{"capacity":5,"rip_chance":0.60}'),
('bag_leather','Leather Bag','holds a decent amount','bag',1500,NULL,0,2,TRUE,'{"capacity":8,"rip_chance":0.45}'),
('bag_canvas','Canvas Bag','sturdy woven canvas','bag',10000,NULL,0,3,TRUE,'{"capacity":12,"rip_chance":0.30}'),
('bag_iron','Iron Bag','reinforced metal ore bag','bag',70000,NULL,0,4,TRUE,'{"capacity":18,"rip_chance":0.18}'),
('bag_diamond','Diamond Bag','unrippable crystalline bag','bag',500000,NULL,0,5,TRUE,'{"capacity":25,"rip_chance":0.08}'),
('bag_obsidian','Risky Bag','30 seconds until the fail safe alarm! make the most of it!','bag',3000000,NULL,0,6,TRUE,'{"capacity":100,"rip_chance":1}'),
('bag_ender','Ender Bag','connected to the Ender Chest','bag',20000000,NULL,0,7,TRUE,'{"capacity":50,"rip_chance":0.02}')
ON DUPLICATE KEY UPDATE price=VALUES(price),name=VALUES(name),description=VALUES(description),metadata=VALUES(metadata),level=VALUES(level)
)SQL");

    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.10, '$.multiore_max', 1)
WHERE item_id = 'pickaxe_diamond' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.15, '$.multiore_max', 2)
WHERE item_id = 'pickaxe_p1' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.20, '$.multiore_max', 2)
WHERE item_id = 'pickaxe_p2' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.25, '$.multiore_max', 3)
WHERE item_id = 'pickaxe_p3' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.30, '$.multiore_max', 4)
WHERE item_id = 'pickaxe_p4' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
    migrations.push_back(R"SQL(
UPDATE user_inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.35, '$.multiore_max', 5)
WHERE item_id = 'pickaxe_p5' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");

    migrations.push_back(R"SQL(
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata) VALUES
('auto_fisher_1','Basic Autofisher','automatically catches fish while you are away (slower rate)','automation',5000000,NULL,0,1,TRUE,'{"tier":1,"rate":300}'),
('auto_fisher_2','Advanced Autofisher','upgraded autofisher with faster catch rate','automation',25000000,NULL,0,2,TRUE,'{"tier":2,"rate":180}')
ON DUPLICATE KEY UPDATE price=VALUES(price),name=VALUES(name),description=VALUES(description),metadata=VALUES(metadata),level=VALUES(level)
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_member_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type VARCHAR(16) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_message_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    event_type VARCHAR(16) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at),
    INDEX idx_guild_user (guild_id, user_id)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_command_usage (
    guild_id BIGINT UNSIGNED NOT NULL,
    command_name VARCHAR(64) NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    usage_date DATE NOT NULL,
    use_count INT NOT NULL DEFAULT 0,
    PRIMARY KEY (guild_id, command_name, channel_id, usage_date),
    INDEX idx_guild_date (guild_id, usage_date)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_daily_stats (
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id VARCHAR(32) NOT NULL DEFAULT '__guild__',
    stat_date DATE NOT NULL,
    messages INT NOT NULL DEFAULT 0,
    edits INT NOT NULL DEFAULT 0,
    deletes INT NOT NULL DEFAULT 0,
    joins INT NOT NULL DEFAULT 0,
    leaves INT NOT NULL DEFAULT 0,
    active_users INT NOT NULL DEFAULT 0,
    PRIMARY KEY (guild_id, channel_id, stat_date),
    INDEX idx_guild_date (guild_id, stat_date)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_voice_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    event_type VARCHAR(20) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_boost_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type VARCHAR(16) NOT NULL,
    boost_id VARCHAR(32) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_activity_log (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    user_name VARCHAR(256) NOT NULL,
    source VARCHAR(16) NOT NULL DEFAULT 'DC',
    action VARCHAR(64) NOT NULL,
    old_value TEXT,
    new_value TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_time (guild_id, created_at)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_user_activity_daily (
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    stat_date DATE NOT NULL,
    messages INT NOT NULL DEFAULT 0,
    edits INT NOT NULL DEFAULT 0,
    deletes INT NOT NULL DEFAULT 0,
    commands_used INT NOT NULL DEFAULT 0,
    voice_minutes INT NOT NULL DEFAULT 0,
    PRIMARY KEY (guild_id, user_id, stat_date),
    INDEX idx_user_date (user_id, stat_date)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_deleted_messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id BIGINT UNSIGNED NOT NULL,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    author_id BIGINT UNSIGNED NOT NULL,
    author_tag VARCHAR(256) NOT NULL DEFAULT '',
    author_avatar VARCHAR(1024) NOT NULL DEFAULT '',
    content TEXT,
    attachment_urls TEXT,
    embeds_summary TEXT,
    deleted_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_channel_deleted (channel_id, deleted_at),
    INDEX idx_guild_deleted (guild_id, deleted_at),
    INDEX idx_author_deleted (author_id, deleted_at),
    INDEX idx_message_id (message_id)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS user_rebirths (
    user_id BIGINT UNSIGNED PRIMARY KEY,
    rebirth_level INT NOT NULL DEFAULT 0,
    total_multiplier DOUBLE NOT NULL DEFAULT 1.0,
    last_rebirth_at TIMESTAMP NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS boss_raids (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id    BIGINT UNSIGNED NOT NULL,
    host_id     BIGINT UNSIGNED NOT NULL,
    entry_fee   BIGINT NOT NULL,
    total_pool  BIGINT NOT NULL,
    members     INT NOT NULL DEFAULT 0,
    boss_killed TINYINT(1) NOT NULL DEFAULT 0,
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS raid_participants (
    raid_id      BIGINT UNSIGNED NOT NULL,
    user_id      BIGINT UNSIGNED NOT NULL,
    damage_dealt BIGINT NOT NULL DEFAULT 0,
    survived     TINYINT(1) NOT NULL DEFAULT 0,
    payout       BIGINT NOT NULL DEFAULT 0,
    PRIMARY KEY (raid_id, user_id)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS commodity_prices (
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    base_price INT NOT NULL DEFAULT 100,
    current_price INT NOT NULL DEFAULT 100,
    price_modifier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
    trend DECIMAL(5,2) NOT NULL DEFAULT 0.00,
    last_updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (commodity_name, commodity_type),
    INDEX idx_type (commodity_type)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS commodity_price_history (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    commodity_name VARCHAR(100) NOT NULL,
    commodity_type ENUM('fish','ore') NOT NULL DEFAULT 'ore',
    price INT NOT NULL,
    recorded_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_commodity (commodity_name, commodity_type),
    INDEX idx_recorded (recorded_at)
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS user_fish_ponds (
    user_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    pond_level INT NOT NULL DEFAULT 1,
    capacity INT NOT NULL DEFAULT 5,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_last_collect (last_collect),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS user_pond_fish (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    fish_name VARCHAR(100) NOT NULL,
    fish_emoji VARCHAR(32) NOT NULL DEFAULT '🐟',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',
    base_value INT NOT NULL DEFAULT 10,
    stocked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user (user_id),
    FOREIGN KEY (user_id) REFERENCES user_fish_ponds(user_id) ON DELETE CASCADE
) ENGINE=InnoDB
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS user_mining_claims (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    ore_name VARCHAR(100) NOT NULL,
    ore_emoji VARCHAR(32) NOT NULL DEFAULT '⛏️',
    rarity VARCHAR(20) NOT NULL DEFAULT 'common',
    yield_min INT NOT NULL DEFAULT 1,
    yield_max INT NOT NULL DEFAULT 3,
    ore_value INT NOT NULL DEFAULT 10,
    purchased_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    last_collect TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_user (user_id),
    INDEX idx_expires (expires_at),
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB
)SQL");

    migrations.push_back("CALL _add_col_if_missing('guild_deleted_messages', 'message_id', 'BIGINT UNSIGNED NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('guild_deleted_messages', 'author_id', 'BIGINT UNSIGNED NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('guild_deleted_messages', 'author_tag', 'VARCHAR(256) NOT NULL DEFAULT \'\')");
    migrations.push_back("CALL _add_col_if_missing('guild_deleted_messages', 'author_avatar', 'VARCHAR(1024) NOT NULL DEFAULT \'\')");
    migrations.push_back("CALL _add_col_if_missing('guild_deleted_messages', 'attachment_urls', 'TEXT')");

    migrations.push_back("CALL _add_col_if_missing('users','fish_caught','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','fish_sold','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','gambling_wins','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','gambling_losses','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','commands_used','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','daily_streak','INT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','work_count','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','ores_mined','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','items_crafted','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','trades_completed','BIGINT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','prestige','INT NOT NULL DEFAULT 0')");
    migrations.push_back("CALL _add_col_if_missing('users','passive','BOOLEAN NOT NULL DEFAULT FALSE')");

    migrations.push_back(R"SQL(
ALTER TABLE guild_deleted_messages 
MODIFY COLUMN author_tag VARCHAR(256) NOT NULL DEFAULT '',
MODIFY COLUMN author_avatar VARCHAR(1024) NOT NULL DEFAULT ''
)SQL");

    // Quiet Moderation Expansion
    migrations.push_back("CALL _add_col_if_missing('guild_infraction_config', 'quiet_global', 'BOOLEAN NOT NULL DEFAULT TRUE')");
    migrations.push_back("CALL _add_col_if_missing('guild_infraction_config', 'quiet_overrides', 'JSON NOT NULL DEFAULT (\'{}\')')");
    migrations.push_back("CALL _add_col_if_missing('guild_infraction_config', 'case_counter', 'INT NOT NULL DEFAULT 1')");
    migrations.push_back("CALL _add_col_if_missing('guild_infraction_config', 'require_reason', 'BOOLEAN NOT NULL DEFAULT FALSE')");

    // ── Interaction Roles & Panels ──────────────────────────────────
    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_interaction_panels (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    name VARCHAR(100) NOT NULL,
    description TEXT,
    panel_type ENUM('button', 'select') NOT NULL DEFAULT 'button',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_interaction_roles (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    panel_id BIGINT UNSIGNED NOT NULL,
    role_id BIGINT UNSIGNED NOT NULL,
    label VARCHAR(80) NOT NULL,
    emoji_raw VARCHAR(200),
    style TINYINT DEFAULT 1,
    INDEX idx_panel (panel_id),
    CONSTRAINT fk_panel_id FOREIGN KEY (panel_id) REFERENCES guild_interaction_panels(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    // ── Ticket System ───────────────────────────────────────────────
    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_ticket_panels (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    name VARCHAR(100) NOT NULL,
    category_id BIGINT UNSIGNED,
    support_role_id BIGINT UNSIGNED,
    ticket_type ENUM('channel', 'thread') NOT NULL DEFAULT 'channel',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_active_tickets (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    panel_id BIGINT UNSIGNED NOT NULL,
    status ENUM('open', 'closed', 'claimed') NOT NULL DEFAULT 'open',
    claimed_by BIGINT UNSIGNED,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_user (guild_id, user_id),
    CONSTRAINT fk_ticket_panel_id FOREIGN KEY (panel_id) REFERENCES guild_ticket_panels(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    // ── Autominer tracking table ─────────────────────────────────────
    // One row per user; active=TRUE means the timer loop will process them.
    // Users are inserted here when they first activate their autominer gear.
    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS user_autominers (
    user_id  BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    active   TINYINT(1) NOT NULL DEFAULT 1,
    last_run TIMESTAMP NULL DEFAULT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
)SQL");

    // ── Mod-mail System ──────────────────────────────────────────────
    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_modmail_config (
    guild_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    category_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    staff_role_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    log_channel_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    enabled BOOLEAN NOT NULL DEFAULT FALSE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS modmail_threads (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    thread_id BIGINT UNSIGNED NOT NULL,
    status ENUM('open', 'closed') NOT NULL DEFAULT 'open',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    closed_at TIMESTAMP NULL DEFAULT NULL,
    INDEX idx_user_guild (user_id, guild_id),
    INDEX idx_thread (thread_id),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
)SQL");

    return migrations;
}


} // namespace db
} // namespace bronx
