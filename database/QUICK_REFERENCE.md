# Database Quick Reference

## 🚀 Quick Start

```bash
# 1. Install MariaDB
sudo apt install mariadb-server libmariadb-dev

# 2. Run setup
cd database && ./setup.sh

# 3. Update password
mysql -u root -p
> ALTER USER 'bronxbot'@'localhost' IDENTIFIED BY 'your_password';

# 4. Edit config
nano data/db_config.json

# 5. Build
cd build && cmake .. && make
```

## 📚 Common Operations

### User Operations
```cpp
// Create or get user
db.ensure_user_exists(user_id);
auto user = db.get_user(user_id);

// Get balances
int64_t wallet = db.get_wallet(user_id);
int64_t bank = db.get_bank(user_id);
int64_t networth = db.get_networth(user_id);

// Update balances (returns new balance or nullopt on failure)
auto new_wallet = db.update_wallet(user_id, 1000);  // Add 1000
auto new_bank = db.update_bank(user_id, -500);      // Remove 500

// Transfer money
auto result = db.transfer_money(from_id, to_id, amount);
if (result == TransactionResult::Success) { /* success */ }
```

### Cooldowns
```cpp
// Check cooldown
if (db.is_on_cooldown(user_id, "daily")) {
    auto expiry = db.get_cooldown_expiry(user_id, "daily");
    // Show time remaining
    return;
}

// Set cooldown (in seconds)
db.set_cooldown(user_id, "daily", 86400);  // 24 hours
db.set_cooldown(user_id, "work", 3600);    // 1 hour
```

### Inventory
```cpp
// Add item
db.add_item(user_id, "diamond_sword", "weapon", 1, R"({"damage": 50})");

// Check if has item
if (db.has_item(user_id, "fishing_rod", 1)) {
    int qty = db.get_item_quantity(user_id, "fishing_rod");
}

// Remove item
db.remove_item(user_id, "bait", 1);

// Get all items
auto inventory = db.get_inventory(user_id);
for (const auto& item : inventory) {
    std::cout << item.item_id << " x" << item.quantity << std::endl;
}
```

### Fishing
```cpp
// Add catch
uint64_t catch_id = db.add_fish_catch(
    user_id, "legendary", "Golden Trout",
    15.3,  // weight
    50000, // value
    "diamond_rod", "magic_bait"
);

// Get unsold fish
auto fish = db.get_unsold_fish(user_id);
auto rare_fish = db.get_fish_by_rarity(user_id, "rare");

// Sell fish
db.sell_fish(catch_id);
db.sell_all_fish_by_rarity(user_id, "normal");

// Active gear
auto [rod, bait] = db.get_active_fishing_gear(user_id);
db.set_active_rod(user_id, "diamond_rod");
db.set_active_bait(user_id, "worms");
```

### Autofisher
```cpp
// Create autofisher
db.create_autofisher(user_id);

// Check if has autofisher
if (db.has_autofisher(user_id)) {
    int64_t balance = db.get_autofisher_balance(user_id);
}

// Deposit/withdraw
db.deposit_to_autofisher(user_id, 1000);
db.withdraw_from_autofisher(user_id, 500);

// Upgrade
db.upgrade_autofisher_efficiency(user_id);
```

### Bazaar
```cpp
// Get shares
int shares = db.get_bazaar_shares(user_id);

// Buy/sell shares
db.buy_bazaar_shares(user_id, 10, 1000);  // 10 shares for 1000 coins
db.sell_bazaar_shares(user_id, 5, 550);   // 5 shares for 550 coins

// Record visit (for dynamic pricing)
db.record_bazaar_visit(user_id, guild_id, amount_spent);

// Calculate current price
double price = db.calculate_bazaar_stock_price(guild_id);
```

### Giveaways
```cpp
// Create giveaway
uint64_t giveaway_id = db.create_giveaway(
    guild_id, channel_id, creator_id,
    1000,  // prize
    1,     // max winners
    3600   // duration (seconds)
);

// Enter giveaway
db.enter_giveaway(giveaway_id, user_id);

// Get entries
auto entries = db.get_giveaway_entries(giveaway_id);

// End giveaway
std::vector<uint64_t> winners = {winner_id};
db.end_giveaway(giveaway_id, winners);

// Guild balance
int64_t balance = db.get_guild_balance(guild_id);
db.donate_to_guild(user_id, guild_id, 1000);
```

### Leaderboards
```cpp
// Get top users
auto leaderboard = db.get_leaderboard("networth", 10);
for (const auto& entry : leaderboard) {
    std::cout << entry.rank << ". User " << entry.user_id 
              << ": " << entry.value << std::endl;
}

// Get user rank
int rank = db.get_user_rank(user_id, "networth");

// Update cache (run periodically)
db.update_leaderboard_cache();
```

### Gambling
```cpp
// Record gambling result
int64_t bet = 100;
int64_t winnings = 200;  // or negative for loss
db.record_gambling_result(user_id, "slots", bet, winnings - bet);

// This automatically updates:
// - total_gambled
// - total_won or total_lost
// - games_played
// - biggest_win/biggest_loss
```

