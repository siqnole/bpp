#pragma once
#include "../command.h"
#include "../command_handler.h"
#include <map>
#include <string>

namespace commands {

// Populate extended help fields on commands that have subcommands, flags,
// or otherwise need richer usage documentation than their one-line description.
// Call this AFTER all commands have been registered with the handler.
inline void populate_extended_help(CommandHandler* handler) {
    auto categories = handler->get_commands_by_category();

    // Flatten into a name -> Command* lookup (first match wins)
    ::std::map<::std::string, Command*> lookup;
    for (auto& [cat, cmds] : categories) {
        for (auto* cmd : cmds) {
            if (lookup.find(cmd->name) == lookup.end()) {
                lookup[cmd->name] = cmd;
            }
        }
    }

    auto set = [&](const ::std::string& name, auto fn) {
        auto it = lookup.find(name);
        if (it != lookup.end()) fn(it->second);
    };

    // ── economy ────────────────────────────────────────────────────────

    set("balance", [](Command* c) {
        c->extended_description =
            "view your wallet, bank balance, and net worth. mention another user to check their balance. "
            "if the server has a server economy enabled, balances shown are per-guild.";
        c->detailed_usage = ".balance [@user]";
        c->examples = {".bal", ".balance @User", ".b", ".$ @User"};
        c->notes = "aliases: bal, b, $, wallet, cash. the footer shows whether global or server economy is active.";
    });

    set("pay", [](Command* c) {
        c->extended_description =
            "send money from your wallet to another user. "
            "a 5% tax applies by default (server economy may use a different rate). "
            "transfers between prestiged and non-prestiged players are blocked. "
            "there is a 30-second cooldown between successive pays to the same recipient.";
        c->detailed_usage = ".pay <@user> <amount>";
        c->examples = {".pay @User 5000", ".pay @User all", ".pay @User 50%"};
        c->notes = "supports shorthand amounts: all, half, 50%, 1k, 1m, etc.";
    });

    set("daily", [](Command* c) {
        c->extended_description =
            "claim your daily cash reward. the payout is 8% of your current net worth, "
            "with a minimum of $500 and a maximum of $250m. resets every 24 hours.";
        c->detailed_usage = ".daily";
        c->examples = {".daily", ".d"};
        c->notes = "aliases: d, 24h. cooldown resets 24 hours after each claim.";
    });

    set("weekly", [](Command* c) {
        c->extended_description =
            "claim your weekly cash reward. the payout is 50% of your current net worth, "
            "with a minimum of $1,000 and a maximum of $1b. resets every 7 days.";
        c->detailed_usage = ".weekly";
        c->examples = {".weekly", ".wk"};
        c->notes = "aliases: wk, 7d. cooldown resets 7 days after each claim.";
    });

    set("work", [](Command* c) {
        c->extended_description =
            "work a random job to earn cash. the payout is 3% of your net worth, "
            "with a minimum of $100 and a maximum of $25m. has a 30-minute cooldown.";
        c->detailed_usage = ".work";
        c->examples = {".work", ".w"};
        c->notes = "aliases: w, job. cooldown is 30 minutes between uses.";
    });

    set("rob", [](Command* c) {
        c->extended_description =
            "attempt to steal money from another user's wallet. success chance depends on "
            "how your wallet compares to the target's. if you fail, you pay a fine of "
            "15–30% of your wallet. there is also a 10% chance of getting caught by the police, "
            "costing you 50–75% of your wallet. has a 2-hour cooldown after any outcome.";
        c->detailed_usage = ".rob <@user>";
        c->examples = {".rob @User", ".r @User"};
        c->notes = "you cannot rob while in passive mode. the target cannot be in passive mode. "
            "both you and the target need at least $100 in your wallets. use `.passive` to toggle passive mode.";
    });

    set("rebirth", [](Command* c) {
        c->extended_description =
            "the ultimate endgame prestige. at prestige 20, rebirth resets your prestige, "
            "balance, inventory, fish, mining claims, skill tree, and cooldowns in exchange "
            "for a permanent 1.1x earnings multiplier on all income. up to 5 rebirths are "
            "available, each with escalating requirements and a stacking multiplier (max ~1.61x). "
            "run without `confirm` to preview your current progress and requirements.";
        c->detailed_usage = ".rebirth [confirm]";
        c->examples = {".rebirth", ".rebirth confirm", ".rb"};
        c->notes = "titles are preserved across rebirths. each rebirth grants a unique title. aliases: rb, transcend.";
    });

    set("bank", [](Command* c) {
        c->extended_description =
            "open an interactive bank menu to deposit, withdraw, upgrade your bank limit, "
            "or take out / repay loans. when called with no arguments the menu appears; "
            "the aliases `dep` / `deposit` and `with` / `withdraw` shortcut directly to "
            "the deposit and withdraw actions.";
        c->detailed_usage = ".bank [deposit|withdraw|upgrade|loan] [amount|max]";
        c->subcommands = {
            {"deposit <amount|max>",  "move cash from your wallet into the bank"},
            {"withdraw <amount|max>", "pull money out of the bank into your wallet"},
            {"upgrade",               "spend cash to raise your bank's storage limit"},
            {"loan",                  "open the loan management panel (take / repay)"},
        };
        c->examples = {".bank", ".dep 5000", ".withdraw max"};
        c->notes = "you earn daily interest on your bank balance — see `.interest`.";
    });

    set("trade", [](Command* c) {
        c->extended_description =
            "send, receive, and manage item trade offers with other players. "
            "offers must be accepted by the recipient before items change hands.";
        c->detailed_usage = ".trade <action> [args...]";
        c->subcommands = {
            {"offer <@user> <your_item> for <their_item>", "propose a trade to another player"},
            {"accept <trade_id>",   "accept an incoming trade offer"},
            {"decline <trade_id>",  "decline an incoming trade offer"},
            {"cancel <trade_id>",   "cancel a trade you sent"},
            {"list",                "view all your pending trades"},
        };
        c->examples = {".trade offer @User fishing_rod for gold_bait", ".trade accept 42", ".trade list"};
    });

    set("use", [](Command* c) {
        c->extended_description =
            "use a consumable item from your inventory — lootboxes, boosts, and tools. "
            "the last argument is treated as a quantity if it's a number.";
        c->detailed_usage = ".use <item_name> [amount]";
        c->examples = {".use common lootbox", ".use xp boost 3", ".open rare lootbox"};
        c->notes = "amount defaults to 1. use `.boosts` to see active boosts and `.lootboxes` for available box types.";
    });

    set("buy", [](Command* c) {
        c->extended_description =
            "purchase an item from the shop. the last argument is parsed as a quantity "
            "(supports `all` / `max` to buy as many as you can afford).";
        c->detailed_usage = ".buy <item_name> [amount|max|all]";
        c->examples = {".buy common bait 50", ".buy fishing rod", ".buy gold pickaxe max"};
    });

    set("prestige", [](Command* c) {
        c->extended_description =
            "reset your economy progress (wallet, bank, items) in exchange for a permanent prestige rank. "
            "higher prestige unlocks multipliers and exclusive items. run without `confirm` to preview requirements.";
        c->detailed_usage = ".prestige [confirm]";
        c->examples = {".prestige", ".prestige confirm"};
    });

    // ── shop / market ──────────────────────────────────────────────────

    set("shop", [](Command* c) {
        c->extended_description =
            "browse the item shop. optionally filter by a category name to narrow results.";
        c->detailed_usage = ".shop [category]";
        c->examples = {".shop", ".shop bait", ".shop tools"};
    });

    set("item", [](Command* c) {
        c->extended_description =
            "owner-only command to manage shop inventory: add items, change prices, "
            "update metadata, or remove items entirely.";
        c->detailed_usage = ".item <action> <args...>";
        c->subcommands = {
            {"add <name> <price> <category> <description...>", "create a new shop item"},
            {"price <item_id> <new_price>",                    "change an item's price"},
            {"update <item_id> <field> <value>",               "update item metadata (description, category, etc.)"},
            {"delete <item_id>",                               "permanently remove an item from the shop"},
        };
        c->examples = {".item add \"super bait\" 500 bait \"catches rare fish more often\"", ".item price 14 2500"};
    });

    set("market", [](Command* c) {
        c->extended_description =
            "browse or buy from the player-driven server market. "
            "admins can use `edit` to add, update, or remove market listings.";
        c->detailed_usage = ".market [edit <action> [key=value ...]]";
        c->subcommands = {
            {"(no args)",                       "browse current market listings"},
            {"edit add key=value ...",           "create a new market listing (name=, desc=, price=, limit=, expires=)"},
            {"edit update <item_id> key=value",  "update an existing listing's fields"},
            {"edit delete <item_id>",            "remove a listing from the market"},
            {"edit help",                        "show the edit sub-system usage"},
        };
        c->examples = {".market", ".market edit add name=\"rare gem\" price=7500 limit=10"};
    });

    // ── fishing ────────────────────────────────────────────────────────

    set("fish", [](Command* c) {
        c->extended_description =
            "the main fishing command. cast your rod and catch fish, manage your inventory, sell catches, "
            "equip gear, and join or create fishing crews. anti-macro system: math captcha triggers randomly "
            "to prevent automation; failing escalates your cooldown penalty (5min → 30min → global blacklist). "
            "skill tree bonuses affect cooldown speed and catch quality.";
        c->detailed_usage = ".fish <action> [args...]";
        c->subcommands = {
            {"cast", "cast your rod and catch a fish (aliases: f, catch)"},
            {"inventory", "view your caught fish (aliases: finv, inv, i)"},
            {"sell [fish_id|all]", "sell unlocked fish for cash (alias: sellfish)"},
            {"info <species>", "details on a specific fish species (alias: finfo)"},
            {"equip [rod] [bait]", "equip fishing gear"},
            {"lock <fish_id>", "lock a fish to protect from bulk-sell (alias: lockfish)"},
            {"suggest <details...>", "propose a new fish species (alias: suggestfish)"},
            {"crew <action> [args...]", "manage your fishing crew"},
        };
        c->examples = {".fish cast", ".f", ".fish inv", ".fish sell all", ".fish equip gold rod"};
        c->notes = "casting has a cooldown that decreases with skill tree bonuses. rare and legendary fish are harder to catch.";
    });

    set("fishdex", [](Command* c) {
        c->extended_description =
            "view your fish collection, optionally filtered by a category. "
            "the last argument is parsed as a page number if it's numeric.";
        c->detailed_usage = ".fishdex [category] [page]";
        c->examples = {".fishdex", ".fishdex freshwater", ".fishdex rare 2"};
    });

    set("lockfish", [](Command* c) {
        c->extended_description =
            "lock a specific fish by ID to protect it from bulk-sell, or set up "
            "auto-lock rules so newly caught fish matching certain criteria are "
            "automatically locked.";
        c->detailed_usage = ".lockfish <fish_id | auto [criteria...]>";
        c->subcommands = {
            {"<fish_id>",                      "toggle lock on a specific fish"},
            {"auto",                           "show your current auto-lock rules"},
            {"auto value <min_value>",         "auto-lock any fish worth at least this much"},
            {"auto rarity <max_percent>",      "auto-lock fish with catch-rate at or below this %"},
        };
        c->examples = {".lockfish 47", ".lockfish auto value 5000", ".lockfish auto rarity 2"};
    });

    set("crew", [](Command* c) {
        c->extended_description =
            "manage your fishing crew — a group of players who share crew bonuses "
            "and can participate in crew leaderboards.";
        c->detailed_usage = ".crew <action> [args...]";
        c->subcommands = {
            {"create <name>",      "create a new crew (costs money)"},
            {"invite <@user>",     "invite a player to your crew"},
            {"join <crew_name>",   "accept a pending crew invitation"},
            {"leave",              "leave your current crew"},
            {"info [crew_name]",   "view crew details and member list"},
            {"kick <@user>",       "kick a member from your crew (captain only)"},
            {"open",               "make the crew joinable without invite"},
            {"close",              "restrict the crew to invite-only"},
            {"disband",            "permanently disband the crew (captain only)"},
            {"leaderboard",        "view the crew leaderboard"},
        };
        c->examples = {".crew create \"deep sea anglers\"", ".crew invite @User", ".crew info"};
    });

    set("autofisher", [](Command* c) {
        c->extended_description =
            "manage an automated fishing process that casts and catches fish for you "
            "while you're away. requires an autofisher item to activate.";
        c->detailed_usage = ".autofisher <action>";
        c->subcommands = {
            {"status", "view your autofisher's current state and catch summary"},
            {"start",  "activate the autofisher (must own the item)"},
            {"stop",   "stop the autofisher and keep uncollected fish"},
            {"sell",   "sell all fish caught by the autofisher"},
            {"config", "adjust autofisher settings (bait, rod, etc.)"},
        };
        c->examples = {".autofisher status", ".af start", ".af sell"};
    });

    set("sellfish", [](Command* c) {
        c->extended_description =
            "sell one or all unlocked fish from your inventory for cash. "
            "locked fish are never sold — use `.lockfish` to protect valuable catches.";
        c->detailed_usage = ".sellfish <fish_id | all>";
        c->examples = {".sellfish 12", ".sellfish all"};
    });

    set("equip", [](Command* c) {
        c->extended_description =
            "equip a fishing rod and/or bait by name, or pass `none` to unequip everything.";
        c->detailed_usage = ".equip [rod_name] [bait_name] | none";
        c->examples = {".equip gold rod", ".equip basic bait", ".equip none"};
    });

    set("inv", [](Command* c) {
        c->extended_description =
            "view your full inventory of rods, baits, titles, and other items. "
            "optionally filter by type to narrow results.";
        c->detailed_usage = ".inv [rod|bait|title]";
        c->examples = {".inv", ".inv rod", ".inv bait"};
    });

    set("suggestfish", [](Command* c) {
        c->extended_description =
            "propose a new fish species to be added to the game. all 10 fields are required. "
            "the description can contain spaces and will consume all remaining arguments.";
        c->detailed_usage = ".suggestfish <name> <emoji> <weight> <min_val> <max_val> <effect> <effect_%> <min_gear> <max_gear> <description...>";
        c->examples = {
            ".suggestfish \"ghost koi\" 👻 12.5 800 3500 xp_boost 15 3 7 a translucent koi that glows under moonlight"
        };
        c->notes = "effect can be: none, xp_boost, money_boost, luck, etc. effect_% is the proc chance (0-100).";
    });

    // ── mining ─────────────────────────────────────────────────────────

    set("mine", [](Command* c) {
        c->extended_description =
            "play the interactive mining minigame. a 3×3 grid appears with glowing ore cells. "
            "click revealed ores to collect them — the session ends when the timer expires or no more ores remain. "
            "multimine chance (from skill tree bonuses) can double ore per click. ore value scales with rarity tier and prestige bonuses. "
            "session ends when the timer expires or you run out of ores.";
        c->detailed_usage = ".mine";
        c->examples = {".mine"};
        c->notes = "higher prestige gives +5% ore value per level. home court (support server) gives +5%. supporter role gives +10%.";
    });

    set("minv", [](Command* c) {
        c->extended_description =
            "view your mining inventory: all collected ores, their values, and quantities. "
            "paginated with button navigation. use `.sellore` to convert ore to cash.";
        c->detailed_usage = ".minv";
        c->examples = {".minv", ".mi", ".mininginfo"};
        c->notes = "aliases: mi, mininginfo. ores are grouped by type.";
    });

    set("sellore", [](Command* c) {
        c->extended_description =
            "sell a mined ore by its inventory ID, or pass `all` to sell every ore at once.";
        c->detailed_usage = ".sellore <ore_id | all>";
        c->examples = {".sellore 5", ".sellore all"};
    });

    // ── gambling ───────────────────────────────────────────────────────

    set("blackjack", [](Command* c) {
        c->extended_description =
            "classic blackjack vs the dealer. hit, stand, double down, or split pairs "
            "to beat the dealer without going over 21. skill tree bonuses apply — "
            "payout bonus increases winnings, loss reduction softens busts, and luck "
            "gives a chance to avoid busting on a bad draw.";
        c->detailed_usage = ".blackjack <amount>";
        c->subcommands = {
            {"hit",         "draw another card"},
            {"stand",       "end your turn and let the dealer play"},
            {"double down", "double your bet and receive exactly one more card"},
            {"split",       "split two equal-value cards into separate hands (costs an extra bet)"},
        };
        c->examples = {".blackjack 1000", ".bj 5000", ".21 500", ".card all"};
        c->notes = "minimum bet is $100. blackjack pays 2.5x. push returns your bet.";
    });

    set("dice", [](Command* c) {
        c->extended_description =
            "roll two dice and win based on the result. "
            "snake eyes or boxcars (2 or 12) pay 4x, a lucky 7 or 11 pays 1.5x, "
            "any other doubles pay 2x. any other roll loses. "
            "skill tree bonuses apply — luck gives a chance to re-roll a losing result.";
        c->detailed_usage = ".dice <amount>";
        c->examples = {".dice 500", ".roll 2000"};
        c->notes = "minimum bet is $50. alias: `roll`.";
    });

    set("roulette", [](Command* c) {
        c->extended_description =
            "start a multiplayer roulette table. anyone can place bets using the buttons "
            "before the game author spins the wheel. "
            "bet on a color (red/black/green), parity (even/odd), or a single number. "
            "color and parity bets pay 2:1; single number and green pay 35:1.";
        c->detailed_usage = ".roulette";
        c->subcommands = {
            {"red / black", "bet on the landing color (2:1)"},
            {"green",       "bet on 0 or 00 (35:1)"},
            {"even / odd",  "bet on number parity — 0/00 do not count (2:1)"},
            {"number",      "bet on a specific number 0-36 or 00 (35:1)"},
            {"spin",        "author-only: close betting and spin the wheel"},
            {"cancel",      "author-only: cancel the game and refund all bets"},
        };
        c->examples = {".roulette", ".rlt"};
        c->notes = "only the player who started the game can spin or cancel.";
    });

    set("crash", [](Command* c) {
        c->extended_description =
            "a multiplier starts at 1.00x and climbs. cash out any time to lock in your "
            "winnings — but if it crashes before you do, you lose everything. "
            "the crash point is pre-determined at game start. "
            "about 4% of rounds crash instantly at 1.00x (house edge).";
        c->detailed_usage = ".crash <amount>";
        c->examples = {".crash 1000", ".cr 5000"};
        c->notes = "use the cash out button before the multiplier crashes. alias: `cr`.";
    });

    set("jackpot", [](Command* c) {
        c->extended_description =
            "view the current progressive jackpot pool and recent winners. "
            "1% of all gambling losses across the server feed the pool. "
            "every gambling win has a 0.01% (1-in-10,000) chance to trigger the jackpot "
            "and claim the entire pool.";
        c->detailed_usage = ".jackpot";
        c->examples = {".jackpot", ".jp"};
        c->notes = "the jackpot is triggered automatically on any gambling win — no extra bet required. alias: `jp`.";
    });

    set("bomb", [](Command* c) {
        c->extended_description =
            "play minesweeper — pick safe cells to multiply your bet. "
            "choose a difficulty (or exact mine count) and optionally set a custom grid size.";
        c->detailed_usage = ".bomb <difficulty|mines> <bet> [columns] [rows]";
        c->subcommands = {
            {"easy",       "few mines, low multipliers"},
            {"medium",     "balanced risk and reward"},
            {"hard",       "many mines, high multipliers"},
            {"impossible", "nearly every cell is a mine"},
            {"<number>",   "set an exact number of mines"},
        };
        c->examples = {".bomb easy 1000", ".bomb hard 5000 5 5", ".bomb 3 2000"};
        c->notes = "default grid is 3×3. max grid is 5×5.";
    });

    set("frogger", [](Command* c) {
        c->extended_description =
            "play frogger — hop across lanes of traffic. harder difficulties have "
            "faster cars and bigger payouts.";
        c->detailed_usage = ".frogger <easy|medium|hard> <bet>";
        c->examples = {".frogger easy 500", ".frogger hard 5000"};
    });

    set("poker", [](Command* c) {
        c->extended_description =
            "play Texas Hold'em poker against other players and/or CPU opponents. "
            "one player starts the table, others join before the hand begins.";
        c->detailed_usage = ".poker <action> [args...]";
        c->subcommands = {
            {"start [min_bet] [cpus]", "create a new poker table (default min bet: 100)"},
            {"join",                   "join an active table before the hand starts"},
        };
        c->examples = {".poker start 500", ".poker start 1000 2", ".poker join"};
    });

    set("lottery", [](Command* c) {
        c->extended_description =
            "buy lottery tickets for a chance at the jackpot pool, "
            "or check the current pool / drawing info.";
        c->detailed_usage = ".lottery [amount | info | pool | status]";
        c->subcommands = {
            {"<amount>",        "purchase that many lottery tickets"},
            {"info / pool / status", "view current pool size and next drawing time"},
        };
        c->examples = {".lottery 5", ".lottery info"};
    });

    set("coinflip", [](Command* c) {
        c->extended_description =
            "flip a coin! bet on heads or tails — defaults to heads if not specified.";
        c->detailed_usage = ".coinflip <amount> [heads|tails]";
        c->examples = {".cf 1000", ".cf 5000 tails"};
    });

    set("slots", [](Command* c) {
        c->extended_description =
            "spin the slot machine. matching symbols multiply your bet — "
            "three 7s triggers the progressive jackpot.";
        c->detailed_usage = ".slots <amount>";
        c->examples = {".slots 500", ".slot 10000"};
        c->notes = "jackpot pool grows with every spin. use `.jackpot` to view it.";
    });

    // ── games ──────────────────────────────────────────────────────────

    set("heist", [](Command* c) {
        c->extended_description =
            "launch or join a cooperative vault heist. 3-8 players work together to crack "
            "the vault — higher difficulty means bigger rewards but harder skill checks.";
        c->detailed_usage = ".heist <action> [args...]";
        c->subcommands = {
            {"start [entry_fee]",   "create a heist lobby (default fee: free)"},
            {"start [difficulty]",  "set difficulty 1-5 when creating"},
            {"join",                "join an active heist lobby"},
            {"status",              "check the current heist's progress"},
        };
        c->examples = {".heist start 1000", ".heist join", ".heist status"};
    });

    set("raid", [](Command* c) {
        c->extended_description =
            "start or check a cooperative boss raid. players pool entry fees and "
            "fight a shared boss — loot is split among surviving raiders.";
        c->detailed_usage = ".raid <action> [entry_fee]";
        c->subcommands = {
            {"start [entry_fee]", "create a raid lobby"},
            {"status",            "view the current raid's boss HP and participants"},
        };
        c->examples = {".raid start 5000", ".raid status"};
    });

    set("react", [](Command* c) {
        c->extended_description =
            "challenge another player (or open to anyone) to a reaction-time duel. "
            "both players wait for a signal then race to click — loser pays the bet.";
        c->detailed_usage = ".react <@user | open> [bet]";
        c->examples = {".react @User 1000", ".react open 500"};
    });

    set("tictactoe", [](Command* c) {
        c->extended_description =
            "challenge someone to classic tic-tac-toe, optionally with a cash bet on the line.";
        c->detailed_usage = ".tictactoe <@user> [bet]";
        c->examples = {".ttt @User", ".ttt @User 2000"};
    });

    // ── passive ────────────────────────────────────────────────────────

    set("claim", [](Command* c) {
        c->extended_description =
            "manage mining claims that passively generate ore over time. "
            "buy claims for specific ore types, collect accumulated resources, "
            "upgrade output, or abandon claims you no longer want.";
        c->detailed_usage = ".claim <action> [args...]";
        c->subcommands = {
            {"buy <ore_type>",   "purchase a new mining claim for that ore"},
            {"list",             "view all your active claims and their output"},
            {"collect",          "collect accumulated ore from all claims"},
            {"upgrade [claim]",  "upgrade a claim to increase yield"},
            {"abandon [claim]",  "permanently remove a claim"},
        };
        c->examples = {".claim buy iron", ".claim collect", ".claim list"};
    });

    set("pond", [](Command* c) {
        c->extended_description =
            "manage a personal fish pond that generates passive income over time. "
            "build it, stock it with fish, then collect earnings periodically.";
        c->detailed_usage = ".pond <action> [args...]";
        c->subcommands = {
            {"build / create",    "construct your fish pond (one-time cost)"},
            {"stock / add",       "add fish from your inventory to the pond"},
            {"collect",           "collect accumulated earnings"},
            {"upgrade",           "increase pond capacity and income rate"},
            {"view",              "see your pond's status and stocked fish"},
        };
        c->examples = {".pond build", ".pond stock", ".pond collect", ".pond view"};
    });

    set("interest", [](Command* c) {
        c->extended_description =
            "claim your daily interest on your bank balance, or check the current interest rate.";
        c->detailed_usage = ".interest [claim | rate | info]";
        c->subcommands = {
            {"claim",       "collect your daily interest payment (default action)"},
            {"rate / info", "display the current interest rate and how it's calculated"},
        };
        c->examples = {".interest", ".interest rate"};
    });

    set("cmarket", [](Command* c) {
        c->extended_description =
            "view real-time commodity market prices that fluctuate over time, "
            "or sell items at the current market rate for a profit.";
        c->detailed_usage = ".cmarket [sell <item_name>]";
        c->subcommands = {
            {"(no args)",        "display the current price sheet for all commodities"},
            {"sell <item_name>", "sell an item from your inventory at the current market price"},
        };
        c->examples = {".cmarket", ".cmarket sell iron ore"};
    });

    // ── leveling ───────────────────────────────────────────────────────

    set("levelconfig", [](Command* c) {
        c->extended_description =
            "view or update server-wide leveling settings. run with no arguments to see "
            "all current values; pass a setting name and value to change it.";
        c->detailed_usage = ".levelconfig [setting] [value]";
        c->examples = {".levelconfig", ".levelconfig xp_rate 1.5", ".levelconfig announce_channel #levels"};
    });

    set("levelroles", [](Command* c) {
        c->extended_description =
            "manage roles automatically granted when members reach certain levels. "
            "with no arguments, lists all configured level-role rewards.";
        c->detailed_usage = ".levelroles [action] [args...]";
        c->subcommands = {
            {"(no args)",                             "list all level-role rewards"},
            {"add <level> <@role> [remove_previous]", "assign a role reward at a level; `remove_previous` auto-removes the prior role"},
            {"remove <level>",                        "delete the role reward at a level"},
        };
        c->examples = {".levelroles", ".levelroles add 10 @Veteran", ".levelroles add 20 @Elite remove_previous", ".levelroles remove 10"};
    });

    set("xpblacklist", [](Command* c) {
        c->extended_description =
            "block specific channels, roles, or users from earning XP. "
            "useful for muting bot-spam channels or restricting leveling.";
        c->detailed_usage = ".xpblacklist <channel|role|user> <add|remove|list> [target]";
        c->subcommands = {
            {"channel add <#channel>",   "stop XP from being earned in this channel"},
            {"channel remove <#channel>","re-enable XP for this channel"},
            {"channel list",             "show all blacklisted channels"},
            {"role add <@role>",         "members with this role earn no XP"},
            {"role remove <@role>",      "un-blacklist this role"},
            {"role list",                "show all blacklisted roles"},
            {"user add <@user>",         "block a specific user from earning XP"},
            {"user remove <@user>",      "un-blacklist a user"},
            {"user list",                "show all blacklisted users"},
        };
        c->examples = {".xpblacklist channel add #bot-spam", ".xpbl role add @Muted", ".xpbl user list"};
    });

    // ── cosmetics ──────────────────────────────────────────────────────

    set("title", [](Command* c) {
        c->extended_description =
            "view your unlocked titles, equip one to display next to your name, or remove "
            "your current title. with no arguments, lists all titles you own.";
        c->detailed_usage = ".title [action] [item_id]";
        c->subcommands = {
            {"(no args)",                "list all your unlocked titles"},
            {"equip / set / use <id>",   "equip a title by its item ID"},
            {"remove / clear / unequip", "unequip your current title"},
        };
        c->examples = {".title", ".title equip 14", ".title remove"};
    });

    // ── utility ───────────────────────────────────────────────────────

    set("ping", [](Command* c) {
        c->extended_description =
            "check bot latency. shows websocket ping (discord gateway) and round-trip time "
            "(how long it took to send and receive a message). "
            "bot owners also see uptime and shard info.";
        c->detailed_usage = ".ping";
        c->examples = {".ping", ".p", ".ms", ".latency"};
        c->notes = "aliases: p, ms, pong, latency.";
    });

    set("userinfo", [](Command* c) {
        c->extended_description =
            "display detailed information about a user: username, display name, nickname, "
            "discord account creation date, server join date, and avatar links. "
            "defaults to yourself if no user is specified.";
        c->detailed_usage = ".userinfo [@user | user_id]";
        c->examples = {".userinfo", ".ui @User", ".whois 123456789"};
        c->notes = "aliases: ui, whois, uinfo, u.";
    });

    set("serverinfo", [](Command* c) {
        c->extended_description =
            "display information about the current server: name, id, owner, creation date, "
            "member count, role count, channel count, boost level, verification level, "
            "vanity url, and shard info. shows custom bio/banner if the server has a profile set up.";
        c->detailed_usage = ".serverinfo";
        c->examples = {".serverinfo", ".si", ".guildinfo"};
        c->notes = "aliases: si, guildinfo, sinfo.";
    });

    set("poll", [](Command* c) {
        c->extended_description =
            "create a poll with reaction-based voting. supports up to 9 options. "
            "users react with the numbered emoji to cast their vote.";
        c->detailed_usage = ".poll \"question\" \"option1\" \"option2\" [more options...]";
        c->examples = {
            ".poll \"best color?\" \"red\" \"blue\" \"green\"",
            ".poll \"movie night?\" \"action\" \"comedy\""
        };
        c->notes = "wrap the question and each option in quotes. maximum 9 options.";
    });

    set("cleanup", [](Command* c) {
        c->extended_description =
            "bulk delete messages in the current channel. optionally filter by user, "
            "bots only, or messages containing specific text. "
            "requires manage messages permission.";
        c->detailed_usage = ".cleanup <count> [@user] [--bots] [--contains <text>]";
        c->flags = {
            {"--bots",            "only delete messages sent by bots"},
            {"--contains <text>", "only delete messages containing this text"},
        };
        c->examples = {".cleanup 20", ".purge 50 @User", ".clear 10 --bots"};
        c->notes = "aliases: purge, clear. discord limits bulk delete to messages under 14 days old.";
    });

    set("suggestion", [](Command* c) {
        c->extended_description =
            "submit a feature suggestion. the suggestion is posted to the configured "
            "suggestions channel for the community to vote on with reactions.";
        c->detailed_usage = ".suggestion <text...>";
        c->examples = {".suggestion add a fishing tournament mode"};
        c->notes = "the suggestions channel must be configured by an admin.";
    });

    set("bugreport", [](Command* c) {
        c->extended_description =
            "report a bot bug. the report is posted to the configured bug-report channel. "
            "include as much detail as possible: what you did, what happened, what you expected.";
        c->detailed_usage = ".bugreport <text...>";
        c->examples = {".bugreport .daily returned an error but still used the cooldown"};
        c->notes = "aliases: bug. the bug-report channel must be configured by an admin.";
    });

    set("giveaway", [](Command* c) {
        c->extended_description =
            "create and manage giveaways. users react to enter. "
            "when the timer ends a winner is drawn automatically. "
            "you can also end a giveaway early or reroll a new winner.";
        c->detailed_usage = ".giveaway <start|end|reroll> [args...]";
        c->subcommands = {
            {"start <duration> <prize...>", "start a giveaway (e.g. 1h, 30m, 7d)"},
            {"end <message_id>",            "end a giveaway early and draw a winner"},
            {"reroll <message_id>",         "reroll a new winner for a finished giveaway"},
        };
        c->examples = {
            ".giveaway start 1h $50,000 cash",
            ".giveaway end 123456789",
            ".giveaway reroll 123456789"
        };
    });

    set("snipe", [](Command* c) {
        c->extended_description =
            "view the last deleted message in the current channel. "
            "only shows messages deleted after the bot last restarted.";
        c->detailed_usage = ".snipe";
        c->examples = {".snipe"};
    });

    set("avatar", [](Command* c) {
        c->extended_description =
            "view a user's avatar in full size. shows both the global avatar and the "
            "server-specific avatar if one exists. defaults to yourself if no user is specified.";
        c->detailed_usage = ".avatar [@user | user_id]";
        c->examples = {".avatar", ".avatar @User", ".av 123456789"};
        c->notes = "aliases: av, pfp, icon.";
    });

    set("invite", [](Command* c) {
        c->extended_description =
            "get the bot's invite link to add it to another server.";
        c->detailed_usage = ".invite";
        c->examples = {".invite"};
    });

    set("settings", [](Command* c) {
        c->extended_description =
            "view or update your personal bot settings: dm notifications, privacy options, "
            "and other per-user preferences. run with no arguments to see all current values.";
        c->detailed_usage = ".settings [key] [value]";
        c->examples = {".settings", ".settings dm_notifications off", ".settings privacy public"};
    });

    // ── utility / permissions ──────────────────────────────────────────

    set("module", [](Command* c) {
        c->extended_description =
            "enable or disable an entire command module for this server. "
            "scope flags let you restrict the change to specific users, roles, or channels.";
        c->detailed_usage = ".module <name> <enable|disable> [flags...]";
        c->flags = {
            {"-c <#channel>", "apply the rule only in this channel"},
            {"-u <@user>",    "apply the rule only to this user"},
            {"-r <@role>",    "apply the rule only to members with this role"},
            {"-e",            "exclusive mode — the rule applies ONLY to the specified target, everyone else is unaffected"},
        };
        c->examples = {".module gambling disable", ".module fishing disable -c #general", ".module economy enable -r @VIP -e"};
    });

    set("command", [](Command* c) {
        c->extended_description =
            "enable or disable a single command for this server. "
            "scope flags let you restrict the change to specific users, roles, or channels.";
        c->detailed_usage = ".command <name> <enable|disable> [flags...]";
        c->flags = {
            {"-c <#channel>", "apply the rule only in this channel"},
            {"-u <@user>",    "apply the rule only to this user"},
            {"-r <@role>",    "apply the rule only to members with this role"},
            {"-e",            "exclusive mode — the rule applies ONLY to the specified target"},
        };
        c->examples = {".command slots disable", ".command rob disable -c #marketplace"};
    });

    set("reactionrole", [](Command* c) {
        c->extended_description =
            "set up reaction roles so users can self-assign roles by reacting to a message. "
            "requires Manage Roles permission.";
        c->detailed_usage = ".reactionrole <^|msg_id|msg_link> <emoji> <@role> [emoji @role ...]";
        c->subcommands = {
            {"<target> <emoji> <@role> [...]", "add reaction role(s) to a message — use `^` for the message above"},
            {"add / create",                   "alias for the default add behavior"},
            {"check <msg_id | msg_link | ^>",  "inspect reaction roles on an existing message"},
            {"silent",                         "toggle silent mode (no DM confirmation to users)"},
        };
        c->examples = {
            ".rr ^ 🎮 @Gamer 🎵 @Music",
            ".reactionrole 123456789 ✅ @Verified",
            ".rr check ^"
        };
        c->notes = "use `^` to target the message directly above the command.";
    });

    // ── moderation ─────────────────────────────────────────────────────

    set("mod", [](Command* c) {
        c->extended_description =
            "consolidated moderation command. subcommands handle: banning/kicking/muting/jailing users, "
            "issuing warnings, viewing/managing infractions, and configuring moderation settings. "
            "requires moderator role or admin permissions.";
        c->detailed_usage = ".mod <action> [args...]";
        c->subcommands = {
            {"ban <@user> [duration] [reason...]", "ban a user (duration: 7d, 30d, permanent, etc.)"},
            {"unban <@user>", "remove a ban"},
            {"kick <@user> [reason...]", "kick a user from the server"},
            {"timeout <@user> <duration> [reason...]", "apply discord timeout (max 28 days)"},
            {"untimeout <@user>", "remove timeout"},
            {"mute <@user> [duration] [reason...]", "assign mute role (custom, server-configured)"},
            {"unmute <@user>", "remove mute role"},
            {"warn <@user> [reason...]", "issue a warning (tracked in history)"},
            {"jail <@user> [duration]", "assign jail role (custom, server-configured)"},
            {"unjail <@user>", "remove jail role"},
            {"case <case_id>", "view details of a specific moderation case"},
            {"history <@user>", "view all infractions for a user"},
            {"modstats [@user]", "view mod action statistics (server or per-mod)"},
            {"pardon <case_id>", "remove an infraction from a user's record"},
            {"reason <case_id> <text...>", "add or update the reason for a case"},
            {"muterole <@role>", "set the role assigned when muting users"},
            {"jailsetup", "configure the jail role and channel"},
            {"logconfig [#channel]", "set the moderation log channel"},
        };
        c->examples = {".mod ban @User 7d spamming", ".mod warn @User", ".mod history @User", ".mod case 142"};
        c->notes = "all actions are logged and auditable. expired timeouts are automatically removed.";
    });

    // ── owner ──────────────────────────────────────────────────────────

    set("givemoney", [](Command* c) {
        c->extended_description =
            "owner-only: add or remove money from one or more users' wallets. "
            "prefix with `add` or `remove`, or use a negative amount.";
        c->detailed_usage = ".givemoney [add|remove] <@user(s)|all|everyone> <amount>";
        c->examples = {".givemoney @User 5000", ".givemoney remove @User 1000", ".givemoney all 500"};
    });

    set("giveitem", [](Command* c) {
        c->extended_description =
            "owner-only: add or remove items from one or more users' inventories.";
        c->detailed_usage = ".giveitem [add|remove] <@user(s)|all|everyone> <item_id> <quantity>";
        c->examples = {".giveitem @User 14 3", ".giveitem remove @User 14 1"};
    });

    set("bac", [](Command* c) {
        c->extended_description =
            "owner-only: manage the global BAC (Bronx AntiCheat) ban list. "
            "banned users cannot use any bot commands.";
        c->detailed_usage = ".bac <add|remove|list> [flags...]";
        c->flags = {
            {"-u <@user>",    "target user to ban or unban"},
            {"-r <reason...>", "reason for the ban (shown in audit)"},
        };
        c->examples = {".bac add -u @User -r macro abuse", ".bac remove -u @User", ".bac list"};
    });

    set("whitelist", [](Command* c) {
        c->extended_description =
            "owner-only: manage the global command whitelist. whitelisted users bypass "
            "all anti-cheat checks and cooldowns.";
        c->detailed_usage = ".whitelist <add|remove|list> [flags...]";
        c->flags = {
            {"-u <@user>",    "target user"},
            {"-r <reason...>", "reason for whitelisting"},
        };
        c->examples = {".whitelist add -u @Trusted -r beta tester", ".whitelist list"};
    });

    // ── pets ───────────────────────────────────────────────────────────

<<<<<<< HEAD
    set("pet", [](Command* c) {
        c->extended_description =
            "manage your pets. pets provide passive percentage bonuses to fishing, gambling, mining, "
            "and other activities while equipped. hunger decays by 1 per hour (max 100); "
            "a starving pet gives no bonus. you can own up to 5 pets at once. "
            "rarity tiers: common, uncommon, rare, epic, legendary, prestige.";
        c->detailed_usage = ".pets <action> [args...]";
        c->subcommands = {
            {"shop [page]",              "browse adoptable pets and their bonuses"},
            {"adopt <species>",          "adopt a pet (costs money, one per species)"},
            {"list",                     "view all your pets, hunger, and equipped status"},
            {"equip <name>",             "equip a pet to activate its bonus"},
            {"feed <name>",              "feed your pet to restore hunger to 100%"},
            {"rename <name> <new_name>", "rename a pet (max 20 characters)"},
            {"release <name>",           "release a pet permanently (requires `confirm`)"},
        };
        c->examples = {
            ".pets shop",
            ".pets adopt cat",
            ".pets feed luna",
            ".pets equip luna",
            ".pets rename luna mooncat",
            ".pets release luna confirm"
        };
        c->notes =
            "aliases: pets. prestige rarity pets require prestige 5+. "
            "feeding costs 1% of your net worth ($1k minimum, $5m maximum).";
    });

    // ── skill_tree ─────────────────────────────────────────────────────

    set("skills", [](Command* c) {
        c->extended_description =
            "view and manage your skill tree. skills provide permanent percentage bonuses to "
            "fishing, mining, and gambling via three branches: angler, prospector, and gambler. "
            "skill points are earned by prestiging — each prestige grants additional points. "
            "requires prestige 1 or higher to unlock.";
        c->detailed_usage = ".skills [branch | invest <skill_name> | respec]";
        c->subcommands = {
            {"(no args)",           "overview of all three branches and your invested points"},
            {"angler",              "view the angler branch (fishing bonuses)"},
            {"prospector",          "view the prospector branch (mining bonuses)"},
            {"gambler",             "view the gambler branch (gambling bonuses)"},
            {"invest <skill_name>", "spend 1 skill point to upgrade a skill"},
            {"respec [confirm]",    "reset all invested points (costs 10% of net worth)"},
        };
        c->examples = {
            ".skills",
            ".skills angler",
            ".skills invest fishing xp bonus",
            ".skills respec confirm"
        };
        c->notes =
            "aliases: skill, skilltree, tree. "
            "skills within a branch require points in lower tiers before unlocking higher tiers.";
    });

    // ── mastery ────────────────────────────────────────────────────────

    set("mastery", [](Command* c) {
        c->extended_description =
            "view your fish and ore mastery progress. mastery is earned by repeatedly catching "
            "the same fish species or mining the same ore type. "
            "each mastery tier grants a permanent sell-value bonus for that species or ore. "
            "tiers: novice, apprentice, journeyman, expert, master, grandmaster.";
        c->detailed_usage = ".mastery [fish [species] | ore [type]]";
        c->subcommands = {
            {"(no args)",       "overview of fish and ore mastery totals"},
            {"fish [species]",  "view fish mastery; filter by species name for detailed progress"},
            {"ore [type]",      "view ore mastery; filter by ore type for detailed progress"},
        };
        c->examples = {
            ".mastery",
            ".mastery fish",
            ".mastery ore",
            ".mastery ore iron"
        };
        c->notes =
            "higher mastery tiers increase the sell value of that specific species or ore. "
            "mastery is cumulative and never resets on prestige or rebirth.";
=======
    set("pets", [](Command* c) {
        c->extended_description =
            "manage your pets — adoptable creatures that provide passive bonuses to fishing, gambling, mining, "
            "and general earning. pets have hunger (decays 1/hour), levels, xp, and rarity tiers. "
            "starving pets don't grant bonuses. rarity tiers: common → uncommon → rare → epic → legendary → prestige.";
        c->detailed_usage = ".pets <action> [args...]";
        c->subcommands = {
            {"shop [page]", "browse adoptable pet species with bonuses"},
            {"adopt <species>", "adopt a pet (costs money, rarity affects price)"},
            {"list", "view all your pets, their levels, and hunger"},
            {"info <name|id>", "detailed stats for a specific pet (hunger, level, xp, bonus type)"},
            {"feed <name|id>", "feed your pet to restore hunger to 100"},
            {"rename <name|id> <new_name>", "give your pet a nickname"},
            {"release <name|id>", "permanently remove a pet"},
        };
        c->examples = {".pets shop", ".pets adopt cat", ".pets feed luna", ".pets info luna"};
        c->notes = "each pet species grants a unique bonus. pets are equipped via `.equip`. pets gain xp when their bonus is active.";
    });

    // ── skill tree ──────────────────────────────────────────────────────

    set("skills", [](Command* c) {
        c->extended_description =
            "view and upgrade your skill tree. skills provide permanent percentage bonuses to fishing, gambling, mining, "
            "work, and general earning. skill points are earned by leveling up. upgrade costs increase with rank.";
        c->detailed_usage = ".skills <action>";
        c->subcommands = {
            {"view", "see all available skills and your current levels"},
            {"upgrade <skill_name>", "spend skill points to increase a skill's rank"},
            {"info <skill_name>", "detailed scaling info for a specific skill"},
        };
        c->examples = {".skills view", ".skills upgrade fishing_xp_bonus", ".skills info gambling_payout_bonus"};
        c->notes = "aliases: skilltree, skill. skill points cap at 100. reset your tree with `.reset-skills` (costs money).";
    });

    // ── mastery ─────────────────────────────────────────────────────────

    set("mastery", [](Command* c) {
        c->extended_description =
            "view your fish and ore mastery progress. mastery is earned by repeatedly catching the same fish species "
            "or mining the same ore type. mastery tiers: common → uncommon → rare → epic → legendary → prestige. "
            "higher mastery tiers grant permanent value bonuses for that species/ore.";
        c->detailed_usage = ".mastery <fish|ore> [species|type]";
        c->subcommands = {
            {"fish [species]", "view fish mastery progress (species optional)"},
            {"ore [type]", "view ore mastery progress (type optional)"},
        };
        c->examples = {".mastery fish", ".mastery fish salmon", ".mastery ore iron"};
        c->notes = "mastery tiers unlock exclusive cosmetics and titles. catch/mine 1000+ of a species/ore to reach prestige.";
>>>>>>> origin/main
    });

    // ── leaderboard ────────────────────────────────────────────────────

    set("leaderboard", [](Command* c) {
        c->extended_description =
            "view ranked leaderboards by category — defaults to net worth. "
            "add `global` to see cross-server standings.";
        c->detailed_usage = ".leaderboard [category] [global]";
        c->examples = {".lb", ".lb fish", ".lb xp global", ".top mining"};
    });
}

} // namespace commands
