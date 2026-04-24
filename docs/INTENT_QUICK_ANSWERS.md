# Quick Answers for Discord Intent Application Form

Use these concise answers to copy-paste into the Discord application form.

---

## Application Details - What does your application do?

```
Multi-purpose Discord bot providing economy, gaming, moderation, and utility features:

ECONOMY: Virtual currency system with fishing mini-game (40+ fish types), shop, trading, and leaderboards
GAMING: Casino games (blackjack, roulette, slots, coinflip, dice, lottery) with interactive controls
MODERATION: Anti-spam detection, text/URL filtering, reaction filtering, automated logging and enforcement
UTILITY: Server/user info, reaction roles, polls, custom prefixes, auto-purge, suggestion system

Supports 120+ servers with dual command interface (slash commands + text prefixes), MySQL database persistence, and sharded architecture for scalability.
```

---

## Server Members Intent

### Why do you need the Guild Members intent?

```
Required for core functionality:

1. USER INFO COMMANDS (/userinfo, /whois) - Fetch member join dates, roles, nicknames using guild_get_member() API
2. SERVER STATISTICS (/serverinfo) - Display accurate member count via guild.member_count property
3. PERMISSION CHECKS - Validate moderation command users have required permissions (KICK/BAN/MODERATE_MEMBERS)
4. REACTION ROLES - Fetch member objects to add/remove roles when users react to messages
5. ECONOMY FEATURES - Display user profiles with server-specific names and verify trading partners

Code refs: commands/utility/userinfo.h (line 32), commands/utility/serverinfo.h (line 23), commands/utility/reactionrole.h (lines 469-495)
```

### Screenshots/videos

```
[Upload screenshots of:]
- /userinfo command showing member join date and roles
- /serverinfo showing member count
- Reaction role being assigned
- Permission check denying unauthorized user
```

### Storing sensitive user info off-platform?

```
No
```

---

## Presence Intent

### Why do you need the Guild Presences intent?

```
NOT APPLYING - Not needed. Bot only sets its own presence status, does not track user online/offline status or activity.
```

---

## Message Content Intent

### Why do you need the Message Content intent?

```
Critical for core features:

1. TEXT PREFIX COMMANDS - Parse commands like !balance, !fish, !help from message.content with custom prefixes
2. ANTI-SPAM DETECTION - Analyze patterns (repeated chars, caps, mention spam, duplicates) to protect from raids
3. CONTENT MODERATION - Scan for blocked words/phrases, regex patterns; delete violations; log to mod channels
4. URL/INVITE FILTERING - Detect Discord invites and URLs; prevent raiding via whitelist/blacklist system
5. INTERACTIVE COMMANDS - Parse user arguments (!pay @user 1000), mentions, item names for games/economy

All processing is real-time in-memory only. No message content stored in database.

Code refs: command_handler.h (line 97), commands/quiet_moderation/antispam.h, commands/quiet_moderation/text_filter.h, commands/quiet_moderation/url_guard.h
```

### Screenshots/videos

```
[Upload screenshots of:]
- User typing !fish and bot responding
- Spam messages being auto-deleted with mod log
- Text filter blocking inappropriate word
- Discord invite link being removed by URL guard
```

### Can users opt-out?

```
No
```

### Why not?

```
Moderation features must apply to all users equally for server security. Allowing opt-out would create loopholes for spammers/raiders. Server admins control which features are enabled. Users can use slash commands instead of text commands if preferred.
```

### Storing message content off-platform?

```
No
```

### Details:

```
We do NOT store message content. Only real-time in-memory processing for commands and moderation.

STORED: User IDs, violation counts, economy data, anonymized statistics
NOT STORED: Message text, history, archives, conversations

Moderation logs (truncated 200 char previews) sent to Discord webhooks stay within Discord platform.
```

### Public Privacy Policy?

```
No (currently in draft)
```

*If they require one, add:*
```
Will be published at [your-bot-website.com/privacy] before verification. Covers: data collection transparency, no message archival, database security, deletion process, no third-party sharing.
```

### ML/AI training?

```
No
```

### Explanation:

```
Message content is NOT used for machine learning or AI training. No data mining, analytics, advertising, or profiling.

"ml_settings" in codebase is for mathematical optimization of in-bot economy (fish prices, game balance), not NLP or user behavior modeling.
```

---

## Checklist Before Submitting

- [ ] Upload 3-4 screenshots demonstrating Server Members Intent usage
- [ ] Upload 3-4 screenshots demonstrating Message Content Intent usage
- [ ] Remove any sensitive tokens/IDs from screenshots
- [ ] Review all "No" answers are justified properly 
- [ ] Add contact info (support server invite, email)
- [ ] Ensure bot is in 75+ servers (current: 120+) ✅
- [ ] Have Privacy Policy ready (even if draft)

---

## Tips

1. **Be specific:** Reference actual code files and line numbers (shows legitimacy)
2. **Show screenshots:** Visual proof is more convincing than text
3. **Emphasize moderation:** Discord prioritizes safety/moderation use cases
4. **Explain "No" to opt-out:** Security argument is valid for moderation features
5. **Highlight real-time processing:** Emphasize you don't store message content permanently

---

## If Rejected

Common rejection reasons and how to address:

1. **"Insufficient use case"** → Add more screenshots showing actual usage
2. **"Data privacy concerns"** → Clarify you don't store message content, only process in-memory
3. **"Opt-out required"** → Explain why moderation features can't have opt-out (security)
4. **"Need Privacy Policy"** → Publish one immediately at a public URL
5. **"Too few servers"** → Wait until 100+ servers (you have 120, so OK)

Good luck!
