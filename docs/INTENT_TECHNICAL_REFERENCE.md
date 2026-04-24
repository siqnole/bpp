# Intent Usage - Technical Code Reference

This document maps Discord Privileged Intents to specific code locations in the bot.

---

## Intent Declaration

**File:** `main_new.cpp`  
**Line:** 120

```cpp
dpp::cluster bot(BOT_TOKEN, 
                 dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_members, 
                 shard_count,
                 0, 1, false,
                 dpp::cache_policy::cpol_default);
```

**Intents Used:**
- `dpp::i_default_intents` - Standard gateway intents (guilds, guild messages, reactions, etc.)
- `dpp::i_message_content` - **PRIVILEGED** - Access to message.content field
- `dpp::i_guild_members` - **PRIVILEGED** - Access to member join/leave events and member list

---

## Message Content Intent Usage

### 1. Main Message Handler
**File:** `main_new.cpp`  
**Line:** 381

```cpp
bot.on_message_create([&bot, &cmd_handler](const dpp::message_create_t& event) {
    cmd_handler.handle_message(bot, event);
});
```

### 2. Command Parsing
**File:** `command_handler.h`  
**Lines:** 97-160

```cpp
void handle_message(dpp::cluster& bot, const dpp::message_create_t& event) {
    if (event.msg.author.is_bot()) return;
    
    std::string content = event.msg.content;  // ← REQUIRES MESSAGE_CONTENT INTENT
    
    // Prefix matching
    for (const auto& pfx : prefixes) {
        if (content.find(pfx) == 0) {
            std::string cmd_part = content.substr(pfx.length());
            // ... parse command and args
        }
    }
}
```

### 3. Anti-Spam Detection
**File:** `commands/quiet_moderation/antispam_handler.cpp`  
**Function:** `register_antispam()`

Analyzes message content for:
- Duplicate message spam
- Excessive caps (`SHOUTING LIKE THIS`)
- Mention spam (`@user @user @user`)
- Character spam (`aaaaaaaaaaaa`)
- Link spam

**Key Code:**
```cpp
bot.on_message_create([](const dpp::message_create_t& event) {
    std::string content = event.msg.content;  // ← REQUIRES MESSAGE_CONTENT
    
    // Check for caps spam
    int caps_count = std::count_if(content.begin(), content.end(), ::isupper);
    float caps_ratio = (float)caps_count / content.length();
    
    // Check for duplicate messages
    if (recent_messages[user_id] == content) {
        // Spam detected
    }
});
```

### 4. Text Filter (Blocked Words)
**File:** `commands/quiet_moderation/text_filter_handler.cpp`  
**File:** `commands/quiet_moderation/text_filter.h` (lines 16-52)

```cpp
bot.on_message_create([](const dpp::message_create_t& event) {
    std::string content = event.msg.content;  // ← REQUIRES MESSAGE_CONTENT
    std::string lower = to_lowercase(content);
    
    // Check blocked words
    for (const auto& word : config.blocked_words) {
        if (lower.find(word) != std::string::npos) {
            bot.message_delete(event.msg.id, event.msg.channel_id);
        }
    }
    
    // Check regex patterns
    for (const auto& pattern : config.blocked_patterns) {
        std::regex rgx(pattern);
        if (std::regex_search(content, rgx)) {
            // Delete message
        }
    }
});
```

### 5. URL/Invite Guard
**File:** `commands/quiet_moderation/url_guard.h` (lines 32-100)

```cpp
std::vector<std::string> extract_urls(const std::string& content) {  // ← REQUIRES MESSAGE_CONTENT
    std::vector<std::string> urls;
    std::regex url_regex(R"((https?://[^\s<>\[\]]+))");
    std::smatch match;
    std::string text = content;
    
    while (std::regex_search(text, match, url_regex)) {
        urls.push_back(match[1].str());
        text = match.suffix();
    }
    return urls;
}

bool is_discord_invite(const std::string& url) {
    return (url.find("discord.gg/") != std::string::npos ||
            url.find("discord.com/invite/") != std::string::npos);
}
```

### 6. Message Update Handler
**File:** `main_new.cpp`  
**Line:** 386

```cpp
bot.on_message_update([&bot, &cmd_handler](const dpp::message_update_t& event) {
    cmd_handler.handle_message_edit(bot, event);
});
```

Prevents users from bypassing filters by editing messages after posting.

### 7. Economy Command Parsing
**File:** `commands/economy_core.h` (lines 390-450)

```cpp
// Pay command example
if (args.size() < 2) {
    return; // Need mention and amount
}

// Parse amount from message content
int64_t amount = parse_amount(args[1], user->wallet);  // ← args from message.content
```

### 8. Utility Commands
**File:** `commands/utility/poll.h`

```cpp
// Poll creation from text
// !poll "Question here?" "Option 1" "Option 2" "Option 3"
// Must parse quoted strings from message.content
```

---

## Guild Members Intent Usage

### 1. User Info Command
**File:** `commands/utility/userinfo.h`  
**Lines:** 32-60

```cpp
bot.guild_get_member(event.msg.guild_id, user_id, 
    [](const dpp::confirmation_callback_t& callback) {  // ← REQUIRES GUILD_MEMBERS INTENT
        
    auto member = std::get<dpp::guild_member>(callback.value);
    
    // Access member-specific data
    std::string nickname = member.get_nickname();
    int64_t joined_at = member.joined_at;
    std::vector<dpp::snowflake> roles = member.roles;
    std::string guild_avatar = member.get_avatar_url(256);
});
```

### 2. Server Info Command
**File:** `commands/utility/serverinfo.h`  
**Lines:** 23, 97

