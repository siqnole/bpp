#include "database.h"
#include "connection_pool.h"
#include "../operations/user/user_operations.h"
#include "../operations/user/cooldown_operations.h"
#include "../operations/economy/inventory_operations.h"
#include "../operations/leveling/leaderboard_operations.h"
#include "../utils/database_utility.h"
#include <iostream>
#include <cstring>
#include <climits>
#include <cmath>
#include <chrono>
#include "../log.h"

namespace bronx {
namespace db {

// ============================================================================
// DATABASE IMPLEMENTATION
// ============================================================================

Database::Database(const DatabaseConfig& config)
    : config_(config), connection_debug_(config.log_connections) {
}

Database::~Database() {
    disconnect();
}

bool Database::connect() {
    if (connected_) return true;
    
    try {
        pool_ = std::make_unique<ConnectionPool>(config_);
        // propagate any debug flag which may have been set before connect
        ConnectionPool::set_verbose_logging(connection_debug_);
        // since the pool no longer pre‑creates connections, test by grabbing one
        auto test_conn = pool_->acquire();
        connected_ = (test_conn != nullptr);
        if (connected_) {
            std::cout << "Database connected successfully" << std::endl;
            // release immediately (shared_ptr destructor closes it)
            test_conn.reset();

            // ============================================================
            // SCHEMA MIGRATIONS — batched onto a single connection for speed
            // ============================================================
            auto migration_start = std::chrono::steady_clock::now();
            std::vector<std::string> migrations;

            // Helper: MySQL-compatible "ADD COLUMN IF NOT EXISTS" via stored procedure
            migrations.push_back(
                "DROP PROCEDURE IF EXISTS _add_col_if_missing");
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
            migrations.push_back(
                "DROP PROCEDURE IF EXISTS _drop_fk_if_exists");
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

            // --- table creation ---
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

            // --- progressive jackpot ---
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
            migrations.push_back(
                "INSERT IGNORE INTO progressive_jackpot (id, pool) VALUES (1, 0)");
            migrations.push_back(
                "CREATE TABLE IF NOT EXISTS jackpot_history ("
                "id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
                "user_id BIGINT UNSIGNED NOT NULL,"
                "amount BIGINT NOT NULL,"
                "pool_before BIGINT NOT NULL DEFAULT 0,"
                "won_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "INDEX idx_jackpot_history_user (user_id),"
                "INDEX idx_jackpot_history_time (won_at)) ENGINE=InnoDB");

            // --- world events ---
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

            // --- column additions (MySQL-compatible) ---
            migrations.push_back("CALL _add_col_if_missing('inventory','level','INT NOT NULL DEFAULT 1')");
            migrations.push_back("CALL _add_col_if_missing('inventory','metadata','TEXT')");
            migrations.push_back("CALL _add_col_if_missing('shop_items','required_level','INT NOT NULL DEFAULT 0')");
            migrations.push_back("CALL _add_col_if_missing('shop_items','level','INT NOT NULL DEFAULT 1')");
            migrations.push_back("CALL _add_col_if_missing('shop_items','usable','BOOLEAN NOT NULL DEFAULT FALSE')");
            migrations.push_back("CALL _add_col_if_missing('shop_items','metadata','TEXT')");

            // --- data migrations ---
            // Fix zero-datetime rows that block ALTER under strict mode
            migrations.push_back(
                "UPDATE inventory SET acquired_at = NOW() "
                "WHERE acquired_at = '0000-00-00 00:00:00' OR acquired_at IS NULL");
            // Expand inventory item_type ENUM to cover all shop categories
            migrations.push_back(
                "ALTER TABLE inventory MODIFY COLUMN item_type "
                "ENUM('potion','upgrade','rod','bait','collectible','other',"
                "'automation','boosts','title','tools','pickaxe','minecart','bag') NOT NULL");
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id = s.item_id "
                "SET i.item_type = s.category");
            migrations.push_back(
                "UPDATE shop_items SET metadata = JSON_SET(COALESCE(metadata,'{}'), '$.bonus', 0) "
                "WHERE category = 'bait' AND (metadata IS NULL OR JSON_EXTRACT(metadata,'$.bonus') IS NULL)");
            migrations.push_back(
                "UPDATE shop_items SET metadata = JSON_SET(COALESCE(metadata,'{}'), '$.multiplier', 0) "
                "WHERE category = 'bait' AND (metadata IS NULL OR JSON_EXTRACT(metadata,'$.multiplier') IS NULL)");
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id = s.item_id "
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
                "UPDATE inventory i JOIN shop_items s ON i.item_id = s.item_id "
                "SET i.metadata = JSON_REMOVE(i.metadata, '$.unlocks[0]') "
                "WHERE s.item_id = 'bait_rare' AND JSON_CONTAINS(i.metadata,'\"common fish\"','$.unlocks')");
            migrations.push_back(
                "UPDATE shop_items SET metadata = JSON_ARRAY_APPEND(metadata,'$.unlocks','\"abyssal leviathan\"') "
                "WHERE item_id='bait_epic' AND NOT JSON_CONTAINS(metadata,'\"abyssal leviathan\"','$.unlocks')");
            migrations.push_back(
                "UPDATE shop_items SET metadata = JSON_ARRAY_APPEND(metadata,'$.unlocks','\"celestial kraken\"') "
                "WHERE item_id='bait_legendary' AND NOT JSON_CONTAINS(metadata,'\"celestial kraken\"','$.unlocks')");
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id=s.item_id "
                "SET i.metadata = JSON_ARRAY_APPEND(i.metadata,'$.unlocks','\"abyssal leviathan\"') "
                "WHERE s.item_id='bait_epic' AND NOT JSON_CONTAINS(i.metadata,'\"abyssal leviathan\"','$.unlocks')");
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id=s.item_id "
                "SET i.metadata = JSON_ARRAY_APPEND(i.metadata,'$.unlocks','\"celestial kraken\"') "
                "WHERE s.item_id='bait_legendary' AND NOT JSON_CONTAINS(i.metadata,'\"celestial kraken\"','$.unlocks')");

            // --- legacy fishing item migration ---
            migrations.push_back("UPDATE inventory SET item_id='rod_wood', level=1 WHERE item_id='fishing_rod'");
            migrations.push_back("UPDATE inventory SET item_id='bait_common', level=1 WHERE item_id='bait'");
            migrations.push_back("UPDATE fish_catches SET rod_id='rod_wood' WHERE rod_id='fishing_rod'");
            migrations.push_back("UPDATE fish_catches SET bait_id='bait_common' WHERE bait_id='bait'");
            migrations.push_back("UPDATE active_fishing_gear SET active_rod_id='rod_wood' WHERE active_rod_id='fishing_rod'");
            migrations.push_back("UPDATE active_fishing_gear SET active_bait_id='bait_common' WHERE active_bait_id='bait'");
            migrations.push_back("DELETE FROM shop_items WHERE item_id IN ('fishing_rod','bait')");

