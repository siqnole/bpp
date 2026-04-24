# 📊 Leaderboard System

A comprehensive leaderboard system with both global and server-specific rankings across multiple categories.

## 🚀 Quick Start

Use `b.lb` to see all available leaderboard categories, or use slash command `/leaderboard`.

## 📋 Available Categories

### 💰 Economy Leaderboards
- `lb networth` - Richest users by total wealth (wallet + bank)
- `lb wallet` - Highest wallet balances  
- `lb bank` - Highest bank balances
- `lb inventory` - Highest inventory values (coming soon)

### 🎣 Fishing Leaderboards
- `lb fish-caught` - Most fish caught
- `lb fish-sold` - Most fish sold
- `lb fish-value` - Most valuable single fish owned
- `lb fishing-profit` - Highest fishing earnings

### 🎰 Gambling Leaderboards
- `lb gambling-wins` - Most gambling wins
- `lb gambling-losses` - Biggest gambling losses (coming soon)
- `lb gambling-profit` - Highest gambling profits (coming soon)
- `lb slots-wins` - Most slots wins (coming soon)
- `lb coinflip-wins` - Most coinflip wins (coming soon)

### 📈 Activity Leaderboards
- `lb commands` - Most commands used (coming soon)
- `lb daily-streak` - Longest daily streaks (coming soon)
- `lb work-count` - Most work commands used (coming soon)

## 🌍 Global vs Server Rankings

**Server Rankings (Default)**
```
b.lb networth
```
Shows rankings for users in the current server only.

**Global Rankings**  
```
b.lb networth global
```
Shows rankings across all servers where the bot is present.

## 📊 Leaderboard Features

- **Top 10 Rankings** - Shows the top 10 users in each category
- **Medal Indicators** - 🥇🥈🥉 for top 3 positions
- **Emoji Indicators** - Visual emojis for each category
- **Value Formatting** - Money amounts formatted with proper separators
- **User Information** - Shows usernames and relevant statistics

## ⚙️ Database Setup

To enable full leaderboard functionality, run the SQL migration:

```sql
-- Run this in your MySQL database
SOURCE /path/to/leaderboard_tables.sql
```

This creates the necessary tables for:
- `user_stats` - User statistics tracking
- `guild_members` - Server membership tracking  
- `gambling_history` - Gambling activity tracking
- `command_usage` - Command usage tracking

## 🔧 Implementation Status

**✅ Fully Implemented:**
- Economy leaderboards (networth, wallet, bank)
- Fish caught tracking
- Fish value analysis
- Basic gambling wins

**🚧 Partially Implemented:**
- Fish sold tracking (requires stat integration)
- Fishing profit tracking (requires stat integration)

**📝 Coming Soon:**
- Gambling losses and profit tracking
- Activity and command usage tracking
- Daily streak tracking
- Game-specific gambling leaderboards

## 🎯 Usage Examples

```bash
# View server networth leaderboard
b.lb networth

# View global wallet leaderboard  
b.lb wallet global

# View fishing leaderboard
b.lb fish-caught

# View most valuable fish
b.lb fish-value

# View gambling wins
b.lb gambling-wins
```

## 🏆 Leaderboard Integration

The leaderboard system integrates with:
- **Economy System** - Wallet, bank, and net worth tracking
- **Fishing System** - Fish catches, sales, and values
- **Gambling System** - Win/loss tracking
- **Guild System** - Server-specific vs global rankings

Users are automatically ranked based on their activity and achievements across all bot systems.