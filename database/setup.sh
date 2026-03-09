#!/bin/bash

# =============================================================================
# South Bronx Bot - Database Setup Script
# =============================================================================

set -e  # Exit on error

echo "╔═══════════════════════════════════════════════════╗"
echo "║   South Bronx Bot - Database Initialization      ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# Default values
DB_HOST="localhost"
DB_PORT="3306"
DB_NAME="bronxbot"
DB_USER="root"
DB_PASS=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --host)
            DB_HOST="$2"
            shift 2
            ;;
        --port)
            DB_PORT="$2"
            shift 2
            ;;
        --database)
            DB_NAME="$2"
            shift 2
            ;;
        --user)
            DB_USER="$2"
            shift 2
            ;;
        --password)
            DB_PASS="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --host HOST          Database host (default: localhost)"
            echo "  --port PORT          Database port (default: 3306)"
            echo "  --database NAME      Database name (default: bronxbot)"
            echo "  --user USER          Database user (default: root)"
            echo "  --password PASS      Database password (default: empty)"
            echo "  --help               Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "Configuration:"
echo "  Host:     $DB_HOST"
echo "  Port:     $DB_PORT"
echo "  Database: $DB_NAME"
echo "  User:     $DB_USER"
echo ""

# Check if MariaDB/MySQL is installed
if ! command -v mysql &> /dev/null; then
    echo "❌ Error: MySQL/MariaDB client not found!"
    echo ""
    echo "Install MariaDB:"
    echo "  Ubuntu/Debian: sudo apt install mariadb-server mariadb-client libmariadb-dev"
    echo "  Fedora/RHEL:   sudo dnf install mariadb-server mariadb-devel"
    echo "  Arch:          sudo pacman -S mariadb"
    exit 1
fi

# Check if MariaDB service is running
if ! systemctl is-active --quiet mariadb && ! systemctl is-active --quiet mysql; then
    echo "⚠️  MariaDB service is not running!"
    echo "Start it with: sudo systemctl start mariadb"
    echo ""
    read -p "Do you want to start it now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo systemctl start mariadb || sudo systemctl start mysql
        echo "✓ Service started"
    else
        echo "❌ Cannot proceed without database service"
        exit 1
    fi
fi

# Build mysql command
# On Ubuntu/Debian, root typically requires sudo due to unix_socket auth
if [ "$DB_USER" = "root" ] && [ -z "$DB_PASS" ]; then
    MYSQL_CMD="sudo mysql"
    echo "Note: Using sudo for root access (unix_socket authentication)"
else
    MYSQL_CMD="mysql -h $DB_HOST -P $DB_PORT -u $DB_USER"
    if [ -n "$DB_PASS" ]; then
        MYSQL_CMD="$MYSQL_CMD -p$DB_PASS"
    fi
fi

# Test connection
echo "Testing database connection..."
if ! $MYSQL_CMD -e "SELECT 1;" &> /dev/null; then
    echo "❌ Failed to connect to database!"
    echo ""
    echo "Troubleshooting:"
    echo "1. Check if MariaDB is running: systemctl status mariadb"
    echo "2. Verify credentials are correct"
    echo "3. Try: sudo mysql -u root"
    echo "4. Reset root password if needed"
    exit 1
fi
echo "✓ Connection successful"
echo ""

# Create database and user
echo "Creating database and user..."

$MYSQL_CMD << EOF
-- Create database
CREATE DATABASE IF NOT EXISTS $DB_NAME CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- Create bot user if not exists
CREATE USER IF NOT EXISTS 'bronxbot'@'localhost' IDENTIFIED BY 'bronxbot_password_change_this';

-- Grant privileges
GRANT ALL PRIVILEGES ON $DB_NAME.* TO 'bronxbot'@'localhost';
FLUSH PRIVILEGES;

-- Allow event scheduler
SET GLOBAL event_scheduler = ON;
EOF

echo "✓ Database and user created"
echo ""

# Run schema
echo "Applying database schema..."
$MYSQL_CMD $DB_NAME < "$(dirname "$0")/schema.sql"
echo "✓ Schema applied successfully"
echo ""

# Verify tables
echo "Verifying tables..."
TABLE_COUNT=$($MYSQL_CMD $DB_NAME -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = '$DB_NAME';")
echo "✓ Created $TABLE_COUNT tables"
echo ""

# Create config file for bot
CONFIG_FILE="$(dirname "$0")/../data/db_config.json"
mkdir -p "$(dirname "$CONFIG_FILE")"

cat > "$CONFIG_FILE" << EOF
{
  "host": "$DB_HOST",
  "port": $DB_PORT,
  "database": "$DB_NAME",
  "user": "bronxbot",
  "password": "bronxbot_password_change_this",
  "pool_size": 10,
  "timeout_seconds": 10
}
EOF

echo "✓ Configuration file created: $CONFIG_FILE"
echo ""

echo "╔═══════════════════════════════════════════════════╗"
echo "║           Database Setup Complete! ✓              ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""
echo "⚠️  IMPORTANT: Change the default password!"
echo "Run: ALTER USER 'bronxbot'@'localhost' IDENTIFIED BY 'your_secure_password';"
echo ""
echo "Next steps:"
echo "1. Update data/db_config.json with your secure password"
echo "2. Build the bot: cd build && cmake .. && make"
echo "3. Run the bot: ./discord-bot"
echo ""
echo "Database statistics:"
$MYSQL_CMD $DB_NAME << EOF
SELECT 
    'users' as table_name, COUNT(*) as row_count FROM users
UNION ALL
SELECT 'inventory', COUNT(*) FROM inventory
UNION ALL
SELECT 'fish_catches', COUNT(*) FROM fish_catches
UNION ALL
SELECT 'giveaways', COUNT(*) FROM giveaways;
EOF

echo ""
echo "Happy coding! 🎉"