            // --- seed shop items ---
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

            // --- sync inventory levels ---
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id=s.item_id "
                "SET i.level = s.level "
                "WHERE i.item_id LIKE 'rod_%' OR i.item_id LIKE 'bait_%'");
            migrations.push_back(
                "UPDATE inventory i JOIN shop_items s ON i.item_id = s.item_id "
                "SET i.metadata = s.metadata "
                "WHERE s.category = 'bait'");

            // --- adjust shop levels + metadata ---
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

            // --- fix inventory bait levels ---
            migrations.push_back(
                "UPDATE inventory SET level = CASE item_id "
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

            // --- adjust inventory levels for rods + baits ---
            migrations.push_back(R"SQL(
UPDATE inventory SET level = CASE item_id
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

            // --- seed mining shop items (pickaxes, minecarts, bags) ---
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

            // --- migrate existing inventory pickaxes to include multiore_chance/multiore_max ---
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.10, '$.multiore_max', 1)
WHERE item_id = 'pickaxe_diamond' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.15, '$.multiore_max', 2)
WHERE item_id = 'pickaxe_p1' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.20, '$.multiore_max', 2)
WHERE item_id = 'pickaxe_p2' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.25, '$.multiore_max', 3)
WHERE item_id = 'pickaxe_p3' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.30, '$.multiore_max', 4)
WHERE item_id = 'pickaxe_p4' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");
            migrations.push_back(R"SQL(
UPDATE inventory SET metadata = JSON_SET(metadata, '$.multiore_chance', 0.35, '$.multiore_max', 5)
WHERE item_id = 'pickaxe_p5' AND (metadata NOT LIKE '%multiore_chance%' OR metadata IS NULL)
)SQL");

            // --- seed autofisher shop items ---
            migrations.push_back(R"SQL(
INSERT INTO shop_items (item_id, name, description, category, price, max_quantity, required_level, level, usable, metadata) VALUES
('auto_fisher_1','Basic Autofisher','automatically catches fish while you are away (slower rate)','automation',5000000,NULL,0,1,TRUE,'{"tier":1,"rate":300}'),
('auto_fisher_2','Advanced Autofisher','upgraded autofisher with faster catch rate','automation',25000000,NULL,0,2,TRUE,'{"tier":2,"rate":180}')
ON DUPLICATE KEY UPDATE price=VALUES(price),name=VALUES(name),description=VALUES(description),metadata=VALUES(metadata),level=VALUES(level)
)SQL");

            // ─── GUILD STATS TABLES ─────────────────────────────────────────
            // Track member joins/leaves
            migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_member_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('join','leave') NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at)
) ENGINE=InnoDB
)SQL");
            // Track message events (message, edit, delete)
            migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_message_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    channel_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('message','edit','delete') NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at),
    INDEX idx_guild_user (guild_id, user_id)
) ENGINE=InnoDB
)SQL");
            // Track command usage per day per channel
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
            // Daily aggregated stats for faster dashboard queries (optional rollup table)
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
            // Track voice channel join/leave events
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
            // Track server boost/unboost events
            migrations.push_back(R"SQL(
CREATE TABLE IF NOT EXISTS guild_boost_events (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    guild_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    event_type ENUM('boost','unboost') NOT NULL,
    boost_id VARCHAR(32) NOT NULL DEFAULT '',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_guild_type (guild_id, event_type),
    INDEX idx_guild_date (guild_id, created_at)
) ENGINE=InnoDB
)SQL");

            // Run all migrations on a single connection for speed
            int ok = execute_batch(migrations);
            auto migration_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - migration_start);
            std::cout << "Schema migrations: " << ok << "/" << migrations.size()
                      << " succeeded in " << migration_elapsed.count() << "ms\n";

            // end migration
            // ------------------------------------------------------------------
        }
        
        return connected_;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        std::cerr << CLR_ERR << "Database connection failed: " << e.what() << CLR_RST << std::endl;
        return false;
    }
}

void Database::disconnect() {
    pool_.reset();
    connected_ = false;
}

bool Database::is_connected() const {
    return connected_;
}

void Database::set_inventory_debug(bool on) {
    inventory_debug_ = on;
}

bool Database::get_inventory_debug() const {
    return inventory_debug_;
}

void Database::set_connection_debug(bool on) {
    connection_debug_ = on;
    // if a pool already exists, update its global flag as well
    if (pool_) {
        ConnectionPool::set_verbose_logging(on);
    }
}

bool Database::get_connection_debug() const {
    return connection_debug_;
}

// ========================================
// FISHING GEAR OPERATIONS
// ========================================

