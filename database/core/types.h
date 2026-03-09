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
    bool dev;
    bool admin;
    bool is_mod;
    bool vip;
    bool passive;
    int prestige;
};

// Loan data structure
struct LoanData {
    uint64_t user_id;
    int64_t principal;        // Original loan amount
    int64_t interest;         // Interest charged (calculated at loan time)
    int64_t remaining;        // Total amount remaining to be paid
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> last_payment_at;
};

// Inventory item
struct InventoryItem {
    uint64_t id;
    std::string item_id;
    std::string item_type;
    int quantity;
    int level;              // item level (for rods, bait, upgrades, etc)
    std::string metadata; // JSON string
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

// Fish catch data
struct FishCatch {
    uint64_t id;
    std::string rarity;
    std::string fish_name;
    double weight;
    int64_t value;
    std::chrono::system_clock::time_point caught_at;
    bool sold;
    std::string rod_id;
    std::string bait_id;
};

// Cooldown data
struct Cooldown {
    uint64_t user_id;
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

// Autofisher v2 configuration
struct AutofisherConfig {
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

// Leveling system structs
struct UserXP {
    uint64_t user_id;
    uint64_t total_xp;
    uint32_t level;
    std::optional<std::chrono::system_clock::time_point> last_message_xp;
};

struct ServerXP {
    uint64_t user_id;
    uint64_t guild_id;
    uint64_t server_xp;
    uint32_t server_level;
    std::optional<std::chrono::system_clock::time_point> last_message_xp;
};

struct ServerLevelingConfig {
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

// XP Blacklist structs
struct XPBlacklistChannel {
    uint64_t id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t added_by;
    std::string reason;
    std::chrono::system_clock::time_point created_at;
};

struct XPBlacklistRole {
    uint64_t id;
    uint64_t guild_id;
    uint64_t role_id;
    uint64_t added_by;
    std::string reason;
    std::chrono::system_clock::time_point created_at;
};

struct XPBlacklistUser {
    uint64_t id;
    uint64_t guild_id;
    uint64_t user_id;
    uint64_t added_by;
    std::string reason;
    std::chrono::system_clock::time_point created_at;
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
