# Complete Bot Commands Reference

> Last updated: February 23, 2026
> 
> This document contains ALL commands available in the bot, organized by category.

---

## Economy

### balance
- **Aliases:** `bal`, `money`
- **Arguments:** `[user]`
- **Description:** Check your wallet, bank & net worth
- **Examples:**
  - `balance` - Check your own balance
  - `balance @user` - Check another user's balance

### bank
- **Aliases:** `dep`, `d`
- **Arguments:** `<amount>` `[upgrade]`
- **Description:** Deposit money into your bank or upgrade your bank limit
- **Subcommands:**
  - `bank <amount>` - Deposit money
  - `bank upgrade <amount>` - Upgrade bank limit with specific amount
  - `bank upgrade max` - Auto-upgrade as much as possible
- **Examples:**
  - `bank 1000` - Deposit $1,000
  - `bank all` - Deposit all wallet money
  - `bank upgrade 500` - Upgrade bank limit by $500
  - `bank upgrade max` - Upgrade bank as much as possible

### withdraw
- **Aliases:** `w`, `with`
- **Arguments:** `<amount>`
- **Description:** Withdraw money from your bank
- **Examples:**
  - `withdraw 500` - Withdraw $500
  - `withdraw all` - Withdraw all bank money
  - `withdraw 50%` - Withdraw 50% of bank balance

### pay
- **Aliases:** `give`
- **Arguments:** `<@user>` `<amount>`
- **Description:** Transfer money to another user
- **Examples:**
  - `pay @user 100` - Pay user $100
  - `pay @user all` - Pay user all your money

### daily
- **Arguments:** None
- **Description:** Claim your daily reward (10% of networth, minimum $500)
- **Cooldown:** 24 hours
- **Details:** Scales with your net worth to remain useful at all progression levels

### weekly
- **Arguments:** None
- **Description:** Claim your weekly reward (70% of networth, minimum $1000)
- **Cooldown:** 7 days
- **Details:** Major reward that scales with your progression

### work
- **Arguments:** None
- **Description:** Work for some easy cash (5% of networth, minimum $100)
- **Cooldown:** 30 minutes
- **Details:** Quick income that scales with your net worth, includes random flavor text

### rob
- **Arguments:** `<@user>`
- **Description:** Attempt to rob another user
- **Details:**
  - Requires at least $100 in wallet
  - Victim must have at least $100
  - Success rate depends on wallet comparison
  - 10% chance to get caught by police
  - Cannot rob users in passive mode
- **Cooldown:** 1 hour