std::pair<std::string, std::string> Database::get_active_fishing_gear(uint64_t user_id) {
    // retrieve the currently equipped rod and bait for the user
    // earlier versions of this function repeatedly crashed for a certain
    // problematic user due to invalid connections or a corrupt database row.
    // we now wrap the entire sequence in error handling, check pool acquisition,
    // and perform validation of returned item ids.  if the row contains an
    // unrecognized item, we automatically clear it to avoid future problems.

    std::pair<std::string,std::string> gear{
        "",""
    };
    // acquire connection separately to limit scope of exception logging
    std::shared_ptr<Connection> conn;
    try {
        conn = pool_->acquire();
    } catch (const std::exception &e) {
        std::cerr << DBG_DB CLR_WARN "get_active_fishing_gear: pool acquire threw: " CLR_RST << e.what() << "\n";
        // treat as unequipped
        return gear;
    } catch (...) {
        std::cerr << DBG_DB CLR_WARN "get_active_fishing_gear: pool acquire threw unknown exception" CLR_RST "\n";
        return gear;
    }
    if (!conn) {
        log_error("get_active_fishing_gear acquire returned null");
        return gear;
    }
    
    try {
        const char* query =
            "SELECT active_rod_id, active_bait_id FROM active_fishing_gear WHERE user_id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (!stmt) {
            log_error("get_active_fishing_gear init stmt");
            pool_->release(conn);
            return gear;
        }
        if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
            log_error("get_active_fishing_gear prepare");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return gear;
        }
        MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[0].buffer = (char*)&user_id;
        bind[0].is_unsigned = 1;
        mysql_stmt_bind_param(stmt, bind);
        if (mysql_stmt_execute(stmt) != 0) {
            log_error("get_active_fishing_gear execute");
            mysql_stmt_close(stmt);
            pool_->release(conn);
            return gear;
        }
        MYSQL_BIND result[2]; memset(result, 0, sizeof(result));
        char rod_buf[101]; unsigned long rod_len;
        char bait_buf[101]; unsigned long bait_len;
        my_bool rod_null = false, bait_null = false;
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = rod_buf;
        result[0].buffer_length = sizeof(rod_buf);
        result[0].is_null = &rod_null;
        result[0].length = &rod_len;
        result[1].buffer_type = MYSQL_TYPE_STRING;
        result[1].buffer = bait_buf;
        result[1].buffer_length = sizeof(bait_buf);
        result[1].is_null = &bait_null;
        result[1].length = &bait_len;
        mysql_stmt_bind_result(stmt, result);
        if (mysql_stmt_fetch(stmt) == 0) {
            if (!rod_null) gear.first = std::string(rod_buf, rod_len);
            if (!bait_null) gear.second = std::string(bait_buf, bait_len);
        }
        mysql_stmt_close(stmt);
        pool_->release(conn);

        // validate the ids against the shop catalog; if an id is no longer
        // valid we clear it (both in-memory and in the database) to avoid
        // repeated errors for the same user.  this handles the "corrupt row"
        // scenario mentioned in earlier comments.
        if (!gear.first.empty() && !get_shop_item(gear.first)) {
            std::cerr << DBG_DB CLR_WARN "get_active_fishing_gear: invalid rod id '" CLR_RST << gear.first
                      << CLR_WARN "' for user " CLR_RST CLR_USER << user_id << CLR_RST CLR_WARN ", clearing" CLR_RST << std::endl;
            set_active_rod(user_id, "");
            gear.first.clear();
        }
        if (!gear.second.empty() && !get_shop_item(gear.second)) {
            std::cerr << DBG_DB CLR_WARN "get_active_fishing_gear: invalid bait id '" CLR_RST << gear.second
                      << CLR_WARN "' for user " CLR_RST CLR_USER << user_id << CLR_RST CLR_WARN ", clearing" CLR_RST << std::endl;
            set_active_bait(user_id, "");
            gear.second.clear();
        }

        return gear;
    } catch (const std::exception& e) {
        std::cerr << DBG_DB CLR_ERR "get_active_fishing_gear exception: " CLR_RST << e.what() << std::endl;
        return gear;
    }
}


