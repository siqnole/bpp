# bronx style guide

everything is lowercase. it just has to work.

---

## philosophy

- **modern, minimal, soft.** the bot's identity is calm and clean — pastels, lowercase, no yelling.
- **pragmatic over dogmatic.** if a pattern works and reads well, use it. don't over-engineer.
- **lowercase everything** in user-facing text. the only exceptions are discord markdown formatting (`**bold**`) and emphatic game callouts (`**JACKPOT!**`).
- **emojis are for navigation, not decoration.** they help users scan — section headers, status indicators, category labels. they're never sprinkled into prose for flavor.

---

## user-facing text

### voice & tone

- second person, casual, lowercase: `"you don't have that much"`, `"you already claimed your daily!"`
- contracted forms: `"you can't"`, `"you're"`, `"don't"`
- direct and brief. no filler, no corporate speak.
- the bot refers to itself as `"i"` (lowercase): `"i'm missing permissions!"`, `"i'm powered by C++ and DPP!"`

### capitalization

- **all lowercase** for sentences, labels, descriptions, error messages, footers, placeholders.
- **bold lowercase** for field labels: `**wallet:**`, `**category:**`, `**usage:**`
- **`**UPPER CASE**` only** inside bold for dramatic game outcomes: `**JACKPOT!**`, `**TRIPLE DIAMONDS!**`, `**SPINNING...**`
- game names in titles may use all caps as a visual identity: `BLACKJACK`, `REACT`, `CLASSIC ROULETTE` — this is an accepted edge case for games that need to feel punchy.
- never capitalize normal sentences. `"Daily Reward Claimed"` → `"daily reward claimed"`.

### formatting

- `**bold**` for labels and emphasis — not markdown headings in most cases.
- `#` / `##` headings reserved for high-impact moments only: jackpot wins, world event banners.
- `>` blockquotes for lists of subcommands, examples, or step-by-step content.
- inline code for command references: `` `.fish` ``, `` `.bank deposit` ``
- `\n` for line breaks, `\n\n` for section breaks within descriptions.
- dollar amounts: `"$" + format_number(amount)` — always with `$` prefix and comma formatting.
- timestamps: `<t:EPOCH:R>` for relative time in cooldown messages.
- arrows for progression: `wood → bronze → silver → gold → diamond`

### error messages

all errors go through `bronx::error()`. keep them lowercase, direct, and helpful:

| type | pattern | example |
|---|---|---|
| missing args | state what's needed | `"specify an amount to bet"` |
| validation | state the constraint | `"minimum bet is $100"` |
| state conflicts | explain why it failed | `"you can't rob while in passive mode!"` |
| cooldowns | tell them when | `"you already claimed your daily! try again <t:...:R>"` |
| system errors | brief, no panic | `"failed to claim daily"` |

### success messages

all successes go through `bronx::success()`. same rules — lowercase, direct:

- `"daily reward claimed! you received $500"`
- `"deposited $1,000 into your bank"`

---

## emoji rules

emojis serve a **functional purpose** — they are visual anchors that help users navigate, not decoration.

### when to use emojis

| context | example | why |
|---|---|---|
| **embed titles** — leading emoji for category identity | `"🐾 Pet Shop"`, `"⛏️ Mining Claims"`, `"📊 Rank Info"` | helps users instantly identify what they're looking at |
| **guide section headers** | `📖 getting started`, `💰 economy`, `🎣 fishing` | scannable menu navigation |
| **status indicators** — custom emojis | `<:check:...>` success, `<:deny:...>` error, `<:warning:...>` warning | clear pass/fail at a glance |
| **navigation buttons** — arrows | `◀️`, `▶️`, `◀ prev`, `next ▶` | obvious directional affordance |
| **game-specific theming** | `🎰` slots, `🔫` russian roulette, `🎲` gambling | the emoji IS the game's identity |
| **log module tags** (internal) | `🐟` fishing, `🃏` blackjack, `🗄` database | fast visual grep in journalctl |

### when NOT to use emojis

- **body text / prose** — never sprinkle emojis into descriptions or explanations.
- **error messages** — the `<:deny:...>` prefix is enough. no additional emojis in the message body.
- **help system detail text** — command details, usage strings, aliases, notes are all emoji-free.
- **field labels** — `**wallet:**`, `**bank:**`, `**net worth:**` — no emojis before labels.
- **admin/owner tooling** — internal tools don't need visual polish. plain text titles are fine.

