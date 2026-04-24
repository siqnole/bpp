# Discord Bot Privileged Intents Application

## Application Details

### What does your application do?

This is a multi-purpose Discord bot that provides economy, gaming, moderation, and utility features for Discord communities. The bot includes:

**Economy System:**
- Virtual currency (wallet/bank) with earning, spending, and trading capabilities
- Fishing mini-game with 40+ fish types and rarity tiers
- Shop system for purchasing items and bait
- Trading system between users
- Global and per-guild leaderboards

**Gaming Features:**
- Casino games (blackjack, roulette, slots, coinflip, dice, lottery)
- Interactive games with button/reaction-based controls
- Frogger mini-game
- Risk-based betting mechanics

**Moderation Tools:**
- Anti-spam detection with configurable thresholds
- Text filter with pattern matching and regex support
- URL/invite link filtering with whitelist/blacklist
- Reaction emoji filtering
- Automated logging to webhook channels
- Timeout/kick/ban enforcement options

**Utility Commands:**
- Server information display (member count, creation date, features)
- User information lookup (join date, roles, avatar)
- Custom reaction roles with persistent database storage
- Poll creation with emoji reactions
- Auto-purge channels with configurable message limits
- Custom prefix system (per-guild and per-user)
- User suggestion/feedback system

**Technical Features:**
- Dual command interface: slash commands AND text-based prefix commands
- Database-backed persistence (MySQL/MariaDB)
- Multi-server support (currently serving 120+ servers)
- Sharded architecture for scalability
- Owner-only administrative commands for bot management

**Screenshots/Examples:**
- Economy commands: [serverinfo showing member count]
- Moderation in action: [text filter, antispam triggers]
- Reaction roles: [users clicking reactions to get roles]
- Games: [blackjack interactive buttons, fishing results]

---

## Privileged Gateway Intents

**Intents Applying For:**
- ✅ Server Members Intent
- ✅ Message Content Intent
- ❌ Presence Intent (NOT needed)

---

## Server Members Intent

### Why do you need the Guild Members intent?

The Server Members Intent is essential for the following core functionality:

1. **User Information Commands** (`/userinfo`, `/whois`)
   - Fetches member join dates, roles, nicknames, and server-specific avatars
   - Uses `guild_get_member()` API calls to retrieve `dpp::guild_member` objects
   - Displays user information to server moderators and members

2. **Server Statistics** (`/serverinfo`)
   - Displays accurate server member count (`guild.member_count` property)
   - Shows server creation date, features, and member statistics
   - Required for bot list integrations (discordbotlist.com stats posting)

3. **Permission Checks for Moderation**
   - Validates that command users have required permissions (KICK_MEMBERS, BAN_MEMBERS, MODERATE_MEMBERS)
   - Uses `guild_member.roles` to check role-based access control
   - Ensures moderation commands can't be abused by unauthorized users

4. **Reaction Role System**
   - Fetches member objects to add/remove roles when users react to messages
   - Verifies role hierarchy before role assignment
   - Requires member data to avoid permission errors

5. **Economic Features**
   - Displays user economy profiles with server-specific display names
   - Shows leaderboard rankings with member information
   - Trading system verifies both parties are server members

### Screenshots/Videos demonstrating use case:

- **Userinfo command:** Shows member join date, roles, server nickname
  - File: `commands/utility/userinfo.h` (lines 32-60)
  - Live example: [Screenshot of /userinfo showing member details]

- **Serverinfo command:** Displays member_count property
  - File: `commands/utility/serverinfo.h` (line 23)
  - Live example: [Screenshot of /serverinfo showing "members: 1,234"]

- **Reaction roles:** Bot adds role to user after reaction
  - File: `commands/utility/reactionrole.h` (lines 469-495)
  - Live example: [Video of user clicking reaction and receiving role]

- **Moderation permission check:** Bot verifies user has BAN_MEMBERS permission
  - File: `commands/moderation_new.h.backup` (lines 133-137)
  - Live example: [Screenshot of permission denied message]

### Are you storing any potentially sensitive user information off-platform?

**No**

We only store:
- User IDs (public Discord snowflakes)
- Economy data (virtual currency balances, fishing inventory)
- Guild-specific settings (custom prefixes, enabled/disabled commands)
- Publicly submitted suggestions/feedback

We do NOT store:
- Email addresses
- IP addresses
- Private messages
- User discriminators or usernames (beyond display purposes)
- Any personally identifiable information

All data is stored in a self-hosted MySQL database with no third-party data sharing except:
- Bot list statistics (aggregated server count, member count) to discordbotlist.com
- Public Discord IDs in moderation logs (webhook embeds sent to Discord)

---

## Presence Intent

### Why do you need the Guild Presences intent?

**NOT APPLYING - We do not need this intent.**

The bot sets its own presence status (showing activity like "with 120 servers") but does NOT listen to or track user presence events. We have no functionality that requires knowing:
- User online/offline status
- User activity (playing games, listening to music)
- User status messages

Our codebase only uses `bot.set_presence()` to update the bot's own status, which does not require the Presence Intent.

---

## Message Content Intent

### Why do you need the Message Content intent?

The Message Content Intent is critical for the bot's core functionality:

1. **Text-Based Prefix Commands**
   - Bot responds to prefix commands like `!balance`, `!fish`, `!help`
   - Must read message.content to parse commands and arguments
   - Supports custom per-guild prefixes (e.g., `?`, `>`, custom strings)
   - File: `command_handler.h` (lines 97-160)

2. **Anti-Spam Detection**
   - Analyzes message content for spam patterns (repeated characters, excessive caps, mention spam)
   - Detects duplicate messages sent rapidly
   - Protects servers from raid attacks and spam bots
   - File: `commands/quiet_moderation/antispam.h`
   - Example: User sends 5 identical messages in 3 seconds → timeout applied

