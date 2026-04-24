#!/bin/bash
# Setup script to enable perf profiling without sudo

echo "🔧 Setting up perf permissions..."
echo "This requires sudo to adjust kernel settings."
echo ""

# Check current setting
current=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
echo "Current perf_event_paranoid: $current"

if [ "$current" != "-1" ]; then
    echo "Setting to -1 (allow all perf events for all users)..."
    sudo sysctl -w kernel.perf_event_paranoid=-1
    
    # Make it persistent across reboots
    if ! grep -q "kernel.perf_event_paranoid" /etc/sysctl.conf 2>/dev/null; then
        echo "Making change persistent..."
        echo "kernel.perf_event_paranoid = -1" | sudo tee -a /etc/sysctl.conf
    fi
    
    echo "[OK] Perf permissions configured!"
else
    echo "[OK] Perf permissions already configured!"
fi

echo ""
echo "You can now run: ./discord-bot -g"