bool Database::set_active_rod(uint64_t user_id, const std::string& rod_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("set_active_rod acquire failed");
        return false;
    }
    
    // Use UPSERT to avoid race conditions between UPDATE and INSERT
    const char* upsert_q = 
        "INSERT INTO active_fishing_gear (user_id, active_rod_id) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE active_rod_id = VALUES(active_rod_id)";
    
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt) {
        log_error("set_active_rod stmt init failed");
        pool_->release(conn);
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, upsert_q, strlen(upsert_q)) != 0) {
        std::string err = mysql_stmt_error(stmt);
        log_error("set_active_rod prepare: " + err);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)rod_id.c_str();
    bind[1].buffer_length = rod_id.length();
    
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    
    if (!success) {
        std::string err = mysql_stmt_error(stmt);
        log_error("set_active_rod execute: " + err);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::set_active_bait(uint64_t user_id, const std::string& bait_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("set_active_bait acquire failed");
        return false;
    }
    const char* upsert_q =
        "INSERT INTO active_fishing_gear (user_id, active_bait_id) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE active_bait_id = VALUES(active_bait_id)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, upsert_q, strlen(upsert_q)) != 0) {
        log_error("set_active_bait upsert prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)bait_id.c_str();
    bind[1].buffer_length = bait_id.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("set_active_bait upsert execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}


// record anonymous fishing log data for ML
bool Database::record_fishing_log(int rod_level, int bait_level, int64_t net_profit) {
    auto conn = pool_->acquire();
    const char* insert_q =
        "INSERT INTO fishing_logs (rod_level, bait_level, net_profit) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, insert_q, strlen(insert_q)) != 0) {
        log_error("record_fishing_log prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&rod_level;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&bait_level;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&net_profit;
    bind[2].is_unsigned = 0;
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("record_fishing_log execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}


bool Database::tune_bait_prices_from_logs(int min_samples) {
    // optional scale factor controlled via ml_settings
    double scale = 1.0;
    if (auto s = get_ml_setting("tune_scale"); s && !s->empty()) {
        try { scale = std::stod(*s); } catch(...) { }
    }
    // optional price bounds (global or per-bait-level)
    auto parse_bound = [&](const std::string &key, int level, int64_t def)->int64_t {
        int64_t result = def;
        if (auto s = get_ml_setting(key); s && !s->empty()) {
            try { result = std::stoll(*s); } catch(...) { }
        }
        // try level-specific key if not equal to def or simply override
        std::string lvlkey = key + "_" + std::to_string(level);
        if (auto s2 = get_ml_setting(lvlkey); s2 && !s2->empty()) {
            try { result = std::stoll(*s2); } catch(...) { }
        }
        return result;
    };
    int64_t price_min = parse_bound("bait_price_min", 0, 1);
    int64_t price_max = parse_bound("bait_price_max", 0, LLONG_MAX);
    auto conn = pool_->acquire();
    const char* sel = "SELECT bait_level, AVG(net_profit), COUNT(*) FROM fishing_logs GROUP BY bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("tune_bait_prices select prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("tune_bait_prices select execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND res[3];
    memset(res, 0, sizeof(res));
    int bait_level;
    double avg_profit;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_DOUBLE;
    res[1].buffer = (char*)&avg_profit;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    // optional baseline profit target; adjustments are computed against this
    double profit_target = 0.0;
    if (auto t = get_ml_setting("tune_target"); t && !t->empty()) {
        try { profit_target = std::stod(*t); } catch(...) { }
    }

    // optional fixed decay applied after all individual adjustments
    int64_t fixed_decay = 0;
    if (auto d = get_ml_setting("tune_decay"); d && !d->empty()) {
        try { fixed_decay = std::stoll(*d); } catch(...) { }
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        if (cnt < min_samples) continue; // not enough data to trust
        // compute profit relative to target
        double profit = avg_profit - profit_target;
        if (profit == 0.0 && fixed_decay == 0) continue; // nothing to do

        // apply logarithmic scaling so huge profits move more slowly
        double adj_base;
        if (profit >= 0) adj_base = log(profit + 1.0);
        else adj_base = -log(-profit + 1.0);

        int64_t adjust = (int64_t)(adj_base / 10.0 * scale);
        if (adjust == 0) {
            // even if adjust rounds to zero, we might still want a fixed decay;
            if (fixed_decay == 0) continue;
        }
        // recalc per-level bounds (global may be overridden)
        int64_t min_for_level = parse_bound("bait_price_min", bait_level, price_min);
        int64_t max_for_level = parse_bound("bait_price_max", bait_level, price_max);
        std::string upd = "UPDATE shop_items SET price = LEAST("
                          + std::to_string(max_for_level) + ", GREATEST("
                          + std::to_string(min_for_level) + ", price + " + std::to_string(adjust) + "))"
                          " WHERE category='bait' AND level=" + std::to_string(bait_level);
        execute(upd.c_str());
        // record change history
        std::string logq = "INSERT INTO ml_price_changes (bait_level,adjust) VALUES (" +
                           std::to_string(bait_level) + "," + std::to_string(adjust) + ")";
        execute(logq.c_str());
    }

    // apply fixed decay across all bait levels (clamped to zero)
    if (fixed_decay > 0) {
        std::string decay_q = "UPDATE shop_items SET price = GREATEST(price - " +
                              std::to_string(fixed_decay) + ", 0) WHERE category='bait'";
        execute(decay_q.c_str());
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return true;
}

std::string Database::get_bait_tuning_report(int min_samples) {
    std::string report;
    auto conn = pool_->acquire();
    const char* sel = "SELECT bait_level, AVG(net_profit), COUNT(*) FROM fishing_logs GROUP BY bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_bait_tuning_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error preparing report)";
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_bait_tuning_report execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error executing report)";
    }
    MYSQL_BIND res[3];
    memset(res, 0, sizeof(res));
    int bait_level;
    double avg_profit;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_DOUBLE;
    res[1].buffer = (char*)&avg_profit;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    report += "level | samples | avg profit | suggested change\n";
    report += "-----+---------+------------+----------------\n";
    while (mysql_stmt_fetch(stmt) == 0) {
        report += std::to_string(bait_level) + "     | ";
        report += std::to_string(cnt) + "       | ";
        report += std::to_string((int64_t)avg_profit) + "       | ";
        if (cnt < min_samples) report += "(insufficient)";
        else {
            int64_t adjust = (int64_t)(avg_profit / 10.0);
            if (adjust>0) report += "+" + std::to_string(adjust);
            else if (adjust<0) report += std::to_string(adjust);
            else report += "(none)";
        }
        report += "\n";
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return report;
}

// produce a human-readable report of price adjustments over the past N hours
std::string Database::get_ml_effect_report(int hours) {
    std::string report;
    auto conn = pool_->acquire();
    // join with shop_items to get bait name for each level
    const char* sel =
        "SELECT m.bait_level, COALESCE(s.name, CONCAT('level',m.bait_level)), SUM(m.adjust), COUNT(*) "
        "FROM ml_price_changes m "
        "LEFT JOIN shop_items s ON s.category='bait' AND s.level=m.bait_level "
        "WHERE m.changed_at >= DATE_SUB(NOW(), INTERVAL ? HOUR) "
        "GROUP BY m.bait_level";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_ml_effect_report prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error preparing report)";
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&hours;
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_ml_effect_report execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return "(error executing report)";
    }
    MYSQL_BIND res[4]; memset(res,0,sizeof(res));
    int bait_level;
    char namebuf[128]; unsigned long namelen = 0; my_bool name_null = false;
    long long sum_adjust;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_STRING;
    res[1].buffer = namebuf;
    res[1].buffer_length = sizeof(namebuf);
    res[1].is_null = &name_null;
    res[1].length = &namelen;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG;
    res[2].buffer = (char*)&sum_adjust;
    res[3].buffer_type = MYSQL_TYPE_LONGLONG;
    res[3].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    report += "bait   | adjustments | total change\n";
    report += "-------+-------------+--------------\n";
    while (mysql_stmt_fetch(stmt) == 0) {
        std::string name = name_null ? ("level" + std::to_string(bait_level)) : std::string(namebuf, namelen);
        report += name + " | ";
        report += std::to_string(cnt) + "           | ";
        report += (sum_adjust>=0?"+":"") + std::to_string(sum_adjust) + "\n";
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return report;
}

// ml settings support
std::optional<std::string> Database::get_ml_setting(const std::string& key) {
    auto conn = pool_->acquire();
    const char* sel = "SELECT `value` FROM ml_settings WHERE `key` = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        log_error("get_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    mysql_stmt_bind_param(stmt, bind);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_ml_setting execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return std::nullopt;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    char buf[1024]; unsigned long len; my_bool isnull=false;
    res[0].buffer_type = MYSQL_TYPE_STRING;
    res[0].buffer = buf;
    res[0].buffer_length = sizeof(buf);
    res[0].is_null = &isnull;
    res[0].length = &len;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) == 0 && !isnull) {
        std::string val(buf,len);
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return val;
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return std::nullopt;
}

bool Database::set_ml_setting(const std::string& key, const std::string& value) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO ml_settings (`key`,`value`) VALUES (?,?) ON DUPLICATE KEY UPDATE `value`=VALUES(`value`)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("set_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)value.c_str();
    bind[1].buffer_length = value.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt)==0);
    if (!success) log_error("set_ml_setting execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::delete_ml_setting(const std::string& key) {
    auto conn = pool_->acquire();
    const char* q = "DELETE FROM ml_settings WHERE `key` = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("delete_ml_setting prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)key.c_str();
    bind[0].buffer_length = key.length();
    mysql_stmt_bind_param(stmt, bind);
    bool success = (mysql_stmt_execute(stmt)==0);
    if (!success) log_error("delete_ml_setting execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<std::pair<std::string,std::string>> Database::list_ml_settings() {
    std::vector<std::pair<std::string,std::string>> out;
    auto conn = pool_->acquire();
    const char* q = "SELECT `key`,`value` FROM ml_settings";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("list_ml_settings prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("list_ml_settings execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return out;
    }
    MYSQL_BIND res[2]; memset(res,0,sizeof(res));
    char kbuf[64]; unsigned long klen;
    char vbuf[1024]; unsigned long vlen;
    res[0].buffer_type = MYSQL_TYPE_STRING; res[0].buffer = kbuf; res[0].buffer_length = sizeof(kbuf); res[0].length=&klen;
    res[1].buffer_type = MYSQL_TYPE_STRING; res[1].buffer = vbuf; res[1].buffer_length = sizeof(vbuf); res[1].length=&vlen;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        out.emplace_back(std::string(kbuf,klen), std::string(vbuf,vlen));
    }
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return out;
}

// lottery entry helpers
bool Database::update_lottery_tickets(uint64_t user_id, int64_t tickets) {
    auto conn = pool_->acquire();
    const char* q = "INSERT INTO lottery_entries (user_id, tickets) VALUES (?,?) "
                    "ON DUPLICATE KEY UPDATE tickets = tickets + VALUES(tickets)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("update_lottery_tickets prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[2]; memset(bind,0,sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&tickets;
    mysql_stmt_bind_param(stmt, bind);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("update_lottery_tickets execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

int64_t Database::get_lottery_user_count() {
    auto conn = pool_->acquire();
    const char* q = "SELECT COUNT(*) FROM lottery_entries";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_lottery_user_count prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_lottery_user_count execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    int64_t count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) != 0) count = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return count;
}

int64_t Database::get_lottery_total_tickets() {
    auto conn = pool_->acquire();
    const char* q = "SELECT COALESCE(SUM(tickets),0) FROM lottery_entries";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_lottery_total_tickets prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_lottery_total_tickets execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND res[1]; memset(res,0,sizeof(res));
    int64_t sum = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&sum;
    mysql_stmt_bind_result(stmt, res);
    if (mysql_stmt_fetch(stmt) != 0) sum = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return sum;
}

// ============================================================================
// AUTOFISHER OPERATIONS
// ============================================================================

bool Database::has_autofisher(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("has_autofisher acquire failed");
        return false;
    }
    const char* q = "SELECT count FROM autofishers WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("has_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("has_autofisher execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int count = 0;
    res[0].buffer_type = MYSQL_TYPE_LONG;
    res[0].buffer = (char*)&count;
    mysql_stmt_bind_result(stmt, res);
    
    bool has = (mysql_stmt_fetch(stmt) == 0 && count > 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return has;
}

bool Database::create_autofisher(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("create_autofisher acquire failed");
        return false;
    }
    const char* q = "INSERT INTO autofishers (user_id, count, active) VALUES (?, 1, FALSE) "
                    "ON DUPLICATE KEY UPDATE count = count + 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("create_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("create_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::upgrade_autofisher_efficiency(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("upgrade_autofisher_efficiency acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET efficiency_level = efficiency_level + 1, "
                    "efficiency_multiplier = 1.00 + (efficiency_level + 1) * 0.10 "
                    "WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("upgrade_autofisher_efficiency prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("upgrade_autofisher_efficiency execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int64_t Database::get_autofisher_balance(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_autofisher_balance acquire failed");
        return 0;
    }
    const char* q = "SELECT balance FROM autofishers WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_balance prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_balance execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int64_t balance = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&balance;
    mysql_stmt_bind_result(stmt, res);
    
    if (mysql_stmt_fetch(stmt) != 0) balance = 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return balance;
}

bool Database::deposit_to_autofisher(uint64_t user_id, int64_t amount) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("deposit_to_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET balance = balance + ?, total_deposited = total_deposited + ? "
                    "WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("deposit_to_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&amount;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&user_id;
    bind[2].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("deposit_to_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::withdraw_from_autofisher(uint64_t user_id, int64_t amount) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("withdraw_from_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET balance = balance - ? "
                    "WHERE user_id = ? AND balance >= ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("withdraw_from_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[3]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&amount;
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (char*)&user_id;
    bind[1].is_unsigned = 1;
    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = (char*)&amount;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0);
    if (!success) log_error("withdraw_from_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::vector<uint64_t> Database::get_all_active_autofishers() {
    std::vector<uint64_t> result;
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_all_active_autofishers acquire failed");
        return result;
    }
    const char* q = "SELECT user_id FROM autofishers WHERE active = TRUE";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_all_active_autofishers prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_all_active_autofishers execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    uint64_t user_id = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&user_id;
    res[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, res);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        result.push_back(user_id);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::activate_autofisher(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("activate_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET active = TRUE WHERE user_id = ? AND count > 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("activate_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("activate_autofisher execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    bool success = (mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

bool Database::deactivate_autofisher(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("deactivate_autofisher acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET active = FALSE WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("deactivate_autofisher prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("deactivate_autofisher execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

int Database::get_autofisher_tier(uint64_t user_id) {
    // Determine tier from inventory (auto_fisher_1, auto_fisher_2 items)
    // Tier 2 > Tier 1, return highest tier available
    if (has_item(user_id, "auto_fisher_2", 1)) return 2;
    if (has_item(user_id, "auto_fisher_1", 1)) return 1;
    return 0;
}

bool Database::update_autofisher_last_run(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("update_autofisher_last_run acquire failed");
        return false;
    }
    const char* q = "UPDATE autofishers SET last_claim = NOW() WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("update_autofisher_last_run prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    bool success = (mysql_stmt_execute(stmt) == 0);
    if (!success) log_error("update_autofisher_last_run execute");
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return success;
}

std::optional<std::chrono::system_clock::time_point> Database::get_autofisher_last_run(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) {
        log_error("get_autofisher_last_run acquire failed");
        return {};
    }
    const char* q = "SELECT UNIX_TIMESTAMP(last_claim) FROM autofishers WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_last_run prepare");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (char*)&user_id;
    bind[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bind);
    
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_last_run execute");
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    
    MYSQL_BIND res[1]; memset(res, 0, sizeof(res));
    int64_t timestamp = 0;
    my_bool is_null = 0;
    res[0].buffer_type = MYSQL_TYPE_LONGLONG;
    res[0].buffer = (char*)&timestamp;
    res[0].is_null = &is_null;
    mysql_stmt_bind_result(stmt, res);
    
    std::optional<std::chrono::system_clock::time_point> result;
    if (mysql_stmt_fetch(stmt) == 0 && !is_null && timestamp > 0) {
        result = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ============================================================
// Autofisher v2 methods
// ============================================================

std::optional<AutofisherConfig> Database::get_autofisher_config(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("get_autofisher_config acquire"); return {}; }
    const char* q =
        "SELECT active, af_rod_id, af_bait_id, af_bait_qty, af_bait_level, af_bait_meta, "
        "max_bank_draw, auto_sell, as_trigger, as_threshold, bag_limit "
        "FROM autofishers WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("get_autofisher_config prepare");
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return {};
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &user_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("get_autofisher_config execute");
        mysql_stmt_close(stmt); pool_->release(conn); return {};
    }

    char rod_buf[101]={}, bait_buf[101]={}, trigger_buf[16]={}, bait_meta_buf[4096]={};
    unsigned long rod_len=0, bait_len=0, trigger_len=0, meta_len=0;
    my_bool rod_null=1, bait_null=1, trigger_null=1, meta_null=1;
    int8_t active_v=0, auto_sell_v=0;
    int bait_qty=0, bait_level=1, bag_limit=10;
    int64_t max_bank_draw=0, as_threshold=0;

    MYSQL_BIND br[11]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_TINY;   br[0].buffer=&active_v;
    br[1].buffer_type=MYSQL_TYPE_STRING; br[1].buffer=rod_buf;   br[1].buffer_length=sizeof(rod_buf);   br[1].is_null=&rod_null;     br[1].length=&rod_len;
    br[2].buffer_type=MYSQL_TYPE_STRING; br[2].buffer=bait_buf;  br[2].buffer_length=sizeof(bait_buf);  br[2].is_null=&bait_null;    br[2].length=&bait_len;
    br[3].buffer_type=MYSQL_TYPE_LONG;   br[3].buffer=&bait_qty;
    br[4].buffer_type=MYSQL_TYPE_LONG;   br[4].buffer=&bait_level;
    br[5].buffer_type=MYSQL_TYPE_STRING; br[5].buffer=bait_meta_buf; br[5].buffer_length=sizeof(bait_meta_buf); br[5].is_null=&meta_null; br[5].length=&meta_len;
    br[6].buffer_type=MYSQL_TYPE_LONGLONG; br[6].buffer=&max_bank_draw;
    br[7].buffer_type=MYSQL_TYPE_TINY;   br[7].buffer=&auto_sell_v;
    br[8].buffer_type=MYSQL_TYPE_STRING; br[8].buffer=trigger_buf; br[8].buffer_length=sizeof(trigger_buf); br[8].is_null=&trigger_null; br[8].length=&trigger_len;
    br[9].buffer_type=MYSQL_TYPE_LONGLONG; br[9].buffer=&as_threshold;
    br[10].buffer_type=MYSQL_TYPE_LONG;  br[10].buffer=&bag_limit;
    mysql_stmt_bind_result(stmt, br);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return {};
    }
    AutofisherConfig cfg;
    cfg.active        = (active_v != 0);
    cfg.tier          = get_autofisher_tier(user_id);
    cfg.af_rod_id     = rod_null  ? "" : std::string(rod_buf,  rod_len);
    cfg.af_bait_id    = bait_null ? "" : std::string(bait_buf, bait_len);
    cfg.af_bait_qty   = bait_qty;
    cfg.af_bait_level = bait_level;
    cfg.af_bait_meta  = meta_null ? "" : std::string(bait_meta_buf, meta_len);
    cfg.max_bank_draw = max_bank_draw;
    cfg.auto_sell     = (auto_sell_v != 0);
    cfg.as_trigger    = trigger_null ? "bag" : std::string(trigger_buf, trigger_len);
    cfg.as_threshold  = as_threshold;
    cfg.bag_limit     = bag_limit;
    mysql_stmt_close(stmt); pool_->release(conn);
    return cfg;
}

bool Database::autofisher_set_rod(uint64_t user_id, const std::string& rod_id) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_rod acquire"); return false; }
    const char* q = "UPDATE autofishers SET af_rod_id = ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_rod prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    unsigned long rid_len = rod_id.size();
    bp[0].buffer_type=MYSQL_TYPE_STRING; bp[0].buffer=(void*)rod_id.c_str(); bp[0].length=&rid_len;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_rod execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_bait(uint64_t user_id, const std::string& bait_id, int level, const std::string& meta) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_bait acquire"); return false; }
    const char* q = "UPDATE autofishers SET af_bait_id=?, af_bait_level=?, af_bait_meta=? WHERE user_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    unsigned long bid_len=bait_id.size(), meta_len=meta.size();
    bp[0].buffer_type=MYSQL_TYPE_STRING;   bp[0].buffer=(void*)bait_id.c_str(); bp[0].length=&bid_len;
    bp[1].buffer_type=MYSQL_TYPE_LONG;     bp[1].buffer=&level;
    bp[2].buffer_type=MYSQL_TYPE_STRING;   bp[2].buffer=(void*)meta.c_str();    bp[2].length=&meta_len;
    bp[3].buffer_type=MYSQL_TYPE_LONGLONG; bp[3].buffer=&user_id; bp[3].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_deposit_bait(uint64_t user_id, int qty) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_deposit_bait acquire"); return false; }
    const char* q = "UPDATE autofishers SET af_bait_qty = af_bait_qty + ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_deposit_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONG;     bp[0].buffer=&qty;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_deposit_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_consume_bait(uint64_t user_id, int qty) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_consume_bait acquire"); return false; }
    const char* q = "UPDATE autofishers SET af_bait_qty = GREATEST(0, af_bait_qty - ?) WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_consume_bait prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONG;     bp[0].buffer=&qty;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_consume_bait execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_max_bank_draw(uint64_t user_id, int64_t amount) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_max_bank_draw acquire"); return false; }
    const char* q = "UPDATE autofishers SET max_bank_draw = ? WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_max_bank_draw prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&amount;
    bp[1].buffer_type=MYSQL_TYPE_LONGLONG; bp[1].buffer=&user_id; bp[1].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_max_bank_draw execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_set_autosell(uint64_t user_id, bool enabled, const std::string& trigger, int64_t threshold) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_set_autosell acquire"); return false; }
    const char* q = "UPDATE autofishers SET auto_sell=?, as_trigger=?, as_threshold=? WHERE user_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_set_autosell prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    int8_t en = enabled ? 1 : 0;
    unsigned long trig_len = trigger.size();
    bp[0].buffer_type=MYSQL_TYPE_TINY;     bp[0].buffer=&en;
    bp[1].buffer_type=MYSQL_TYPE_STRING;   bp[1].buffer=(void*)trigger.c_str(); bp[1].length=&trig_len;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&threshold;
    bp[3].buffer_type=MYSQL_TYPE_LONGLONG; bp[3].buffer=&user_id; bp[3].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_set_autosell execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

bool Database::autofisher_add_fish(uint64_t user_id, const std::string& fish_name, int64_t value, const std::string& metadata) {
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_add_fish acquire"); return false; }
    const char* q = "INSERT INTO autofisher_fish (user_id, fish_name, value, metadata) VALUES (?,?,?,?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_add_fish prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return false;
    }
    MYSQL_BIND bp[4]; memset(bp, 0, sizeof(bp));
    unsigned long name_len=fish_name.size(), meta_len=metadata.size();
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id;  bp[0].is_unsigned=1;
    bp[1].buffer_type=MYSQL_TYPE_STRING;   bp[1].buffer=(void*)fish_name.c_str(); bp[1].length=&name_len;
    bp[2].buffer_type=MYSQL_TYPE_LONGLONG; bp[2].buffer=&value;
    bp[3].buffer_type=MYSQL_TYPE_STRING;   bp[3].buffer=(void*)metadata.c_str();  bp[3].length=&meta_len;
    mysql_stmt_bind_param(stmt, bp);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    if (!ok) log_error("autofisher_add_fish execute");
    mysql_stmt_close(stmt); pool_->release(conn); return ok;
}

// ---------------------------------------------------------------------------
// Batch insert multiple fish into autofisher storage in one round-trip.
// ---------------------------------------------------------------------------
bool Database::autofisher_add_fish_batch(uint64_t user_id, const std::vector<AutofishFishRow>& rows) {
    if (rows.empty()) return true;
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_add_fish_batch acquire"); return false; }

    std::string sql = "INSERT INTO autofisher_fish (user_id, fish_name, value, metadata) VALUES ";
    std::string uid_str = std::to_string(user_id);

    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) sql += ',';
        char esc_name[201], esc_meta[8193];
        mysql_real_escape_string(conn->get(), esc_name, rows[i].fish_name.c_str(), rows[i].fish_name.size());
        mysql_real_escape_string(conn->get(), esc_meta, rows[i].metadata.c_str(),
                                 std::min(rows[i].metadata.size(), (size_t)4096));
        sql += '(';
        sql += uid_str;
        sql += ",'";
        sql += esc_name;
        sql += "',";
        sql += std::to_string(rows[i].value);
        sql += ",'";
        sql += esc_meta;
        sql += "')";
    }

    bool ok = (mysql_real_query(conn->get(), sql.c_str(), sql.size()) == 0);
    if (!ok) {
        last_error_ = mysql_error(conn->get());
        log_error("autofisher_add_fish_batch");
    }
    pool_->release(conn);
    return ok;
}

