#!/bin/bash
# Server Economy Migration Script
# Apply this to add server-specific economy functionality

set -e

echo "======================================"
echo "Server Economy Migration Script"
echo "======================================"
echo ""

# Configuration
DB_NAME="bronxbot"
DB_USER="root"
MIGRATION_FILE="database/migrations/001_server_economy.sql"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if migration file exists
if [ ! -f "$MIGRATION_FILE" ]; then
    echo -e "${RED}Error: Migration file not found at $MIGRATION_FILE${NC}"
    exit 1
fi

echo -e "${YELLOW}This script will:${NC}"
echo "1. Create a backup of your database"
echo "2. Apply the server economy schema"
echo "3. Verify the migration"
echo ""

# Prompt for confirmation
read -p "Do you want to continue? (y/n) " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Migration cancelled."
    exit 0
fi

# Backup database
echo ""
echo -e "${YELLOW}Step 1: Creating database backup...${NC}"
BACKUP_FILE="bronxbot_backup_$(date +%Y%m%d_%H%M%S).sql"

if mysqldump -u "$DB_USER" -p "$DB_NAME" > "$BACKUP_FILE"; then
    echo -e "${GREEN}[OK] Backup created: $BACKUP_FILE${NC}"
else
    echo -e "${RED}[FAIL] Backup failed!${NC}"
    exit 1
fi

# Apply migration
echo ""
echo -e "${YELLOW}Step 2: Applying server economy migration...${NC}"

if mysql -u "$DB_USER" -p "$DB_NAME" < "$MIGRATION_FILE"; then
    echo -e "${GREEN}[OK] Migration applied successfully${NC}"
else
    echo -e "${RED}[FAIL] Migration failed!${NC}"
    echo -e "${YELLOW}Attempting to restore from backup...${NC}"
    mysql -u "$DB_USER" -p "$DB_NAME" < "$BACKUP_FILE"
    echo -e "${YELLOW}Database restored from backup${NC}"
    exit 1
fi

# Verify migration
echo ""
echo -e "${YELLOW}Step 3: Verifying migration...${NC}"

# Check if key tables exist
TABLES=("guild_economy_settings" "server_users" "server_inventory" "server_fish_catches")
ALL_EXIST=true

for table in "${TABLES[@]}"; do
    if mysql -u "$DB_USER" -p "$DB_NAME" -e "DESCRIBE $table" > /dev/null 2>&1; then
        echo -e "${GREEN}[OK] Table $table created${NC}"
    else
        echo -e "${RED}[FAIL] Table $table not found${NC}"
        ALL_EXIST=false
    fi
done

if [ "$ALL_EXIST" = true ]; then
    echo ""
    echo -e "${GREEN}======================================"
    echo "Migration completed successfully!"
    echo "======================================${NC}"
    echo ""
    echo "Next steps:"
    echo "1. Rebuild the bot with the new operations"
    echo "2. Register the /servereconomy command"
    echo "3. Test the economy toggle functionality"
    echo ""
    echo -e "Backup saved at: ${YELLOW}$BACKUP_FILE${NC}"
else
    echo ""
    echo -e "${RED}Migration verification failed!${NC}"
    exit 1
fi