### edge case exceptions

these are the only acceptable deviations:

1. **game outcome callouts** — a jackpot win can be `"# 🎰💰 PROGRESSIVE JACKPOT WON! 💰🎰"` because it's a rare, high-impact event and deserves the fanfare.
2. **world event banners** — `"# 🌍 World Events"` uses heading + emoji because it's a navigational section header within the embed.
3. **slot machine reels** — the slot symbols (`🍒`, `🍋`, `💎`, `7️⃣`) are the content, not decoration.
4. **boss archetypes** — `🐉`, `🗿`, `🦑` represent the boss itself; they're content.
5. **pet species** — the emoji represents the actual pet type: 🐱, 🐶, 🐦.
6. **progress/reaction games** — where the emoji IS the gameplay mechanic.

---

## embed structure

### construction

- use `bronx::create_embed(description, color)` as the base factory. it sets description, color, and timestamp.
- use `bronx::success(msg)`, `bronx::error(msg)`, `bronx::info(msg)` for one-liner status embeds.
- **description-first** — put content in `.set_description()`, not fields. fields are rarely needed.
- use `bronx::send_message(bot, event, embed)` for text commands — it handles reply fallback and permission errors.
- use `bronx::safe_slash_reply(bot, event, embed)` for slash commands.
- use `bronx::safe_message_edit(bot, msg)` for edits.

### color palette

| constant | hex | use |
|---|---|---|
| `COLOR_DEFAULT` | `#B4A7D6` | soft lavender — default for most embeds |
| `COLOR_SUCCESS` | `#A8D5BA` | soft green — confirmations, rewards |
| `COLOR_ERROR` | `#E5989B` | soft red — errors, failures |
| `COLOR_WARNING` | `#F4D9C6` | soft peach — warnings, cautions |
| `COLOR_INFO` | `#A7C7E7` | soft blue — informational, neutral |

custom colors are acceptable for specific systems (e.g. dark red for russian roulette, purple for world events) but should stay within the soft/muted aesthetic. no neon, no pure primary colors.

### titles

- most embeds **don't need a title**. if the description is self-explanatory, skip it.
- when used: `emoji + lowercase/title case label`. prefer `"🐾 pet shop"` over `"🐾 Pet Shop"` going forward — match the lowercase identity.
- game embeds may use `UPPER CASE` titles as their own visual style: `"BLACKJACK"`, `"REACT"`.
- never use a title AND a `#` heading in the description — pick one.

### footers

- `bronx::add_invoker_footer(embed, user)` randomly rotates between:
  - `"invoked by {name}"` with avatar
  - fun fact about the bot
  - invite prompt
  - economy command suggestion
- manual footers for contextual info: `"chamber 3/6 • pot: $50,000"`, `"global economy"`.
- footers are always lowercase.

### buttons

buttons should trend **lowercase** to match the bot's identity:

| style | use case | example |
|---|---|---|
| lowercase | default for most actions | `"hit"`, `"stand"`, `"cash out"`, `"back"` |
| emoji + lowercase | directional / category actions | `"⬅️ left"`, `"💰 cash out"` |
| Title Case | acceptable for formal actions (banking, confirmations) | `"Deposit"`, `"Confirm Loan"` |
| emoji + Title Case | social/multiplayer joins | `"⚔️ Join Raid"`, `"🔑 Join Heist"` |
| bare arrows | pagination | `"◀"`, `"▶"` |

prefer lowercase. Title Case is acceptable but not preferred.

### select menus

- placeholders are always lowercase: `"select a category"`, `"select a guide topic"`
- option labels match their context: category names lowercase, command names lowercase.

### modals

- modal titles: lowercase — `"report a bug"`, `"suggest a fish"`
- input labels: Sentence case is acceptable here since they're form fields: `"What command has the issue?"`, `"Enter amount to bet"`

---

## C++ code style

### naming

| thing | convention | example |
|---|---|---|
| variables | `snake_case` | `user_id`, `bet_amount`, `wallet` |
| member variables | `snake_case` + trailing `_` | `db_`, `cache_`, `bac_mutex_` |
| functions | `snake_case` | `get_wallet()`, `update_wallet()`, `send_message()` |
| classes / structs | `PascalCase` | `Database`, `CommandHandler`, `BotStats` |
| constants | `UPPER_SNAKE_CASE` | `COLOR_DEFAULT`, `MAX_BET`, `BAC_HISTORY_SIZE` |
| namespaces | `snake_case` | `bronx::db`, `commands::fishing`, `bronx::cache` |
| files | `snake_case` | `command_handler.h`, `cache_manager.cpp` |
| macros | `UPPER_SNAKE_CASE` | `DBG_FISH`, `CLR_VAL` |