```cpp
dpp::guild* guild = dpp::find_guild(event.msg.guild_id);
if (guild) {
    uint32_t member_count = guild->member_count;  // ← REQUIRES GUILD_MEMBERS INTENT
    
    description += "**members:** " + std::to_string(member_count) + "\n";
}
```

**Also used for bot list stats:**
**File:** `main_new.cpp`  
**Line:** 268

```cpp
for (auto gid : event.guilds) {
    dpp::guild* g = dpp::find_guild(gid);
    if (g) {
        users += g->member_count;  // ← REQUIRES GUILD_MEMBERS INTENT
    }
}
```

### 3. Permission Checks (Moderation)
**File:** `commands/moderation_new.h.backup`  
**Lines:** 23-35

```cpp
bot.guild_get_member(event.msg.guild_id, invoker_id,
    [](const dpp::confirmation_callback_t& callback) {  // ← REQUIRES GUILD_MEMBERS INTENT
        
    auto invoker = std::get<dpp::guild_member>(callback.value);
    dpp::guild* g = dpp::find_guild(event.msg.guild_id);
    
    if (!g->base_permissions(invoker).can(dpp::p_kick_members)) {
        // User doesn't have permission
        event.reply(bronx::error("you don't have permission to kick members"));
        return;
    }
});
```

Similar checks for:
- `dpp::p_ban_members` (ban command)
- `dpp::p_moderate_members` (timeout command)
- `dpp::p_manage_roles` (reaction roles)

### 4. Reaction Roles
**File:** `commands/utility/reactionrole.h`  
**Lines:** 469-495

```cpp
bot.on_message_reaction_add([](const dpp::message_reaction_add_t& event) {
    // Get member to add role
    bot.guild_get_member(event.reacting_guild->id, event.reacting_user.id,
        [](const dpp::confirmation_callback_t& callback) {  // ← REQUIRES GUILD_MEMBERS INTENT
            
        auto member = std::get<dpp::guild_member>(callback.value);
        
        // Add role to member
        bot.guild_member_add_role(guild_id, user_id, role_id);
    });
});
```

### 5. Member Count for Features
**File:** `commands/utility.h.backup`  
**Line:** 358 (comment)

```cpp
// Note: member_count requires GUILD_MEMBERS intent enabled in Discord Developer Portal
```

---

## Intents NOT Used

### Presence Intent (NOT NEEDED)

**What we DON'T do:**
- Listen to `on_presence_update` events
- Track user online/offline status
- Monitor user activity (playing games, listening to music)
- Log user status changes

**What we DO do (doesn't require Presence Intent):**
```cpp
// Set BOT's own presence (doesn't require Presence Intent)
bot.set_presence(dpp::presence(dpp::presence_status::ps_online, activity));
```

**File:** `main_new.cpp`  
**Lines:** 369, 377

This only updates the bot's own status, visible to users. Does not require tracking other users' presence.

---

## Event Handlers Summary

### Handlers Requiring Message Content Intent:
1. `on_message_create` - Command parsing, moderation filters
2. `on_message_update` - Edit-based filter bypass prevention

### Handlers Requiring Guild Members Intent:
1. `guild_get_member()` calls - User info, permission checks
2. `guild.member_count` property access - Server statistics
3. `on_message_reaction_add` - Reaction role member fetching

### Handlers NOT Requiring Privileged Intents:
1. `on_ready` - Bot initialization
2. `on_slashcommand` - Slash command handling (no message content needed)
3. `on_button_click` - Interactive button responses
4. `on_select_click` - Dropdown menu interactions
5. `on_message_reaction_add/remove` - Doesn't need presence intent

---

## Data Flow

### Message Content Data Flow:
```
Discord → Gateway Event (on_message_create) 
       → event.msg.content [MESSAGE_CONTENT INTENT REQUIRED]
       → In-memory processing (command parsing / filter check)
       → Action taken (command response / message delete)
       → Data discarded (NOT stored in database)
```

### Guild Members Data Flow:
```
Discord → guild_get_member() API call [GUILD_MEMBERS INTENT REQUIRED]
       → dpp::guild_member object returned
       → Extract: join_date, roles, nickname, avatar
       → Display in embed / use for permission check
       → Data discarded (only user_id stored in DB for economy)
```

---

## Database Schema (What We Store)

### Tables Using User Data:
```sql
CREATE TABLE users (
    user_id BIGINT UNSIGNED PRIMARY KEY,  -- Discord ID (public)
    wallet BIGINT DEFAULT 0,               -- Virtual currency
    bank BIGINT DEFAULT 0,                 -- Virtual currency
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE inventory (
    user_id BIGINT UNSIGNED,
    item_id VARCHAR(50),
    quantity INT
);

CREATE TABLE cooldowns (
    user_id BIGINT UNSIGNED,
    command VARCHAR(50),
    expires_at BIGINT
);
```

### Tables NOT Storing Message Content:
- No `messages` table
- No `message_content` column anywhere
- No `user_activity` or `presence_status` tracking

---

## Verification Checklist

✅ Message Content Intent declared in code (line 120)  
✅ Message Content Intent used for legitimate purposes (moderation, commands)  
✅ No message content stored permanently  
✅ Guild Members Intent declared in code (line 120)  
✅ Guild Members Intent used for legitimate purposes (user info, stats, permissions)  
✅ No sensitive member data stored  
✅ Presence Intent NOT requested (not used)  
✅ All event handlers documented  
✅ Data flow is transparent and privacy-respecting  

---

## Contact for Technical Verification

If Discord staff need to verify these claims:
1. Full source code available at: [GitHub repo URL]
2. Can provide additional code snippets on request
3. Can demonstrate features in live test server
4. Available for video call walkthrough if needed

---

*Last Updated: February 23, 2026*
*Bot Version: Production (120+ servers)*
*Codebase: C++ using D++ library (DPP)*
