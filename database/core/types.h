#pragma once
#include <mariadb/mysql.h>
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <chrono>
#include <cstdint>

namespace bronx {
namespace db {

// Database configuration
struct DatabaseConfig {
    std::string host = "localhost";
    uint16_t port = 3306;
    std::string database = "bronxbot";
    std::string user = "root";
    std::string password = "";
    uint32_t pool_size = 10; // Connection pool size
    uint32_t timeout_seconds = 10;
    bool log_connections = false; // print each new connection when true
};

// User economy data
struct UserData {
    uint64_t user_id;
    int64_t wallet;
    int64_t bank;
    int64_t bank_limit;
    double interest_rate;
    int interest_level;
    std::optional<std::chrono::system_clock::time_point> last_interest_claim;
    std::optional<std::chrono::system_clock::time_point> last_daily;
    std::optional<std::chrono::system_clock::time_point> last_work;
    std::optional<std::chrono::system_clock::time_point> last_beg;
    std::optional<std::chrono::system_clock::time_point> last_rob;
    int64_t total_gambled;
    int64_t total_won;
    int64_t total_lost;

    // Flattened common stats (hybrid model)
    int64_t fish_caught      = 0;
    int64_t fish_sold         = 0;
    int64_t gambling_wins     = 0;
    int64_t gambling_losses   = 0;
    int64_t commands_used     = 0;
    int     daily_streak      = 0;
    int64_t work_count        = 0;
    int64_t ores_mined        = 0;
    int64_t items_crafted     = 0;
    int64_t trades_completed  = 0;

