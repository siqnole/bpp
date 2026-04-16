# Bronx Bot - Advanced Economy & Gambling Architecture
> an attempt to process and execute highly-concurrent transactional tasks across Discord environments and verify state consistency through cryptographic auditing

![a perfect audit pass from the verification system](/audit_report.png)

**Bronx** is a lightweight, modular C++ Discord Bot framework built on D++ for high-performance economy systems. It features transaction-level gambling verification, dynamic global feature flags, concurrent cache management, and multi-server orchestration.

## Please watch 
**[this incredible video on Event-Driven Architecture](https://www.youtube.com/watch?v=placeholder-link)** from the **System Design community** before reading the rest of this README. It provides an excellent introduction to handling race conditions in distributed systems and motivates the design of this framework.
> The process of reliable transaction management is broken down into five stages: (1) formulating a bounded context, (2) recording pre-execution state data, (3) choosing an atomic locking mechanism, (4) designing a verification function to assess consistency, and (5) selecting an asynchronous approach for logging. At each stage, we discuss how prior system design knowledge may be embedded into the process. *(adapted from the video)*

--- 

## Table of Contents

- [Installation](#installation)
- [Quick Start](#quick-start)
- [Application Commands](#application-commands)
  - [Common Flags](#common-flags)
  - [User Commands](#user-commands)
  - [Admin Commands](#admin-commands)
  - [Global Management Mode](#global-management-mode)
- [Architecture](#architecture)
  - [Command Consolidation](#command-consolidation)
  - [Transaction Identification](#transaction-identification)
  - [Bot Core Network](#bot-core-network)
  - [Verification Constraint](#verification-constraint)
- [Execution Workflow](#execution-workflow)
  - [Live Visualizations](#live-visualizations)
  - [Activity Logging](#activity-logging)
  - [State Persistence](#state-persistence)
- [Auditing & Interactive Dashboard](#auditing--interactive-dashboard)
  - [State Auto‑Detection](#state-auto-detection)
  - [Pagination Widgets](#pagination-widgets)
  - [Transaction History Review](#transaction-history-review)
- [Parallel Server Optimization](#parallel-server-optimization)
  - [Guild Configuration Format](#guild-configuration-format)
  - [Running Multiple Shards](#running-multiple-shards)
- [Comparing Analytics](#comparing-analytics)
  - [Using `get_gambling_stats()`](#using-get_gambling_stats)
  - [Example Usage](#example-usage)
- [System Outputs](#system-outputs)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Installation

**Requirements:** C++17+, D++ (DPP) Library, MySQL, CMake.

```bash
git clone https://github.com/siqnole/bronxbot.git
cd bronxbot
mkdir build && cd build
cmake .. && make -j4
```

No external message broker is required for basic operations—local memory fallback is built-in.

---

## Quick Start

### Run the bot against the primary global database

```bash
./bronx_bot --mode prod --shard-count 4
```

The terminal dashboard will appear showing the connection status, shard latency, and incoming event stream. The bot automatically routes new commands using the parent-subcommand router structure.

### Verify gambling state interactively

In any Discord channel the bot is in, use:
```
/gamblingaudit
```

The evaluation embed opens with user constraints—showing win rates and total generated transaction histories. 

### Run on a specific guild constraint

```bash
./bronx_bot --mode dev --debug-guild 123456789012345678
```

---

## Application Commands

### Common Flags

| Alias | Default Setup | Description |
|------|---------|-------------|
| `--mode {dev,prod}` | `dev` | Run in development or production environment. |
| `--export-db PATH` | — | Save current local db cache to SQL dump. |

### User Commands

| Command | Subcommands | Description |
|------|---------|-------------|
| `/gamble` | `slots`, `coinflip`, `dice`, `roulette` | Core economy verification games. |
| `/fish` | `cast`, `inventory`, `sell`, `info` | Incremental interaction system. |
| `/gamblingaudit`| — | Personal dashboard for transaction validation. |

### Admin Commands

| Command | Required Permission | Description |
|------|---------|-------------|
| `/log config` | `Administrator` | Route system webhooks into Discord channels. |
| `/gambaudit` | `Manage Server` | Audit a specific user's transaction history. |

### Global Management Mode

| Command | Scope | Description |
|------|---------|-------------|
| `/feature toggle` | Owner Only | Global killswitch for beta features (e.g. `logger_beta`). |

---

## Architecture

### Command Consolidation

To bypass Discord's 100-slash-command limit, inputs are linearly mapped from parent commands to subcommands logic maps. This ensures the router always receives well‑formatted inputs, independent of the nested depth.

```cpp
auto [subcommand, params] = parse_slash_subcommand(event, "gamble");
```

### Transaction Identification

To prevent race conditions during high‑frequency calls (like spamming `/gamble coinflip`), a **Transaction mapping** is applied immediately after the initial wallet extraction:

```cpp
std::string txn_id = create_gambling_transaction(
    db, user_id, "slots", bet, winnings, balance_before, "", "slots_data"
);
```

The string ID is generated via a UUID v4 engine randomly initialized once and kept track of during the execution block. This technique allows us to confidently track atomic wallet updates.

### Bot Core Network

- **Input:** Normalized Discord `slashcommand_t`
- **Cache Layer:** Projects input to memory checks (e.g., Message Cache limits outputs).
- **Hidden Layers:** Subsystem processing routines (e.g., `bronx::db::gambling_verification`).
- **Output:** Application embed or message string sent back to the API.

The bot relies heavily on lambda architectures hooked into DPP's core listeners.

### Verification Constraint

The total safety requirement is a boolean-check flow:

```cpp
if (!verify_gambling_transaction(db, user_id, txn_id, balance_before, balance_before + winnings)) {
    send_error("verification failed - race condition detected");
    return;
}
```

- **verify_gambling_transaction:** Asserts the absolute equivalence between expected balances and the true database values.
- If it fails, any wallet update is halted—eliminating double-spending without needing harsh SQL locks.

---

## Execution Workflow

### Live Visualizations

All user-responses conform to our Style Guide and utilize a soft-color embed template for aesthetic continuity. The layout includes:

- **Primary Output:** Core action text set directly in the description (`bronx::create_embed`).
- **Outcome Status:** Lowercase, functional formatting (`amount: +$2,000`).
- **Error Bounds:** Red/Orange fallback warnings if checks fail.

A background thread handles async webhook dispatches to keep interaction replies fast (under 3s).

### Activity Logging

For every critical action, the script records:

- Discord Timestamp
- Event Type (`GAMB` vs `GBHK`)
- User Metadata
- Contextual amounts and before/after balances.

These are written direct to the database via native MySQL APIs.

### State Persistence

The user wallets are updated through atomic `update_wallet` calls **only when the pre-verification passes**. Database configurations natively use single-threaded mutex mappings (`std::lock_guard<std::mutex>`) when executing critical inserts. 

---

## Auditing & Interactive Dashboard

The Evaluation embed mode loads user history and presents an **interactive dashboard**.

### State Auto‑Detection

- The `gamblingaudit` module queries `get_gambling_stats(db, user_id)` to automatically discover historical data.
- This ensures the audit always covers the exact balance sequences a user was trained/exposed to in the system.

### Pagination Widgets

A custom button row allows interactive scrubbing through the history logs. The history panels update instantly, while the loss metrics (Total Wagered) remain static.

### Transaction History Review

If the command was executed on a user suspected of exploits, an owner **Step Slider / Limit param** (`/gambaudit max:200`) forces deeper history recall. Scrubbing through history logs shows visually how the user's wallet progressed over time.

---

## Parallel Server Optimization

To manage different economy rules across various Discord guilds, the bot relies on database configuration rows in `guild_settings`.

### Guild Configuration Format

The database utilizes specific columns to structure each server's experience. Supported domains:

| Setting Key | Type | Default | Description |
|-----|------|---------|-------------|
| `guild_id` | bigint | **required** | The root Discord identification node. |
| `prefix` | varchar | `.` | Command prefix for legacy routing. |
| `economy_mode` | enum | `global` | Determines if wallet stats sync globally or per-server. |
| `beta_tester` | tinyint | `0` | Flips on experimental slash features for the guild. |

**Example Config Retrieval:**
```cpp
bool is_beta = db->get_guild_setting(guild_id, "beta_tester") == "1";
```

### Running Multiple Shards

Each server is essentially routed as a separate event inside DPP's shard cluster. The events execute concurrently; thread isolation ensures one server doing heavy gambling doesn't throttle another server's normal chatting endpoints. 

---

## Comparing Analytics

After tracking numerous transactions across the ecosystem, use the integrated tools to generate comparison views.

### Using `get_gambling_stats()`

```cpp
auto stats = get_gambling_stats(db, user_id);
```

This populates structures tracking:
- **Metrics:** Unverified transactions, Verified transactions, Win rate %, Max balance reached.

### Example Usage

```
.gambaudit 123456789012345678 100
```
This forces the analyzer to span the latest 100 vectors for that specific user, compiling result sets in real time.

---

## System Outputs

| Target | Description |
|------|-------------|
| `history` table | Primary archive containing all temporal metadata, domain bounds, and IDs. |
| `webhook_logs` | Async dispatch routing pointing to target Discord Channels. |
| `terminal stdout` | Live JSON/String output of the active C++ event loop threads. |

---

## Troubleshooting

### Timeouts occurring on Commands (`Discord API Timeout`)
These happen when `interaction_create` takes longer than 3 seconds. The bot automatically falls back to `defer()` patterns for operations known to scrape history tables.

### Audit Result Shows `FAILED` (`Race condition flag`)
This happens when multiple connections execute simultaneously on the same user. The `verification_check` accurately blocks the write to prevent double-spending. The user must retry their original sub-command.

### No Logs being Emitted
Check your webhook configuration using `/log status`. If the `logger_beta` feature isn't enabled via the Database, the async threads will short-circuit and remain dormant.

### Bot Predicting a Flat Line (Ignoring Inputs)
If the cluster fails to respond, ensure:
- **Database connections** aren't saturated (threadpool checks).
- **DPP Token** is valid and not rate-limited.
- **Intents** for message reading are enabled via the Discord Developer Portal.

---

## License

Distributed under the MIT License. See `LICENSE` for more information.
