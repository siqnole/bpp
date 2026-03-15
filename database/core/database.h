#pragma once
#include "types.h"
#include "connection_pool.h"
#include <memory>
#include <functional>
#include <map>
#include <string>
#include <cstdint>
#include <unordered_set>
#include <shared_mutex>

// forward declare TitleDef from commands namespace to avoid circular include
namespace commands { struct TitleDef; }

namespace bronx {
namespace db {


// Database class with prepared statements and caching
class Database {
public:
    explicit Database(const DatabaseConfig& config);
    ~Database();
    
    // Initialize database connection
    bool connect();
    void disconnect();
    bool is_connected() const;
    
    // ========================================
    // USER OPERATIONS
    // ========================================
    
    // Get or create user data
    std::optional<UserData> get_user(uint64_t user_id);
    bool create_user(uint64_t user_id);
    bool ensure_user_exists(uint64_t user_id);
    std::vector<uint64_t> get_all_user_ids();
    
    // Balance operations
    int64_t get_wallet(uint64_t user_id);
    int64_t get_bank(uint64_t user_id);
    int64_t get_bank_limit(uint64_t user_id);
    int64_t get_networth(uint64_t user_id);
    int64_t get_fish_inventory_value(uint64_t user_id, uint64_t guild_id = 0);
    int64_t get_total_networth(uint64_t user_id, uint64_t guild_id = 0); // wallet + bank + fish inventory value
    
    // Update operations (returns new balance)
    std::optional<int64_t> update_wallet(uint64_t user_id, int64_t amount);
    std::optional<int64_t> update_bank(uint64_t user_id, int64_t amount);
    bool update_bank_limit(uint64_t user_id, int64_t new_limit);

    // Atomic deposit from wallet into bank; negative amount returns false.
    bool deposit(uint64_t user_id, int64_t amount);
    bool withdraw(uint64_t user_id, int64_t amount);  // move money from bank to wallet atomically
    
    // Atomic transfer
    TransactionResult transfer_money(uint64_t from_user, uint64_t to_user, int64_t amount);
    
    // Cooldowns (guild_id=0 → global)
    bool is_on_cooldown(uint64_t user_id, const std::string& command, uint64_t guild_id = 0);
    bool set_cooldown(uint64_t user_id, const std::string& command, int seconds, uint64_t guild_id = 0);
    std::optional<std::chrono::system_clock::time_point> get_cooldown_expiry(uint64_t user_id, const std::string& command, uint64_t guild_id = 0);
    // Atomic cooldown claim - returns true if cooldown was successfully claimed (wasn't on cooldown)
    // Returns false if already on cooldown. This is race-condition safe.
    bool try_claim_cooldown(uint64_t user_id, const std::string& command, int seconds, uint64_t guild_id = 0);
    
    // Interest system
    bool can_claim_interest(uint64_t user_id);
    int64_t claim_interest(uint64_t user_id); // Returns interest amount
    bool upgrade_interest(uint64_t user_id);
    
    // Statistics
    bool increment_stat(uint64_t user_id, const std::string& stat_name, int64_t amount = 1);
    int64_t get_stat(uint64_t user_id, const std::string& stat_name);
    
    // Passive mode (rob protection)
    bool is_passive(uint64_t user_id);
    bool set_passive(uint64_t user_id, bool passive);

    // Prestige system
    int get_prestige(uint64_t user_id);
    bool set_prestige(uint64_t user_id, int prestige);
    bool increment_prestige(uint64_t user_id);
    // Perform a full prestige reset: clears fish inventory, bait, rods, upgrades, tools,
    // autofishers, balance, and increments prestige level
    bool perform_prestige(uint64_t user_id);

    // ========================================
    // Loan operations (guild_id=0 → global)
    // ========================================
    
    // Create a new loan with interest (interest_rate is a percentage, e.g., 10.0 for 10%)
    bool create_loan(uint64_t user_id, int64_t principal, double interest_rate, uint64_t guild_id = 0);
    
    // Get user's current loan
    std::optional<LoanData> get_loan(uint64_t user_id, uint64_t guild_id = 0);
    
    // Make a payment towards the loan (returns new remaining balance)
    std::optional<int64_t> make_loan_payment(uint64_t user_id, int64_t amount, uint64_t guild_id = 0);
    
