#!/usr/bin/env bash
# Simple wrapper to restart the bot whenever it exits unexpectedly.
# Place this script in the same directory as the built `discord-bot` binary.

# You can pass command‑line arguments to the bot through this script like:
#   ./run_discord_bot.sh --config /path/to/config.json

# Load environment variables from .env if it exists
ENV_FILE="$(dirname "$0")/../.env"
if [ -f "$ENV_FILE" ]; then
    set -a
    source "$ENV_FILE"
    set +a
    echo "[run_discord_bot] Loaded environment from $ENV_FILE"
fi

# Export BOT_TOKEN from DISCORD_TOKEN if not already set
if [ -z "$BOT_TOKEN" ] && [ -n "$DISCORD_TOKEN" ]; then
    export BOT_TOKEN="$DISCORD_TOKEN"
fi

# fail fast if the binary doesn't exist
if [ ! -x "../discord-bot" ]; then
    echo "Error: discord-bot not found or not executable in $(dirname "$0")/.."
    exit 1
fi

while true; do
    # launch the bot; forward all arguments
    ./discord-bot "$@"
    exit_code=$?

    if [ $exit_code -eq 0 ]; then
        echo "[run_discord_bot] bot exited normally (status 0)."
    else
        echo "[run_discord_bot] bot crashed or was killed (status $exit_code)."
    fi

    # small delay before restarting to avoid spinning
    sleep 1
    echo "[run_discord_bot] restarting..."
done
