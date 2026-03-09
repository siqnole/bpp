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

    set("sellore", [](Command* c) {
        c->extended_description =
            "sell a mined ore by its inventory ID, or pass `all` to sell every ore at once.";
        c->detailed_usage = ".sellore <ore_id | all>";
        c->examples = {".sellore 5", ".sellore all"};
    });

    // ── gambling ───────────────────────────────────────────────────────

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