std::vector<AutofishFish> Database::autofisher_get_fish(uint64_t user_id) {
    std::vector<AutofishFish> out;
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_get_fish acquire"); return out; }
    const char* q = "SELECT id, fish_name, value, metadata FROM autofisher_fish WHERE user_id=? ORDER BY caught_at";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        log_error("autofisher_get_fish prepare"); if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) {
        log_error("autofisher_get_fish execute"); mysql_stmt_close(stmt); pool_->release(conn); return out;
    }
    uint64_t id=0; int64_t value=0;
    char name_buf[101]={}, meta_buf[4096]={};
    unsigned long name_len=0, meta_len=0;
    my_bool meta_null=1;
    MYSQL_BIND br[4]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&id; br[0].is_unsigned=1;
    br[1].buffer_type=MYSQL_TYPE_STRING;   br[1].buffer=name_buf; br[1].buffer_length=sizeof(name_buf); br[1].length=&name_len;
    br[2].buffer_type=MYSQL_TYPE_LONGLONG; br[2].buffer=&value;
    br[3].buffer_type=MYSQL_TYPE_STRING;   br[3].buffer=meta_buf; br[3].buffer_length=sizeof(meta_buf); br[3].is_null=&meta_null; br[3].length=&meta_len;
    mysql_stmt_bind_result(stmt, br);
    while (mysql_stmt_fetch(stmt) == 0) {
        AutofishFish f;
        f.id        = id;
        f.fish_name = std::string(name_buf, name_len);
        f.value     = value;
        f.metadata  = meta_null ? "" : std::string(meta_buf, meta_len);
        out.push_back(f);
    }
    mysql_stmt_close(stmt); pool_->release(conn); return out;
}

