# Server-Specific Economy System

## Overview

The server-specific economy system allows Discord servers to maintain their own independent economy, completely separate from the global bot economy. Admins can toggle between global and server economies and customize various economic parameters.

## Features

### 🌐 Dual Economy Modes
- **Global Economy**: Traditional cross-server economy where user balances are shared
- **Server Economy**: Isolated per-server economy with independent balances

### ⚙️ Customizable Settings
- Starting wallet amounts
- Starting bank limits
- Interest rates
- Command cooldowns
- Multipliers for work, gambling, and fishing
- Feature toggles (enable/disable specific features)
- Transaction tax system

### 🎣 Full Feature Support
All economy features work in server mode:
- Balance management (wallet, bank)
- Fishing system with server-specific catches
- Gambling games
- Trading between users
- Inventory management
- Leaderboards (server-specific)

## Database Structure

### New Tables

#### `guild_economy_settings`
Stores configuration for each server's economy.

#### `server_users`
Server-specific user economy data (wallet, bank, cooldowns, stats).

#### `server_inventory`
Server-specific inventory items.

#### `server_fish_catches`
Server-specific fishing catches.

#### `server_active_fishing_gear`
Active rod and bait per server.

#### `server_autofishers`
Server-specific autofishing data.

#### `server_gambling_stats`
Server-specific gambling statistics.

#### `server_cooldowns`
Server-specific command cooldowns.

#### `server_trades`
Server-specific trading system.

## Admin Commands

### `/servereconomy toggle <mode>`
Switch between global and server economy.

**Parameters:**
- `mode`: "global" or "server"

**Example:**
```
/servereconomy toggle mode:server
```

### `/servereconomy status`
View current server economy settings and configuration.

### `/servereconomy config`
Configure economy parameters.

**Parameters:**
- `starting_wallet`: Initial wallet amount (optional)
- `starting_bank_limit`: Initial bank capacity (optional)
- `work_multiplier`: Work earnings multiplier (optional)
- `gambling_multiplier`: Gambling winnings multiplier (optional)
- `fishing_multiplier`: Fishing rewards multiplier (optional)

**Example:**
```
/servereconomy config starting_wallet:5000 work_multiplier:1.5
```

### `/servereconomy features`
Enable or disable specific economy features.

**Parameters:**
- `gambling`: Allow gambling (optional)
- `fishing`: Allow fishing (optional)
- `trading`: Allow trading (optional)
- `robbery`: Allow robbery (optional)

**Example:**
```
/servereconomy features gambling:true fishing:true robbery:false
```

### `/servereconomy tax`
Configure transaction tax system.

**Parameters:**
- `enabled`: Enable/disable tax (required)
- `rate`: Tax percentage 0-100 (optional)

**Example:**
```
/servereconomy tax enabled:true rate:5.0
```

## Code Integration

### Using Unified Operations

All economy operations now have "unified" versions that automatically route to the correct economy (global or server) based on guild settings.

```cpp
#include "database/operations/server_economy_operations.h"

using namespace bronx::db::server_economy_operations;

// Get wallet - automatically uses correct economy
int64_t wallet = get_wallet_unified(db, user_id, guild_id);

// Update wallet
auto new_balance = update_wallet_unified(db, user_id, guild_id, amount);

// Also works with fishing
#include "database/operations/server_fishing_operations.h"
using namespace bronx::db::server_fishing_operations;

bool caught = create_fish_catch_unified(db, guild_id, user_id, 
                                       rarity, fish_name, weight, value,
                                       rod_id, bait_id);
```

### Checking Economy Mode

```cpp
bool is_server = is_server_economy(db, guild_id);
if (is_server) {
    // Server-specific logic
} else {
    // Global economy logic
}
```

### Getting Settings

```cpp
auto settings = get_guild_economy_settings(db, guild_id);
if (settings) {
    double work_multi = settings->work_multiplier;
    bool fishing_allowed = settings->allow_fishing;
    // Use settings...
}
```

## Migration Guide