### Interest
```cpp
// Check if can claim
if (db.can_claim_interest(user_id)) {
    int64_t interest = db.claim_interest(user_id);
    // Interest automatically added to bank
}

// Upgrade interest rate
db.upgrade_interest(user_id);
```

## 🗄️ Database Tables

### Primary Keys
| Table | Primary Key | Description |
|-------|-------------|-------------|
| `users` | `user_id` | Discord user ID (BIGINT) |
| `inventory` | `id` (auto) | Inventory entry ID |
| `fish_catches` | `id` (auto) | Fish catch ID |
| `cooldowns` | `(user_id, command)` | Composite key |
| `giveaways` | `id` (auto) | Giveaway ID |
| `bazaar_stock` | `user_id` | One record per user |

### Important Indexes
- `users`: wallet, bank, networth (wallet+bank), last_active
- `inventory`: user_id, item_id, item_type
- `fish_catches`: user_id, rarity, weight, value, sold
- `cooldowns`: expires_at (for cleanup)
- `giveaways`: guild_id, active, ends_at

## 🔧 Maintenance Commands

### Backup
```bash
mysqldump -u bronxbot -p bronxbot > backup.sql
mysqldump -u bronxbot -p bronxbot | gzip > backup.sql.gz
```

### Restore
```bash
mysql -u bronxbot -p bronxbot < backup.sql
```

### Check Status
```sql
-- Connection count
SHOW STATUS LIKE 'Threads_connected';

-- Table sizes
SELECT table_name, 
       ROUND((data_length + index_length) / 1024 / 1024, 2) AS size_mb
FROM information_schema.tables
WHERE table_schema = 'bronxbot'
ORDER BY size_mb DESC;

-- Active queries
SHOW PROCESSLIST;
```

### Optimize
```bash
mysqlcheck -u bronxbot -p --analyze bronxbot
mysqlcheck -u bronxbot -p --optimize bronxbot
```

## ⚠️ Common Errors

### Connection Failed
```bash
# Check service
systemctl status mariadb

# Check port
netstat -tlnp | grep 3306

# Test connection
mysql -u bronxbot -p bronxbot
```

### Permission Denied
```sql
GRANT ALL PRIVILEGES ON bronxbot.* TO 'bronxbot'@'localhost';
FLUSH PRIVILEGES;
```

### Table Doesn't Exist
```bash
# Re-run schema
mysql -u bronxbot -p bronxbot < database/schema.sql
```

## 📊 Performance Tips

1. **Use connection pool** - Don't create new connections per query
2. **Batch operations** - Update multiple users in one transaction
3. **Cache hot data** - Store frequently accessed data in memory
4. **Index properly** - Add indexes for common WHERE clauses
5. **Use prepared statements** - Query plan caching + SQL injection prevention
6. **Monitor slow queries** - Enable slow query log
7. **Partition old data** - Partition fish_catches by date
8. **Regular maintenance** - Analyze/optimize tables monthly

## 🎯 Transaction Examples

### Safe Money Transfer
```cpp
auto result = db.transfer_money(from_id, to_id, amount);
switch (result) {
    case TransactionResult::Success:
        // Transfer successful
        break;
    case TransactionResult::InsufficientFunds:
        // Not enough money
        break;
    case TransactionResult::DatabaseError:
        // Database error, transaction rolled back
        break;
}
```

### Atomic Item Purchase
```cpp
// Check balance
int64_t wallet = db.get_wallet(user_id);
if (wallet < item_price) {
    return; // Not enough money
}

// Deduct money
auto new_balance = db.update_wallet(user_id, -item_price);
if (!new_balance) {
    return; // Failed to deduct
}

// Add item
if (!db.add_item(user_id, item_id, item_type, 1)) {
    // Refund if item add fails
    db.update_wallet(user_id, item_price);
    return;
}
```

## 📝 Configuration

### `data/db_config.json`
```json
{
  "host": "localhost",
  "port": 3306,
  "database": "bronxbot",
  "user": "bronxbot",
  "password": "your_secure_password",
  "pool_size": 10,
  "timeout_seconds": 10
}
```

### Load Config in Code
```cpp
#include "config_loader.h"

auto config = bronx::load_database_config("data/db_config.json");
bronx::db::Database db(config);
if (!db.connect()) {
    std::cerr << "Database connection failed: " << db.get_last_error() << std::endl;
    return 1;
}
```

## 🚀 Integration Checklist

- [ ] MariaDB installed and running
- [ ] Setup script executed successfully
- [ ] Password changed from default
- [ ] `data/db_config.json` created and updated
- [ ] CMakeLists.txt updated with database sources
- [ ] Bot rebuilt with `make`
- [ ] Database connection tested
- [ ] First economy command implemented
- [ ] Error handling added
- [ ] Cooldown system implemented
- [ ] Backup strategy planned

## 📞 Need Help?

1. Check [database/README.md](README.md) for full documentation
2. Read [database/IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) for overview
3. Review schema in [database/schema.sql](schema.sql)
4. Check example code in [database/database.cpp](database.cpp)
