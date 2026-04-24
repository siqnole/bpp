#!/bin/bash
# Live Flamegraph Generator for Discord Bot
# This script profiles the bot process and generates interactive, live-updating flamegraphs

set -e

BOT_PID=$1
if [ -z "$BOT_PID" ]; then
    echo "Usage: $0 <bot_pid>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FLAMEGRAPH_DIR="/tmp/flamegraph_$$"
DATA_DIR="$FLAMEGRAPH_DIR/data"
WEB_DIR="$FLAMEGRAPH_DIR/web"

# Create directories
mkdir -p "$DATA_DIR" "$WEB_DIR"

echo "🔥 Flamegraph profiler starting..."
echo "   Bot PID: $BOT_PID"
echo "   Output: $FLAMEGRAPH_DIR"

# Check if FlameGraph tools are available
if [ ! -d "/opt/FlameGraph" ]; then
    echo "[WARN] FlameGraph tools not found. Installing..."
    sudo mkdir -p /opt
    sudo git clone https://github.com/brendangregg/FlameGraph /opt/FlameGraph 2>/dev/null || true
fi

# Download d3-flame-graph if not present
if [ ! -f "$WEB_DIR/d3-flamegraph.js" ]; then
    echo "📦 Downloading d3-flame-graph..."
    cd "$WEB_DIR"
    # Note: package name is "d3-flame-graph" (with hyphen)
    curl -sL "https://cdn.jsdelivr.net/npm/d3-flame-graph@4.1.3/dist/d3-flamegraph.js" -o d3-flamegraph.js
    curl -sL "https://cdn.jsdelivr.net/npm/d3-flame-graph@4.1.3/dist/d3-flamegraph.css" -o d3-flamegraph.css
    curl -sL "https://d3js.org/d3.v7.min.js" -o d3.v7.min.js
fi