    // Role flags
    bool dev;
    bool admin;
    bool is_mod;
    bool maintainer       = false;
    bool contributor      = false;
    bool vip;
    bool passive;
    int prestige;
};

// Loan data structure
struct LoanData {
    uint64_t user_id;
    uint64_t guild_id     = 0;  // 0 = global
    int64_t principal;           // Original loan amount
    double  interest_rate = 5.0; // Interest rate at loan time
    int64_t remaining;           // Total amount remaining to be paid
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> last_payment_at;
};

// Inventory item (unified: guild_id=0 → global, >0 → per-server)
struct InventoryItem {
    uint64_t id;
    uint64_t guild_id = 0;  // 0 = global economy
    std::string item_id;
    std::string item_type;
    int quantity;
    int level;              // item level (for rods, bait, upgrades, etc)
    std::string metadata;   // JSON string
};


// Shop catalog item
struct ShopItem {
    std::string item_id;
    std::string name;
    std::string description;
    std::string category;
    int64_t price;
    int max_quantity;      // NULL/unlimited stored as -1 in struct
    int required_level;
    int level;             // item level (for rods/bait compatibility)
    bool usable;
    std::string metadata;  // JSON properties
};

// Server-specific market catalog item
struct MarketItem {
    uint64_t guild_id;
    std::string item_id;
    std::string name;
    std::string description;
    std::string category;   // e.g. "role" or "channel"
    int64_t price;
    int max_quantity;       // NULL/unlimited stored as -1
    std::string metadata;   // JSON properties (type/target/etc)
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

// Fish catch data (unified: guild_id=0 → global, >0 → per-server)
struct FishCatch {
    uint64_t id;
    uint64_t guild_id = 0;  // 0 = global
    std::string rarity;
    std::string fish_name;
    double weight;
    int64_t value;
    std::chrono::system_clock::time_point caught_at;
    bool sold;
    std::string rod_id;
    std::string bait_id;
};

// Cooldown data (unified: guild_id=0 → global, >0 → per-server)
struct Cooldown {
    uint64_t user_id;
    uint64_t guild_id = 0;  // 0 = global
    std::string command;
    std::chrono::system_clock::time_point expires_at;
};

// Leaderboard entry
struct LeaderboardEntry {
    uint64_t user_id;
    std::string username;
    int64_t value;
    int rank;
    std::string extra_info;
};

// Reaction role persistent row
struct ReactionRoleRow {
    uint64_t guild_id;
    uint64_t message_id;
    uint64_t channel_id;
    std::string emoji_raw;
    uint64_t emoji_id;
    uint64_t role_id;
};

// Autopurge schedule row
struct AutopurgeRow {
    uint64_t id;
    uint64_t user_id;        // creator of the schedule
    uint64_t guild_id;
    uint64_t channel_id;
    int interval_seconds;
    int message_limit;
    uint64_t target_user_id; // 0 => creator
    uint64_t target_role_id; // 0 => none
};

// User suggestion (feedback table row)
struct Suggestion {
    uint64_t id;
    uint64_t user_id;
    std::string suggestion;
    int64_t networth;
    std::chrono::system_clock::time_point submitted_at;
    bool read;
};

// User bug report (bug_reports table row)
struct BugReport {
    uint64_t id;
    uint64_t user_id;
    std::string command_or_feature;
    std::string reproduction_steps;
    std::string expected_behavior;
    std::string actual_behavior;
    int64_t networth;
    std::chrono::system_clock::time_point submitted_at;
    bool read;
    bool resolved;
};

// Command history entry (for owner auditing)
struct HistoryEntry {
    uint64_t id;
    uint64_t user_id;
    std::string entry_type;  // CMD, BAL, FSH, PAY, GAM, SHP
    std::string description;
    int64_t amount;          // balance change (can be 0)
    int64_t balance_after;   // wallet after action
    std::chrono::system_clock::time_point created_at;
};

// Transaction result
enum class TransactionResult {
    Success,
    InsufficientFunds,
    DatabaseError,
    InvalidAmount,
    UserNotFound
};

// Autofisher v2 configuration (unified: guild_id=0 → global)
struct AutofisherConfig {
    uint64_t guild_id    = 0;   // 0 = global
    bool active          = false;
    int  tier            = 0;
    std::string af_rod_id;      // rod equipped to the autofisher
    std::string af_bait_id;     // bait type the autofisher uses
    int         af_bait_qty     = 0;
    int         af_bait_level   = 1;
    std::string af_bait_meta;
    int64_t     max_bank_draw   = 0; // 0 = disabled; max the AF can spend from user's bank per bait buy
    bool        auto_sell       = false;
    std::string as_trigger      = "bag"; // "bag" | "count" | "balance"
    int64_t     as_threshold    = 0;
    int         bag_limit       = 10;
};

// A single fish sitting in autofisher storage
struct AutofishFish {
    uint64_t    id        = 0;
    uint64_t    user_id   = 0;
    uint64_t    guild_id  = 0;  // 0 = global
    std::string fish_name;
    int64_t     value     = 0;
    std::string metadata;
};

// Global blacklist entry with reason
struct BlacklistEntry {
    uint64_t user_id;
    std::string reason;
};

// Global whitelist entry with reason
struct WhitelistEntry {
    uint64_t user_id;
    std::string reason;
};

// Leveling system structs (unified: guild_id=0 → global XP)
struct UserXP {
    uint64_t user_id;
    uint64_t guild_id  = 0;  // 0 = global XP
    uint64_t total_xp;
    uint32_t level;
    std::optional<std::chrono::system_clock::time_point> last_message_xp;
};

struct GuildLevelingConfig {
    uint64_t guild_id;
    bool enabled;
    bool reward_coins;
    int coins_per_message;
    int min_xp_per_message;
    int max_xp_per_message;
    int min_message_chars;
    int xp_cooldown_seconds;
    std::optional<uint64_t> announcement_channel;
    bool announce_levelup;
};

struct LevelRole {
    uint64_t id;
    uint64_t guild_id;
    uint32_t level;
    uint64_t role_id;
    std::string role_name;
    std::string description;
    bool remove_previous;
};

struct PatchNote {
    uint32_t id;
    std::string version;
    std::string notes;
    uint64_t author_id;
    std::chrono::system_clock::time_point created_at;
};

// XP Blacklist — consolidated (replaces channel/role/user variants)
enum class XPBlacklistTargetType { Channel, Role, User };

struct XPBlacklistEntry {
    uint64_t id;
    uint64_t guild_id;
    XPBlacklistTargetType target_type;
    uint64_t target_id;   // channel_id, role_id, or user_id
    uint64_t added_by;
    std::string reason;
    std::chrono::system_clock::time_point created_at;
};

// Guild bot staff (replaces server_bot_admins + server_bot_mods)
enum class GuildStaffRole { Admin, Mod };

struct GuildBotStaffRow {
    uint64_t guild_id;
    uint64_t user_id;
    GuildStaffRole role;
    uint64_t granted_by;
    std::chrono::system_clock::time_point granted_at;
};

// Guild moderation config (NEW — persists configs that were in-memory)
struct GuildModerationConfig {
    uint64_t guild_id;
    bool antispam_enabled        = false;
    bool text_filter_enabled     = false;
    bool url_guard_enabled       = false;
    bool reaction_filter_enabled = false;
    std::string antispam_config;         // JSON
    std::string text_filter_config;      // JSON
    std::string url_guard_config;        // JSON
    std::string reaction_filter_config;  // JSON
};

// Guild log configuration (guild_log_configs table)
struct LogConfig {
    uint64_t guild_id;
    std::string log_type;
    uint64_t channel_id;
    std::string webhook_url;
    uint64_t webhook_id;
    std::string webhook_token;
    bool enabled;
};

// ── Infraction System ──────────────────────────────────────────────

// Infraction row (guild_infractions table)
struct InfractionRow {
    uint64_t id;
    uint64_t guild_id;
    uint32_t case_number;
    uint64_t user_id;
    uint64_t moderator_id;
    std::string type;          // enum string: warn, timeout, mute, jail, kick, ban, auto_*
    std::string reason;
    double points;
    uint32_t duration_seconds; // 0 = permanent
    std::optional<std::chrono::system_clock::time_point> expires_at;
    bool active;
    bool pardoned;
    uint64_t pardoned_by;
    std::optional<std::chrono::system_clock::time_point> pardoned_at;
    std::string pardoned_reason;
    std::string metadata;      // JSON
    std::chrono::system_clock::time_point created_at;
};

struct InfractionCounts {
    int total;
    int active;
    int pardoned;
};

// Per-guild infraction config (guild_infraction_config table)
struct InfractionConfig {
    uint64_t guild_id;

