# BPP Command Reference

This document is automatically generated from the bot's source code.

## Categories
- [Fun](#fun)
- [Utility](#utility)
- [Admin](#admin)
- [Automation](#automation)
- [Cosmetics](#cosmetics)
- [Economy](#economy)
- [Fishing](#fishing)
- [Fun](#fun)
- [Gambling](#gambling)
- [Games](#games)
- [Leaderboard](#leaderboard)
- [Leveling](#leveling)
- [Market](#market)
- [Media](#media)
- [Mining](#mining)
- [Moderation](#moderation)
- [Nlb](#nlb)
- [Owner](#owner)
- [Passive](#passive)
- [Shop](#shop)
- [Social](#social)
- [Utility](#utility)

---

## Fun

### `reel`
Get a random Instagram Reel

---

## Utility

### `download`
Download media from URL

---

### `gif`
Convert video to GIF

---

### `ocr`
Extract text from an image

---

### `transcribe`
Transcribe audio to text

---

## Admin

### `servereconomy`
Manage server-specific economy settings

---

## Automation

### `autofisher`
manage your autofisher

manage an automated fishing process that casts and catches fish for you 

**Subcommands:**

- `autofisher status`: view your autofisher's current state and catch summary
- `autofisher start`: activate the autofisher (must own the item)
- `autofisher stop`: stop the autofisher and keep uncollected fish
- `autofisher sell`: sell all fish caught by the autofisher
- `autofisher config`: adjust autofisher settings (bait, rod, etc.)

**Examples:**
- `.autofisher status`
- `.af start`
- `.af sell`

---

## Cosmetics

### `title`
view and equip your titles

view your unlocked titles, equip one to display next to your name, or remove 

**Subcommands:**

- `title (no args)`: list all your unlocked titles
- `title equip / set / use <id>`: equip a title by its item ID
- `title remove / clear / unequip`: unequip your current title

**Examples:**
- `.title`
- `.title equip 14`
- `.title remove`

---

## Economy

### `achievements`
view your achievements and progress

---

### `balance`
check your wallet, bank & net worth

view your wallet, bank balance, and net worth. mention another user to check their balance. 

**Examples:**
- `.bal`
- `.balance @User`
- `.b`
- `.$ @User`

**Note:** aliases: bal, b, $, wallet, cash. the footer shows whether global or server economy is active.

---

### `bank`
deposit money into your bank or upgrade your bank limit

open an interactive bank menu to deposit, withdraw, upgrade your bank limit, 

**Subcommands:**

- `bank deposit <amount|max>`: move cash from your wallet into the bank
- `bank withdraw <amount|max>`: pull money out of the bank into your wallet
- `bank upgrade`: spend cash to raise your bank's storage limit
- `bank loan`: open the loan management panel (take / repay)

**Examples:**
- `.bank`
- `.dep 5000`
- `.withdraw max`

**Note:** you earn daily interest on your bank balance — see `.interest`.

---

### `bazaar`
open P2P marketplace — buy and sell items with other players

---

### `boosts`
view your active boosts

---

### `challenges`
view & claim daily challenges

---

### `craft`
Craft items from collected materials

---

### `daily`
claim your daily reward

claim your daily cash reward. the payout is 8% of your current net worth, 

**Examples:**
- `.daily`
- `.d`

**Note:** aliases: d, 24h. cooldown resets 24 hours after each claim.

---

### `event`
view the current world event

---

### `lootboxes`
view available lootbox types

---

### `mastery`
View your species mastery progress and value bonuses

view your fish and ore mastery progress. mastery is earned by repeatedly catching the same fish species 

**Subcommands:**

- `mastery fish [species]`: view fish mastery progress (species optional)
- `mastery ore [type]`: view ore mastery progress (type optional)

**Examples:**
- `.mastery fish`
- `.mastery fish salmon`
- `.mastery ore iron`

**Note:** mastery tiers unlock exclusive cosmetics and titles. catch/mine 1000+ of a species/ore to reach prestige.

---

### `money`
manage your economy and finances

---

### `passive`
toggle passive mode (can't rob or be robbed)

---

### `pay`
transfer money to another user

send money from your wallet to another user. 

**Examples:**
- `.pay @User 5000`
- `.pay @User all`
- `.pay @User 50%`

**Note:** supports shorthand amounts: all, half, 50%, 1k, 1m, etc.

---

### `pet`
manage your pets and their bonuses

---

### `prestige`
prestige to reset your progress and gain a prestige rank

reset your economy progress (wallet, bank, items) in exchange for a permanent prestige rank. 

**Examples:**
- `.prestige`
- `.prestige confirm`

---

### `rebirth`
transcend beyond prestige for permanent multipliers

the ultimate endgame prestige. at prestige 20, rebirth resets your prestige, 

**Examples:**
- `.rebirth`
- `.rebirth confirm`
- `.rb`

**Note:** titles are preserved across rebirths. each rebirth grants a unique title. aliases: rb, transcend.

---

### `rob`
attempt to rob another user

attempt to steal money from another user's wallet. success chance depends on 

**Examples:**
- `.rob @User`
- `.r @User`

**Note:** you cannot rob while in passive mode. the target cannot be in passive mode. 

---

### `skills`
view & manage your skill tree

view and upgrade your skill tree. skills provide permanent percentage bonuses to fishing, gambling, mining, 

**Subcommands:**

- `skills view`: see all available skills and your current levels
- `skills upgrade <skill_name>`: spend skill points to increase a skill's rank
- `skills info <skill_name>`: detailed scaling info for a specific skill

**Examples:**
- `.skills view`
- `.skills upgrade fishing_xp_bonus`
- `.skills info gambling_payout_bonus`

**Note:** aliases: skilltree, skill. skill points cap at 100. reset your tree with `.reset-skills` (costs money).

---

### `streak`
view your daily login streak

---

### `supportdaily`
claim your special support server daily reward

---

### `supportshop`
access the exclusive support server shop

---

### `trade`
send and receive item trade offers with other players

send, receive, and manage item trade offers with other players. 

**Subcommands:**

- `trade offer <@user> <your_item> for <their_item>`: propose a trade to another player
- `trade accept <trade_id>`: accept an incoming trade offer
- `trade decline <trade_id>`: decline an incoming trade offer
- `trade cancel <trade_id>`: cancel a trade you sent
- `trade list`: view all your pending trades

**Examples:**
- `.trade offer @User fishing_rod for gold_bait`
- `.trade accept 42`
- `.trade list`

---

### `use`
use an item from your inventory (lootboxes, boosts, tools)

use a consumable item from your inventory — lootboxes, boosts, and tools. 

**Examples:**
- `.use common lootbox`
- `.use xp boost 3`
- `.open rare lootbox`

**Note:** amount defaults to 1. use `.boosts` to see active boosts and `.lootboxes` for available box types.

---

### `weekly`
claim your weekly reward (50% of networth)

claim your weekly cash reward. the payout is 50% of your current net worth, 

**Examples:**
- `.weekly`
- `.wk`

**Note:** aliases: wk, 7d. cooldown resets 7 days after each claim.

---

### `withdraw`
withdraw money from your bank

---

### `work`
work for some easy cash

work a random job to earn cash. the payout is 3% of your net worth, 

**Examples:**
- `.work`
- `.w`

**Note:** aliases: w, job. cooldown is 30 minutes between uses.

---

## Fishing

### `crew`
manage your fishing crew

manage your fishing crew — a group of players who share crew bonuses 

**Subcommands:**

- `crew create <name>`: create a new crew (costs money)
- `crew invite <@user>`: invite a player to your crew
- `crew join <crew_name>`: accept a pending crew invitation
- `crew leave`: leave your current crew
- `crew info [crew_name]`: view crew details and member list
- `crew kick <@user>`: kick a member from your crew (captain only)
- `crew open`: make the crew joinable without invite
- `crew close`: restrict the crew to invite-only
- `crew disband`: permanently disband the crew (captain only)
- `crew leaderboard`: view the crew leaderboard

**Examples:**
- `.crew create \"deep sea anglers\`
- `.crew invite @User`
- `.crew info`

---

### `equip`
equip or unequip a rod and/or bait

equip a fishing rod and/or bait by name, or pass `none` to unequip everything.

**Examples:**
- `.equip gold rod`
- `.equip basic bait`
- `.equip none`

---

### `finfo`
view detailed information about a fish

---

### `finv`
view your fish inventory

---

### `fish`
cast your line and catch fish

the main fishing command. cast your rod and catch fish, manage your inventory, sell catches, 

**Subcommands:**

- `fish cast`: cast your rod and catch a fish (aliases: f, catch)
- `fish inventory`: view your caught fish (aliases: finv, inv, i)
- `fish sell [fish_id|all]`: sell unlocked fish for cash (alias: sellfish)
- `fish info <species>`: details on a specific fish species (alias: finfo)
- `fish equip [rod] [bait]`: equip fishing gear
- `fish lock <fish_id>`: lock a fish to protect from bulk-sell (alias: lockfish)
- `fish suggest <details...>`: propose a new fish species (alias: suggestfish)
- `fish crew <action> [args...]`: manage your fishing crew

**Examples:**
- `.fish cast`
- `.f`
- `.fish inv`
- `.fish sell all`
- `.fish equip gold rod`

**Note:** casting has a cooldown that decreases with skill tree bonuses. rare and legendary fish are harder to catch.

---

### `fishdex`
view your fish collection — optionally filter by category

view your fish collection, optionally filtered by a category. 

**Examples:**
- `.fishdex`
- `.fishdex freshwater`
- `.fishdex rare 2`

---

### `inv`
view your inventory and equipped gear

view your full inventory of rods, baits, titles, and other items. 

**Examples:**
- `.inv`
- `.inv rod`
- `.inv bait`

---

### `lockfish`
lock or unlock a fish to protect it from selling

lock a specific fish by ID to protect it from bulk-sell, or set up 

**Subcommands:**

- `lockfish <fish_id>`: toggle lock on a specific fish
- `lockfish auto`: show your current auto-lock rules
- `lockfish auto value <min_value>`: auto-lock any fish worth at least this much
- `lockfish auto rarity <max_percent>`: auto-lock fish with catch-rate at or below this %

**Examples:**
- `.lockfish 47`
- `.lockfish auto value 5000`
- `.lockfish auto rarity 2`

---

### `sellfish`
sell fish from your inventory

sell one or all unlocked fish from your inventory for cash. 

**Examples:**
- `.sellfish 12`
- `.sellfish all`

---

### `suggestfish`
Suggest a new fish to be added to the game

propose a new fish species to be added to the game. all 10 fields are required. 

**Examples:**
- `.suggestfish \"ghost koi\" 👻 12.5 800 3500 xp_boost 15 3 7 a translucent koi that glows under moonlight`

**Note:** effect can be: none, xp_boost, money_boost, luck, etc. effect_% is the proc chance (0-100).

---

## Fun

### `fyp`
get a random tiktok/short

---

### `hug`
hug someone!

---

### `kiss`
kiss someone!

---

### `pat`
pat someone's head!

---

### `study`
get your friends to study with fake facts!

---

### `yt`
search and get a random youtube video

---

## Gambling

### `blackjack`
play blackjack against the dealer

classic blackjack vs the dealer. hit, stand, double down, or split pairs 

**Subcommands:**

- `blackjack hit`: draw another card
- `blackjack stand`: end your turn and let the dealer play
- `blackjack double down`: double your bet and receive exactly one more card
- `blackjack split`: split two equal-value cards into separate hands (costs an extra bet)

**Examples:**
- `.blackjack 1000`
- `.bj 5000`
- `.21 500`
- `.card all`

**Note:** minimum bet is $100. blackjack pays 2.5x. push returns your bet.

---

### `bomb`
play minesweeper - reveal safe cells without hitting mines!

play minesweeper — pick safe cells to multiply your bet. 

**Subcommands:**

- `bomb easy`: few mines, low multipliers
- `bomb medium`: balanced risk and reward
- `bomb hard`: many mines, high multipliers
- `bomb impossible`: nearly every cell is a mine
- `bomb <number>`: set an exact number of mines

**Examples:**
- `.bomb easy 1000`
- `.bomb hard 5000 5 5`
- `.bomb 3 2000`

**Note:** default grid is 3×3. max grid is 5×5.

---

### `coinflip`
flip a coin and bet on the outcome

flip a coin! bet on heads or tails — defaults to heads if not specified.

**Examples:**
- `.cf 1000`
- `.cf 5000 tails`

---

### `crash`
bet on a rising multiplier — cash out before it crashes!

a multiplier starts at 1.00x and climbs. cash out any time to lock in your 

**Examples:**
- `.crash 1000`
- `.cr 5000`

**Note:** use the cash out button before the multiplier crashes. alias: `cr`.

---

### `dice`
roll two dice and bet on the outcome

roll two dice and win based on the result. 

**Examples:**
- `.dice 500`
- `.roll 2000`

**Note:** minimum bet is $50. alias: `roll`.

---

### `frogger`
play frogger - hop across logs without falling!

play frogger — hop across lanes of traffic. harder difficulties have 

**Examples:**
- `.frogger easy 500`
- `.frogger hard 5000`

---

### `gamble`
play various gambling games

---

### `gamblingaudit`
verify your gambling transaction history

---

### `gstats`
view your gambling and game statistics

---

### `jackpot`
view the progressive jackpot pool

view the current progressive jackpot pool and recent winners. 

**Examples:**
- `.jackpot`
- `.jp`

**Note:** the jackpot is triggered automatically on any gambling win — no extra bet required. alias: `jp`.

---

### `lottery`
buy lottery tickets; pool starts at 30,000,000 and increases by 30% of ticket costs

buy lottery tickets for a chance at the jackpot pool, 

**Subcommands:**

- `lottery <amount>`: purchase that many lottery tickets
- `lottery info / pool / status`: view current pool size and next drawing time

**Examples:**
- `.lottery 5`
- `.lottery info`

---

### `poker`
play Texas Hold'em poker with other players

play Texas Hold'em poker against other players and/or CPU opponents. 

**Subcommands:**

- `poker start [min_bet] [cpus]`: create a new poker table (default min bet: 100)
- `poker join`: join an active table before the hand starts

**Examples:**
- `.poker start 500`
- `.poker start 1000 2`
- `.poker join`

---

### `roulette`
start a roulette game - everyone can bet!

start a multiplayer roulette table. anyone can place bets using the buttons 

**Subcommands:**

- `roulette red / black`: bet on the landing color (2:1)
- `roulette green`: bet on 0 or 00 (35:1)
- `roulette even / odd`: bet on number parity — 0/00 do not count (2:1)
- `roulette number`: bet on a specific number 0-36 or 00 (35:1)
- `roulette spin`: author-only: close betting and spin the wheel
- `roulette cancel`: author-only: cancel the game and refund all bets

**Examples:**
- `.roulette`
- `.rlt`

**Note:** only the player who started the game can spin or cancel.

---

### `russian_roulette`
Play Russian Roulette with up to 16 players!

---

### `slots`
spin the slot machine

spin the slot machine. matching symbols multiply your bet — 

**Examples:**
- `.slots 500`
- `.slot 10000`

**Note:** jackpot pool grows with every spin. use `.jackpot` to view it.

---

## Games

### `blacktea`
Play Black Tea — multiplayer word game

---

### `raid`
start a cooperative boss raid with your crew

start or check a cooperative boss raid. players pool entry fees and 

**Subcommands:**

- `raid start [entry_fee]`: create a raid lobby
- `raid status`: view the current raid's boss HP and participants

**Examples:**
- `.raid start 5000`
- `.raid status`

---

### `react`
Challenge someone to a reaction time game

challenge another player (or open to anyone) to a reaction-time duel. 

**Examples:**
- `.react @User 1000`
- `.react open 500`

---

### `tictactoe`
Challenge someone to tic-tac-toe with optional bet

challenge someone to classic tic-tac-toe, optionally with a cash bet on the line.

**Examples:**
- `.ttt @User`
- `.ttt @User 2000`

---

### `whitetea`
Play White Tea — each player gets their own pattern per round!

---

## Leaderboard

### `leaderboard`
view various leaderboards

view ranked leaderboards by category — defaults to net worth. 

**Examples:**
- `.lb`
- `.lb fish`
- `.lb xp global`
- `.top mining`

---

## Leveling

### `level`
configure and manage server leveling

---

### `rank`
view your or someone else's level and XP

---

### `xpblacklist`
manage XP blacklists for channels, roles, and users

block specific channels, roles, or users from earning XP. 

**Subcommands:**

- `xpblacklist channel add <#channel>`: stop XP from being earned in this channel
- `xpblacklist channel remove <#channel>`: re-enable XP for this channel
- `xpblacklist channel list`: show all blacklisted channels
- `xpblacklist role add <@role>`: members with this role earn no XP
- `xpblacklist role remove <@role>`: un-blacklist this role
- `xpblacklist role list`: show all blacklisted roles
- `xpblacklist user add <@user>`: block a specific user from earning XP
- `xpblacklist user remove <@user>`: un-blacklist a user
- `xpblacklist user list`: show all blacklisted users

**Examples:**
- `.xpblacklist channel add #bot-spam`
- `.xpbl role add @Muted`
- `.xpbl user list`

---

## Market

### `buy`
purchase an item from the server market

purchase an item from the shop. the last argument is parsed as a quantity 

**Examples:**
- `.buy common bait 50`
- `.buy fishing rod`
- `.buy gold pickaxe max`

---

### `market`
browse or buy from the server market

browse or buy from the player-driven server market. 

**Subcommands:**

- `market (no args)`: browse current market listings
- `market edit add key=value ...`: create a new market listing (name=, desc=, price=, limit=, expires=)
- `market edit update <item_id> key=value`: update an existing listing's fields
- `market edit delete <item_id>`: remove a listing from the market
- `market edit help`: show the edit sub-system usage

**Examples:**
- `.market`
- `.market edit add name=\"rare gem\" price=7500 limit=10`

---

## Media

### `caption`
add a top caption to an image

---

### `meme`
create an impact-style meme

---

### `motivate`
create a motivational poster

---

### `random`
apply 3 random effects to media

---

### `reverse`
reverse a gif or video

---

### `speed`
change the speed of a gif or video (e.g. 2x, 0.5x, 30fps)

---

## Mining

### `mine`
start a mining session to collect ores

play the interactive mining minigame. a 3×3 grid appears with glowing ore cells. 

**Examples:**
- `.mine`

**Note:** higher prestige gives +5% ore value per level. home court (support server) gives +5%. supporter role gives +10%.

---

### `minv`
view your mined ore inventory

view your mining inventory: all collected ores, their values, and quantities. 

**Examples:**
- `.minv`
- `.mi`
- `.mininginfo`

**Note:** aliases: mi, mininginfo. ores are grouped by type.

---

### `sellore`
sell a mined ore by ID or 'all'

sell a mined ore by its inventory ID, or pass `all` to sell every ore at once.

**Examples:**
- `.sellore 5`
- `.sellore all`

---

## Moderation

### `antispam`
Configure anti-spam protection

---

### `automod`
configure automatic moderation guards

---

### `ban`
ban a user from the server

---

### `case`
look up details for a moderation case

---

### `duration`
update the duration for an existing timed infraction

---

### `endgame`
end active games in this channel

---

### `history`
view a user's infraction history

---

### `infractions`
configure infraction settings

---

### `jail`
strip roles and assign jail role to a user

---

### `jailsetup`
set the jail role and channel

---

### `kick`
kick a member from the server

---

### `lockdown`
lock a channel to prevent users from sending messages

---

### `log`
configure server logging

---

### `massban`
ban multiple users at once

---

### `masskick`
kick multiple users at once

---

### `massmute`
mute multiple users at once

---

### `masstimeout`
timeout multiple users at once

---

### `mod`
moderation tools and configuration

consolidated moderation command. subcommands handle: banning/kicking/muting/jailing users, 

**Subcommands:**

- `mod ban <@user> [duration] [reason...]`: ban a user (duration: 7d, 30d, permanent, etc.)
- `mod unban <@user>`: remove a ban
- `mod kick <@user> [reason...]`: kick a user from the server
- `mod timeout <@user> <duration> [reason...]`: apply discord timeout (max 28 days)
- `mod untimeout <@user>`: remove timeout
- `mod mute <@user> [duration] [reason...]`: assign mute role (custom, server-configured)
- `mod unmute <@user>`: remove mute role
- `mod warn <@user> [reason...]`: issue a warning (tracked in history)
- `mod jail <@user> [duration]`: assign jail role (custom, server-configured)
- `mod unjail <@user>`: remove jail role
- `mod case <case_id>`: view details of a specific moderation case
- `mod history <@user>`: view all infractions for a user
- `mod modstats [@user]`: view mod action statistics (server or per-mod)
- `mod pardon <case_id>`: remove an infraction from a user's record
- `mod reason <case_id> <text...>`: add or update the reason for a case
- `mod muterole <@role>`: set the role assigned when muting users
- `mod jailsetup`: configure the jail role and channel
- `mod logconfig [#channel]`: set the moderation log channel

**Examples:**
- `.mod ban @User 7d spamming`
- `.mod warn @User`
- `.mod history @User`
- `.mod case 142`

**Note:** all actions are logged and auditable. expired timeouts are automatically removed.

---

### `modlog-channel`
set the moderation log channel

---

### `modmail`
manage modmail threads and configuration

---

### `modstats`
view moderation statistics for a moderator

---

### `mute`
assign mute role to a user

---

### `muterole`
set the mute role for this server

---

### `note`
add a moderation note to a user's record

---

### `pardon`
pardon (forgive) a moderation case

---

### `purge`
delete multiple messages in the current channel

---

### `quiet`
toggle quiet moderation settings

---

### `reactionfilter`
Configure reaction filtering for offensive emojis

---

### `reason`
update the reason for an existing infraction case

---

### `slowmode`
set the slowmode for the current channel

---

### `softban`
ban and immediately unban a user to clear messages

---

### `textfilter`
Configure the text filter (whitelists, blacklists, toggles).

---

### `timeout`
apply discord native timeout to a user

---

### `unban`
unban a user by id

---

### `unjail`
remove a user from jail and restore their roles

---

### `unlock`
unlock a channel to allow users to send messages again

---

### `unmute`
remove mute role from a user

---

### `untimeout`
remove a timeout from a user

---

### `urlguard`
Configure URL and invite link filtering

---

### `warn`
issue a warning to a user

---

## Nlb

### `nlb`
paginated leaderboard navigator

---

## Owner

### `bac`
manage BAC global bans (owner only)

owner-only: manage the global BAC (Bronx AntiCheat) ban list. 

**Flags:**

- `-u <@user>`: target user to ban or unban
- `-r <reason...>`: reason for the ban (shown in audit)

**Examples:**
- `.bac add -u @User -r macro abuse`
- `.bac remove -u @User`
- `.bac list`

---

### `cleantitles`
purge legacy active_title items from all inventories

---

### `cmdh`
view command history and balance changes for a user (owner only)

---

### `dbdebug`
enable/disable verbose database connection logging (owner only)

---

### `feature`
manage runtime feature flags

---

### `gambaudit`
audit a user's gambling transactions (owner only)

---

### `giveitem`
add or remove items from users (owner only)

owner-only: add or remove items from one or more users' inventories.

**Examples:**
- `.giveitem @User 14 3`
- `.giveitem remove @User 14 1`

---

### `givemoney`
add or remove money from users (owner only)

owner-only: add or remove money from one or more users' wallets. 

**Examples:**
- `.givemoney @User 5000`
- `.givemoney remove @User 1000`
- `.givemoney all 500`

---

### `health`
view real-time bot health & infrastructure (owner only)

---

### `invdebug`
enable/disable inventory debug logging (owner only)

---

### `logbeta`
Toggle beta tester status for a server (owner only)

---

### `mlclassify`
run market state classifier and update regime (owner only)

---

### `mldelete`
remove an ML configuration key (owner only)

---

### `mlset`
set an ML configuration key/value (owner only)

---

### `mlstatus`
show ML configuration settings (owner only)

---

### `mysql`
execute arbitrary mysql statement (owner only)

---

### `ostats`
view detailed bot statistics (owner only)

---

### `patchadd`
Add new patch notes (owner only)

---

### `patchdelete`
Delete a patch note by ID or version (owner only)

---

### `presence`
change bot presence/status (owner only)

---

### `purgeuser`
completely wipe a user's data from the database (owner only)

---

### `servers`
view and manage all servers the bot is in (owner only)

---

### `spawnevent`
force a world event to spawn (owner only)

---

### `suggestions`
view and manage user suggestions (owner only)

---

### `titledb`
manage dynamic titles (add/edit/remove/list)

---

### `whitelist`
manage global command whitelist (owner only)

owner-only: manage the global command whitelist. whitelisted users bypass 

**Flags:**

- `-u <@user>`: target user
- `-r <reason...>`: reason for whitelisting

**Examples:**
- `.whitelist add -u @Trusted -r beta tester`
- `.whitelist list`

---

## Passive

### `claim`
manage mining claims for passive ore income

manage mining claims that passively generate ore over time. 

**Subcommands:**

- `claim buy <ore_type>`: purchase a new mining claim for that ore
- `claim list`: view all your active claims and their output
- `claim collect`: collect accumulated ore from all claims
- `claim upgrade [claim]`: upgrade a claim to increase yield
- `claim abandon [claim]`: permanently remove a claim

**Examples:**
- `.claim buy iron`
- `.claim collect`
- `.claim list`

---

### `cmarket`
view fluctuating commodity market prices

view real-time commodity market prices that fluctuate over time, 

**Subcommands:**

- `cmarket (no args)`: display the current price sheet for all commodities
- `cmarket sell <item_name>`: sell an item from your inventory at the current market price

**Examples:**
- `.cmarket`
- `.cmarket sell iron ore`

---

### `interest`
claim daily interest on your bank balance

claim your daily interest on your bank balance, or check the current interest rate.

**Subcommands:**

- `interest claim`: collect your daily interest payment (default action)
- `interest rate / info`: display the current interest rate and how it's calculated

**Examples:**
- `.interest`
- `.interest rate`

---

### `pond`
manage your fish pond for passive income

manage a personal fish pond that generates passive income over time. 

**Subcommands:**

- `pond build / create`: construct your fish pond (one-time cost)
- `pond stock / add`: add fish from your inventory to the pond
- `pond collect`: collect accumulated earnings
- `pond upgrade`: increase pond capacity and income rate
- `pond view`: see your pond's status and stocked fish

**Examples:**
- `.pond build`
- `.pond stock`
- `.pond collect`
- `.pond view`

---

## Shop

### `item`
manage shop items (owner only)

owner-only command to manage shop inventory: add items, change prices, 

**Subcommands:**

- `item add <name> <price> <category> <description...>`: create a new shop item
- `item price <item_id> <new_price>`: change an item's price
- `item update <item_id> <field> <value>`: update item metadata (description, category, etc.)
- `item delete <item_id>`: permanently remove an item from the shop

**Examples:**
- `.item add \"super bait\" 500 bait \"catches rare fish more often\`
- `.item price 14 2500`

---

### `sellitem`
sell shop items back for 40% of original value

---

### `shop`
browse items available for purchase

browse the item shop. optionally filter by a category name to narrow results.

**Examples:**
- `.shop`
- `.shop bait`
- `.shop tools`

---

### `tuneprices`
adjust bait prices using logged fishing data (owner only)

---

## Social

### `heist`
start a cooperative vault heist (3-8 players)

launch or join a cooperative vault heist. 3-8 players work together to crack 

**Subcommands:**

- `heist start [entry_fee]`: create a heist lobby (default fee: free)
- `heist start [difficulty]`: set difficulty 1-5 when creating
- `heist join`: join an active heist lobby
- `heist status`: check the current heist's progress

**Examples:**
- `.heist start 1000`
- `.heist join`
- `.heist status`

---

## Utility

### `autopurge`
automatically delete your messages on a timer (multiple schedules allowed)

---

### `autorole`
manage auto-assigned roles for new members

---

### `avatar`
display a user's avatar

view a user's avatar in full size. shows both the global avatar and the 

**Examples:**
- `.avatar`
- `.avatar @User`
- `.av 123456789`

**Note:** aliases: av, pfp, icon.

---

### `banner`
display a user's banner

---

### `boss`
view the current global boss status and progress

---

### `cleanup`
delete recent bot messages and commands that invoked them

bulk delete messages in the current channel. optionally filter by user, 

**Flags:**

- `--bots`: only delete messages sent by bots
- `--contains <text>`: only delete messages containing this text

**Examples:**
- `.cleanup 20`
- `.purge 50 @User`
- `.clear 10 --bots`

**Note:** aliases: purge, clear. discord limits bulk delete to messages under 14 days old.

---

### `command`
enable or disable a command. scope: -u <user> -r <role> -c <channel> -e (exclusive)

enable or disable a single command for this server. 

**Flags:**

- `-c <#channel>`: apply the rule only in this channel
- `-u <@user>`: apply the rule only to this user
- `-r <@role>`: apply the rule only to members with this role
- `-e`: exclusive mode — the rule applies ONLY to the specified target

**Examples:**
- `.command slots disable`
- `.command rob disable -c #marketplace`

---

### `commands`
show all commands and whether they are enabled

---

### `dcme`
disconnect yourself from your current voice channel

---

### `giveaway`
manage server giveaways

create and manage giveaways. users react to enter. 

**Subcommands:**

- `giveaway start <duration> <prize...>`: start a giveaway (e.g. 1h, 30m, 7d)
- `giveaway end <message_id>`: end a giveaway early and draw a winner
- `giveaway reroll <message_id>`: reroll a new winner for a finished giveaway

**Examples:**
- `.giveaway start 1h $50`
- `000 cash`
- `.giveaway end 123456789`
- `.giveaway reroll 123456789`

---

### `guide`
in-depth master guide to using the bot

---

### `help`
display all available commands

---

### `invite`
get the bot invite link

get the bot's invite link to add it to another server.

**Examples:**
- `.invite`

---

### `module`
enable or disable a module. scope: -u <user> -r <role> -c <channel> -e (exclusive)

enable or disable an entire command module for this server. 

**Flags:**

- `-c <#channel>`: apply the rule only in this channel
- `-u <@user>`: apply the rule only to this user
- `-r <@role>`: apply the rule only to members with this role
- `-e`: exclusive mode — the rule applies ONLY to the specified target, everyone else is unaffected

**Examples:**
- `.module gambling disable`
- `.module fishing disable -c #general`
- `.module economy enable -r @VIP -e`

---

### `modules`
display toggleable modules and their enabled/disabled state

---

### `patch`
View the latest bot updates and patch notes

---

### `payout`
pay a user from the server treasury

---

### `permissions`
show all module and command permission settings for this guild

---

### `ping`
check bot latency and response time

check bot latency. shows websocket ping (discord gateway) and round-trip time 

**Examples:**
- `.ping`
- `.p`
- `.ms`
- `.latency`

**Note:** aliases: p, ms, pong, latency.

---

### `poll`
create a poll with optional role-based voting restrictions

create a poll with reaction-based voting. supports up to 9 options. 

**Examples:**
- `.poll \"best color?\" \"red\" \"blue\" \"green\`
- `.poll \"movie night?\" \"action\" \"comedy\`

**Note:** wrap the question and each option in quotes. maximum 9 options.

---

### `prefix`
configure custom command prefixes (utility)

---

### `privacy`
manage your data privacy and opt-out preferences

---

### `reactionrole`
create a reaction role on a message (manage roles required)

set up reaction roles so users can self-assign roles by reacting to a message. 

**Subcommands:**

- `reactionrole <target> <emoji> <@role> [...]`: add reaction role(s) to a message — use `^` for the message above
- `reactionrole add / create`: alias for the default add behavior
- `reactionrole check <msg_id | msg_link | ^>`: inspect reaction roles on an existing message
- `reactionrole silent`: toggle silent mode (no DM confirmation to users)

**Examples:**
- `.rr ^ 🎮 @Gamer 🎵 @Music`
- `.reactionrole 123456789 ✅ @Verified`
- `.rr check ^`

**Note:** use `^` to target the message directly above the command.

---

### `report`
submit a bug report for the bot

---

### `role`
add roles to multiple users at once

---

### `rolepanel`
manage interaction-based role panels (buttons/select)

---

### `rr-sync`
force sync reaction roles to ensure everyone has their roles

---

### `say`
echo a message as the bot

---

### `serveravatar`
display the server's avatar/icon

---

### `serverbanner`
display the server's banner

---

### `serverconfig`
Configure custom server metadata

---

### `serverinfo`
display information about the server

display information about the current server: name, id, owner, creation date, 

**Examples:**
- `.serverinfo`
- `.si`
- `.guildinfo`

**Note:** aliases: si, guildinfo, sinfo.

---

### `settings`
Configure bot settings for your server

view or update your personal bot settings: dm notifications, privacy options, 

**Examples:**
- `.settings`
- `.settings dm_notifications off`
- `.settings privacy public`

---

### `snipe`
view recently deleted messages in this channel

view the last deleted message in the current channel. 

**Examples:**
- `.snipe`

---

### `stats`
view server statistics with charts

---

### `steal`
steal custom emojis into this server (manage emojis required)

---

### `suggest`
submit a suggestion for the bot

---

### `ticket`
configure the ticket system

---

### `treasury`
view server giveaway treasury balance

---

### `userinfo`
display information about a user

display detailed information about a user: username, display name, nickname, 

**Examples:**
- `.userinfo`
- `.ui @User`
- `.whois 123456789`

**Note:** aliases: ui, whois, uinfo, u.

---

