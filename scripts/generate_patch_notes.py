#!/usr/bin/env python3
import subprocess
import argparse
import sys
import re
from datetime import datetime

# Category Mapping
CATEGORIES = {
    "✨ Features": ["feat", "feature", "add", "implement", "new"],
    "🐛 Bug Fixes": ["fix", "bug", "patch", "resolved", "issue"],
    "⚡ Performance": ["perf", "optimize", "speed"],
    "🛠️ Refactors": ["refactor", "cleanup", "rename", "move", "structured"],
    "📚 Documentation": ["docs", "readme", "guide", "wiki"],
    "🪵 Internal": ["chore", "internal", "update", "bump", "build", "ci"]
}

def get_git_commits(since=None, count=10):
    """Fetch commits using git log."""
    cmd = ["git", "log", "--oneline", "--no-merges"]
    if since:
        cmd.append(f"--since={since}")
    cmd.append(f"-n {count}")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return result.stdout.strip().split("\n")
    except subprocess.CalledProcessError as e:
        print(f"Error fetching commits: {e}")
        return []

def categorize_commit(message):
    """Categorize a commit message based on keywords or prefixes."""
    msg_lower = message.lower()
    
    # Check for explicit prefixes like feat: or fix:
    prefix_match = re.match(r"^(\w+):", msg_lower)
    if prefix_match:
        prefix = prefix_match.group(1)
        for cat, keywords in CATEGORIES.items():
            if prefix in keywords:
                # Clean up prefix from message
                clean_msg = re.sub(r"^(\w+):\s*", "", message)
                return cat, clean_msg
    
    # Fallback to keyword matching
    for cat, keywords in CATEGORIES.items():
        if any(keyword in msg_lower for keyword in keywords):
            return cat, message
            
    return "📝 Other Changes", message

def generate_patch_notes(commits):
    """Group commits by category and format for .patchadd."""
    grouped = {}
    
    for line in commits:
        if not line:
            continue
        
        # Split hash and message
        parts = line.split(" ", 1)
        if len(parts) < 2:
            continue
        
        commit_hash, message = parts
        category, clean_message = categorize_commit(message)
        
        if category not in grouped:
            grouped[category] = []
        grouped[category].append(clean_message)
    
    # Build the final string
    output = []
    output.append("### 🚀 New Updates")
    
    # Sort categories to keep them consistent
    for cat in sorted(grouped.keys()):
        output.append(f"\n**{cat}**")
        for msg in grouped[cat]:
            # Capitalize first letter and add bullet
            formatted_msg = msg[0].upper() + msg[1:] if msg else msg
            output.append(f"• {formatted_msg}")
            
    return "\n".join(output)

def main():
    parser = argparse.ArgumentParser(description="Auto-generate patch notes from git commits.")
    parser.add_argument("--since", help="How far back to look (e.g. '24 hours ago', '7 days ago')")
    parser.add_argument("--count", type=int, default=15, help="Number of commits to include (default: 15)")
    parser.add_argument("--output", help="Save output to a specific file")
    
    args = parser.parse_args()
    
    commits = get_git_commits(since=args.since, count=args.count)
    if not commits or (len(commits) == 1 and not commits[0]):
        print("No commits found in the specified range.")
        return

    notes = generate_patch_notes(commits)
    
    if args.output:
        with open(args.output, "w") as f:
            f.write(notes)
        print(f"Patch notes saved to {args.output}")
    else:
        print("\n--- GENERATED PATCH NOTES ---\n")
        print(notes)
        print("\n-----------------------------\n")
        print("Tip: You can use `.patchadd <content>` with this output in Discord.")

if __name__ == "__main__":
    main()
