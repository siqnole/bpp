

### Linux (Ubuntu/Debian)
```bash
# Install dependencies
sudo apt-get install cmake g++ git libssl-dev libopus-dev

# Clone and build DPP
git clone https://github.com/brainboxdotcc/DPP.git
cd DPP
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### macOS
```bash
# Install dependencies
brew install cmake opus openssl

# Clone and build DPP
git clone https://github.com/brainboxdotcc/DPP.git
cd DPP
mkdir build && cd build
cm
+ake ..
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### Windows
Download pre-built binaries from [DPP releases](https://github.com/brainboxdotcc/DPP/releases) or build from source using Visual Studio.

## Setup

1. Create a Discord bot at [Discord Developer Portal](https://discord.com/developers/applications)
2. Copy the bot token
3. Open `main.cpp` and replace `YOUR_BOT_TOKEN_HERE` with your actual bot token
4. Enable the "Message Content Intent" in the bot settings

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Running

```bash
./discord-bot
```

By default the bot will stop if it encounters a crash (segfault) or any
other error.  For development or deployment you can use the provided
wrapper script that will automatically relaunch it.

```bash
# make the supervisor executable once:
chmod +x run_discord_bot.sh
# start the bot through the script; arguments are forwarded
./run_discord_bot.sh --config /path/to/config.json
```

The script lives alongside the binary and loops forever, restarting
`discord-bot` with a one‑second delay whenever it exits with a non‑zero
status.  Feel free to hook it up to a systemd service, cron job, or
container entrypoint if you need a more persistent setup.

### Systemd unit

A sample `discord-bot.service` file is included at the project root.  It
simply invokes the wrapper script so that systemd will bring the process
back up automatically on failure.  Edit the paths and configuration to suit
your installation:

```ini
[Unit]
Description=Discord Bot (auto-restarting)
After=network.target

[Service]
WorkingDirectory=/path/to/your/repo/build
ExecStart=/path/to/your/repo/run_discord_bot.sh --config /path/to/config.json
Restart=always
RestartSec=5
#User=discordbot        # uncomment/modify as appropriate
#EnvironmentFile=/etc/default/discord-bot

[Install]
WantedBy=multi-user.target
```

Install it like any other unit:

```sh
sudo cp discord-bot.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now discord-bot.service
```

The service will log to `journalctl` and restart the bot after crashes or
unexpected exits.

## License

MIT
