#!/bin/bash
# ============================================================================
# Migration Script: Local MySQL -> Aiven MySQL
# Migrates: pickaxes, minecarts, bags, prestige, rebirth, commands, all stats
# Uses mysqldump + INSERT ... ON DUPLICATE KEY UPDATE for safe upserts
# ============================================================================

set -euo pipefail

# --- Source (local) ---
SRC_HOST="localhost"
SRC_PORT="3306"
SRC_USER="bronxbot"
SRC_PASS="${DB_SRC_PASS:-}"
SRC_DB="bronxbot"

# --- Destination (Aiven) ---
DST_HOST="bronx-bronx.f.aivencloud.com"
DST_PORT="14629"
DST_USER="avnadmin"
DST_PASS="${DB_DST_PASS:-}"
DST_DB="bronxbot"

TMPDIR="/tmp/bpp_migration"
mkdir -p "$TMPDIR"

SRC_CMD="mysql -u${SRC_USER} -p${SRC_PASS} -h${SRC_HOST} -P${SRC_PORT} ${SRC_DB}"
DST_CMD="mysql -u${DST_USER} -p${DST_PASS} -h${DST_HOST} -P${DST_PORT} --ssl ${DST_DB}"

echo "============================================"
echo "  BPP Database Migration: Local -> Aiven"
echo "============================================"
echo ""

# Helper: run query on source
src_query() {
    mysql -u"${SRC_USER}" -p"${SRC_PASS}" -h"${SRC_HOST}" -P"${SRC_PORT}" "${SRC_DB}" -N -e "$1" 2>/dev/null
}

# Helper: run query on destination
dst_query() {
    mysql -u"${DST_USER}" -p"${DST_PASS}" -h"${DST_HOST}" -P"${DST_PORT}" --ssl "${DST_DB}" -N -e "$1" 2>/dev/null
}

# Helper: run SQL file on destination
dst_exec_file() {
    mysql -u"${DST_USER}" -p"${DST_PASS}" -h"${DST_HOST}" -P"${DST_PORT}" --ssl "${DST_DB}" 2>/dev/null < "$1"
}

#######################################################################
# STEP 0: Ensure all source users exist on destination
#######################################################################
echo "[0/7] Ensuring all source users exist on destination..."

src_query "SELECT user_id FROM users" > "$TMPDIR/src_users.txt"
dst_query "SELECT user_id FROM users" > "$TMPDIR/dst_users.txt"

# Find missing users
MISSING_USERS=$(comm -23 <(sort "$TMPDIR/src_users.txt") <(sort "$TMPDIR/dst_users.txt"))
MISSING_COUNT=$(echo "$MISSING_USERS" | grep -c '[0-9]' || true)