### passive
- **Arguments:** None
- **Description:** Toggle passive mode (can't rob or be robbed)
- **Restrictions:**
  - Cannot go passive if you robbed someone in the last 30 minutes
  - Cannot rob for 30 minutes after changing passive mode
- **Cooldown:** 30 minutes

---

## Gambling

### coinflip
- **Aliases:** `cf`, `flip`
- **Arguments:** `<heads|tails>` `<amount>`
- **Description:** Flip a coin and bet on the outcome
- **Minimum bet:** $50
- **Examples:**
  - `coinflip heads 100` - Bet $100 on heads
  - `cf tails all` - Bet all on tails

### dice
- **Aliases:** `roll`
- **Arguments:** `<amount>`
- **Description:** Roll two dice and bet on the outcome
- **Payouts:**
  - 7 or 11: 2x
  - Doubles: 3x
  - 2 or 12 (Snake Eyes/Boxcars): 5x
- **Minimum bet:** $50

### slots
- **Aliases:** `slot`
- **Arguments:** `<amount>`
- **Description:** Spin the slot machine
- **Payouts:**
  - 7️⃣7️⃣7️⃣ (Jackpot): 50x
  - 💎💎💎: 20x
  - 🍇🍇🍇: 10x
  - 🍊🍊🍊: 5x
  - 🍋🍋🍋: 3x
  - 🍒🍒🍒: 2x
  - Two match: 0.5x
- **Minimum bet:** $100

### blackjack
- **Aliases:** `bj`, `21`
- **Arguments:** `<amount>`
- **Description:** Play blackjack against the dealer
- **Actions:** Hit, Stand, Double Down, Split
- **Payouts:**
  - Blackjack: 1.5x (2.5x total)
  - Win: 1x (2x total)
  - Push: 0x (1x total)
- **Minimum bet:** $100

### roulette
- **Aliases:** `rlt`
- **Arguments:** None
- **Description:** Start a roulette game - everyone can bet!
- **Betting options:**
  - Red (2:1)
  - Black (2:1)
  - Green (35:1)
  - Even (2:1)
  - Odd (2:1)
  - Single Number (35:1)

### russian_roulette
- **Aliases:** `rusrou`
- **Arguments:** None
- **Description:** Play Russian Roulette with up to 16 players!
- **Details:** Join via reaction, players take turns

### frogger
- **Aliases:** `frog`
- **Arguments:** `<easy|medium|hard>` `<amount>`
- **Description:** Play frogger - hop across logs without falling!
- **Multipliers:**
  - Easy: 10% per log
  - Medium: 15% per log
  - Hard: 20% per log
- **Minimum bet:** $100

### lottery
- **Aliases:** `lotto`
- **Arguments:** `[ticket_count|max|all]`
- **Description:** Buy lottery tickets; pool starts at 30,000,000 and increases by 30% of ticket costs
- **Ticket cost:** $300-$1000 (random per ticket)
- **Subcommands:**
  - `lottery` or `lottery info` - View current pool
  - `lottery <count>` - Buy specific number of tickets
  - `lottery max` - Buy as many tickets as possible

---

## Fishing

### fish
- **Aliases:** `cast`, `fih`
- **Arguments:** None
- **Description:** Cast your line and catch fish
- **Requirements:** Equipped rod and bait
- **Cooldown:** 30 seconds
- **Details:**
  - Uses equipped bait based on rod capacity
  - Catch quality depends on rod/bait level and luck
  - Fish have varying rarity and value
  - Bonus fish possible with high-quality bait

### finv
- **Aliases:** `fishnet`, `fishinv`
- **Arguments:** `[page]`
- **Description:** View your fish inventory
- **Features:**
  - Paginated display (16 fish per page)
  - Quick-sell from menu
  - Shows locked status
  - Displays total value

### finfo
- **Aliases:** `fishinfo`, `fi`
- **Arguments:** `[fish_id]`
- **Description:** View detailed information about a fish
- **Details:**
  - Shows fish statistics and rarity
  - Displays value and effect information
  - Can browse all caught fish

### sellfish
- **Aliases:** `sf`, `fishsell`
- **Arguments:** `<fish_id|all>`
- **Description:** Sell fish from your inventory
- **Examples:**
  - `sellfish FISH123` - Sell specific fish
  - `sellfish all` - Sell all unlocked fish

### lockfish
- **Aliases:** `lock`, `fav`, `favourite`
- **Arguments:** `<fish_id>` OR `auto [value <min>] [rarity <max_percent>]`
- **Description:** Lock or unlock a fish to protect it from selling
- **Examples:**
  - `lockfish FISH123` - Toggle lock on specific fish
  - `lockfish auto value 1000` - Auto-lock fish worth $1000+
  - `lockfish auto rarity 5` - Auto-lock fish with ≤5% catch rate

### inv
- **Aliases:** `inventory`, `items`
- **Arguments:** None
- **Description:** View your inventory and equipped gear
- **Shows:** Rods, bait, equipped items

### equip
- **Aliases:** `gear`
- **Arguments:** `<item_name|none>` OR `<rod|bait>` `<item_name|none>`
- **Description:** Equip or unequip a rod or bait
- **Examples:**
  - `equip wooden rod` - Equip wooden rod
  - `equip common` - Equip common bait
  - `equip rod none` - Unequip rod
  - `equip` - View current gear

---

## Shop

### shop
- **Aliases:** `store`
- **Arguments:** `[category]`
- **Description:** Browse items available for purchase
- **Categories:**
  - Rod
  - Bait
  - Fishing Rod
  - Fishing Bait
- **Features:** Interactive category selection menu

### buy
- **Aliases:** `purchase`
- **Arguments:** `<item_id>` `[amount]`
- **Description:** Purchase an item from the shop (specify amount for consumables)
- **Examples:**
  - `buy rod_wood` - Buy wooden rod
  - `buy bait_common 10` - Buy 10 common bait
  - `buy common max` - Buy maximum possible

---

## Games

### blacktea
- **Aliases:** `bt`, `black-tea`
- **Arguments:** None
- **Description:** Play Black Tea — multiplayer word game
- **Details:**
  - 3+ players required
  - 30-second lobby
  - Find words containing given 3-letter patterns
  - 2 lives per player
  - Words must be unique across the entire game

---

## Leaderboard

### leaderboard
- **Aliases:** `lb`, `top`
- **Arguments:** `[category]` `[global]`
- **Description:** View server and global leaderboards
- **Categories:**
  - `networth` (nw) - Net worth rankings
  - `wallet` (bal) - Wallet rankings
  - `bank` - Bank rankings
  - `fish-caught` (fc) - Total fish caught
  - `fish-sold` (fs) - Total fish sold
  - `fish-value` (fv) - Most valuable fish
  - `fishing-profit` (fp) - Fishing profit
  - `gambling-profit` (gp) - Gambling winnings
  - `gambling-losses` (gl) - Gambling losses
  - `commands` (cmd) - Commands used
- **Examples:**
  - `lb` - Server net worth leaderboard
  - `lb wallet` - Server wallet leaderboard
  - `lb nw global` - Global net worth leaderboard
  - `lb fc` - Server fish caught

---

## Utility

### help
- **Aliases:** `h`, `cmds`
- **Arguments:** `[command|module]`
- **Description:** Display all available commands
- **Features:**
  - Interactive category browser
  - Command-specific help
  - Module information
- **Examples:**
  - `help` - Browse all commands
  - `help balance` - Get help on balance command
  - `help economy` - View economy module

### modules
- **Arguments:** None
- **Description:** Display toggleable modules and their enabled/disabled state
- **Permissions:** View only (admin/owner can manage)

### commands
- **Arguments:** None
- **Description:** Show all commands and whether they are enabled
- **Permissions:** View only (admin/owner can manage)

### reactionrole
- **Aliases:** `rr`
- **Arguments:** `[add]` `[-s]` `<message_id|message_link>` `<emoji>` `<@role|role_id|role_name>`
- **Description:** Create a reaction role on a message (manage roles required)
- **Permissions:** Manage Roles or Administrator
- **Examples:**
  - `rr 123456789 🎉 @Members` - Add reaction role
  - `rr add -s https://discord.com/channels/.../... ✅ Verified` - Silent add
  - `rr 123456789 :custom: 987654321` - With custom emoji

---

## Owner (Bot Owner Only)

### stats
- **Aliases:** `statistics`, `botinfo`
- **Description:** View detailed bot statistics (owner only)
- **Shows:**
  - Uptime
  - Websocket latency
  - Server/user/channel counts
  - Command usage statistics
  - Error analytics

### servers
- **Aliases:** `serverlist`, `guilds`
- **Description:** View and manage all servers the bot is in (owner only)
- **Features:**
  - Paginated list
  - Sort by name, members, or ID
  - Leave servers from interface

### givemoney
- **Aliases:** `payout`
- **Arguments:** `<user(s)|all|everyone>` `<amount>`
- **Description:** Add or remove money from users (owner only)
- **Examples:**
  - `givemoney @user 1000` - Give user $1,000
  - `givemoney all 500` - Give all users $500
  - `givemoney @user -100` - Remove $100 from user
  - `givemoney @user1 @user2 @user3 1000` - Give multiple users $1,000

### giveitem
- **Arguments:** `<user(s)|all|everyone>` `<item_id>` `<quantity>`
- **Description:** Add or remove items from users (owner only)
- **Examples:**
  - `giveitem @user rod_wood 1` - Give user a wooden rod
  - `giveitem all bait_common 10` - Give all users 10 common bait
  - `giveitem @user rod_wood -1` - Remove item

### item
- **Arguments:** `<add|price|delete|update>` `[args...]`
- **Description:** Manage shop items (owner only)
- **Subcommands:**
  - `item add <id> <price> [name] [category]` - Add item
  - `item price <id> <newprice>` - Update price
  - `item delete <id>` - Delete item
  - `item update <id> <field>=<value> [...]` - Update item fields

### tuneprices
- **Description:** Adjust bait prices using logged fishing data (owner only)
- **Details:** Uses ML to optimize bait prices based on player outcomes

### blacklist
- **Arguments:** `<add|remove|list>` `-u <user>`
- **Description:** Manage global command blacklist (owner only)
- **Examples:**
  - `blacklist add -u @user` - Add user to blacklist
  - `blacklist remove -u 123456789` - Remove user
  - `blacklist list` - View blacklisted users

### whitelist
- **Arguments:** `<add|remove|list>` `-u <user>`
- **Description:** Manage global command whitelist (owner only)
- **Syntax:** Same as blacklist

### module
- **Arguments:** `<name>` `<enable|disable>` `[-c <channel>]` `[-u <user>]` `[-r <role>]` `[-e]`
- **Description:** Enable or disable a module for this guild (admin/owner)
- **Scope flags:**
  - `-c <channel>` - Apply to specific channel
  - `-u <user>` - Apply to specific user
  - `-r <role>` - Apply to specific role
  - `-e` - Exclusive mode (only with channel)
- **Examples:**
  - `module gambling disable` - Disable gambling for entire server
  - `module economy enable -c #general` - Enable economy in specific channel
  - `module fishing disable -r @NewMembers` - Disable for role

### command
- **Arguments:** `<name>` `<enable|disable>` `[-c <channel>]` `[-u <user>]` `[-r <role>]` `[-e]`
- **Description:** Enable or disable a command for this guild (admin/owner)
- **Syntax:** Same as module command
- **Examples:**
  - `command rob disable` - Disable rob command
  - `command fish enable -c #fishing-zone` - Enable fish in specific channel

### presence
- **Aliases:** `status`, `activity`
- **Arguments:** `<status>` `<activity_type>` `<text>` `[url]`
- **Description:** Change bot presence/status (owner only)
- **Status types:** online, idle, dnd, invisible
- **Activity types:** playing, listening, watching, streaming, competing
- **Examples:**
  - `presence online playing with commands`
  - `presence dnd listening to music`
  - `presence online streaming https://twitch.tv/example Live!`

### suggestions
- **Description:** View and manage user suggestions (owner only)
- **Features:**
  - Paginated view
  - Mark as read
  - Delete suggestions
  - Sort by date, balance, or alphabetical

### mysql
- **Aliases:** `sql`
- **Arguments:** `<SQL statement>`
- **Description:** Execute arbitrary MySQL statement (owner only)
- **Examples:**
  - `mysql SELECT * FROM users LIMIT 10;`
  - `mysql SHOW TABLES;`

### mlstatus
- **Arguments:** `[hours]`
- **Description:** Show ML configuration settings (owner only)
- **Details:** Shows price tuning settings and recent adjustments

### mlset
- **Arguments:** `<key>` `<value>`
- **Description:** Set an ML configuration key/value (owner only)
- **Available keys:**
  - `tune_scale` - Price tuning scale factor
  - `profit_floor` - Minimum profit threshold
  - `bait_delta_cap` - Maximum price change per run
  - `bait_price_min` - Minimum bait price
  - `bait_price_max` - Maximum bait price

### mldelete
- **Arguments:** `<key>`
- **Description:** Remove an ML configuration key (owner only)

### invdebug
- **Arguments:** `[on|off]`
- **Description:** Enable/disable inventory debug logging (owner only)

### dbdebug
- **Arguments:** `[on|off]`
- **Description:** Enable/disable verbose database connection logging (owner only)

---

## Moderation

### antispam
- **Description:** Configure anti-spam settings
- **Permissions:** Administrator
- **Features:**
  - Message spam detection
  - Auto-mute/ban options
  - Configurable thresholds

### urlguard
- **Description:** Configure URL filtering and protection
- **Permissions:** Administrator
- **Features:**
  - Block malicious URLs
  - Whitelist trusted domains

### textfilter
- **Description:** Configure text/word filtering
- **Permissions:** Administrator
- **Features:**
  - Block inappropriate words
  - Auto-delete filtered messages

### reactionfilter
- **Description:** Configure reaction filtering
- **Permissions:** Administrator
- **Features:**
  - Control allowed reactions
  - Auto-remove filtered reactions

---

## Special Syntax Features

### Amount Parsing
Most commands that accept amounts support advanced parsing:
- **Keywords:** `all`, `max`, `half`, `lifesavings`
- **Percentages:** `50%`, `75%`
- **Suffixes:** `1k` (1,000), `1.5m` (1,500,000), `2b` (2,000,000,000)
- **Scientific notation:** `1e6` (1,000,000)

### Examples:
- `pay @user 50%` - Pay 50% of wallet
- `bank 1.5m` - Deposit $1,500,000
- `withdraw all` - Withdraw all bank money
- `coinflip heads 100k` - Bet $100,000

---

## Cooldowns Summary

| Command | Cooldown |
|---------|----------|
| daily | 24 hours |
| weekly | 7 days |
| work | 30 minutes |
| rob | 1 hour |
| passive | 30 minutes |
| fish | 30 seconds |

---

## Notes

1. **Slash Commands:** Most commands support both text prefix (`.`) and slash command (`/`) invocation
2. **Permissions:** Some commands require specific Discord permissions (noted in descriptions)
3. **Server-Specific:** Commands like `module` and `command` work on a per-server basis
4. **Case Insensitive:** All commands and arguments are case-insensitive unless noted
5. **Mentions:** Where user mentions are required, you can also use user IDs

---

*This reference was automatically generated from the bot's source code.*