# Create the HTML viewer
cat > "$WEB_DIR/index.html" <<'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>bronx</title>
    <link rel="stylesheet" href="d3-flamegraph.css">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        :root {
            --bg: #0a0a0a;
            --fg: #f5f5f5;
            --accent: #b4a7d6;
            --accent-muted: #a099bec7;
            --border: #1f1f1f;
            --card-bg: #111111;
        }
        
        body {
            margin: 0;
            padding: 20px;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg);
            color: var(--fg);
            line-height: 1.6;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 10px;
            margin-bottom: 20px;
            padding-bottom: 15px;
            border-bottom: 2px solid var(--accent);
        }
        .title {
            font-size: 1.6rem;
            font-weight: 900;
            background: linear-gradient(135deg, var(--accent) 0%, #60a5fa 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
            white-space: nowrap;
        }
        .stats {
            display: flex;
            gap: 16px;
            font-size: 13px;
            flex-wrap: wrap;
        }
        .stat {
            display: flex;
            flex-direction: column;
        }
        .stat-label {
            color: #94a3b8;
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .stat-value {
            font-size: 16px;
            font-weight: bold;
            color: var(--accent);
            white-space: nowrap;
        }
        #flamegraph {
            background: var(--card-bg);
            border: 1px solid var(--border);
            border-radius: 16px;
            overflow: hidden;
            padding: 10px;
        }
        .status {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 8px 16px;
            background: var(--accent);
            color: white;
            border-radius: 12px;
            font-size: 12px;
            font-weight: bold;
            animation: pulse 2s infinite;
            box-shadow: 0 4px 20px rgba(180, 167, 214, 0.3);
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.7; }
        }
        .controls {
            margin-bottom: 15px;
            display: flex;
            gap: 10px;
            align-items: center;
            flex-wrap: wrap;
        }
        button {
            padding: 10px 20px;
            background: var(--accent-muted);
            color: white;
            border: none;
            border-radius: 12px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
            box-shadow: 0 4px 20px rgba(180, 167, 214, 0.2);
        }
        button:hover {
            background: var(--accent);
            transform: translateY(-2px);
            box-shadow: 0 8px 30px rgba(180, 167, 214, 0.4);
        }
        button:disabled {
            background: #333;
            cursor: not-allowed;
            transform: none;
        }
        button.active {
            background: var(--accent);
            color: #000;
            box-shadow: 0 4px 20px rgba(180, 167, 214, 0.3);
        }
        .error {
            padding: 20px;
            background: rgba(244, 135, 113, 0.1);
            border: 1px solid rgba(244, 135, 113, 0.3);
            color: #f48771;
            border-radius: 12px;
            margin: 20px 0;
        }
        .legend {
            margin-top: 20px;
            padding: 20px;
            background: var(--card-bg);
            border: 1px solid var(--border);
            border-radius: 16px;
        }
        .legend-title {
            font-size: 14px;
            font-weight: bold;
            color: var(--accent);
            margin-bottom: 12px;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .legend-items {
            display: flex;
            gap: 20px;
            flex-wrap: wrap;
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .legend-color {
            width: 40px;
            height: 20px;
            border-radius: 4px;
            border: 1px solid var(--border);
        }
        .legend-label {
            font-size: 13px;
            color: #94a3b8;
        }
        .time-axis-container {
            background: var(--card-bg);
            border: 1px solid var(--border);
            border-top: none;
            border-radius: 0 0 16px 16px;
            padding: 0 10px 12px 10px;
            margin-top: -18px;
        }
        .time-axis-container svg {
            width: 100%;
            overflow: visible;
        }
        .time-axis-container .tick line {
            stroke: #333;
        }
        .time-axis-container .tick text {
            fill: #94a3b8;
            font-size: 11px;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
        }
        .time-axis-container .domain {
            stroke: var(--border);
        }
        .time-axis-label {
            fill: var(--accent);
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        ::-webkit-scrollbar {
            width: 10px;
            height: 10px;
        }
        ::-webkit-scrollbar-track {
            background: var(--bg);
        }
        ::-webkit-scrollbar-thumb {
            background: var(--border);
            border-radius: 5px;
        }
        ::-webkit-scrollbar-thumb:hover {
            background: var(--accent);
        }
        
        /* Override d3-flamegraph colors */
        .d3-flame-graph rect {
            stroke: var(--border) !important;
            stroke-width: 0.5px;
        }
        .d3-flame-graph text {
            fill: var(--fg) !important;
            font-size: 12px;
            pointer-events: none;
        }
        .d3-flame-graph-label {
            pointer-events: none;
            white-space: nowrap;
            text-overflow: ellipsis;
            overflow: hidden;
            max-width: 100%;
        }
        /* Improve readability of long function names */
        .d3-flame-graph text tspan {
            text-overflow: ellipsis;
            overflow: hidden;
        }
        /* Custom tooltip styling for d3-flamegraph built-in tooltip */
        .d3-flame-graph-tip {
            background: rgba(15, 15, 15, 0.95) !important;
            border: 1px solid var(--border) !important;
            border-radius: 8px !important;
            padding: 10px 14px !important;
            color: var(--fg) !important;
            font-size: 12px !important;
            line-height: 1.6 !important;
            max-width: 500px !important;
            box-shadow: 0 8px 32px rgba(0,0,0,0.6) !important;
            backdrop-filter: blur(10px);
            z-index: 10000 !important;
        }
        .tt-name {
            font-weight: 600;
            color: var(--accent);
            word-break: break-all;
            margin-bottom: 4px;
        }
        .tt-row {
            display: flex;
            justify-content: space-between;
            gap: 20px;
        }
        .tt-label {
            color: #94a3b8;
        }
        .tt-value {
            color: var(--fg);
            font-weight: 500;
            font-variant-numeric: tabular-nums;
        }
        /* Legacy tooltip element (hidden) */
        #flamegraph-tooltip {
            display: none;
        }
        /* Details bar (below controls) */
        #details {
            padding: 8px 16px;
            background: var(--card-bg);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 8px;
            font-size: 13px;
            color: #94a3b8;
            min-height: 20px;
        }
        /* Grouped node indicator */
        .grouped-indicator {
            fill: var(--accent) !important;
            font-size: 10px !important;
        }
    </style>
</head>
<body>
    <div class="status">● LIVE</div>
    
    <div class="header">
        <div class="title">bronx - live flamegraph</div>
        <div class="stats">
            <div class="stat">
                <span class="stat-label">last update</span>
                <span class="stat-value" id="lastUpdate">-</span>
            </div>
            <div class="stat">
                <span class="stat-label">samples</span>
                <span class="stat-value" id="sampleCount">-</span>
            </div>
            <div class="stat">
                <span class="stat-label">wall time</span>
                <span class="stat-value" id="wallTime">0s</span>
            </div>
            <div class="stat">
                <span class="stat-label">cpu time</span>
                <span class="stat-value" id="cpuTime">-</span>
            </div>
        </div>
    </div>
    
    <div class="controls">
        <button id="refreshBtn">Refresh Now</button>
        <button id="toggleAutoRefresh">Pause Auto-Refresh</button>
        <button id="resetZoom">Reset Zoom</button>
        <button id="toggleTimeMode">Show Wall Time</button>
    </div>
    
    <div id="errorMsg"></div>
    <div id="flamegraph-tooltip"></div>
    <div id="details"></div>
    <div id="flamegraph"></div>
    
    <div id="timeAxis" class="time-axis-container">
        <svg id="timeAxisSvg"></svg>
    </div>
    
    <div class="legend">
        <div class="legend-title">color legend</div>
        <div class="legend-items">
            <div class="legend-item">
                <div class="legend-color" style="background: #ffffb2;"></div>
                <span class="legend-label">Low CPU usage (cool)</span>
            </div>
            <div class="legend-item">
                <div class="legend-color" style="background: #fecc5c;"></div>
                <span class="legend-label">Moderate CPU usage</span>
            </div>
            <div class="legend-item">
                <div class="legend-color" style="background: #fd8d3c;"></div>
                <span class="legend-label">High CPU usage</span>
            </div>
            <div class="legend-item">
                <div class="legend-color" style="background: #e31a1c;"></div>
                <span class="legend-label">Very high CPU usage (hot)</span>
            </div>
        </div>
    </div>
    
    <script src="d3.v7.min.js"></script>
    <script src="d3-flamegraph.js"></script>
    <script>
        // Check if libraries loaded
        if (typeof d3 === 'undefined') {
            document.getElementById('errorMsg').innerHTML = 
                '<div class="error">[FAIL] Error: d3.js failed to load</div>';
            throw new Error('d3.js not loaded');
        }
        
        if (typeof flamegraph === 'undefined') {
            document.getElementById('errorMsg').innerHTML = 
                '<div class="error">[FAIL] Error: d3-flamegraph.js failed to load</div>';
            throw new Error('d3-flamegraph not loaded');
        }
        
        let autoRefresh = true;
        let refreshTimer = null;
        let uptimeTimer = null;
        const REFRESH_INTERVAL = 3000;
        const startTime = Date.now();
        let sampleFreq = 997; // Default, will be updated from metadata
        let totalSamples = 0;
        let currentZoomSamples = 0; // Samples in the currently zoomed view
        let timeMode = 'cpu'; // 'cpu' or 'wall'
        let rootSamples = 0; // Total root samples for percentage calc
        const expandedGroups = new Set(); // Track which groups are expanded
        
        // Custom tooltip using DOM event delegation
        const tooltipEl = document.getElementById('flamegraph-tooltip');
        tooltipEl.style.cssText = 'position:fixed;pointer-events:none;background:rgba(15,15,15,0.95);border:1px solid var(--border);border-radius:8px;padding:10px 14px;color:var(--fg);font-size:12px;line-height:1.6;z-index:10000;max-width:500px;box-shadow:0 8px 32px rgba(0,0,0,0.6);backdrop-filter:blur(10px);display:none;';
        
        document.getElementById('flamegraph').addEventListener('mouseover', function(e) {
            // Walk up from target to find the g.d3-flame-graph-frame element
            let target = e.target;
            while (target && target !== this) {
                if (target.__data__ && target.__data__.data) break;
                if (target.tagName === 'g' && target.__data__) break;
                target = target.parentElement;
            }
            if (!target || !target.__data__) return;
            const d = target.__data__;
            const name = (d.data && d.data.name) || '(unknown)';
            const samples = (d.data && d.data.value) || d.value || 0;
            const pct = rootSamples > 0 ? ((samples / rootSamples) * 100) : 0;
            const cpuTime = sampleFreq > 0 ? samples / sampleFreq : 0;
            const wallTime = rootSamples > 0 
                ? getWallTimeSec() * (samples / rootSamples) 
                : 0;
            const displayTime = timeMode === 'wall' ? wallTime : cpuTime;
            const timeLabel = timeMode === 'wall' ? 'Wall Time' : 'CPU Time';
            
            let html = '<div class="tt-name">' + name.replace(/</g, '&lt;').replace(/>/g, '&gt;') + '</div>';
            html += '<div class="tt-row"><span class="tt-label">Samples</span><span class="tt-value">' + formatNumber(samples) + '</span></div>';
            html += '<div class="tt-row"><span class="tt-label">Percentage</span><span class="tt-value">' + pct.toFixed(2) + '%</span></div>';
            html += '<div class="tt-row"><span class="tt-label">' + timeLabel + '</span><span class="tt-value">' + formatTime(displayTime) + '</span></div>';
            if (d.data && d.data._grouped) {
                html += '<div class="tt-row"><span class="tt-label">Grouped</span><span class="tt-value">' + d.data._groupCount + ' functions (click to expand)</span></div>';
            }
            tooltipEl.innerHTML = html;
            tooltipEl.style.display = 'block';
            const x = e.pageX + 15;
            const y = e.pageY - 10;
            tooltipEl.style.left = Math.min(x, window.innerWidth - 520) + 'px';
            tooltipEl.style.top = Math.min(y, window.innerHeight - 150) + 'px';
        });
        
        document.getElementById('flamegraph').addEventListener('mousemove', function(e) {
            if (tooltipEl.style.display === 'block') {
                const x = e.pageX + 15;
                const y = e.pageY - 10;
                tooltipEl.style.left = Math.min(x, window.innerWidth - 520) + 'px';
                tooltipEl.style.top = Math.min(y, window.innerHeight - 150) + 'px';
            }
        });
        
        document.getElementById('flamegraph').addEventListener('mouseout', function(e) {
            // Only hide if we're actually leaving the flamegraph
            if (!this.contains(e.relatedTarget)) {
                tooltipEl.style.display = 'none';
            }
        });
        
        // Group sibling nodes with the same sample count
        function groupSiblings(node) {
            if (!node.children || node.children.length <= 1) return node;
            
            // First, recursively process all children
            node.children = node.children.map(child => groupSiblings(child));
            
            // Group children with identical values
            const byValue = {};
            const kept = [];
            
            node.children.forEach(child => {
                // Only group leaf-like nodes or those with same value
                const key = (child.value || 0).toString();
                if (!byValue[key]) byValue[key] = [];
                byValue[key].push(child);
            });
            
            for (const [val, siblings] of Object.entries(byValue)) {
                if (siblings.length >= 3) {
                    // Create a group ID for tracking expansion
                    const groupId = node.name + '::group_v' + val + '_n' + siblings.length;
                    
                    if (expandedGroups.has(groupId)) {
                        // User expanded this group - show individual items
                        siblings.forEach(s => {
                            s._wasGrouped = true;
                            kept.push(s);
                        });
                    } else {
                        // Merge into a group node
                        const totalValue = siblings.reduce((sum, s) => sum + (s.value || 0), 0);
                        const names = siblings.map(s => s.name);
                        const groupNode = {
                            name: siblings.length + ' functions (' + formatNumber(parseInt(val)) + ' samples each)',
                            value: totalValue,
                            _grouped: true,
                            _groupId: groupId,
                            _groupCount: siblings.length,
                            _groupNames: names,
                            children: siblings.slice(0, 3).map(s => ({
                                name: s.name,
                                value: s.value,
                                children: s.children
                            }))
                        };
                        // Show first 3 as preview children
                        kept.push(groupNode);
                    }
                } else {
                    siblings.forEach(s => kept.push(s));
                }
            }
            
            node.children = kept;
            return node;
        }
        
        // Format time intelligently
        function formatTime(seconds, short) {
            if (seconds >= 3600) {
                return short ? (seconds / 3600).toFixed(1) + 'h' : (seconds / 3600).toFixed(2) + 'h';
            } else if (seconds >= 60) {
                return short ? Math.floor(seconds / 60) + 'm ' + Math.floor(seconds % 60) + 's' : (seconds / 60).toFixed(2) + 'm';
            } else if (seconds >= 1) {
                return short ? seconds.toFixed(1) + 's' : seconds.toFixed(3) + 's';
            } else if (seconds >= 1e-3) {
                return short ? (seconds * 1e3).toFixed(0) + 'ms' : (seconds * 1e3).toFixed(2) + 'ms';
            } else if (seconds >= 1e-6) {
                return short ? (seconds * 1e6).toFixed(0) + '\u00b5s' : (seconds * 1e6).toFixed(2) + '\u00b5s';
            } else if (seconds >= 1e-9) {
                return short ? (seconds * 1e9).toFixed(0) + 'ns' : (seconds * 1e9).toFixed(2) + 'ns';
            } else {
                return short ? (seconds * 1e12).toFixed(0) + 'ps' : (seconds * 1e12).toFixed(2) + 'ps';
            }
        }
        
        // Time axis with d3
        const timeAxisSvg = d3.select('#timeAxisSvg');
        let timeScale = d3.scaleLinear();
        let timeAxisG = null;
        
        function getWallTimeSec() {
            return (Date.now() - startTime) / 1000;
        }

        function updateTimeAxis(samples, freq) {
            const containerWidth = document.getElementById('flamegraph').offsetWidth - 20;
            const cpuTimeSec = samples / freq;
            // Wall time proportional to sample ratio
            const wallTimeSec = rootSamples > 0 
                ? getWallTimeSec() * (samples / rootSamples) 
                : getWallTimeSec();
            const displayTimeSec = timeMode === 'wall' ? wallTimeSec : cpuTimeSec;
            const displayTimeMs = displayTimeSec * 1000;
            const modeLabel = timeMode === 'wall' ? 'wall' : 'cpu';
            
            timeAxisSvg.attr('width', containerWidth).attr('height', 52);
            timeAxisSvg.selectAll('*').remove();
            
            // Determine appropriate time unit and label
            let unit, divisor, unitLabel;
            if (displayTimeSec >= 3600) {
                unit = 'h'; divisor = 3600000; unitLabel = 'hours';
            } else if (displayTimeSec >= 60) {
                unit = 'm'; divisor = 60000; unitLabel = 'minutes';
            } else if (displayTimeSec >= 1) {
                unit = 's'; divisor = 1000; unitLabel = 'seconds';
            } else if (displayTimeSec >= 1e-3) {
                unit = 'ms'; divisor = 1; unitLabel = 'milliseconds';
            } else if (displayTimeSec >= 1e-6) {
                unit = '\u00b5s'; divisor = 0.001; unitLabel = 'microseconds';
            } else if (displayTimeSec >= 1e-9) {
                unit = 'ns'; divisor = 0.000001; unitLabel = 'nanoseconds';
            } else {
                unit = 'ps'; divisor = 0.000000001; unitLabel = 'picoseconds';
            }
            
            timeScale = d3.scaleLinear()
                .domain([0, displayTimeMs / divisor])
                .range([0, containerWidth - 60]);
            
            const tickCount = Math.min(Math.max(Math.floor(containerWidth / 100), 4), 12);
            
            const axis = d3.axisBottom(timeScale)
                .ticks(tickCount)
                .tickFormat(d => d === 0 ? '0' : d3.format('.3~g')(d) + unit);
            
            timeAxisG = timeAxisSvg.append('g')
                .attr('transform', 'translate(30, 2)')
                .call(axis);
            
            // Total time label centered below axis
            const totalLabel = 'total ' + modeLabel + ' time: ' + formatTime(displayTimeSec);
            timeAxisSvg.append('text')
                .attr('class', 'time-axis-label')
                .attr('x', containerWidth / 2)
                .attr('y', 42)
                .attr('text-anchor', 'middle')
                .text(totalLabel);

            // Unit label on the right edge
            timeAxisSvg.append('text')
                .attr('fill', '#555')
                .attr('font-size', '10px')
                .attr('x', containerWidth - 10)
                .attr('y', 16)
                .attr('text-anchor', 'end')
                .text(unitLabel);
        }
        
        // Format large numbers with M, B, T suffixes
        function formatNumber(num) {
            if (num >= 1e12) {
                return (num / 1e12).toFixed(2) + 'T';
            } else if (num >= 1e9) {
                return (num / 1e9).toFixed(2) + 'B';
            } else if (num >= 1e6) {
                return (num / 1e6).toFixed(2) + 'M';
            } else if (num >= 1000) {
                return (num / 1000).toFixed(1) + 'K';
            }
            return num.toString();
        }
        
        // Live-update wall time and cpu time stats every second
        function updateTimeStats() {
            // Wall time always ticks from real elapsed time
            document.getElementById('wallTime').textContent = formatTime(getWallTimeSec(), true);
            // CPU time updates from last known sample data
            if (totalSamples > 0) {
                document.getElementById('cpuTime').textContent = formatTime(totalSamples / sampleFreq, true);
            }
        }
        
        const chart = flamegraph()
            .width(window.innerWidth - 40)
            .cellHeight(18)
            .transitionDuration(750)
            .minFrameSize(5)
            .transitionEase(d3.easeCubic)
            .sort(true)
            .title("")
            .selfValue(false)
            .setDetailsElement(document.getElementById('details'))
            .onClick(function(d) {
                // Check if this is a grouped node - expand it
                if (d && d.data && d.data._grouped && d.data._groupId) {
                    expandedGroups.add(d.data._groupId);
                    // Force a reload to re-process groups
                    loadFlamegraph();
                    return;
                }
                // Track if user has zoomed away from root
                if (d && d.parent) {
                    isZoomed = true;
                    zoomHistory.push(d);
                    // Update time axis for zoomed view
                    currentZoomSamples = d.data.value || d.value || 0;
                    updateTimeAxis(currentZoomSamples, sampleFreq);
                } else {
                    isZoomed = false;
                    zoomHistory = [];
                    // Reset time axis to full view
                    currentZoomSamples = totalSamples;
                    updateTimeAxis(totalSamples, sampleFreq);
                }
            })
            .label(function(d) {
                // Format long function names for better display
                const name = d.data.name;
                const maxLen = Math.floor(d.x1 - d.x0); // Available width in pixels
                if (name.length * 7 > maxLen) {
                    // Truncate long names intelligently
                    const visibleChars = Math.floor(maxLen / 7);
                    if (visibleChars < 4) return '';
                    // Try to show important parts (function name, not full path)
                    const parts = name.split('::');
                    const lastPart = parts[parts.length - 1];
                    if (lastPart.length <= visibleChars) {
                        return lastPart;
                    }
                    return name.substring(0, visibleChars - 3) + '...';
                }
                return name;
            });
        
        const container = d3.select("#flamegraph");
        let isFirstLoad = true;
        let isZoomed = false; // Track if user has zoomed
        let zoomHistory = []; // Track zoom history for back navigation
        
        async function loadFlamegraph() {
            try {
                const response = await fetch('data.json?t=' + Date.now());
                if (!response.ok) {
                    // 404 is expected before the first profiling cycle completes
                    if (response.status === 404) return;
                    throw new Error('Failed to fetch data.json: ' + response.status);
                }
                // Parse JSON text manually to catch truncation
                const text = await response.text();
                let raw;
                try {
                    raw = JSON.parse(text);
                } catch (parseErr) {
                    // Truncated JSON - skip this update silently
                    console.warn('Skipping truncated JSON update');
                    return;
                }
                
                // Handle metadata envelope: { meta: {...}, data: {...} }
                let data, meta;
                if (raw.meta && raw.data) {
                    meta = raw.meta;
                    data = raw.data;
                    sampleFreq = meta.freq || 997;
                    totalSamples = meta.total_samples || 0;
                } else {
                    // Legacy format (no envelope)
                    data = raw;
                }
                
                // Skip updates if user is zoomed in to avoid disruption
                if (isZoomed && !isFirstLoad) {
                    // Just update stats, don't redraw the graph
                    document.getElementById('lastUpdate').textContent = 
                        new Date().toLocaleTimeString();
                    
                    rootSamples = totalSamples;
                    document.getElementById('sampleCount').textContent = formatNumber(totalSamples);
                    return; // Don't update the graph while zoomed
                }
                
                // Update with no transition to avoid jarring animation during auto-refresh
                if (!isFirstLoad) {
                    chart.transitionDuration(0);
                }
                
                // Use root node value for flamegraph rendering, meta.total_samples for stats
                rootSamples = data.value || 0;
                
                // Apply grouping to reduce visual clutter
                const groupedData = groupSiblings(JSON.parse(JSON.stringify(data)));
                
                // Guard against empty/invalid data that causes d3-flamegraph to crash
                if (!groupedData || (!groupedData.value && (!groupedData.children || groupedData.children.length === 0))) {
                    return;
                }
                
                try {
                    container.datum(groupedData).call(chart);
                } catch (renderErr) {
                    console.warn('Chart render error (skipping):', renderErr.message);
                    return;
                }
                
                // Re-enable transitions for user interactions
                if (!isFirstLoad) {
                    chart.transitionDuration(750);
                } else {
                    isFirstLoad = false;
                }
                
                // Update stats
                document.getElementById('lastUpdate').textContent = 
                    new Date().toLocaleTimeString();
                
                document.getElementById('sampleCount').textContent = formatNumber(totalSamples);
                
                // Update time axis for full view
                if (!isZoomed) {
                    currentZoomSamples = totalSamples;
                    updateTimeAxis(totalSamples, sampleFreq);
                }
                
                // Clear any error messages
                document.getElementById('errorMsg').innerHTML = '';
                    
            } catch (error) {
                console.error('Failed to load flamegraph:', error);
                document.getElementById('errorMsg').innerHTML = 
                    '<div class="error">[WARN] ' + error.message + '</div>';
            }
        }
        
        function startAutoRefresh() {
            if (refreshTimer) clearInterval(refreshTimer);
            refreshTimer = setInterval(loadFlamegraph, REFRESH_INTERVAL);
        }
        
        function stopAutoRefresh() {
            if (refreshTimer) {
                clearInterval(refreshTimer);
                refreshTimer = null;
            }
        }
        
        document.getElementById('refreshBtn').addEventListener('click', loadFlamegraph);
        
        document.getElementById('toggleAutoRefresh').addEventListener('click', (e) => {
            autoRefresh = !autoRefresh;
            if (autoRefresh) {
                e.target.textContent = 'Pause Auto-Refresh';
                startAutoRefresh();
            } else {
                e.target.textContent = 'Resume Auto-Refresh';
                stopAutoRefresh();
            }
        });
        
        document.getElementById('resetZoom').addEventListener('click', () => {
            chart.resetZoom();
            isZoomed = false;
            isFirstLoad = true;
            currentZoomSamples = totalSamples;
            updateTimeAxis(totalSamples, sampleFreq);
        });
        
        document.getElementById('toggleTimeMode').addEventListener('click', (e) => {
            timeMode = timeMode === 'cpu' ? 'wall' : 'cpu';
            e.target.textContent = timeMode === 'cpu' ? 'Show Wall Time' : 'Show CPU Time';
            e.target.classList.toggle('active', timeMode === 'wall');
            // Redraw axis
            const samples = isZoomed ? currentZoomSamples : totalSamples;
            if (samples > 0) {
                updateTimeAxis(samples, sampleFreq);
            }
        });
        
        window.addEventListener('resize', () => {
            chart.width(window.innerWidth - 40);
            if (container.datum()) {
                container.call(chart);
            }
            // Redraw time axis on resize
            const samples = isZoomed ? currentZoomSamples : totalSamples;
            if (samples > 0) {
                updateTimeAxis(samples, sampleFreq);
            }
        });
        
        // Add right-click to zoom back to root
        document.getElementById('flamegraph').addEventListener('contextmenu', (e) => {
            e.preventDefault();
            if (isZoomed) {
                chart.resetZoom();
                isZoomed = false;
                zoomHistory = [];
                isFirstLoad = true;
                currentZoomSamples = totalSamples;
                updateTimeAxis(totalSamples, sampleFreq);
            }
            return false;
        });
        
        // Initial load and start auto-refresh
        loadFlamegraph();
        startAutoRefresh();
        
        // Start live time stats
        updateTimeStats();
        uptimeTimer = setInterval(updateTimeStats, 1000);
    </script>
</body>
</html>
EOF

echo "✔ Created web interface"

# Function to generate flamegraph from perf data
generate_flamegraph() {
    local perf_file="$DATA_DIR/perf.data"
    local folded_file="$DATA_DIR/out.folded"
    local cumulative_file="$DATA_DIR/cumulative.folded"
    local json_file="$WEB_DIR/data.json"
    
    # Generate the folded stack file from current perf data
    # --inline: resolve inlined functions for machine-level depth
    perf script --inline -i "$perf_file" 2>/dev/null | \
        /opt/FlameGraph/stackcollapse-perf.pl > "$folded_file" 2>/dev/null || return 1
    
    # Append to cumulative file
    cat "$folded_file" >> "$cumulative_file"
    
    # Merge duplicate stacks in cumulative file
    awk '
    {
        split($0, parts, " ")
        count = parts[length(parts)]
        delete parts[length(parts)]
        stack = ""
        for (i in parts) stack = stack parts[i] " "
        sub(/ $/, "", stack)
        stacks[stack] += count
    }
    END {
        for (stack in stacks) {
            print stack " " stacks[stack]
        }
    }
    ' "$cumulative_file" > "$cumulative_file.tmp" && mv "$cumulative_file.tmp" "$cumulative_file"
    
    # Convert to JSON format for d3-flamegraph
    python3 <<PYEOF
import json
import sys

def parse_folded(filename):
    stacks = {}
    with open(filename, 'r') as f:
        for line in f:
            if not line.strip():
                continue
            parts = line.rsplit(' ', 1)
            if len(parts) != 2:
                continue
            stack, count = parts
            count = int(count)
            frames = stack.split(';')
            # Build tree
            current = stacks
            for frame in frames:
                if frame not in current:
                    current[frame] = {'value': 0, 'children': {}}
                current[frame]['value'] += count
                current = current[frame]['children']
    return stacks

def tree_to_json(tree, name='all'):
    children = []
    value = 0
    for key, data in tree.items():
        child_json = tree_to_json(data['children'], key)
        child_json['value'] = data['value']
        children.append(child_json)
        value += data['value']
    
    result = {'name': name}
    if children:
        result['children'] = children
    if value > 0:
        result['value'] = value
    return result

try:
    # Compute total samples directly from the folded file (sum of all line counts)
    actual_total = 0
    with open('$cumulative_file', 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.rsplit(' ', 1)
            if len(parts) == 2:
                try:
                    actual_total += int(parts[1])
                except ValueError:
                    pass

    tree = parse_folded('$cumulative_file')
    json_data = tree_to_json(tree, 'bronx')
    # Wrap in metadata envelope with profiling info
    # freq=997 Hz, so each sample = ~1.003ms of CPU time
    output = {
        'meta': {
            'freq': 997,
            'total_samples': actual_total,
        },
        'data': json_data
    }
    with open('$json_file.tmp', 'w') as f:
        json.dump(output, f)
    import os
    os.replace('$json_file.tmp', '$json_file')
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
PYEOF
}

# Start simple HTTP server in background
cd "$WEB_DIR"
python3 -m http.server 8899 > /dev/null 2>&1 &
HTTP_SERVER_PID=$!
echo "✔ Web server started on http://localhost:8899 (PID: $HTTP_SERVER_PID)"

# Cleanup function
cleanup() {
    echo ""
    echo "🛑 Stopping profiler..."
    pkill -P $$ perf 2>/dev/null || true
    kill $HTTP_SERVER_PID 2>/dev/null || true
    echo "✔ Cleanup complete"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

# Main profiling loop
echo "🔥 Starting continuous profiling..."
echo "   Press Ctrl+C to stop"
echo ""

ITERATION=0
while true; do
    ITERATION=$((ITERATION + 1))
    
    # Check if bot process is still running
    if ! kill -0 $BOT_PID 2>/dev/null; then
        echo "[WARN] Bot process (PID $BOT_PID) has terminated"
        break
    fi
    
    # Record performance data for 3 seconds
    # --call-graph dwarf: DWARF unwinding for deep stack traces including inlined functions
    # -F 997: ~1000 samples/sec (prime to avoid aliasing with periodic work)
    # -g: enable call-graph recording
    perf record -F 997 -p $BOT_PID --call-graph dwarf,32768 -o "$DATA_DIR/perf.data" -- sleep 3 2>/dev/null || {
        echo "[WARN] perf record failed (may need: sudo sysctl -w kernel.perf_event_paranoid=-1)"
        sleep 3
        continue
    }
    
    # Generate flamegraph
    if generate_flamegraph; then
        echo "[$(date '+%H:%M:%S')] ✔ Flamegraph updated (#$ITERATION)"
    else
        echo "[$(date '+%H:%M:%S')] [WARN] Failed to generate flamegraph"
    fi
    
    # Brief pause before next iteration
    sleep 0.5
done

echo "🏁 Profiling complete"