if [ "$MISSING_COUNT" -gt 0 ]; then
    echo "  -> Found $MISSING_COUNT users in local that are missing from Aiven. Inserting..."
    
    # Build an INSERT for each missing user (copy full row)
    for uid in $MISSING_USERS; do
        src_query "SELECT CONCAT(
            'INSERT IGNORE INTO users (user_id, wallet, bank, bank_limit, interest_rate, interest_level, ',
            'last_interest_claim, last_daily, last_work, last_beg, last_rob, total_gambled, total_won, total_lost, ',
            'commands_used, dev, admin, is_mod, maintainer, contributor, vip, prestige, created_at) VALUES (',
            user_id, ',', wallet, ',', bank, ',', bank_limit, ',', interest_rate, ',', interest_level, ',',
            IFNULL(CONCAT('''', last_interest_claim, ''''), 'NULL'), ',',
            IFNULL(CONCAT('''', last_daily, ''''), 'NULL'), ',',
            IFNULL(CONCAT('''', last_work, ''''), 'NULL'), ',',
            IFNULL(CONCAT('''', last_beg, ''''), 'NULL'), ',',
            IFNULL(CONCAT('''', last_rob, ''''), 'NULL'), ',',
            total_gambled, ',', total_won, ',', total_lost, ',',
            commands_used, ',', dev, ',', admin, ',', is_mod, ',',
            maintainer, ',', contributor, ',', vip, ',', prestige, ',',
            '''', created_at, '''',
            ');')
            FROM users WHERE user_id = $uid" >> "$TMPDIR/insert_users.sql"
    done
    
    dst_exec_file "$TMPDIR/insert_users.sql"
    echo "  -> Inserted $MISSING_COUNT missing users."
else
    echo "  -> All users already exist on destination."
fi

#######################################################################
# STEP 1: Migrate Pickaxes (inventory where item_type='pickaxe')
#######################################################################
echo ""
echo "[1/7] Migrating pickaxes..."

src_query "SELECT CONCAT(
    'INSERT INTO inventory (user_id, item_id, item_type, quantity, metadata, level, acquired_at) VALUES (',
    user_id, ', ''', item_id, ''', ''pickaxe'', ', quantity, ', ',
    IFNULL(CONCAT('''', REPLACE(metadata, '''', '\\\\'''), ''''), 'NULL'), ', ',
    level, ', ''', acquired_at, ''') ',
    'ON DUPLICATE KEY UPDATE quantity = GREATEST(quantity, VALUES(quantity)), ',
    'level = GREATEST(level, VALUES(level)), ',
    'metadata = COALESCE(VALUES(metadata), metadata);')
FROM inventory WHERE item_type = 'pickaxe'" > "$TMPDIR/migrate_pickaxes.sql"

PICKAXE_COUNT=$(wc -l < "$TMPDIR/migrate_pickaxes.sql" | tr -d ' ')
if [ "$PICKAXE_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_pickaxes.sql"
    echo "  -> Migrated $PICKAXE_COUNT pickaxe records."
else
    echo "  -> No pickaxe records to migrate."
fi

#######################################################################
# STEP 2: Migrate Minecarts (inventory where item_type='minecart')
#######################################################################
echo ""
echo "[2/7] Migrating minecarts..."

src_query "SELECT CONCAT(
    'INSERT INTO inventory (user_id, item_id, item_type, quantity, metadata, level, acquired_at) VALUES (',
    user_id, ', ''', item_id, ''', ''minecart'', ', quantity, ', ',
    IFNULL(CONCAT('''', REPLACE(metadata, '''', '\\\\'''), ''''), 'NULL'), ', ',
    level, ', ''', acquired_at, ''') ',
    'ON DUPLICATE KEY UPDATE quantity = GREATEST(quantity, VALUES(quantity)), ',
    'level = GREATEST(level, VALUES(level)), ',
    'metadata = COALESCE(VALUES(metadata), metadata);')
FROM inventory WHERE item_type = 'minecart'" > "$TMPDIR/migrate_minecarts.sql"

MINECART_COUNT=$(wc -l < "$TMPDIR/migrate_minecarts.sql" | tr -d ' ')
if [ "$MINECART_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_minecarts.sql"
    echo "  -> Migrated $MINECART_COUNT minecart records."
else
    echo "  -> No minecart records to migrate."
fi

#######################################################################
# STEP 3: Migrate Bags (inventory where item_type='bag')
#######################################################################
echo ""
echo "[3/7] Migrating bags..."

src_query "SELECT CONCAT(
    'INSERT INTO inventory (user_id, item_id, item_type, quantity, metadata, level, acquired_at) VALUES (',
    user_id, ', ''', item_id, ''', ''bag'', ', quantity, ', ',
    IFNULL(CONCAT('''', REPLACE(metadata, '''', '\\\\'''), ''''), 'NULL'), ', ',
    level, ', ''', acquired_at, ''') ',
    'ON DUPLICATE KEY UPDATE quantity = GREATEST(quantity, VALUES(quantity)), ',
    'level = GREATEST(level, VALUES(level)), ',
    'metadata = COALESCE(VALUES(metadata), metadata);')
FROM inventory WHERE item_type = 'bag'" > "$TMPDIR/migrate_bags.sql"

BAG_COUNT=$(wc -l < "$TMPDIR/migrate_bags.sql" | tr -d ' ')
if [ "$BAG_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_bags.sql"
    echo "  -> Migrated $BAG_COUNT bag records."
else
    echo "  -> No bag records to migrate."
fi

#######################################################################
# STEP 4: Migrate Prestige (users.prestige column)
#######################################################################
echo ""
echo "[4/7] Migrating prestige data..."

src_query "SELECT CONCAT(
    'UPDATE users SET prestige = GREATEST(prestige, ', prestige, '), ',
    'commands_used = GREATEST(commands_used, ', commands_used, ') ',
    'WHERE user_id = ', user_id, ';')
FROM users WHERE prestige > 0" > "$TMPDIR/migrate_prestige.sql"

PRESTIGE_COUNT=$(wc -l < "$TMPDIR/migrate_prestige.sql" | tr -d ' ')
if [ "$PRESTIGE_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_prestige.sql"
    echo "  -> Updated prestige for $PRESTIGE_COUNT users."
else
    echo "  -> No prestige data to migrate."
fi

#######################################################################
# STEP 5: Migrate Rebirths (user_rebirths table)
#######################################################################
echo ""
echo "[5/7] Migrating rebirth data..."

src_query "SELECT CONCAT(
    'INSERT INTO user_rebirths (user_id, rebirth_level, total_multiplier, last_rebirth_at, created_at) VALUES (',
    user_id, ', ', rebirth_level, ', ', total_multiplier, ', ',
    IFNULL(CONCAT('''', last_rebirth_at, ''''), 'NULL'), ', ',
    '''', created_at, ''') ',
    'ON DUPLICATE KEY UPDATE ',
    'rebirth_level = GREATEST(rebirth_level, VALUES(rebirth_level)), ',
    'total_multiplier = GREATEST(total_multiplier, VALUES(total_multiplier));')
FROM user_rebirths" > "$TMPDIR/migrate_rebirths.sql"

REBIRTH_COUNT=$(wc -l < "$TMPDIR/migrate_rebirths.sql" | tr -d ' ')
if [ "$REBIRTH_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_rebirths.sql"
    echo "  -> Migrated $REBIRTH_COUNT rebirth records."
else
    echo "  -> No rebirth data to migrate."
fi

#######################################################################
# STEP 6: Migrate Commands (command_stats + commands_used in users)
#######################################################################
echo ""
echo "[6/7] Migrating command data..."

# 6a: Migrate commands_used from users table (for ALL users with commands_used > 0)
src_query "SELECT CONCAT(
    'UPDATE users SET commands_used = GREATEST(commands_used, ', commands_used, ') ',
    'WHERE user_id = ', user_id, ';')
FROM users WHERE commands_used > 0" > "$TMPDIR/migrate_commands_used.sql"

CMD_USED_COUNT=$(wc -l < "$TMPDIR/migrate_commands_used.sql" | tr -d ' ')
if [ "$CMD_USED_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_commands_used.sql"
    echo "  -> Updated commands_used for $CMD_USED_COUNT users."
fi

# 6b: Migrate command_stats (individual records - check for missing entries by id)
# Get max id on destination
DST_MAX_CMD_ID=$(dst_query "SELECT COALESCE(MAX(id), 0) FROM command_stats" | tr -d '[:space:]')
echo "  -> Destination max command_stats id: $DST_MAX_CMD_ID"

src_query "SELECT CONCAT(
    'INSERT IGNORE INTO command_stats (id, user_id, command_name, guild_id, used_at) VALUES (',
    id, ', ', user_id, ', ''', REPLACE(command_name, '''', '\\\\'''), ''', ',
    IFNULL(guild_id, 'NULL'), ', ''', used_at, ''');')
FROM command_stats WHERE id > $DST_MAX_CMD_ID" > "$TMPDIR/migrate_command_stats.sql"

CMD_STATS_COUNT=$(wc -l < "$TMPDIR/migrate_command_stats.sql" | tr -d ' ')
if [ "$CMD_STATS_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_command_stats.sql"
    echo "  -> Migrated $CMD_STATS_COUNT new command_stats records."
else
    echo "  -> No new command_stats to migrate."
fi

# 6c: Migrate command_history (append missing entries)
DST_MAX_HIST_ID=$(dst_query "SELECT COALESCE(MAX(id), 0) FROM command_history" | tr -d '[:space:]')
echo "  -> Destination max command_history id: $DST_MAX_HIST_ID"

src_query "SELECT CONCAT(
    'INSERT IGNORE INTO command_history (id, user_id, entry_type, description, amount, balance_after, created_at) VALUES (',
    id, ', ', user_id, ', ''', entry_type, ''', ''', REPLACE(description, '''', '\\\\'''), ''', ',
    IFNULL(amount, 'NULL'), ', ',
    IFNULL(balance_after, 'NULL'), ', ''', created_at, ''');')
FROM command_history WHERE id > $DST_MAX_HIST_ID" > "$TMPDIR/migrate_command_history.sql"

CMD_HIST_COUNT=$(wc -l < "$TMPDIR/migrate_command_history.sql" | tr -d ' ')
if [ "$CMD_HIST_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_command_history.sql"
    echo "  -> Migrated $CMD_HIST_COUNT new command_history records."
else
    echo "  -> No new command_history to migrate."
fi

#######################################################################
# STEP 7: Migrate All Stats (user_stats, gambling_stats, daily_stats)
#######################################################################
echo ""
echo "[7/7] Migrating all stats..."

# 7a: user_stats (unique on user_id + stat_name)
src_query "SELECT CONCAT(
    'INSERT INTO user_stats (user_id, stat_name, stat_value) VALUES (',
    user_id, ', ''', REPLACE(stat_name, '''', '\\\\'''), ''', ', stat_value, ') ',
    'ON DUPLICATE KEY UPDATE stat_value = GREATEST(stat_value, VALUES(stat_value));')
FROM user_stats" > "$TMPDIR/migrate_user_stats.sql"

USER_STATS_COUNT=$(wc -l < "$TMPDIR/migrate_user_stats.sql" | tr -d ' ')
if [ "$USER_STATS_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_user_stats.sql"
    echo "  -> Migrated $USER_STATS_COUNT user_stats records."
else
    echo "  -> No user_stats to migrate."
fi

# 7b: gambling_stats (unique on user_id + game_type)
src_query "SELECT CONCAT(
    'INSERT INTO gambling_stats (user_id, game_type, games_played, total_bet, total_won, total_lost, biggest_win, biggest_loss) VALUES (',
    user_id, ', ''', game_type, ''', ', games_played, ', ', total_bet, ', ', total_won, ', ', total_lost, ', ', biggest_win, ', ', biggest_loss, ') ',
    'ON DUPLICATE KEY UPDATE ',
    'games_played = GREATEST(games_played, VALUES(games_played)), ',
    'total_bet = GREATEST(total_bet, VALUES(total_bet)), ',
    'total_won = GREATEST(total_won, VALUES(total_won)), ',
    'total_lost = GREATEST(total_lost, VALUES(total_lost)), ',
    'biggest_win = GREATEST(biggest_win, VALUES(biggest_win)), ',
    'biggest_loss = GREATEST(biggest_loss, VALUES(biggest_loss));')
FROM gambling_stats" > "$TMPDIR/migrate_gambling_stats.sql"

GAMBLING_COUNT=$(wc -l < "$TMPDIR/migrate_gambling_stats.sql" | tr -d ' ')
if [ "$GAMBLING_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_gambling_stats.sql"
    echo "  -> Migrated $GAMBLING_COUNT gambling_stats records."
else
    echo "  -> No gambling_stats to migrate."
fi

# 7c: daily_stats
src_query "SHOW CREATE TABLE daily_stats\G" > "$TMPDIR/daily_stats_schema.txt" 2>/dev/null || true

# Check if daily_stats has a unique key we can use
HAS_DAILY_STATS=$(src_query "SELECT COUNT(*) FROM daily_stats" | tr -d '[:space:]')
if [ "$HAS_DAILY_STATS" -gt 0 ]; then
    src_query "SELECT CONCAT(
        'INSERT IGNORE INTO daily_stats SELECT * FROM (SELECT ',
        GROUP_CONCAT(CONCAT('''', COLUMN_NAME, '''') SEPARATOR ', '),
        ') as cols;')
    FROM INFORMATION_SCHEMA.COLUMNS 
    WHERE TABLE_SCHEMA='bronxbot' AND TABLE_NAME='daily_stats'" 2>/dev/null || true
    
    # Simple approach: dump and insert ignore
    mysqldump -u"${SRC_USER}" -p"${SRC_PASS}" -h"${SRC_HOST}" -P"${SRC_PORT}" \
        --no-create-info --insert-ignore --complete-insert \
        "${SRC_DB}" daily_stats 2>/dev/null > "$TMPDIR/migrate_daily_stats.sql"
    
    DAILY_COUNT=$(grep -c 'INSERT' "$TMPDIR/migrate_daily_stats.sql" 2>/dev/null || echo "0")
    if [ "$DAILY_COUNT" -gt 0 ]; then
        dst_exec_file "$TMPDIR/migrate_daily_stats.sql"
        echo "  -> Migrated daily_stats ($DAILY_COUNT INSERT statements)."
    else
        echo "  -> No daily_stats records to migrate."
    fi
else
    echo "  -> No daily_stats records on source."
fi

# 7d: Also sync total_gambled, total_won, total_lost from users table
src_query "SELECT CONCAT(
    'UPDATE users SET ',
    'total_gambled = GREATEST(total_gambled, ', total_gambled, '), ',
    'total_won = GREATEST(total_won, ', total_won, '), ',
    'total_lost = GREATEST(total_lost, ', total_lost, ') ',
    'WHERE user_id = ', user_id, ';')
FROM users WHERE total_gambled > 0 OR total_won > 0 OR total_lost > 0" > "$TMPDIR/migrate_user_gamble_stats.sql"

GAMBLE_USER_COUNT=$(wc -l < "$TMPDIR/migrate_user_gamble_stats.sql" | tr -d ' ')
if [ "$GAMBLE_USER_COUNT" -gt 0 ]; then
    dst_exec_file "$TMPDIR/migrate_user_gamble_stats.sql"
    echo "  -> Updated gambling totals for $GAMBLE_USER_COUNT users."
else
    echo "  -> No user gambling totals to migrate."
fi

#######################################################################
# VERIFICATION
#######################################################################
echo ""
echo "============================================"
echo "  Verification: Post-Migration Counts"
echo "============================================"

echo ""
echo "Table                     | Local  | Aiven"
echo "--------------------------+--------+-------"

for q in \
    "pickaxes|SELECT COUNT(*) FROM inventory WHERE item_type='pickaxe'" \
    "minecarts|SELECT COUNT(*) FROM inventory WHERE item_type='minecart'" \
    "bags|SELECT COUNT(*) FROM inventory WHERE item_type='bag'" \
    "prestige users|SELECT COUNT(*) FROM users WHERE prestige > 0" \
    "rebirths|SELECT COUNT(*) FROM user_rebirths" \
    "command_stats|SELECT COUNT(*) FROM command_stats" \
    "command_history|SELECT COUNT(*) FROM command_history" \
    "user_stats|SELECT COUNT(*) FROM user_stats" \
    "gambling_stats|SELECT COUNT(*) FROM gambling_stats" \
    "daily_stats|SELECT COUNT(*) FROM daily_stats" \
; do
    LABEL=$(echo "$q" | cut -d'|' -f1)
    QUERY=$(echo "$q" | cut -d'|' -f2)
    
    SRC_CNT=$(src_query "$QUERY" 2>/dev/null | tr -d '[:space:]')
    DST_CNT=$(dst_query "$QUERY" 2>/dev/null | tr -d '[:space:]')
    
    printf "%-25s | %6s | %6s" "$LABEL" "$SRC_CNT" "$DST_CNT"
    
    if [ "$SRC_CNT" = "$DST_CNT" ]; then
        echo "  [OK]"
    elif [ "$DST_CNT" -ge "$SRC_CNT" ] 2>/dev/null; then
        echo "  [OK] (Aiven has more)"
    else
        echo "  [WARN] MISMATCH"
    fi
done

echo ""
echo "Migration complete!"
echo "Temp files stored in: $TMPDIR"