3. **Content Moderation - Text Filter**
   - Scans messages for blocked words/phrases configured by server admins
   - Supports regex pattern matching for advanced filtering
   - Deletes violating messages and logs to moderation channel
   - File: `commands/quiet_moderation/text_filter.h` (lines 16-52)

4. **URL/Invite Filtering**
   - Detects URLs and Discord invite links in messages
   - Prevents server raiding via unauthorized invite links
   - Supports domain whitelist/blacklist (e.g., allow youtube.com, block scam sites)
   - File: `commands/quiet_moderation/url_guard.h` (lines 32-100)

5. **Interactive Games and Commands**
   - Economy commands parse user input (e.g., `!pay @user 1000`)
   - Fishing commands read bait selection and action arguments
   - Poll commands extract poll question and options from message
   - Echo/utility commands that mirror or manipulate user text

6. **Command Context and User Intent**
   - Many commands require understanding message context (mentions, arguments)
   - Example: `!userinfo @someone` needs to parse the mention from content
   - Shop commands parse item names/quantities from text

### Screenshots/Videos demonstrating use case:

- **Prefix command parsing:**
  - File: `main_new.cpp` (line 381: `bot.on_message_create`)
  - Live example: [Screenshot of user typing "!fish" and bot responding]

- **Anti-spam in action:**
  - File: `commands/quiet_moderation/antispam_handler.cpp`
  - Live example: [Screenshot of spam message deleted with moderator log]

- **Text filter blocking:**
  - File: `commands/quiet_moderation/text_filter_handler.cpp`
  - Live example: [Video of message with blocked word being deleted instantly]

- **URL guard detecting invite:**
  - Live example: [Screenshot of unauthorized Discord invite link being removed]

### Can users opt-out of having their message content data tracked?

**No - opt-out is not possible**

**Rationale:**
- The bot provides server-wide moderation features that must apply to all users equally for security
- Allowing opt-out would create moderation loopholes (spammers could opt-out to bypass filters)
- Message content is only processed in real-time for command execution and moderation
- We do NOT store full message content in our database (only metadata for violation tracking)

**User Control:**
- Server administrators control which moderation features are enabled
- Users can choose not to use text-based prefix commands (slash commands work without Message Content Intent)
- Servers can disable the bot entirely or kick it if they don't want message scanning

### Are you storing message content data off-platform?

**No**

We do NOT store message content in our database. 

**What we store:**
- User IDs who violated anti-spam rules (for violation count tracking)
- Anonymized statistics (e.g., "5 messages deleted today")
- Moderation logs sent to Discord webhooks (which stay within Discord's platform)

**What we do NOT store:**
- Full message text
- Message history or archives
- User conversations
- Personal communications

**Message Content Usage:**
- Analyzed in real-time only (in-memory processing)
- Immediately discarded after command execution or moderation check
- Truncated previews (max 200 characters) MAY appear in webhook logs for moderator context, but these logs are sent to Discord channels (staying on-platform)

### Do you have a public Privacy Policy?

**No - not yet published**

Currently in draft stage. Will include:
- Data collection transparency (what IDs and economy data we store)
- No message content archival policy
- Database security practices
- User data deletion process
- Third-party disclosure limitations

*Will be published at [bot website URL] before bot verification completion.*

### Will message content data be used to train machine learning or AI models?

**No**

We do NOT use message content for:
- Machine learning training
- AI model development
- Data mining or analytics
- Third-party data sales
- Advertising or profiling

**Our ML-related features (if any):**
- The codebase contains `ml_settings` database tables, but these are for:
  - Dynamic pricing algorithms for in-bot economy (fish prices, shop items)
  - Mathematical optimization of game balance (not training on user data)
  - No natural language processing or user behavior modeling

---

## Summary of Data Practices

### Data We Collect:
- Discord User IDs (public snowflakes)
- Guild IDs (server identifiers)
- Economy balances (virtual currency)
- Command usage statistics
- Moderation violation counts
- User-submitted suggestions

### Data We Do NOT Collect:
- Message content (beyond real-time processing)
- Private/DM conversations
- Email addresses or personal info
- User presence/activity status
- IP addresses

### Data Retention:
- Economy data: Retained until user requests deletion
- Moderation logs: Sent to Discord webhooks (not stored by us)
- Command settings: Retained for guild customization

### User Rights:
- Data deletion available on request (contact bot owner)
- Server admins control bot permissions
- No data shared with third parties except aggregated statistics to bot lists

---

## Code References

**Main Bot Initialization:**
- File: `main_new.cpp` (line 120)
- Intents declared: `dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_members`

**Message Content Usage:**
- `commands/quiet_moderation/antispam_handler.cpp` - Anti-spam detection
- `commands/quiet_moderation/text_filter_handler.cpp` - Content filtering
- `commands/quiet_moderation/url_guard.h` - URL extraction and filtering
- `command_handler.h` - Prefix command parsing

**Guild Members Usage:**
- `commands/utility/userinfo.h` - Member information display
- `commands/utility/serverinfo.h` - Server statistics
- `commands/utility/reactionrole.h` - Role assignment based on reactions
- `commands/moderation_new.h.backup` - Permission verification

---

## Contact Information

**Bot Owner:** [Your Discord Username#0000]
**Support Server:** [Invite Link]
**GitHub Repository:** [https://github.com/yourusername/bot-repo] (if public)
**Email:** [your-email@example.com]

---

*This application was generated based on comprehensive codebase analysis. All claims are verifiable in the source code.*
