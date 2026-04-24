-- Initial patch note for leveling system
USE bronxbot;

INSERT INTO patch_notes (version, notes, author_id)
VALUES (
    '1.0.0',
    '**🎉 New Leveling System**

• **XP Tracking**: Earn XP from chatting in servers
• **Dual Leaderboards**: Global XP and per-server rankings
• **Level Roles**: Unlock roles as you level up
• **Coin Rewards**: Optionally earn coins with each message (configurable)
• **Admin Controls**: Fully customizable with `levelconfig` command
  - Set XP range per message
  - Configure cooldown between rewards
  - Set minimum message length
  - Toggle coin rewards on/off
  - Reset server XP (preserves global XP)
• **Role Rewards**: Admins can set up role rewards at specific levels with `levelroles`
• **Progress Tracking**: Use `/rank` to view your XP, level, and progress

**Commands:**
• `/rank` or `rank` - View your XP and level progress
• `levelconfig` - Admin command to configure leveling settings
• `levelroles` - Admin command to manage level-based role rewards
• `/leaderboard global-xp` - View global XP leaderboard
• `/leaderboard server-xp` - View server XP leaderboard',
    487343554251939842
);