    // Check if user has an active loan
    bool has_active_loan(uint64_t user_id, uint64_t guild_id = 0);
    
    // Pay off loan completely
    bool payoff_loan(uint64_t user_id, uint64_t guild_id = 0);
    
    // Get total amount owed
    int64_t get_loan_balance(uint64_t user_id, uint64_t guild_id = 0);

    // enable/disable verbose inventory debug messages (add_item/remove_item)
    // when enabled, these methods will print detailed quantity changes to stderr.
    void set_inventory_debug(bool on);
    bool get_inventory_debug() const;

    // enable/disable verbose logging of each database connect call
    void set_connection_debug(bool on);
    bool get_connection_debug() const;

    
    // ========================================
    // INVENTORY OPERATIONS
    // ========================================
    
    std::vector<InventoryItem> get_inventory(uint64_t user_id, uint64_t guild_id = 0);
    std::optional<InventoryItem> get_item(uint64_t user_id, const std::string& item_id, uint64_t guild_id = 0);
    // `level` parameter added so items can carry their own level (rods/bait/upgrades, etc)
    bool add_item(uint64_t user_id, const std::string& item_id, const std::string& item_type, int quantity, const std::string& metadata = "", int level = 1, uint64_t guild_id = 0);
    bool remove_item(uint64_t user_id, const std::string& item_id, int quantity, uint64_t guild_id = 0);
    bool has_item(uint64_t user_id, const std::string& item_id, int quantity = 1, uint64_t guild_id = 0);
    int get_item_quantity(uint64_t user_id, const std::string& item_id, uint64_t guild_id = 0);
    bool update_item_metadata(uint64_t user_id, const std::string& item_id, const std::string& new_metadata, uint64_t guild_id = 0);
    // Count how many distinct users own a given item_id (used for purchase-limit titles)
    int count_item_owners(const std::string& item_id);
    // Delete every row from inventory matching item_id, returning the number of
    // rows removed.  Used for cleanup tasks (e.g. purge legacy active_title
    // entries).
    // Remove every row matching item_id from the inventory table.  This is
    // primarily used by maintenance routines (see owner cleantitles command)
    // to purge obsolete `active_title` entries, but can be reused for other
    // cleanup tasks if necessary.
    int delete_inventory_item_for_all_users(const std::string& item_id);
    // Return list of user IDs who currently own at least one of item_id
    std::vector<uint64_t> get_users_with_item(const std::string& item_id);
    
    // ========================================
    // SHOP / ITEM CATALOG OPERATIONS
    // ========================================
    std::vector<ShopItem> get_shop_items();
    std::optional<ShopItem> get_shop_item(const std::string& item_id);

    // server-specific market operations (per-guild)
    std::vector<MarketItem> get_market_items(uint64_t guild_id);
    std::optional<MarketItem> get_market_item(uint64_t guild_id, const std::string& item_id);
    bool create_market_item(const MarketItem& item);
    bool update_market_item(const MarketItem& item);
    bool delete_market_item(uint64_t guild_id, const std::string& item_id);
    bool adjust_market_item_quantity(uint64_t guild_id, const std::string& item_id, int delta);
    bool create_shop_item(const ShopItem& item);
    bool update_shop_item_price(const std::string& item_id, int64_t new_price);
    bool update_shop_item(const ShopItem& item); // general update
    bool delete_shop_item(const std::string& item_id);

    // Dynamic titles stored in shop_items table with category='title' and
    // metadata JSON containing rotation_slot/purchase_limit.  The helper
    // methods below let the owner command manage them easily.
    std::vector<commands::TitleDef> get_dynamic_titles();
    std::optional<commands::TitleDef> get_dynamic_title(const std::string& item_id);
    bool create_dynamic_title(const commands::TitleDef& title);
    bool update_dynamic_title(const commands::TitleDef& title);
    bool delete_dynamic_title(const std::string& item_id);

    // ========================================
    // FISHING OPERATIONS
    // ========================================
    
    uint64_t add_fish_catch(uint64_t user_id, const std::string& rarity, const std::string& fish_name, 
                           double weight, int64_t value, const std::string& rod_id, const std::string& bait_id, uint64_t guild_id = 0);

