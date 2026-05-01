# BRONX BOT (BPP) | for-AI-parsing | lang:en | style:lowercase-minimal

<user>
identity: bronx bot (bpp)
tone: lowercase, modern, minimal, soft
focus: premium c++ dpp bot + node.js dashboard
preference: pragmatic implementation over abstraction
</user>

<gates label="HARD GATES | priority: gates>rules>rhythm | STOP on fail">

GATE-1 BRANDING:
  trigger: all user-facing text (bot or dashboard)
  action: set-to-lowercase
  exception: discord markdown formatting (`**bold**`), game status callouts (`**JACKPOT!**`), slot symbols
  ref: [STYLE_GUIDE.md](file:///home/siqnole/Documents/code/bpp/STYLE_GUIDE.md)

GATE-2 ARCHITECTURE:
  bot-core: c++17 | dpp | mysql (mysql-connector-c)
  site-core: node.js | express | vanilla js/css
  deployment: vps (systemd) | docker (builder image)

GATE-3 SAFETY:
  sensitive: ignore `.env` changes | don't leak `107.173.67.242` credentials
  pre-check: must check `mysql_stmt` preparation result
  rollback: `cp ~/.claude/CLAUDE.md .bak` before major rule changes

GATE-4 BUILD INTEGRITY:
  rule: NEVER deploy code to production without local build verification
  action: always run `cmake -B build_local -S . && cmake --build build_local -j$(nproc)` locally first
  validation: fix ALL errors before touching production, warnings alone are acceptable
  merge-conflicts: resolve completely before committing, verify no `<<<<<<< HEAD` markers remain
  rationale: broken code reaching production wastes time, breaks services, and erodes trust
  
</gates>

<rules>

C++ STYLE:
  conv: snake_case variables/functions | PascalCase Classes | snake_case_ MemberVariables_
  header: prefer header-only (.h) for commands | .cpp for core logic
  guards: `#pragma once` only

DASHBOARD STYLE:
  tokens: use CSS variables in `:root` | alpha-based borders (`rgba(255,255,255,0.06)`)
  ux: dark mode only | glassmorphism on raised surfaces | no external fonts
  ref: [SITE_STYLE_GUIDE.md](file:///home/siqnole/Documents/code/bpp/site/SITE_STYLE_GUIDE.md)

EMOJI LOGIC:
  usage: scannable anchors (navigation/titles) | never for flavor/prose
  standard indicators: <:check:...> success | <:deny:...> error

</rules>

<rhythm>
build-bot: `./deploy_bot.sh` (natively cross-compiles via docker builder)
deploy-site: `./deploy_site.sh` (rsync to vps + npm install)
patch-notes: `./scripts/generate_patch_notes.py` -> `.patchadd` in discord
db-migration: update `database/schema.sql` + apply to vps mysql
</rhythm>

<conn label="VPS DEPLOYMENT (107.173.67.242)">
ssh-alias: `ssh vps` (config alias)
bot-service: `bpp-bot.service` (path: `/opt/bpp`)
site-service: `bpp-site.service` (path: `/opt/bpp/site`)
nginx: `bpp-nginx.conf` (port 80 -> 3000)
</conn>

<db label="DATABASE (MariaDB on VPS)">
host: `127.0.0.1` (on vps)
user: `bronxbot`
pass: `bronx2026_secure`
db: `bronxbot`
access: `ssh vps "mariadb -u bronxbot -pbronx2026_secure bronxbot"`
</db>


<ref label="CORE FILES">
- [commands/patch.h](file:///home/siqnole/Documents/code/bpp/commands/patch.h) → patch notes logic
- [database/core/database.h](file:///home/siqnole/Documents/code/bpp/database/core/database.h) → mysql connection pool
- [site/server.js](file:///home/siqnole/Documents/code/bpp/site/server.js) → dashboard entry
</ref>
