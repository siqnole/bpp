# Bronx Bot (BPP) Setup Guide

This guide documents the VPS environment and deployment lifecycle for the Bronx project.

## 🏗️ Project Architecture

- **Backend (Bot)**: C++17 using the [DPP library](https://dpp.dev/). High-performance, multi-threaded, and low-latency.
- **Frontend (Dashboard)**: Node.js (Express) with Vanilla JavaScript/CSS. No heavy frameworks (React/Vue/Tailwind) allowed.
- **Database**: MariaDB/MySQL with a persistent connection pool.
- **Hosting**: Direct VPS deployment via systemd services and Nginx reverse proxy.

## 🚀 Deployment Lifecycle

Everything is automated via shell scripts in the root directory.

### 1. Bot Deployment (`./deploy_bot.sh`)
- **Native Cross-Compilation**: Uses a local Docker builder (`Dockerfile.debian`) to compile against the VPS's Debian environment.
- **Live Patching**: Uploads the binary as a `.tmp` file and swaps it to avoid "Text bit busy" errors.
- **Service Management**: Restarts `bpp-bot.service`.

### 2. Dashboard Deployment (`./deploy_site.sh`)
- **Syncing**: Uses `rsync` to push code while excluding `node_modules` and `.env`.
- **Install**: Runs `npm install --production` on the remote host.
- **Service Management**: Restarts `bpp-site.service`.

## 🖥️ VPS & Server Configuration

- **Host**: `107.173.67.242`
- **Root Directory**: `/opt/bpp`
- **Services**:
    - `bpp-bot`: The main discord bot process. Logs available via `journalctl -u bpp-bot -f`.
    - `bpp-site`: The Node.js dashboard. Runs on port `3000`.
    - `nginx`: Manages the `bronxbot.xyz` domain and SSL (handled via `bpp-nginx.conf`).

## 🛠️ Developer Tooling

- **Patch Notes**: Use `./scripts/generate_patch_notes.py` to auto-generate release notes from recent commits.
- **Database**: Schema definitions are in `database/schema.sql`.

## 🎨 Design Philosophy

We follow a strict **lowercase, minimal, and soft** aesthetic.
- **Bot**: [STYLE_GUIDE.md](file:///home/siqnole/Documents/code/bpp/STYLE_GUIDE.md)
- **Web**: [SITE_STYLE_GUIDE.md](file:///home/siqnole/Documents/code/bpp/site/SITE_STYLE_GUIDE.md)

---

*For AI Assistants: See [CLAUDE.md](file:///home/siqnole/Documents/code/bpp/CLAUDE.md) for detailed structural rules and coding standards.*