    // Batch insert multiple fish catches in a single multi-row INSERT.
    // Dramatically reduces round-trips for autofisher and multi-bait casts.
    struct FishCatchRow {
        std::string rarity;
        std::string fish_name;
        double weight;
        int64_t value;
        std::string rod_id;
        std::string bait_id;
    };
    bool add_fish_catches_batch(uint64_t user_id, const std::vector<FishCatchRow>& rows, uint64_t guild_id = 0);

    std::vector<FishCatch> get_unsold_fish(uint64_t user_id, uint64_t guild_id = 0);
    std::vector<FishCatch> get_fish_by_rarity(uint64_t user_id, const std::string& rarity, uint64_t guild_id = 0);
    bool sell_fish(uint64_t fish_id);
    bool sell_all_fish_by_rarity(uint64_t user_id, const std::string& rarity, uint64_t guild_id = 0);
    
    // Count total fish caught by rarity (including sold ones)
    int64_t count_fish_caught_by_rarity(uint64_t user_id, const std::string& rarity, uint64_t guild_id = 0);
    int64_t count_total_fish_caught(uint64_t user_id, uint64_t guild_id = 0);
    int64_t count_prestige_fish_caught(uint64_t user_id, uint64_t guild_id = 0);
    // Returns map<fish_name, total_times_caught> from fish_catches (survives prestige)
    std::map<std::string, int64_t> get_fish_catch_counts_by_species(uint64_t user_id, uint64_t guild_id = 0);

    // logging for machine learning (anonymous)
    bool record_fishing_log(int rod_level, int bait_level, int64_t net_profit);

    // Adjust bait prices based on logged outcomes; skips levels with fewer than min_samples entries.
    bool tune_bait_prices_from_logs(int min_samples = 50);
    // produce a human-readable report of average profit / sample counts per bait level
    std::string get_bait_tuning_report(int min_samples = 50);

    // machine learning settings storage
    std::optional<std::string> get_ml_setting(const std::string& key);
    bool set_ml_setting(const std::string& key, const std::string& value);
    bool delete_ml_setting(const std::string& key);
    std::vector<std::pair<std::string,std::string>> list_ml_settings();
    // report adjustments made by ML tuning over the past N hours
    std::string get_ml_effect_report(int hours);

    // Market state classifier (Markov regime detection)
    // Reads fishing_logs, classifies the current economic regime, and writes
    // the result to ml_settings["market_regime"].  Returns a human-readable
    // summary suitable for mlstatus.  min_samples mirrors tune_bait_prices.
    std::string classify_market_state(int min_samples = 50);
 
    // Returns the pre-built report string from MarketStateClassifier::build_report().
    std::string get_market_state_report();

    // Active gear (guild_id=0 → global)
    std::pair<std::string, std::string> get_active_fishing_gear(uint64_t user_id, uint64_t guild_id = 0); // {rod_id, bait_id}
    bool set_active_rod(uint64_t user_id, const std::string& rod_id, uint64_t guild_id = 0);
    bool set_active_bait(uint64_t user_id, const std::string& bait_id, uint64_t guild_id = 0);
    
    // Autofisher (guild_id=0 → global)
    bool has_autofisher(uint64_t user_id, uint64_t guild_id = 0);
    bool create_autofisher(uint64_t user_id, uint64_t guild_id = 0);
    bool upgrade_autofisher_efficiency(uint64_t user_id, uint64_t guild_id = 0);
    int64_t get_autofisher_balance(uint64_t user_id, uint64_t guild_id = 0);
    bool deposit_to_autofisher(uint64_t user_id, int64_t amount, uint64_t guild_id = 0);
    bool withdraw_from_autofisher(uint64_t user_id, int64_t amount, uint64_t guild_id = 0);
    std::vector<std::pair<uint64_t,uint64_t>> get_all_active_autofishers();  // {user_id, guild_id}
    bool activate_autofisher(uint64_t user_id, uint64_t guild_id = 0);
    bool deactivate_autofisher(uint64_t user_id, uint64_t guild_id = 0);
    int get_autofisher_tier(uint64_t user_id, uint64_t guild_id = 0);
    bool update_autofisher_last_run(uint64_t user_id, uint64_t guild_id = 0);
    std::optional<std::chrono::system_clock::time_point> get_autofisher_last_run(uint64_t user_id, uint64_t guild_id = 0);

