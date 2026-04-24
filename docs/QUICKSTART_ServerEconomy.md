# Server Economy Quick Start Guide

## 🚀 Getting Started in 5 Minutes

### Step 1: Apply Database Migration

```bash
cd /home/siqnole/Documents/code/bpp
chmod +x database/migrations/apply_server_economy.sh
./database/migrations/apply_server_economy.sh
```

Or manually:
```bash
mysql -u root -p bronxbot < database/migrations/001_server_economy.sql
```

### Step 2: Rebuild the Bot

```bash
cd build
cmake ..
make
```

### Step 3: Restart the Bot

```bash
sudo systemctl restart discord-bot.service
```

### Step 4: Enable Server Economy (In Discord)

As a server admin, run:
```
/servereconomy toggle mode:server
```

### Step 5: Configure Settings (Optional)

```
/servereconomy config starting_wallet:5000 work_multiplier:1.5
/servereconomy features gambling:true fishing:true
/servereconomy tax enabled:true rate:5.0
```

### Step 6: Check Status

```
/servereconomy status
```

## 🎯 Common Use Cases

### Give New Users More Starting Money
```
/servereconomy config starting_wallet:10000
```

### Make Fishing More Rewarding
```
/servereconomy config fishing_multiplier:2.0
```

### Disable Gambling But Keep Other Features
```
/servereconomy features gambling:false
```

### Add Transaction Tax to Control Inflation
```
/servereconomy tax enabled:true rate:3.0
```

### Boost Work Earnings
```
/servereconomy config work_multiplier:2.5
```

## 📊 Monitoring

### Check Active Economy Mode
```sql
SELECT guild_id, economy_mode FROM guild_economy_settings;
```

### View Server Economy Stats
```sql
SELECT 
    COUNT(*) as total_users,
    SUM(wallet) as total_wallet,
    SUM(bank) as total_bank,
    AVG(networth) as avg_networth
FROM server_users 
WHERE guild_id = YOUR_GUILD_ID;
```

### Top Users in Server Economy
```sql
SELECT user_id, networth 
FROM server_users 
WHERE guild_id = YOUR_GUILD_ID 
ORDER BY networth DESC 
LIMIT 10;
```

## 🔧 Troubleshooting

### Migration Failed
```bash
# Restore from backup
mysql -u root -p bronxbot < bronxbot_backup_YYYYMMDD_HHMMSS.sql
```

### Command Not Showing Up
- Make sure the bot was recompiled with the new files
- Verify the command is registered with Discord
- Check bot permissions

### Users Can't See Balances
- This is normal after switching to server economy
- Server economy starts fresh with configured starting values
- Global balances remain unchanged

### Performance Issues
```sql
-- Optimize tables
ANALYZE TABLE server_users, server_fish_catches, server_inventory;

-- Check indexes
SHOW INDEX FROM server_users;
```

## 📝 Testing Checklist

- [ ] Database migration applied successfully
- [ ] Bot compiles without errors
- [ ] `/servereconomy` command appears
- [ ] Can toggle between global/server mode
- [ ] Can modify settings
- [ ] Balance commands work in both modes
- [ ] Fishing works in server mode
- [ ] Trading works in server mode
- [ ] Leaderboards show server-specific data

## 🆘 Need Help?

1. Check [README_ServerEconomy.md](README_ServerEconomy.md) for detailed documentation
2. Review example commands in [commands/economy_examples.h](commands/economy_examples.h)
3. Check database logs for SQL errors
4. Verify all migration tables were created

## 🎓 Next Steps

1. **Update Existing Commands**: Use the unified operations (`*_unified` functions)
2. **Add Custom Features**: Extend `guild_economy_settings` table
3. **Create Server Leaderboards**: Use `server_leaderboard_cache` table
4. **Implement Custom Events**: Server-specific economy events
5. **Add Admin Tools**: Bulk operations, economy resets, etc.

---

**Version**: 1.0.0  
**Last Updated**: 2026-02-23