    double point_timeout = 0.25;
    double point_mute    = 0.50;
    double point_kick    = 2.00;
    double point_ban     = 5.00;
    double point_warn    = 0.10;

    uint32_t default_duration_timeout = 259200;   // 3d
    uint32_t default_duration_mute    = 604800;   // 7d
    uint32_t default_duration_kick    = 1209600;  // 14d
    uint32_t default_duration_ban     = 15552000; // 180d
    uint32_t default_duration_warn    = 604800;   // 7d

    std::string escalation_rules;  // JSON array

    uint64_t mute_role_id    = 0;
    uint64_t jail_role_id    = 0;
    uint64_t jail_channel_id = 0;
    uint64_t log_channel_id  = 0;

    bool dm_on_action = true;
};

// Escalation rule (deserialized from escalation_rules JSON)
struct EscalationRule {
    double threshold_points;
    int within_days;
    std::string action;               // timeout, mute, jail, kick, ban
    uint32_t action_duration_seconds;  // 0 = permanent
    std::string reason_template;
};

// Extended auto-mod config (guild_automod_config table)
struct AutomodConfig {
    uint64_t guild_id;

    bool account_age_enabled         = false;
    uint32_t account_age_days        = 7;
    std::string account_age_action   = "kick";

    bool default_avatar_enabled      = false;
    std::string default_avatar_action = "kick";

    bool mutual_servers_enabled      = false;
    uint32_t mutual_servers_min      = 1;
    std::string mutual_servers_action = "kick";

    bool nickname_sanitize_enabled   = false;
    std::string nickname_sanitize_format = "Moderated Nickname {n}";
    std::string nickname_bad_patterns;  // JSON array of regex strings

    bool infraction_escalation_enabled = true;
};

// Role-based permission class (guild_role_classes table)
struct RoleClass {
    uint32_t id;
    uint64_t guild_id;
    std::string name;
    int priority;
    bool inherit_lower;
    std::string restrictions;  // JSON: {allowed_commands, denied_commands, allowed_modules, denied_modules}
    std::chrono::system_clock::time_point created_at;
};

// Maps a Discord role to a class (guild_role_class_members table)
struct RoleClassMember {
    uint64_t guild_id;
    uint64_t role_id;
    uint32_t class_id;
};

// Progressive Jackpot data
struct JackpotData {
    int64_t pool;
    uint64_t last_winner_id;
    int64_t last_won_amount;
    int64_t total_won_all_time;
    int times_won;
};

struct JackpotHistoryEntry {
    uint64_t user_id;
    int64_t amount;
    int64_t pool_before;
    int64_t won_at_timestamp;
};

// World Event data
struct WorldEventData {
    uint64_t id;
    std::string event_type;
    std::string event_name;
    std::string description;
    std::string emoji;
    std::string bonus_type;
    double bonus_value;
    int64_t started_at_timestamp;
    int64_t ends_at_timestamp;
    bool active;
};

} // namespace db
} // namespace bronx