    // Autofisher v2 – own gear, bait pool, fish storage, auto-sell (guild_id=0 → global)
    std::optional<AutofisherConfig> get_autofisher_config(uint64_t user_id, uint64_t guild_id = 0);
    bool autofisher_set_rod(uint64_t user_id, const std::string& rod_id, uint64_t guild_id = 0);
    bool autofisher_set_bait(uint64_t user_id, const std::string& bait_id, int level, const std::string& meta, uint64_t guild_id = 0);
    bool autofisher_deposit_bait(uint64_t user_id, int qty, uint64_t guild_id = 0);
    bool autofisher_consume_bait(uint64_t user_id, int qty, uint64_t guild_id = 0);
    bool autofisher_set_max_bank_draw(uint64_t user_id, int64_t amount, uint64_t guild_id = 0);
    bool autofisher_set_autosell(uint64_t user_id, bool enabled, const std::string& trigger, int64_t threshold, uint64_t guild_id = 0);
    bool autofisher_add_fish(uint64_t user_id, const std::string& fish_name, int64_t value, const std::string& metadata, uint64_t guild_id = 0);

    // Batch insert multiple fish into autofisher storage in one round-trip.
    struct AutofishFishRow {
        std::string fish_name;
        int64_t value;
        std::string metadata;
    };
    bool autofisher_add_fish_batch(uint64_t user_id, const std::vector<AutofishFishRow>& rows, uint64_t guild_id = 0);

    std::vector<AutofishFish> autofisher_get_fish(uint64_t user_id, uint64_t guild_id = 0);
    int autofisher_fish_count(uint64_t user_id, uint64_t guild_id = 0);
    // Returns total raw value; does NOT touch wallet (caller applies fee and transfers)
    int64_t autofisher_clear_fish(uint64_t user_id, uint64_t guild_id = 0);
    
    // ========================================
    // BAZAAR OPERATIONS
    // ========================================
    
    int get_bazaar_shares(uint64_t user_id);
    bool buy_bazaar_shares(uint64_t user_id, int shares, int64_t cost);
    bool sell_bazaar_shares(uint64_t user_id, int shares, int64_t value);
    void record_bazaar_visit(uint64_t user_id, uint64_t guild_id, int64_t spent);
    double calculate_bazaar_stock_price(uint64_t guild_id);
    
    // ========================================
    // GIVEAWAY OPERATIONS
    // ========================================
    
    int64_t get_guild_balance(uint64_t guild_id);
    bool donate_to_guild(uint64_t user_id, uint64_t guild_id, int64_t amount);
    struct ActiveGiveawayRow {
        uint64_t id;
        uint64_t guild_id;
        uint64_t channel_id;
        uint64_t message_id;
        uint64_t created_by;
        int64_t prize;
        int max_winners;
        std::chrono::system_clock::time_point ends_at;
    };
    
    uint64_t create_giveaway(uint64_t guild_id, uint64_t channel_id, uint64_t created_by, 
                            int64_t prize, int max_winners, int duration_seconds);
    bool enter_giveaway(uint64_t giveaway_id, uint64_t user_id);
    std::vector<uint64_t> get_giveaway_entries(uint64_t giveaway_id);
    bool end_giveaway(uint64_t giveaway_id, const std::vector<uint64_t>& winner_ids);
    std::vector<ActiveGiveawayRow> get_active_giveaways();
    
    // ========================================
    // LEADERBOARD OPERATIONS
    // ========================================
    
    std::vector<LeaderboardEntry> get_leaderboard(const std::string& type, int limit = 10);
    int get_user_rank(uint64_t user_id, const std::string& type);
    void update_leaderboard_cache(); // Background task
    
    // Specific leaderboard methods
    std::vector<LeaderboardEntry> get_networth_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_wallet_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_bank_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_inventory_value_leaderboard(uint64_t guild_id, int limit = 10);
    
    // Fishing leaderboards
    std::vector<LeaderboardEntry> get_fish_caught_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_fish_sold_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_most_valuable_fish_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_fishing_profit_leaderboard(uint64_t guild_id, int limit = 10);
    