int Database::autofisher_fish_count(uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    const char* q = "SELECT COUNT(*) FROM autofisher_fish WHERE user_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
    mysql_stmt_bind_param(stmt, bp);
    if (mysql_stmt_execute(stmt) != 0) { mysql_stmt_close(stmt); pool_->release(conn); return 0; }
    int64_t count=0;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&count;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt); pool_->release(conn); return (int)count;
}

int64_t Database::autofisher_clear_fish(uint64_t user_id) {
    // Sum values then delete
    auto conn = pool_->acquire();
    if (!conn) { log_error("autofisher_clear_fish acquire"); return 0; }
    // sum
    int64_t total = 0;
    {
        const char* q = "SELECT COALESCE(SUM(value),0) FROM autofisher_fish WHERE user_id=?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (stmt && mysql_stmt_prepare(stmt, q, strlen(q)) == 0) {
            MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
            bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
            mysql_stmt_bind_param(stmt, bp);
            if (mysql_stmt_execute(stmt) == 0) {
                MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
                br[0].buffer_type=MYSQL_TYPE_LONGLONG; br[0].buffer=&total;
                mysql_stmt_bind_result(stmt, br);
                mysql_stmt_fetch(stmt);
            }
            mysql_stmt_close(stmt);
        }
    }
    // delete
    {
        const char* q = "DELETE FROM autofisher_fish WHERE user_id=?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
        if (stmt && mysql_stmt_prepare(stmt, q, strlen(q)) == 0) {
            MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
            bp[0].buffer_type=MYSQL_TYPE_LONGLONG; bp[0].buffer=&user_id; bp[0].is_unsigned=1;
            mysql_stmt_bind_param(stmt, bp);
            mysql_stmt_execute(stmt);
            mysql_stmt_close(stmt);
        }
    }
    pool_->release(conn);
    return total;
}