### file organization

- header-only (`.h`) for most command and utility code. `.cpp` for database core, cache manager, and heavy implementations.
- `#pragma once` — no include guards.
- includes: standard library first, then `<dpp/dpp.h>`, then project headers.
- commands aggregate via `get_<feature>_commands(db)` factory functions returning `std::vector<Command*>`.
- interactions registered via `register_<feature>_interactions(bot, db)` functions.
- database operations: `database/operations/<domain>/<domain>_operations.h` pattern.

### namespaces

- `bronx` — top-level for bot framework utilities (embeds, colors, send helpers).
- `bronx::db` — database layer.
- `bronx::cache`, `bronx::local`, `bronx::batch`, `bronx::api`, `bronx::hybrid` — performance subsystems.
- `commands` — all command definitions.
- `commands::<feature>` — feature-specific commands: `commands::fishing`, `commands::gambling`, `commands::pets`, etc.
- `bronx::db::<domain>_operations` — database operation namespaces.

### comments

- `//` only — never `/* */` block comments.
- casual, person-to-person tone: `"// DMs are likely closed - nothing more we can do"`
- **major section** — `=` lines, full width:
  ```cpp
  // ============================================================================
  // GLOBAL BOSS SYSTEM — Randomized Thresholds + Gambling
  // ============================================================================
  ```
- **sub-section** — `─` (unicode box-drawing):
  ```cpp
  // ── Boss archetype definitions ──────────────────────────────────────────────
  ```
- inline explanations for magic numbers/codes: `// 50001 = Missing Access, 50013 = Missing Permissions`
- doc blocks above complex systems use consecutive `//` lines with indented bullet lists.

### patterns

- `static` local variables for one-time initialization of command lists.
- lambda captures for command handlers — capture `db` pointer and `&bot` reference.
- `std::optional` returns from database lookups — always check before use.
- RAII with `std::lock_guard` / `std::unique_lock` for mutexes.
- `constexpr` for compile-time constants, `static inline const` for non-trivial constants.
- C++17 features: structured bindings, `std::optional`, `if constexpr`, `std::string_view` where appropriate.

---

## web / dashboard style

### javascript

- single class encapsulation: `BronxBotDashboard`.
- `camelCase` for methods and properties: `currentGuild`, `initCommandPalette()`, `showNotification()`.
- same `//` comment style and `──` section dividers as C++.

### html

- lowercase branding: `"bronx · dashboard"` (middot separator).
- BEM-adjacent class naming: `sidebar-brand`, `cmd-palette-overlay`, `toast-container`.
- all UI text lowercase: `"log in with discord to manage your servers"`.

### css

- CSS custom properties with short semantic names: `--bg`, `--fg`, `--accent`, `--border`, `--radius`.
- accent color `#b4a7d6` — same lavender as the bot's `COLOR_DEFAULT`.
- dark theme with glassmorphism: `backdrop-filter: blur(20px)`, soft shadows.
- section dividers: `/* --- Section Name ------------------------------------------ */`

---

## branding

- always `"bronx"` — lowercase, no caps, no stylization.
- prefix: `b.` for text commands.
- support server: `discord.gg/bronx`, linked as `[support](https://discord.gg/bronx)`.
- github: `github.com/siqnole/bronxbot`.

---

## known inconsistencies to clean up

these exist in the codebase and should trend toward the rules above over time:

| area | current state | preferred direction |
|---|---|---|
| embed titles | mixed Title Case / lowercase | lowercase preferred: `"🐾 pet shop"` not `"🐾 Pet Shop"` |
| button labels | ~50/50 lowercase vs Title Case | lowercase preferred for most |
| modal input labels | Sentence case | acceptable — forms are the one place this is fine |
| some embed titles missing emoji | admin tools have plain titles | acceptable — internal tools don't need polish |
| custom colors outside palette | dark red, blue, purple used ad-hoc | keep them muted/soft, document if reused |

---

*this guide is a living document. when in doubt: lowercase, minimal, functional.*