    // Gambling leaderboards
    std::vector<LeaderboardEntry> get_gambling_wins_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_gambling_losses_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_gambling_profit_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_slots_wins_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_coinflip_wins_leaderboard(uint64_t guild_id, int limit = 10);
    
    // Activity leaderboards
    std::vector<LeaderboardEntry> get_commands_used_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_daily_streak_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_work_count_leaderboard(uint64_t guild_id, int limit = 10);
    std::vector<LeaderboardEntry> get_prestige_leaderboard(uint64_t guild_id, int limit = 10);
    
    // Helper method for stats-based leaderboards
    std::vector<LeaderboardEntry> get_stats_leaderboard(const std::string& stat_name, uint64_t guild_id, int limit, const std::string& emoji = "");
    
    // ========================================
    // LEVELING SYSTEM (unified: guild_id=0 → global, >0 → per-server)
    // ========================================
    
    // User XP operations (guild_id=0 → global XP)
    std::optional<UserXP> get_user_xp(uint64_t user_id, uint64_t guild_id = 0);
    bool create_user_xp(uint64_t user_id, uint64_t guild_id = 0);
    bool add_xp(uint64_t user_id, uint64_t xp_amount, uint32_t& new_level, bool& leveled_up, uint64_t guild_id = 0);
    bool set_xp(uint64_t user_id, uint64_t xp_amount, uint64_t guild_id = 0);
    bool reset_guild_xp(uint64_t guild_id);                             // reset all users in a guild
    bool reset_user_guild_xp(uint64_t user_id, uint64_t guild_id);     // reset single user in guild
    
    // Guild leveling configuration
    std::optional<GuildLevelingConfig> get_guild_leveling_config(uint64_t guild_id);
    bool create_guild_leveling_config(uint64_t guild_id);
    bool update_guild_leveling_config(const GuildLevelingConfig& config);
    
    // Level roles
    std::vector<LevelRole> get_level_roles(uint64_t guild_id);
    std::optional<LevelRole> get_level_role_at_level(uint64_t guild_id, uint32_t level);
    bool create_level_role(const LevelRole& role);
    bool delete_level_role(uint64_t guild_id, uint32_t level);
    bool delete_level_role_by_id(uint64_t id);
    
    // Level calculation helpers
    uint32_t calculate_level_from_xp(uint64_t xp);
    uint64_t calculate_xp_for_level(uint32_t level);
    uint64_t calculate_xp_for_next_level(uint32_t current_level);
    
    // Leveling leaderboards
    std::vector<LeaderboardEntry> get_global_xp_leaderboard(int limit = 10);
    std::vector<LeaderboardEntry> get_server_xp_leaderboard(uint64_t guild_id, int limit = 10);
    int get_user_global_xp_rank(uint64_t user_id);
    int get_user_server_xp_rank(uint64_t user_id, uint64_t guild_id);
    
    // XP blacklist (consolidated: channel/role/user → target_type)
    bool add_xp_blacklist(uint64_t guild_id, const std::string& target_type, uint64_t target_id, uint64_t added_by, const std::string& reason = "");
    bool remove_xp_blacklist(uint64_t guild_id, const std::string& target_type, uint64_t target_id);
    bool is_xp_blacklisted(uint64_t guild_id, const std::string& target_type, uint64_t target_id);
    std::vector<XPBlacklistEntry> get_xp_blacklist(uint64_t guild_id, const std::string& target_type = "");
    bool should_block_xp(uint64_t guild_id, uint64_t channel_id, uint64_t user_id, const std::vector<uint64_t>& user_role_ids);
    
    // Guild bot staff (replaces server_bot_admins + server_bot_mods)
    bool add_guild_staff(uint64_t guild_id, uint64_t user_id, const std::string& role, uint64_t granted_by);
    bool remove_guild_staff(uint64_t guild_id, uint64_t user_id);
    bool is_guild_staff(uint64_t guild_id, uint64_t user_id, const std::string& role = "");
    std::vector<GuildBotStaffRow> get_guild_staff(uint64_t guild_id, const std::string& role = "");
    
    // Guild moderation config (NEW — persistent mod config)
    std::optional<GuildModerationConfig> get_guild_mod_config(uint64_t guild_id);
    bool upsert_guild_mod_config(const GuildModerationConfig& config);
    