std::vector<Database::ActiveGiveawayRow> Database::get_active_giveaways() {
    std::vector<ActiveGiveawayRow> result;
    auto conn = pool_->acquire();
    if (!conn) return result;
    
    const char* q = "SELECT id, guild_id, channel_id, COALESCE(message_id, 0), created_by, "
                    "prize_amount, max_winners, UNIX_TIMESTAMP(ends_at) "
                    "FROM giveaways WHERE active = TRUE AND ends_at > NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    uint64_t id, guild_id, channel_id, message_id, created_by;
    int64_t prize;
    int max_winners;
    long long ends_at_ts;
    
    MYSQL_BIND br[8];
    memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &id;         br[0].is_unsigned = 1;
    br[1].buffer_type = MYSQL_TYPE_LONGLONG; br[1].buffer = &guild_id;   br[1].is_unsigned = 1;
    br[2].buffer_type = MYSQL_TYPE_LONGLONG; br[2].buffer = &channel_id; br[2].is_unsigned = 1;
    br[3].buffer_type = MYSQL_TYPE_LONGLONG; br[3].buffer = &message_id; br[3].is_unsigned = 1;
    br[4].buffer_type = MYSQL_TYPE_LONGLONG; br[4].buffer = &created_by; br[4].is_unsigned = 1;
    br[5].buffer_type = MYSQL_TYPE_LONGLONG; br[5].buffer = &prize;
    br[6].buffer_type = MYSQL_TYPE_LONG;     br[6].buffer = &max_winners;
    br[7].buffer_type = MYSQL_TYPE_LONGLONG; br[7].buffer = &ends_at_ts;
    
    if (mysql_stmt_bind_result(stmt, br) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    mysql_stmt_store_result(stmt);
    while (mysql_stmt_fetch(stmt) == 0) {
        ActiveGiveawayRow row;
        row.id = id;
        row.guild_id = guild_id;
        row.channel_id = channel_id;
        row.message_id = message_id;
        row.created_by = created_by;
        row.prize = prize;
        row.max_winners = max_winners;
        row.ends_at = std::chrono::system_clock::from_time_t(static_cast<time_t>(ends_at_ts));
        result.push_back(row);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

// ========================================
// GIVEAWAY OPERATIONS
// ========================================

int64_t Database::get_guild_balance(uint64_t guild_id) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* q = "SELECT balance FROM guild_balances WHERE guild_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return 0;
    }
    
    int64_t balance = 0;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &balance;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return balance;
}

bool Database::donate_to_guild(uint64_t user_id, uint64_t guild_id, int64_t amount) {
    if (amount <= 0) return false;
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    // Ensure guild_balances row exists
    const char* q1 = "INSERT IGNORE INTO guild_balances (guild_id, balance) VALUES (?, 0)";
    MYSQL_STMT* s1 = mysql_stmt_init(conn->get());
    if (s1 && mysql_stmt_prepare(s1, q1, strlen(q1)) == 0) {
        MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
        bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id; bp[0].is_unsigned = 1;
        mysql_stmt_bind_param(s1, bp);
        mysql_stmt_execute(s1);
    }
    if (s1) mysql_stmt_close(s1);
    
    // Add to guild balance
    const char* q2 = "UPDATE guild_balances SET balance = balance + ?, total_donated = total_donated + ? WHERE guild_id = ?";
    MYSQL_STMT* s2 = mysql_stmt_init(conn->get());
    if (!s2 || mysql_stmt_prepare(s2, q2, strlen(q2)) != 0) {
        if (s2) mysql_stmt_close(s2);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bp2[3]; memset(bp2, 0, sizeof(bp2));
    bp2[0].buffer_type = MYSQL_TYPE_LONGLONG; bp2[0].buffer = &amount;
    bp2[1].buffer_type = MYSQL_TYPE_LONGLONG; bp2[1].buffer = &amount;
    bp2[2].buffer_type = MYSQL_TYPE_LONGLONG; bp2[2].buffer = &guild_id; bp2[2].is_unsigned = 1;
    mysql_stmt_bind_param(s2, bp2);
    
    bool ok = mysql_stmt_execute(s2) == 0;
    mysql_stmt_close(s2);
    pool_->release(conn);
    return ok;
}

uint64_t Database::create_giveaway(uint64_t guild_id, uint64_t channel_id, uint64_t created_by,
                                   int64_t prize, int max_winners, int duration_seconds) {
    auto conn = pool_->acquire();
    if (!conn) return 0;
    
    const char* q = "INSERT INTO giveaways (guild_id, channel_id, created_by, prize_amount, max_winners, ends_at) "
                    "VALUES (?, ?, ?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    MYSQL_BIND bp[6]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &guild_id;          bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &channel_id;        bp[1].is_unsigned = 1;
    bp[2].buffer_type = MYSQL_TYPE_LONGLONG; bp[2].buffer = &created_by;        bp[2].is_unsigned = 1;
    bp[3].buffer_type = MYSQL_TYPE_LONGLONG; bp[3].buffer = &prize;
    bp[4].buffer_type = MYSQL_TYPE_LONG;     bp[4].buffer = &max_winners;
    bp[5].buffer_type = MYSQL_TYPE_LONG;     bp[5].buffer = &duration_seconds;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool_->release(conn);
        return 0;
    }
    
    uint64_t giveaway_id = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return giveaway_id;
}

bool Database::enter_giveaway(uint64_t giveaway_id, uint64_t user_id) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    const char* q = "INSERT IGNORE INTO giveaway_entries (giveaway_id, user_id) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &giveaway_id; bp[0].is_unsigned = 1;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &user_id;     bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    bool ok = mysql_stmt_execute(stmt) == 0 && mysql_stmt_affected_rows(stmt) > 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

std::vector<uint64_t> Database::get_giveaway_entries(uint64_t giveaway_id) {
    std::vector<uint64_t> result;
    auto conn = pool_->acquire();
    if (!conn) return result;
    
    const char* q = "SELECT user_id FROM giveaway_entries WHERE giveaway_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return result;
    }
    
    MYSQL_BIND bp[1]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_LONGLONG; bp[0].buffer = &giveaway_id; bp[0].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt); pool_->release(conn); return result;
    }
    
    uint64_t uid;
    MYSQL_BIND br[1]; memset(br, 0, sizeof(br));
    br[0].buffer_type = MYSQL_TYPE_LONGLONG; br[0].buffer = &uid; br[0].is_unsigned = 1;
    mysql_stmt_bind_result(stmt, br);
    mysql_stmt_store_result(stmt);
    
    while (mysql_stmt_fetch(stmt) == 0) {
        result.push_back(uid);
    }
    
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return result;
}