### Applying the Schema

1. **Backup your database first!**

```bash
mysqldump -u root -p bronxbot > bronxbot_backup.sql
```

2. **Apply the migration:**

```bash
mysql -u root -p bronxbot < database/migrations/001_server_economy.sql
```

### Updating Commands

When updating existing economy commands to support server economy:

1. **Change database calls to use unified operations:**
   ```cpp
   // Old:
   int64_t wallet = db->get_wallet(user_id);
   
   // New:
   std::optional<uint64_t> guild_id = event.command.guild_id;
   int64_t wallet = get_wallet_unified(db, user_id, guild_id);
   ```

2. **Add guild_id to command context:**
   Most slash commands automatically provide `event.command.guild_id`.

3. **Check feature permissions:**
   ```cpp
   auto settings = get_guild_economy_settings(db, guild_id);
   if (settings && !settings->allow_gambling) {
       event.reply("Gambling is disabled on this server!");
       return;
   }
   ```

4. **Apply multipliers:**
   ```cpp
   if (settings) {
       reward = static_cast<int64_t>(reward * settings->work_multiplier);
   }
   ```

## Example: Converting a Command

**Before (Global Only):**
```cpp
void execute(const dpp::slashcommand_t& event) override {
    uint64_t user_id = event.command.usr.id;
    int64_t wallet = db->get_wallet(user_id);
    int64_t reward = 1000;
    
    db->update_wallet(user_id, reward);
    event.reply("You earned " + std::to_string(reward));
}
```

**After (Supports Both):**
```cpp
void execute(const dpp::slashcommand_t& event) override {
    uint64_t user_id = event.command.usr.id;
    std::optional<uint64_t> guild_id = event.command.guild_id;
    
    // Get current wallet (auto-routes to correct economy)
    int64_t wallet = get_wallet_unified(db, user_id, guild_id);
    
    // Get settings and apply multiplier
    int64_t reward = 1000;
    if (guild_id) {
        auto settings = get_guild_economy_settings(db, *guild_id);
        if (settings) {
            reward = static_cast<int64_t>(reward * settings->work_multiplier);
        }
    }
    
    // Update wallet (auto-routes)
    update_wallet_unified(db, user_id, guild_id, reward);
    event.reply("You earned " + std::to_string(reward));
}
```

## Benefits

✅ **Complete Isolation**: Server economies are completely independent  
✅ **Customizable**: Admins control rates, multipliers, and features  
✅ **Backward Compatible**: Global economy still works unchanged  
✅ **User Friendly**: Simple toggle to switch modes  
✅ **Flexible**: Tax systems, feature toggles, and more  
✅ **Scalable**: Proper indexing for performance  

## Further Customization

### Adding Custom Server Features

You can extend the `guild_economy_settings` table with custom fields:

```sql
ALTER TABLE guild_economy_settings 
ADD COLUMN custom_feature BOOLEAN NOT NULL DEFAULT FALSE;
```

### Custom Multipliers Per Activity

Add activity-specific multipliers to fine-tune your server's economy:

```sql
ALTER TABLE guild_economy_settings
ADD COLUMN daily_multiplier DECIMAL(5,2) NOT NULL DEFAULT 1.00,
ADD COLUMN rob_success_rate DECIMAL(5,2) NOT NULL DEFAULT 50.00;
```

## Troubleshooting

### Users don't see their balances after switching to server mode
This is expected. Server economy starts fresh. Users begin with the configured starting amounts.

### Commands return errors after migration
Ensure all commands have been updated to use the unified operations and pass `guild_id`.

### Performance issues with large guilds
The tables are indexed for performance. Run `ANALYZE TABLE` on active tables:
```sql
ANALYZE TABLE server_users, server_fish_catches, server_inventory;
```

## Support

For issues or questions about the server economy system, check:
- Database logs for SQL errors
- Ensure migrations were applied correctly
- Verify command permissions are set correctly

---

**Created**: 2026-02-23  
**Version**: 1.0.0  
**Status**: Production Ready