    // ========================================
    // INFRACTION SYSTEM
    // ========================================
    std::optional<InfractionRow> create_infraction(
        uint64_t guild_id, uint64_t user_id, uint64_t moderator_id,
        const std::string& type, const std::string& reason,
        double points, uint32_t duration_seconds, const std::string& metadata_json);
    std::optional<InfractionRow> get_infraction(uint64_t guild_id, uint32_t case_number);
    std::vector<InfractionRow> get_user_infractions(uint64_t guild_id, uint64_t user_id,
        bool active_only = false, int limit = 25, int offset = 0);
    double get_user_active_points(uint64_t guild_id, uint64_t user_id, int within_days = 0);
    std::vector<InfractionRow> get_recent_infractions(uint64_t guild_id,
        int limit = 25, int offset = 0, const std::string& type_filter = "");
    std::vector<InfractionRow> get_moderator_actions(uint64_t guild_id,
        uint64_t moderator_id, int limit = 25, int offset = 0);
    InfractionCounts count_infractions(uint64_t guild_id, uint64_t user_id);
    int count_guild_infractions(uint64_t guild_id, const std::string& type_filter = "", uint64_t user_id = 0);
    bool pardon_infraction(uint64_t guild_id, uint32_t case_number, uint64_t pardoned_by, const std::string& reason = "");
    int bulk_pardon_user(uint64_t guild_id, uint64_t user_id, uint64_t pardoned_by, const std::string& reason = "");
    int pardon_user_type(uint64_t guild_id, uint64_t user_id, const std::string& type,
        uint64_t pardoned_by, const std::string& reason = "");
    int expire_infractions();

    // Infraction config
    std::optional<InfractionConfig> get_infraction_config(uint64_t guild_id);
    bool upsert_infraction_config(const InfractionConfig& config);

    // Automod config
    std::optional<AutomodConfig> get_automod_config(uint64_t guild_id);
    bool upsert_automod_config(const AutomodConfig& config);

    // Role classes
    std::optional<RoleClass> create_role_class(uint64_t guild_id, const std::string& name,
        int priority, bool inherit_lower, const std::string& restrictions_json);
    bool update_role_class(uint32_t class_id, const std::string& name,
        int priority, bool inherit_lower, const std::string& restrictions_json);
    bool delete_role_class(uint32_t class_id);
    std::vector<RoleClass> get_role_classes(uint64_t guild_id);
    bool assign_role_to_class(uint64_t guild_id, uint64_t role_id, uint32_t class_id);
    bool remove_role_from_class(uint64_t guild_id, uint64_t role_id);
    std::vector<RoleClassMember> get_class_members(uint32_t class_id);

    // ========================================
    // PATCH NOTES
    // ========================================
    bool add_patch_note(const std::string& version, const std::string& notes, uint64_t author_id);
    std::optional<PatchNote> get_latest_patch();
    std::vector<PatchNote> get_all_patches(int limit = 50, int offset = 0);
    int get_patch_count();
    
    // ========================================
    // GAMBLING STATISTICS
    // ========================================
    
    bool record_gambling_result(uint64_t user_id, const std::string& game_type, 
                               int64_t bet, int64_t result); // result = winnings (can be negative)
    
    // lottery statistics and entries
    bool update_lottery_tickets(uint64_t user_id, int64_t tickets); // add tickets for a user
    int64_t get_lottery_user_count();
    int64_t get_lottery_total_tickets();

    // ========================================
    // UTILITY
    // ========================================
    
    // Reaction roles persistence
    bool add_reaction_role(uint64_t guild_id, uint64_t message_id, uint64_t channel_id, const std::string& emoji_raw, uint64_t emoji_id, uint64_t role_id);
    bool remove_reaction_role(uint64_t message_id, const std::string& emoji_raw);
    std::vector<ReactionRoleRow> get_all_reaction_roles();

    // Autopurge scheduling
    // Returns new row id (0 on failure)
    uint64_t add_autopurge(uint64_t user_id, uint64_t guild_id, uint64_t channel_id,
                            int interval_seconds, int message_limit,
                            uint64_t target_user_id = 0, uint64_t target_role_id = 0);
    // Removes an autopurge entry; only succeeds if entry belongs to the given user
    bool remove_autopurge(uint64_t autopurge_id, uint64_t user_id);
    std::vector<AutopurgeRow> get_all_autopurges();
    std::vector<AutopurgeRow> get_autopurges_for_user(uint64_t user_id);