bool Database::end_giveaway(uint64_t giveaway_id, const std::vector<uint64_t>& winner_ids) {
    auto conn = pool_->acquire();
    if (!conn) return false;
    
    // Build JSON array of winner IDs
    std::string json = "[";
    for (size_t i = 0; i < winner_ids.size(); ++i) {
        if (i > 0) json += ",";
        json += std::to_string(winner_ids[i]);
    }
    json += "]";
    
    const char* q = "UPDATE giveaways SET active = FALSE, winner_ids = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (!stmt || mysql_stmt_prepare(stmt, q, strlen(q)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        pool_->release(conn);
        return false;
    }
    
    unsigned long json_len = json.size();
    MYSQL_BIND bp[2]; memset(bp, 0, sizeof(bp));
    bp[0].buffer_type = MYSQL_TYPE_STRING;   bp[0].buffer = (void*)json.c_str(); bp[0].buffer_length = json_len; bp[0].length = &json_len;
    bp[1].buffer_type = MYSQL_TYPE_LONGLONG; bp[1].buffer = &giveaway_id;        bp[1].is_unsigned = 1;
    mysql_stmt_bind_param(stmt, bp);
    
    bool ok = mysql_stmt_execute(stmt) == 0;
    mysql_stmt_close(stmt);
    pool_->release(conn);
    return ok;
}

} // namespace db
} // namespace bronx
