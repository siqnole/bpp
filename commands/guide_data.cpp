#include "guide_data.h"

namespace commands {
namespace guide {

std::vector<GuideSection> get_guide_sections() {
    return {

    // ── getting started ────────────────────────────────────────────────
    {
        "getting started", "📖", "first time? start here", {
        {
            "day 1 quick start", "🚀",
            "**getting started — day 1 quick start**\n\n"
            "welcome! here's everything you need to do in your first 5 minutes:\n\n"
            "**step 1: get your starter cash**\n"
            "> type `.daily` — this gives you coins based on your net worth (min $500)\n\n"
            "**step 2: bank your money immediately**\n"
            "> type `.dep all` — this protects your money from robbery\n\n"
            "**step 3: start fishing**\n"
            "> type `.equip wooden rod` then `.equip common bait`\n"
            "> type `.fish` — catch some fish!\n"
            "> type `.sell` — sell your fish for cash\n\n"
            "**step 4: check daily challenges**\n"
            "> type `.challenges` — complete these for bonus rewards\n"
            "> keep a streak going for multiplied payouts\n\n"
            "**step 5: upgrade your gear**\n"
            "> type `.shop` — browse fishing rods, bait, and tools\n"
            "> type `.buy <item>` — better gear = more money\n\n"
            "**the loop:** daily → fish → sell → bank → challenges → upgrade → repeat\n\n"
            "**next steps:**\n"
            "> • try `.mine` for another income source\n"
            "> • try `.work` every 30 minutes for free money\n"
            "> • read the full guide below when you're ready"
        },
        {
            "overview", "🌐",
            "**getting started — overview**\n\n"
            "bronx is an economy-driven discord bot built around fishing, mining, gambling, "
            "crafting, pets, skill trees, and a full progression system that rewards long-term "
            "play — everything runs on a single shared currency and every system feeds into the "
            "others, so the more you explore the faster you grow.\n\n"
            "**quick start:**\n"
            "> use `.daily` to collect your first coins, `.fish` to start catching fish, "
            "`.mine` to dig for ore, and `.sell` / `.sellore` to convert what you find into "
            "cash — deposit into your `.bank` early because interest accrues daily and rob "
            "protection only covers banked funds.\n\n"
            "**core loop:**\n"
            "> earn → bank → upgrade gear → earn faster → prestige → unlock multipliers → repeat\n\n"
            "**tips for day one:**\n"
            "> • check `.challenges` every day for bonus payouts and streak rewards\n"
            "> • equip your starter rod and bait before fishing (`.equip`)\n"
            "> • type `.guide economy` or pick a topic below to learn any system in depth\n"
            "> • join the support server (`.invite`) if you get stuck"
        },
        {
            "prefix & commands", "⌨️",
            "**getting started — prefix & commands**\n\n"
            "the default prefix is `b.` but server admins can add custom prefixes with "
            "`.prefix add <prefix>` and you can set a personal prefix with `.userprefix add <prefix>` "
            "— the bot also supports slash commands for most features so you can type `/` and "
            "browse from there.\n\n"
            "**finding commands:**\n"
            "> `.help` — dropdown menu with every command sorted by category\n"
            "> `.help <command>` — detailed info for one specific command including subcommands, flags, and examples\n"
            "> `.help <category>` — list every command in a module (economy, fishing, mining, etc.)\n"
            "> `.guide` — you're here, this covers *how* things work rather than just what commands exist\n\n"
            "**good to know:**\n"
            "> • most commands have shorter aliases — `.bal` instead of `.balance`, `.cf` instead of `.coinflip`\n"
            "> • commands are case-insensitive\n"
            "> • the bot responds to both text commands and slash commands"
        }}
    },

    // ── economy ────────────────────────────────────────────────────────
    {
        "economy", "💰", "wallet, bank, earning, spending, and progression", {
        {
            "earning money", "🪙",
            "**economy — earning money**\n\n"
            "there are several ways to bring in cash and the best strategy is to rotate between "
            "all of them because daily challenges reward variety.\n\n"
            "**regular income:**\n"
            "> `.daily` — 8% of your net worth (min $500, max $250M), 24h cooldown\n"
            "> `.weekly` — 50% of your net worth (min $1K, max $1B), 7-day cooldown\n"
            "> `.work` — 3% of your net worth (min $100, max $25M), 30min cooldown\n\n"
            "**activity income:**\n"
            "> `.fish` then `.sell` — catch fish and sell them, value scales with rod/bait tier and fish rarity\n"
            "> `.mine` then `.sellore` — mine ore on a 3×3 grid and sell what you collect\n"
            "> gambling — `.slots`, `.coinflip`, `.crash`, `.blackjack`, `.poker`, `.roulette`, etc.\n\n"
            "**passive income:**\n"
            "> `.pond` — stock fish in a pond that generates coins every 6 hours\n"
            "> `.claim` — buy mining claims that produce ore every 4 hours\n"
            "> `.interest` — earn 0.1–0.5% daily interest on your bank balance (scales with prestige)\n"
            "> `.market` — buy low, sell high on a fluctuating commodity market\n\n"
            "**tips:**\n"
            "> • always bank your money — wallet funds can be robbed, bank funds cannot\n"
            "> • work has the shortest cooldown so spam it between fishing and mining sessions\n"
            "> • daily and weekly scale with net worth so the richer you get the more they pay"
        },
        {
            "wallet & bank", "🏦",
            "**economy — wallet & bank**\n\n"
            "your money exists in two places — your wallet (vulnerable to robbery) and your bank "
            "(safe but has a storage limit) — rob protection and smart banking are the core of "
            "staying wealthy.\n\n"
            "**bank basics:**\n"
            "> `.bank` — opens an interactive menu with deposit, withdraw, upgrade, and loan options\n"
            "> `.dep <amount|max>` — quick deposit shortcut\n"
            "> `.withdraw <amount|max>` — quick withdraw shortcut\n"
            "> your bank has a capacity limit — every $1 you spend on upgrades adds $2 of storage space\n"
            "> preset upgrades are available at +5K, +10K, +25K, +50K, +100K\n\n"
            "**loans:**\n"
            "> you can take out a loan through `.bank` → loan panel — 10% interest is added immediately "
            "so only borrow if you have a plan to make it back quickly\n\n"
            "**interest:**\n"
            "> `.interest` — collect daily interest on your bank balance\n"
            "> base rate starts at 0.1%, increases by 0.05% per prestige level, caps at 0.5% (prestige 8+)\n"
            "> max payout per collection is $500K so it rewards consistent collecting over hoarding\n\n"
            "**robbery:**\n"
            "> `.rob @user` — 2h cooldown, 30–60% success chance based on wallet comparison\n"
            "> success: steal 15–50% of their wallet — failure: lose 15–30% of yours\n"
            "> 10% chance of police catch on top of failure = lose 50–75%\n"
            "> `.passive` toggles passive mode which makes you immune to robbery but also prevents you from robbing others"
        },
        {
            "prestige & rebirth", "⭐",
            "**economy — prestige & rebirth**\n\n"
            "prestige is the main long-term progression system — you sacrifice everything to earn permanent "
            "multipliers that make every future playthrough faster and more rewarding.\n\n"
            "**prestige:**\n"
            "> `.prestige` — preview requirements, `.prestige confirm` — execute\n"
            "> resets: wallet, bank, inventory, fish, bait, rods, potions, autofisher\n"
            "> keeps: titles, gambling stats, fish caught stats, mastery progress\n"
            "> reward: permanent +5% fishing value bonus per prestige level, access to prestige gear in the shop, "
            "higher bank interest rates, more skill tree points\n\n"
            "**prestige 1 requirements:**\n"
            "> $500M net worth + 1,000 common / 200 rare / 50 epic / 10 legendary fish caught\n"
            "> each subsequent prestige roughly doubles the net worth requirement and scales fish needs\n\n"
            "**rebirth (post-P20):**\n"
            "> `.rebirth` — available at prestige 20, resets your prestige back to 0\n"
            "> grants a permanent ×1.1 global multiplier on all earnings that stacks multiplicatively\n"
            "> max 5 rebirths = ×1.61 permanent bonus to everything\n"
            "> rebirth requirements escalate: R1 needs P20 + $50B, R5 needs $1T + 100K fish + 50K ores + 25K gambles\n"
            "> rebirth titles: reborn → twice reborn → thrice reborn → ascended → transcendent\n\n"
            "**strategy:**\n"
            "> don't rush prestige — farm until the requirements feel easy, then prestige and the multipliers "
            "will make your next run significantly faster"
        }}
    },

    // ── fishing ────────────────────────────────────────────────────────
    {
        "fishing", "🎣", "rods, bait, fishdex, crews, and autofisher", {
        {
            "basics", "🐟",
            "**fishing — basics**\n\n"
            "fishing is the primary moneymaker in the early game — you cast with `.fish`, "
            "click the \"reel in\" button when it appears, and the fish you catch depends on "
            "your rod, bait, luck, and any active boosts.\n\n"
            "**core loop:**\n"
            "> `.fish` → click reel in → fish lands in your inventory → `.sell` or `.sellfish` to convert to cash\n"
            "> 30-second cooldown between casts\n\n"
            "**gear tiers:**\n"
            "> rods: wood → bronze → silver → gold → diamond → prestige (P1–P5)\n"
            "> bait: common → uncommon → rare → epic → legendary → prestige\n"
            "> better gear = better odds at rarer fish = more money per cast\n"
            "> equip gear with `.equip <item>`, check inventory with `.inv`\n\n"
            "**fish rarities:**\n"
            "> common (most frequent), uncommon, rare, epic, legendary (extremely rare)\n"
            "> each fish has a unique value formula — some use flat pricing, others scale with your "
            "stats, net worth, or prestige level (wealthy, banker, fisher, ascended types)\n"
            "> seasonal fish rotate through spring / summer / fall / winter — check `.fishdex` for availability\n\n"
            "**protecting your catches:**\n"
            "> `.lockfish <id>` — prevents a specific fish from being bulk-sold\n"
            "> `.lockfish auto value 5000` — auto-lock any fish worth $5K+\n"
            "> `.lockfish auto rarity 2` — auto-lock fish with ≤2% catch rate"
        },
        {
            "fishdex & mastery", "📚",
            "**fishing — fishdex & mastery**\n\n"
            "the fishdex tracks every species you've caught — think of it like a pokédex for fish — "
            "and the mastery system rewards you for catching the same species repeatedly.\n\n"
            "**fishdex:**\n"
            "> `.fishdex` — browse your full collection\n"
            "> `.fishdex <category>` — filter by freshwater, saltwater, etc.\n"
            "> completing rarities earns collection bonuses (all common = +2% fish value permanently)\n\n"
            "**mastery tiers (per species):**\n"
            "> 1 catch = novice (+0%) → 10 = apprentice (+1%) → 25 = journeyman (+2%) → "
            "50 = expert (+3%) → 100 = master (+5%) → 250 = grandmaster (+7%) → "
            "500 = legend (+10%) → 1,000 = mythic (+15%)\n"
            "> these bonuses are permanent value multipliers on that specific species\n"
            "> check progress with `.mastery fish`\n\n"
            "**strategy:**\n"
            "> mastery stacks with prestige bonuses, skill tree bonuses, pet bonuses, crew bonuses, and "
            "world events — a fully stacked legendary fish at mythic mastery with a prestige rod "
            "and an active crew can be worth 10x+ what a new player would get for the same catch"
        },
        {
            "crews & autofisher", "👥",
            "**fishing — crews & autofisher**\n\n"
            "crews are small fishing teams (2–5 players) that boost everyone's fish value when "
            "members are active together, and the autofisher lets you earn passively while you're "
            "away from the keyboard.\n\n"
            "**crews:**\n"
            "> `.crew create <name>` — start a crew\n"
            "> `.crew invite @user` — invite someone\n"
            "> `.crew info` — view your crew stats\n"
            "> when 2+ crew members fish within the same hour, everyone gets +15% fish value\n"
            "> if all members are active, the bonus increases to +25%\n\n"
            "**autofisher:**\n"
            "> purchase from the `.shop` — comes in tier 1 (catches every 30min) and tier 2 (every 20min)\n"
            "> the autofisher uses its own rod and bait slot separate from your manual fishing gear\n"
            "> `.autofisher equip <rod/bait>` — set up its gear\n"
            "> `.autofisher deposit <bait> <amount>` — load bait into the hopper\n"
            "> `.autofisher start` / `.autofisher stop` — toggle it on and off\n"
            "> `.autofisher collect` — grab your catches and sell them\n"
            "> auto-sell mode sells catches at 80% value automatically (manual sell = 100%)\n"
            "> it draws bait purchase funds from your bank up to a configured limit\n\n"
            "**tip:** the autofisher is one of the best investments in the game — even at 80% sell rate "
            "it earns around the clock while you sleep"
        }}
    },

    // ── mining ─────────────────────────────────────────────────────────
    {
        "mining", "⛏️", "ores, pickaxes, claims, and the mining grid", {
        {
            "how mining works", "🪨",
            "**mining — how mining works**\n\n"
            "mining uses an interactive 3×3 grid — ores appear on cells and you click them to collect "
            "before the timer runs out, and the ores you find depend on your pickaxe tier.\n\n"
            "**core loop:**\n"
            "> `.mine` → click ores on the grid → ores go into your mining inventory → `.sellore` to cash out\n"
            "> 30-second cooldown between digs\n\n"
            "**ore tiers:**\n"
            "> common → uncommon → rare → epic → legendary → prestige (P1–P10)\n"
            "> 60+ ore types ranging from basic stone ($5–20) to omega crystal ($10M–60M)\n"
            "> higher tier pickaxes unlock access to rarer ore types\n\n"
            "**equipment:**\n"
            "> pickaxes determine what ores can appear\n"
            "> minecarts and bags affect how much you can carry per session\n"
            "> bag rip chance means you can lose ores if you time out, so click fast\n\n"
            "**bonuses:**\n"
            "> +5% value per prestige level\n"
            "> +5% in the support server (home court advantage)\n"
            "> +10% for server boosters\n"
            "> mastery works for ores too — same tiers as fishing (1 → 1,000 catches)\n"
            "> check ore mastery with `.mastery ore`\n\n"
            "**mining claims (passive):**\n"
            "> `.claim buy <ore_type>` — purchase a claim that passively produces 1–3 ore every 4 hours\n"
            "> max 5 claims at once, expire after 7 days (renewable)\n"
            "> costs range from $10K (common) to $5M (legendary)\n"
            "> `.claim collect` to grab your passive ore, `.claim list` to see what you own"
        }}
    },

    // ── gambling ───────────────────────────────────────────────────────
    {
        "gambling", "🎰", "games, odds, progressive jackpot, and strategy", {
        {
            "games & odds", "🎲",
            "**gambling — games & odds**\n\n"
            "twelve gambling games are available and they all feed into the progressive jackpot "
            "system — 1% of all gambling losses goes into a shared pool and every win has a "
            "0.01% (1 in 10,000) chance of triggering the jackpot payout.\n\n"
            "**games:**\n"
            "> `.slots <bet>` — match symbols, triple 7s = ×50 payout\n"
            "> `.coinflip <bet> [heads|tails]` — 50/50, min bet $50\n"
            "> `.crash <bet>` — multiplier climbs, cash out before it crashes (mean ~2.5×, 4% instant bust)\n"
            "> `.blackjack <bet>` — classic 21\n"
            "> `.poker <bet>` — texas hold'em with 2–6 players\n"
            "> `.roulette <bet> <choice>` — standard roulette table\n"
            "> `.dice <bet>` — roll against the house\n"
            "> `.frogger <bet>` — dodge traffic\n"
            "> `.minesweeper <bet>` — reveal tiles, avoid mines\n"
            "> `.lottery` — weekly drawing\n"
            "> `.rr` — russian roulette\n\n"
            "**limits:**\n"
            "> max bet: $2B, 3-second cooldown between bets\n\n"
            "**progressive jackpot:**\n"
            "> `.jackpot` — view the current pool size\n"
            "> every gambling session has that 0.01% chance so the more you play the more tickets "
            "you're entering — the pool grows from everyone's losses across all servers\n\n"
            "**strategy:**\n"
            "> crash is the highest-skill game — learning when to cash out is the difference between "
            "consistent profit and going broke\n"
            "> slots have the worst expected value but the highest ceiling (×50 on triple 7s)\n"
            "> coinflip is the safest for grinding challenges since it's a true 50/50"
        }}
    },

    // ── crafting ───────────────────────────────────────────────────────
    {
        "crafting", "🔨", "recipes, ingredients, and what to craft first", {
        {
            "crafting system", "📜",
            "**crafting — recipes & strategy**\n\n"
            "the crafting system turns raw materials (fish, ore, bait, coins) into powerful consumables "
            "and upgrades — it's the primary item sink that gives value to common drops you'd otherwise "
            "just sell.\n\n"
            "**how to craft:**\n"
            "> `.craft` — opens an interactive paginated browser with all available recipes\n"
            "> recipes are sorted by category: fishing, mining, utility, prestige\n"
            "> click a recipe to see ingredients, then confirm to craft\n\n"
            "**key recipes:**\n"
            "> **bait refinery** — 50 common bait → 5 rare bait (consolidation)\n"
            "> **bait distillery** — 25 rare bait → 3 epic bait\n"
            "> **lootbox fusion** — 5 common lootboxes → 1 uncommon (works up the chain)\n"
            "> **lucky charm** — gold ore + rare fish + lucky coin → +20% luck for 2 hours\n"
            "> **treasure compass** — treasure map + metal detector + 500 coins → guaranteed $50K–$500K find\n"
            "> **insurance policy** — protects against robbery for 24 hours\n"
            "> **auto-sell net** — next 50 fish auto-sell at 110% value\n"
            "> **drill bit** — next 20 mine commands yield double ore\n"
            "> **meteor pickaxe** — 2× ore value for 24 hours\n"
            "> **philosopher's bait** — prestige 2 required, guarantees epic+ fish for 5 casts\n"
            "> **gilded rod** — cosmetic rod with +5% fish value\n\n"
            "**strategy:**\n"
            "> craft bait refineries early and often — upgrading bait tier is the fastest way to increase "
            "fishing income, and common bait is practically free\n"
            "> save lootbox fusion for when you have a stockpile of 25+ common boxes"
        }}
    },

    // ── pets ───────────────────────────────────────────────────────────
    {
        "pets", "🐾", "species, feeding, bonuses, and leveling", {
        {
            "pet system", "🐱",
            "**pets — how they work**\n\n"
            "pets are adoptable companions that provide passive percentage bonuses to different activities — "
            "they have a hunger meter that decays over time and a level system that scales their bonus.\n\n"
            "**commands:**\n"
            "> `.pet adopt <type>` — buy a pet from the pet shop\n"
            "> `.pet feed <name>` — restore hunger (uses fish/ore from your inventory)\n"
            "> `.pet equip <name>` — set as your active pet (only one active at a time)\n"
            "> `.pet view` — see all your pets and their stats\n"
            "> `.pet rename <old> <new>` — rename a pet\n"
            "> `.pet release <name>` — release a pet permanently\n\n"
            "**hunger & leveling:**\n"
            "> hunger starts at 100 and decays by 1 per hour — low hunger reduces the bonus proportionally\n"
            "> pets gain xp from related activities (fishing pet gains xp when you fish, etc.)\n"
            "> xp per level = 100 × 1.15^(level−1), so early levels are fast and later ones take grinding\n\n"
            "**species and bonuses:**\n"
            "> **common ($50K):** cat (+3% luck), dog (+3% xp), hamster (+5% work)\n"
            "> **uncommon ($200K):** parrot (+5% fish value), rabbit (+5% daily), fox (+5% rob protection)\n"
            "> **rare ($1M):** owl (+8% ore value), dolphin (+5% rare fish), wolf (+3% gambling luck)\n"
            "> **epic ($10M):** phoenix (+5% all value), unicorn (+3% legendary fish)\n"
            "> **legendary ($100M):** dragon (+8% all earnings)\n"
            "> **prestige ($500M):** void cat (+10% prestige bonus)\n\n"
            "**strategy:**\n"
            "> your first pet should match your main activity — if you fish a lot, get the parrot "
            "for +5% fish value, if you mine, get the owl for +8% ore value\n"
            "> keep hunger above 50 or the bonus drops significantly\n"
            "> the dragon is the best all-rounder but costs $100M, so it's a mid-game goal"
        }}
    },

    // ── skill trees ────────────────────────────────────────────────────
    {
        "skill trees", "🌳", "prestige points, branches, and builds", {
        {
            "skill tree system", "🧠",
            "**skill trees — prestige point investment**\n\n"
            "after your first prestige you unlock the skill tree — three branches that let you specialize "
            "your playstyle by spending prestige points earned from each prestige level.\n\n"
            "**points earned:**\n"
            "> P1–P5: 2 points per prestige\n"
            "> P6–P10: 3 points per prestige\n"
            "> P11+: 4 points per prestige\n\n"
            "**branches:**\n"
            "> 🎣 **angler** — fishing cooldown reduction, fish value, rare fish chance, double catch, "
            "epic+ fish chance, legendary fish chance\n"
            "> ⛏️ **prospector** — ore yield, ore value, rare ore chance, double ore, celestial ore chance, "
            "void crystal chance\n"
            "> 🎰 **gambler** — win chance, payout bonus, win streak bonus, loss reduction, jackpot chance, "
            "critical hit (2× payout) chance\n\n"
            "**each skill has up to 5 ranks (some cap at 2–3) across 5 tiers:**\n"
            "> investing in higher tiers requires filling earlier tiers first — plan your build before "
            "spending points because respec costs 10% of your net worth (min $500K)\n\n"
            "**commands:**\n"
            "> `.skills` — view your tree and available points\n"
            "> `.skills invest <branch> <skill>` — spend a point\n"
            "> `.skills respec` — reset all points (costs coins)\n"
            "> `.skills info` — detailed breakdown of all skills and their effects\n\n"
            "**strategy:**\n"
            "> focus one branch early — spreading points across all three dilutes the bonus\n"
            "> angler is the safest for consistent income, gambler is high-risk high-reward, "
            "prospector is good if you mine heavily"
        }}
    },

    // ── daily challenges & streaks ─────────────────────────────────────
    {
        "challenges", "📋", "daily challenges, streaks, and milestones", {
        {
            "daily system", "🎯",
            "**challenges — daily tasks & streak rewards**\n\n"
            "every day you get 3 random challenges (easy / medium / hard) that reward coins and xp — "
            "completing them consistently builds a streak that unlocks escalating bonus payouts "
            "and at high enough streaks you earn exclusive titles.\n\n"
            "**commands:**\n"
            "> `.challenges` — view today's challenges and claim completed ones\n"
            "> `.streak` — check your current streak and upcoming milestones\n\n"
            "**challenge rewards (% of net worth):**\n"
            "> easy: 2% + 50 xp — medium: 4% + 100 xp — hard: 8% + 200 xp\n"
            "> payout range: $500 minimum, $50M maximum per challenge\n\n"
            "**challenge types:**\n"
            "> catch X fish, sell $X worth of fish, catch X rare+ fish, mine X ores, "
            "win X gambles, profit $X gambling, use /work X times, earn $X total, "
            "pay others $X, use X commands\n\n"
            "**streak milestones:**\n"
            "> day 3: $5K — day 7: $50K — day 14: $200K — day 21: $500K\n"
            "> day 30: $1M + title — day 60: $5M — day 90: $10M\n"
            "> day 180: $50M — day 365: $250M\n\n"
            "**tips:**\n"
            "> challenges reset daily so check them first thing\n"
            "> easy challenges are worth doing even if the payout seems small — the streak "
            "multiplier and milestone bonuses add up enormously over time\n"
            "> missing a single day resets your streak — set a reminder"
        }}
    },

    // ── passive income ─────────────────────────────────────────────────
    {
        "passive income", "💤", "pond, claims, market, and interest", {
        {
            "passive systems", "📈",
            "**passive income — earning while you're away**\n\n"
            "four systems generate income without active play — they reward you for checking in "
            "a few times a day and managing your investments.\n\n"
            "**fish pond (`.pond`):**\n"
            "> build for $50K, stock it with fish from your inventory\n"
            "> every 6 hours it produces coins based on fish rarity (common $30 → prestige $5K per fish)\n"
            "> 5 upgrade levels: tiny (5 fish cap) → grand (40 fish cap)\n"
            "> upgrades: L2 $100K, L3 $500K, L4 $2M, L5 $10M\n"
            "> subcommands: build / stock / collect / upgrade / view / remove\n\n"
            "**mining claims (`.claim`):**\n"
            "> buy a claim on a specific ore type — produces 1–3 ore every 4 hours\n"
            "> max 5 claims, expire after 7 days (renewable at half cost)\n"
            "> costs: common $10K → legendary $5M\n"
            "> subcommands: buy / collect / renew / list / abandon\n\n"
            "**commodity market (`.market`):**\n"
            "> fish and ore prices fluctuate ±10–30% daily\n"
            "> check the market for trends, buy when prices dip, sell when they spike\n"
            "> subcommands: view / sell / buy / history\n\n"
            "**bank interest (`.interest`):**\n"
            "> 0.1% daily base rate, +0.05% per prestige level, caps at 0.5% (P8+)\n"
            "> max payout $500K per collection, 24h cooldown\n"
            "> this is why banking your money matters — every dollar earns interest\n\n"
            "**strategy:**\n"
            "> build your pond immediately and stock it with the best fish you can spare\n"
            "> mining claims on rare+ ores are the best value relative to cost\n"
            "> collect from all four sources daily — it adds up to significant free income"
        }}
    },

    // ── world events ───────────────────────────────────────────────────
    {
        "world events", "🌍", "random events, boss fights, and raids", {
        {
            "events & bosses", "⚡",
            "**world events — random boosts & community bosses**\n\n"
            "the bot randomly spawns server-wide events that buff specific activities — they appear "
            "roughly every 3–4 hours and last 15–120 minutes, plus there are community boss fights "
            "and cooperative raids.\n\n"
            "**random events (`.event`):**\n"
            "> 🎣 rare fish spawning (+25% rare fish), golden tide (+30% fish value), bait frenzy (+50% catches)\n"
            "> ⛏️ gold rush (+50% gold ore), meteor shower (fragments), crystal cavern (+40% ore value)\n"
            "> 🎰 casino night (+10% payout), lucky hour (+5% luck), jackpot fever (2× jackpot chance)\n"
            "> 💰 tax holiday (no robbery), double daily (2× daily), market boom (20% shop discount)\n"
            "> 📊 double xp (2×), knowledge rush (+50% xp)\n\n"
            "**global boss (`.boss`):**\n"
            "> community-wide goal — everyone contributes by fishing, mining, and gambling normally\n"
            "> the boss has an archetype that biases which activities count more\n"
            "> rewards are ranked: #1 gets 15–20 legendary lootboxes + $100M, lower ranks scale down\n\n"
            "**boss raids (`.raid`):**\n"
            "> cooperative 2–8 player fight with $10K entry fee\n"
            "> 3 rounds of attack / defend / heal actions via buttons\n"
            "> boss attacks 20–40 damage per round, your attacks do 50–80\n"
            "> payout = entry × players × 2, split by damage contribution\n\n"
            "**social features:**\n"
            "> `.heist start [difficulty]` — 3+ player cooperative vault robbery\n"
            "> roles assigned based on your strengths: lockpicker (fishing), tunneler (mining), "
            "hacker (gambling), muscle (default), lookout (stealth)\n"
            "> vault tiers: corner store ($25K) → dragon's hoard ($10M)\n"
            "> `.trade offer @user <item> for <item>` — player-to-player item trading with escrow (5% tax)"
        }}
    },

    // ── leveling ───────────────────────────────────────────────────────
    {
        "leveling", "📊", "xp, ranks, and level configuration", {
        {
            "xp system", "📶",
            "**leveling — xp & rank system**\n\n"
            "the bot tracks two separate xp values — global level (across all servers) and server level "
            "(per-server) — both contribute to leaderboard rankings and unlock-based features.\n\n"
            "**how it works:**\n"
            "> you earn xp from sending messages and using commands\n"
            "> `.rank` — view your current level, xp, and progress bar for both global and server\n"
            "> the rank card shows your position on both leaderboards\n\n"
            "**admin configuration (`.levelconfig`):**\n"
            "> enable / disable leveling per server\n"
            "> toggle coin rewards for leveling up\n"
            "> set coins earned per message\n"
            "> set xp range (min–max per message)\n"
            "> set minimum message length to earn xp\n"
            "> set xp cooldown in seconds (prevent spam-leveling)\n"
            "> toggle level-up announcements\n"
            "> set the announcement channel\n\n"
            "**tips:**\n"
            "> xp cooldown prevents rapid message spam from inflating levels\n"
            "> some world events grant double xp — take advantage of those windows\n"
            "> pet dogs give +3% xp bonus, which compounds over time"
        }}
    },

    // ── achievements & titles ──────────────────────────────────────────
    {
        "achievements", "🏆", "achievements, milestones, titles, and leaderboards", {
        {
            "progression tracking", "🎖️",
            "**achievements — progression tracking**\n\n"
            "the bot automatically tracks your stats across all activities and unlocks achievements "
            "at specific thresholds — these award fishing gear, coins, and titles.\n\n"
            "**achievements (`.achievements`):**\n"
            "> fish value sold: $1M / $10M / $100M / $1B / $1T\n"
            "> fish caught: 100 / 1K / 10K / 100K / 1M\n"
            "> gambling wins: 50 / 500 / 5K / 50K\n"
            "> games won: 25 / 250 / 2.5K / 25K\n"
            "> commands used: 1K / 10K / 100K / 1M\n"
            "> gambling profit: $100K / $1M / $100M / $1B\n"
            "> fishdex completion: 10% / 25%+\n"
            "> rewards scale from common bait to prestige-5 rods and bait\n\n"
            "**milestones (automatic):**\n"
            "> triggered automatically when you hit stat thresholds: 100 / 500 / 1K / 2.5K / 5K / 10K / 25K / 50K / 100K\n"
            "> categories: fish caught, games played, games won, gambling wins, commands used\n"
            "> rewards: $5K → $10M depending on milestone\n"
            "> titles awarded at 50K+ and 100K+\n\n"
            "**titles:**\n"
            "> cosmetic labels shown on leaderboards — some are purchasable from the shop, "
            "some are limited edition (only X copies exist), some are auto-awarded for achievements\n"
            "> limited titles rotate weekly in the shop — 4 rotation slots + permanent listings\n"
            "> rebirth titles (reborn → transcendent) are special prestige markers\n\n"
            "**leaderboards (`.lb`):**\n"
            "> global and per-server rankings by net worth, fish caught, xp, and more\n"
            "> your equipped title appears next to your name on the board"
        }}
    },

    // ── server setup ───────────────────────────────────────────────────
    {
        "server setup", "🔧", "for owners and admins — configuration guide", {
        {
            "initial setup", "⚙️",
            "**server setup — getting started as an admin**\n\n"
            "when the bot joins a new server it runs a 4-step setup wizard automatically — if you "
            "missed it or need to reconfigure, you can re-run `.setup` at any time.\n\n"
            "**setup wizard steps:**\n"
            "> step 1: welcome and overview\n"
            "> step 2: economy mode — global (shared across all servers) vs server (independent per server)\n"
            "> step 3: feature toggles — enable/disable gambling, fishing, trading, robbery\n"
            "> step 4: additional configuration options\n\n"
            "**key admin commands:**\n"
            "> `.prefix add <prefix>` — add a custom server prefix\n"
            "> `.levelconfig` — configure xp/leveling settings (cooldown, range, announcements, channel)\n"
            "> `.commands` — toggle individual commands or entire modules on/off per channel\n"
            "> `.servereconomy` — switch between global and server-isolated economy\n\n"
            "**moderation tools:**\n"
            "> `.antispam` — rate limiting (5 msgs/5s), duplicate detection, mention spam, emoji spam, "
            "caps spam (70%+ threshold), newline spam — configurable thresholds and whitelist/blacklist "
            "for roles, users, and channels — escalation: 2 violations → 15min timeout, 5 → kick, 10 → ban\n"
            "> `.urlguard` — block all links, discord invites, or external invites with domain whitelist/blacklist\n"
            "> `.textfilter` — block specific words, regex patterns, or emoji names — optionally scan usernames and filenames\n"
            "> `.reactionfilter` — filter unwanted reactions\n"
            "> all moderation filters use a 3-violations → 10min timeout escalation by default\n\n"
            "**economy for server owners:**\n"
            "> server economy mode gives you a completely separate economy — players start from zero "
            "in your server and their progress doesn't carry to other servers\n"
            "> this is good for competitive servers or communities that want a fresh start\n"
            "> global mode (default) means players keep their progress everywhere"
        },
        {
            "module management", "🎛️",
            "**server setup — module management**\n\n"
            "every command belongs to a module (economy, fishing, mining, gambling, etc.) and you "
            "can toggle modules or individual commands on or off per channel, per role, or server-wide.\n\n"
            "**how to toggle:**\n"
            "> `.commands` — view the current state of all modules and commands\n"
            "> the toggle system respects a hierarchy: server-wide → channel → role → user\n"
            "> disabling a module disables all commands in that module\n"
            "> disabling a specific command only affects that one command\n\n"
            "**common configurations:**\n"
            "> • disable gambling in general chat but enable it in a #casino channel\n"
            "> • disable fishing everywhere except #fishing\n"
            "> • disable moderation commands for non-admin roles\n"
            "> • create a #bot-commands channel and restrict economy commands there\n\n"
            "**tips for admins:**\n"
            "> use `.commands` regularly to audit what's enabled where\n"
            "> if members report a command not working, check if it's been disabled with `.commands`\n"
            "> the setup wizard's feature toggles are a quick way to configure large groups of commands at once"
        }}
    },

    // ── anticheat ──────────────────────────────────────────────────────
    {
        "anticheat", "🛡️", "BAC system, captchas, and fair play", {
        {
            "how BAC works", "🔒",
            "**anticheat — bronx anticheat (BAC)**\n\n"
            "the bot runs an automatic anticheat system called BAC that monitors command timing patterns "
            "to detect macros and automated tools — it's designed to keep the economy fair for everyone.\n\n"
            "**how it works:**\n"
            "> BAC tracks the timing intervals between your commands — if the intervals are suspiciously "
            "regular (like a script running every exactly 5 seconds) it flags your account\n"
            "> it also detects raw spam: more than 5 commands in 10 seconds triggers immediately\n\n"
            "**strike system:**\n"
            "> strike 1: captcha sent to your DMs + 5-minute command timeout\n"
            "> strike 2: 10-minute command timeout + warning that next strike is permanent\n"
            "> strike 3: permanent ban from the bot (appealable in the support server)\n\n"
            "**captchas:**\n"
            "> simple math problem (e.g. \"what is 47 + 23?\") with 4 button options\n"
            "> you have 60 seconds to answer — failing or ignoring escalates your strike\n\n"
            "**fishing anti-macro:**\n"
            "> fishing has its own separate anti-macro layer — random math captchas every 8–20 casts\n"
            "> 120 seconds to answer — strike 1: 5min cooldown, strike 2: 30min, strike 3: permanent fish blacklist\n\n"
            "**good to know:**\n"
            "> playing normally will never trigger BAC — it specifically targets inhuman timing consistency\n"
            "> if you get a false positive, solve the captcha and you're immediately cleared\n"
            "> bot owner and whitelisted users are exempt"
        }}
    },

    // ── advanced tips ──────────────────────────────────────────────────
    {
        "advanced tips", "💡", "optimization, stacking bonuses, and endgame", {
        {
            "stacking bonuses", "📊",
            "**advanced — stacking bonuses for maximum income**\n\n"
            "the real power of the economy comes from layering every available multiplier on top of "
            "each other — here's everything that can stack and how to maximize it.\n\n"
            "**fishing value multipliers (all stack):**\n"
            "> • prestige bonus: +5% per prestige level\n"
            "> • skill tree (angler): up to +2% per rank in appraiser's eye\n"
            "> • mastery: up to +15% per species at mythic tier\n"
            "> • crew bonus: +15% with 2+ active, +25% with all active\n"
            "> • pet bonus: parrot +5%, phoenix +5%, dragon +8%\n"
            "> • world events: golden tide +30%, bait frenzy +50%\n"
            "> • rebirth multiplier: up to ×1.61 at 5 rebirths\n"
            "> • crafted boosts: lucky charm +20%, auto-sell net +10%\n\n"
            "**mining value multipliers (all stack):**\n"
            "> • prestige bonus: +5% per prestige level\n"
            "> • skill tree (prospector): up to +2% per rank in gem cutter\n"
            "> • mastery: up to +15% per ore at mythic tier\n"
            "> • pet bonus: owl +8%, phoenix +5%, dragon +8%\n"
            "> • world events: gold rush +50%, crystal cavern +40%\n"
            "> • rebirth multiplier: up to ×1.61\n"
            "> • crafted boosts: drill bit (2× quantity), meteor pickaxe (2× value)\n\n"
            "**daily income multipliers:**\n"
            "> • prestige perks scale daily/weekly/work payouts\n"
            "> • pet rabbit +5% daily\n"
            "> • world events: double daily (2×)\n"
            "> • rebirth multiplier: up to ×1.61\n\n"
            "**endgame loop:**\n"
            "> prestige as high as you can → invest skill points → master every species → "
            "rebirth at P20 → repeat with permanent ×1.1 multiplier → aim for 5 rebirths "
            "and mythic mastery on all legendaries — at that point you're earning hundreds of "
            "millions per session"
        }}
    },

    // ══════════════════════════════════════════════════════════════════════
    // ADMIN-ONLY SECTIONS (admin_only = true)
    // ══════════════════════════════════════════════════════════════════════

    // ── command management (admin) ─────────────────────────────────────
    {
        "command management", "⚙️", "enable/disable commands per channel", {
        {
            "toggles & hierarchy", "🔘",
            "**command management — toggles & hierarchy**\n\n"
            "you can enable or disable any command or entire module on a per-channel, per-role, "
            "or server-wide basis — the system uses a hierarchy that resolves conflicts cleanly.\n\n"
            "**hierarchy (highest priority first):**\n"
            "> user override → role override → channel override → server default → global default\n\n"
            "**commands:**\n"
            "> `.commands` — view current toggle states for all modules and commands\n"
            "> `.commands <module>` — view/toggle a specific module\n"
            "> `.commands <command> enable/disable` — toggle a specific command\n"
            "> `.commands <module> enable/disable #channel` — toggle for a specific channel\n"
            "> `.commands <command> enable/disable @role` — toggle for a specific role\n\n"
            "**use cases:**\n"
            "> • disable gambling everywhere except #casino\n"
            "> • disable economy commands in #general to reduce spam\n"
            "> • disable moderation commands for everyone except @Mods\n"
            "> • disable fishing except in #fishing and #bot-commands\n\n"
            "**bulk operations:**\n"
            "> you can toggle entire modules in the setup wizard (`.setup`) for fast configuration\n"
            "> module toggles affect all commands in that module\n"
            "> command-level toggles override module-level toggles"
        },
        {
            "cooldown management", "⏱️",
            "**command management — cooldown management**\n\n"
            "most economy commands have built-in cooldowns to prevent spam and maintain balance — "
            "as a server owner using server economy mode, you can adjust these.\n\n"
            "**default cooldowns:**\n"
            "> `.fish` — 30 seconds\n"
            "> `.mine` — 30 seconds\n"
            "> `.daily` — 24 hours\n"
            "> `.weekly` — 7 days\n"
            "> `.work` — 30 minutes\n"
            "> `.rob` — 2 hours\n"
            "> gambling — 3 seconds between bets\n\n"
            "**server economy mode:**\n"
            "> when you enable server economy (`.servereconomy`), your server gets an independent "
            "economy with its own balances, and you can customize cooldowns and rewards\n"
            "> this is useful for competitive servers or communities that want tighter control\n\n"
            "**customization options:**\n"
            "> `.servereconomy cooldown <command> <seconds>` — set custom cooldown\n"
            "> `.servereconomy reward <command> <multiplier>` — adjust payout scaling\n"
            "> these only affect your server — global economy users keep default settings"
        }
    }, true}, // admin_only = true

    // ── economy tuning (admin) ─────────────────────────────────────────
    {
        "economy tuning", "📊", "balance your server's economy", {
        {
            "server economy mode", "💵",
            "**economy tuning — server economy mode**\n\n"
            "by default, all servers share a global economy — your balance, fish, and gear carry "
            "across every server running the bot — but you can enable server economy mode to create "
            "an isolated economy just for your community.\n\n"
            "**enabling server economy:**\n"
            "> `.servereconomy enable` — switch your server to isolated mode\n"
            "> `.servereconomy disable` — switch back to global mode\n"
            "> `.servereconomy status` — check current mode\n\n"
            "**what changes in server mode:**\n"
            "> • player balances start from zero and don't transfer out\n"
            "> • fish, ore, and gear are server-specific\n"
            "> • leaderboards show only your server's players\n"
            "> • you can customize cooldowns and payouts\n"
            "> • prestige and rebirth progress is server-specific\n\n"
            "**when to use server economy:**\n"
            "> • competitive servers where you want a level playing field\n"
            "> • roleplay servers with custom economy theming\n"
            "> • communities that want tighter control over progression\n"
            "> • servers testing economy changes before suggesting them globally"
        },
        {
            "balancing tips", "⚖️",
            "**economy tuning — balancing tips**\n\n"
            "if you're running server economy mode and want to tune the experience, here are "
            "some guidelines based on how the global economy is balanced.\n\n"
            "**income sources (global defaults):**\n"
            "> daily: 8% of net worth ($500 min, $250M max)\n"
            "> weekly: 50% of net worth ($1K min, $1B max)\n"
            "> work: 3% of net worth ($100 min, $25M max)\n"
            "> fishing: ~$1K–$10M per rare fish depending on gear\n"
            "> mining: ~$500–$5M per rare ore depending on gear\n"
            "> gambling: net-zero with skill, negative with bad luck\n\n"
            "**money sinks (what removes money):**\n"
            "> shop purchases (gear, bait, titles)\n"
            "> bank upgrades\n"
            "> loan interest (10%)\n"
            "> crafting costs\n"
            "> trading tax (5%)\n"
            "> skill tree respec (10% of net worth)\n"
            "> failed robbery (lose 15–30% of wallet)\n"
            "> passive system costs (pond, claims)\n\n"
            "**healthy economy indicators:**\n"
            "> • top players are 100–1000× richer than new players (not 10000×)\n"
            "> • rare items maintain value because they're hard to get\n"
            "> • prestige feels like an achievement, not a grind\n"
            "> • multiple viable paths to wealth (fishing, mining, gambling, passive)"
        }
    }, true}, // admin_only = true

    // ── moderation config (admin) ──────────────────────────────────────
    {
        "moderation config", "🔨", "automod, filters, and escalation", {
        {
            "automod systems", "🤖",
            "**moderation config — automod systems**\n\n"
            "the bot includes four automod systems that detect and respond to problematic behavior — "
            "each can be configured independently with whitelist/blacklist support.\n\n"
            "**antispam (`.antispam`):**\n"
            "> rate limiting: 5 messages per 5 seconds by default\n"
            "> duplicate detection: 3 identical messages triggers action\n"
            "> mention spam: 5+ mentions per message\n"
            "> emoji spam: 10+ emojis per message\n"
            "> caps spam: 70%+ uppercase (minimum 10 characters)\n"
            "> newline spam: 10+ newlines per message\n\n"
            "**url guard (`.urlguard`):**\n"
            "> block all links, discord invites, or external invites\n"
            "> domain whitelist for approved sites\n"
            "> domain blacklist for known bad actors\n\n"
            "**text filter (`.textfilter`):**\n"
            "> blocked words (exact match)\n"
            "> regex patterns for complex matching\n"
            "> emoji name filtering\n"
            "> optional: scan usernames, attachment filenames\n\n"
            "**reaction filter (`.reactionfilter`):**\n"
            "> block specific emoji reactions\n"
            "> useful for preventing reaction spam or inappropriate emoji"
        },
        {
            "escalation system", "📈",
            "**moderation config — escalation system**\n\n"
            "automod uses a graduated escalation system — violations accumulate and trigger "
            "increasingly severe responses to give users a chance to self-correct.\n\n"
            "**default escalation (antispam):**\n"
            "> 2 violations → 15-minute timeout\n"
            "> 5 violations → kick from server\n"
            "> 10 violations → permanent ban\n\n"
            "**default escalation (url guard, text filter):**\n"
            "> 3 violations → 10-minute timeout\n"
            "> further violations continue timeout\n\n"
            "**customization:**\n"
            "> you can adjust thresholds and actions per system\n"
            "> whitelist roles, users, or channels to exempt them\n"
            "> blacklist specific users for stricter enforcement\n\n"
            "**violation decay:**\n"
            "> violations decay over time (typically 24–48 hours of good behavior)\n"
            "> this prevents ancient minor infractions from stacking to bans\n\n"
            "**logging:**\n"
            "> all automod actions are logged if you've configured a mod-log channel\n"
            "> use `.modlog #channel` to set the channel for mod action logging"
        }
    }, true} // admin_only = true

    }; // end sections vector
}

std::vector<GuideSearchResult> search_guide(const std::string& query, bool include_admin) {
    std::vector<GuideSearchResult> results;
    auto sections = get_guide_sections();
    
    // lowercase the query
    std::string query_lower = query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(),
        [](unsigned char c) { return std::tolower(c); });
    