    // Global blacklist/whitelist for command abuse prevention
    bool add_global_blacklist(uint64_t user_id, const std::string& reason = "");
    bool remove_global_blacklist(uint64_t user_id);
    bool is_global_blacklisted(uint64_t user_id);
    std::vector<BlacklistEntry> get_global_blacklist();

    bool add_global_whitelist(uint64_t user_id, const std::string& reason = "");
    bool remove_global_whitelist(uint64_t user_id);
    bool is_global_whitelisted(uint64_t user_id);
    std::vector<WhitelistEntry> get_global_whitelist();

    // Suggestions operations
    bool add_suggestion(uint64_t user_id, const std::string& text, int64_t networth);
    std::vector<Suggestion> fetch_suggestions(const std::string& order_clause);
    bool mark_suggestion_read(uint64_t suggestion_id);
    bool delete_suggestion(uint64_t suggestion_id);

    // Bug report operations
    bool add_bug_report(uint64_t user_id, const std::string& command_or_feature,
                        const std::string& reproduction_steps, const std::string& expected_behavior,
                        const std::string& actual_behavior, int64_t networth);
    std::vector<BugReport> fetch_bug_reports(const std::string& order_clause);
    bool mark_bug_report_read(uint64_t report_id);
    bool mark_bug_report_resolved(uint64_t report_id);
    bool delete_bug_report(uint64_t report_id);
    int get_bug_report_count();

    // Command history operations (owner auditing)
    bool log_history(uint64_t user_id, const std::string& entry_type, 
                     const std::string& description, int64_t amount = 0, int64_t balance_after = 0);
    std::vector<HistoryEntry> fetch_history(uint64_t user_id, int limit = 50, int offset = 0);
    int get_history_count(uint64_t user_id);
    bool clear_history(uint64_t user_id);  // delete all history for a user

    // custom prefixes
    bool add_user_prefix(uint64_t user_id, const std::string& prefix);
    bool remove_user_prefix(uint64_t user_id, const std::string& prefix);
    std::vector<std::string> get_user_prefixes(uint64_t user_id);
    bool add_guild_prefix(uint64_t guild_id, const std::string& prefix);
    bool remove_guild_prefix(uint64_t guild_id, const std::string& prefix);
    std::vector<std::string> get_guild_prefixes(uint64_t guild_id);

    // guild command/module toggles (supports scoped overrides)
    // scope_type may be "guild", "channel", "role", or "user"; scope_id only used for non-guild scopes.
    // exclusive=true means ONLY this scope can use it (blocks all others)
    bool set_guild_command_enabled(uint64_t guild_id, const std::string& command, bool enabled,
                                   const std::string& scope_type = "guild", uint64_t scope_id = 0,
                                   bool exclusive = false);
    bool is_guild_command_enabled(uint64_t guild_id, const std::string& command,
                                  uint64_t user_id = 0, uint64_t channel_id = 0,
                                  const std::vector<uint64_t>& roles = {});
    std::vector<std::string> get_disabled_commands(uint64_t guild_id);

    bool set_guild_module_enabled(uint64_t guild_id, const std::string& module, bool enabled,
                                  const std::string& scope_type = "guild", uint64_t scope_id = 0,
                                  bool exclusive = false);
    bool is_guild_module_enabled(uint64_t guild_id, const std::string& module,
                                 uint64_t user_id = 0, uint64_t channel_id = 0,
                                 const std::vector<uint64_t>& roles = {});
    std::vector<std::string> get_disabled_modules(uint64_t guild_id);

    // Fetch ALL settings rows for a guild (for display)
    struct ModuleSettingRow { std::string module; bool enabled; };
    struct CommandSettingRow { std::string command; bool enabled; };
    struct ScopedSettingRow { std::string name; std::string scope_type; uint64_t scope_id; bool enabled; bool exclusive; };
    std::vector<ModuleSettingRow> get_all_module_settings(uint64_t guild_id);
    std::vector<CommandSettingRow> get_all_command_settings(uint64_t guild_id);
    std::vector<ScopedSettingRow> get_all_module_scope_settings(uint64_t guild_id);
    std::vector<ScopedSettingRow> get_all_command_scope_settings(uint64_t guild_id);

    // ========================================
    // BULK SETTINGS FETCH (for periodic sync)
    // ========================================
    // Fetch all guild prefixes across ALL guilds in one query
    struct GuildPrefixRow { uint64_t guild_id; std::string prefix; };
    std::vector<GuildPrefixRow> get_all_guild_prefixes_bulk();

    // Fetch all module settings across ALL guilds in one query
    struct GuildModuleRow { uint64_t guild_id; std::string module; bool enabled; };
    std::vector<GuildModuleRow> get_all_module_settings_bulk();

    // Fetch all command settings across ALL guilds in one query
    struct GuildCommandRow { uint64_t guild_id; std::string command; bool enabled; };
    std::vector<GuildCommandRow> get_all_command_settings_bulk();

    // ========================================
    // PRIVACY & OPT-OUT
    // ========================================
    
    // Opt-out management — users can request complete data removal
    bool set_opted_out(uint64_t user_id, bool opted_out);
    bool is_opted_out(uint64_t user_id);
    bool delete_all_user_data(uint64_t user_id);
    
    // Encrypted identity cache (AES-256-CBC, 30-day TTL)
    bool cache_encrypted_identity(uint64_t user_id,
                                   const std::string& username,
                                   const std::string& nickname,
                                   const std::string& avatar_hash);
    
    // Forward-declared struct for decrypted identity
    struct DecryptedIdentityResult {
        std::string username;
        std::string nickname;
        std::string avatar_hash;
    };
    std::optional<DecryptedIdentityResult> get_cached_identity(uint64_t user_id);
    
    // Purge expired identity cache entries (called periodically)
    int purge_expired_identities();
    
    // Get all opted-out user IDs (for in-memory caching in hot path)
    std::vector<uint64_t> get_all_opted_out_users();

    // ========================================
    // PROGRESSIVE JACKPOT
    // ========================================
    int64_t get_jackpot_pool();
    std::optional<JackpotData> get_jackpot();
    bool contribute_to_jackpot(int64_t amount);
    int64_t try_win_jackpot(uint64_t user_id);
    std::vector<JackpotHistoryEntry> get_jackpot_history(int limit = 10);

    // ========================================
    // WORLD EVENTS
    // ========================================
    std::optional<WorldEventData> get_active_world_event();
    bool start_world_event(const std::string& event_type, const std::string& event_name,
                           const std::string& description, const std::string& emoji,
                           const std::string& bonus_type, double bonus_value, int duration_minutes);
    bool end_active_world_event();
    int expire_world_events();
    double get_world_event_bonus(const std::string& bonus_type);
    std::vector<WorldEventData> get_world_event_history(int limit = 10);

    // Execute raw query (use sparingly)
    bool execute(const std::string& query);
    
    // Execute multiple DDL/DML statements on a single connection.
    // Returns the number of statements that succeeded.
    int execute_batch(const std::vector<std::string>& queries);
    
    // Get last error message
    std::string get_last_error() const;
    
    // Public accessors for operation functions
    ConnectionPool* get_pool() const { return pool_.get(); }
    void log_error(const std::string& context);
    
private:
    
    // Helper methods
    MYSQL_STMT* prepare_statement(const std::string& query);
    bool execute_prepared(MYSQL_STMT* stmt);
    
    std::unique_ptr<ConnectionPool> pool_;
    DatabaseConfig config_;
    bool connected_ = false;
    bool inventory_debug_ = false;
    bool connection_debug_ = false; // controls pool verbosity
    mutable std::string last_error_;
    
    // In-memory cache of user IDs known to exist, avoiding redundant
    // INSERT IGNORE round-trips to a remote database.
    mutable std::shared_mutex known_users_mutex_;
    std::unordered_set<uint64_t> known_users_;
    
public:
    // Fast check if user is already known to exist in-memory
    bool is_user_known(uint64_t user_id) const {
        std::shared_lock lk(known_users_mutex_);
        return known_users_.count(user_id) > 0;
    }
    void mark_user_known(uint64_t user_id) {
        std::unique_lock lk(known_users_mutex_);
        known_users_.insert(user_id);
    }
};

} // namespace db
} // namespace bronx