    for (size_t si = 0; si < sections.size(); si++) {
        const auto& section = sections[si];
        
        // skip admin sections if not requested
        if (section.admin_only && !include_admin) continue;
        
        for (size_t pi = 0; pi < section.pages.size(); pi++) {
            const auto& page = section.pages[pi];
            
            // lowercase the content for searching
            std::string content_lower = page.content;
            std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            
            std::string title_lower = page.title;
            std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            
            std::string section_lower = section.name;
            std::transform(section_lower.begin(), section_lower.end(), section_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            
            // calculate relevance
            int relevance = 0;
            std::string match_context;
            
            // exact match in title = highest relevance
            if (title_lower.find(query_lower) != std::string::npos) {
                relevance += 100;
                match_context = "title: " + page.title;
            }
            
            // match in section name
            if (section_lower.find(query_lower) != std::string::npos) {
                relevance += 50;
                if (match_context.empty()) match_context = "section: " + section.name;
            }
            
            // match in content
            size_t pos = content_lower.find(query_lower);
            if (pos != std::string::npos) {
                relevance += 25;
                // extract context around match (50 chars before/after)
                size_t start = (pos > 50) ? pos - 50 : 0;
                size_t len = std::min((size_t)100 + query.length(), page.content.length() - start);
                std::string snippet = page.content.substr(start, len);
                // clean up the snippet (remove newlines, trim)
                for (char& c : snippet) {
                    if (c == '\n') c = ' ';
                }
                if (match_context.empty()) {
                    match_context = "..." + snippet + "...";
                }
                
                // count occurrences for higher relevance
                size_t count = 0;
                size_t search_pos = 0;
                while ((search_pos = content_lower.find(query_lower, search_pos)) != std::string::npos) {
                    count++;
                    search_pos += query_lower.length();
                }
                relevance += (int)(count * 5); // +5 per occurrence
            }
            
            if (relevance > 0) {
                results.push_back({
                    si, pi,
                    section.name, page.title,
                    match_context,
                    relevance
                });
            }
        }
    }
    
    // sort by relevance descending
    std::sort(results.begin(), results.end(),
        [](const GuideSearchResult& a, const GuideSearchResult& b) {
            return a.relevance > b.relevance;
        });
    
    return results;
}

std::vector<std::string> get_section_names(bool include_admin) {
    std::vector<std::string> names;
    auto sections = get_guide_sections();
    for (const auto& section : sections) {
        if (section.admin_only && !include_admin) continue;
        names.push_back(section.name);
    }
    return names;
}

} // namespace guide
} // namespace commands
