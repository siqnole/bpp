#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "../achievements.h"
#include "../global_boss.h"
#include "../daily_challenges/daily_stat_tracker.h"
#include <dpp/dpp.h>
#include <algorithm>

using namespace bronx::db;

namespace commands {
namespace fishing {

inline Command* get_sellfish_command(Database* db) {
    static Command* sellfish = new Command("sellfish", "sell fish from your inventory", "fishing", {"sf", "fishsell"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("specify a fish ID or `all` to sell all unlocked fish"));
                return;
            }
            
            ::std::string target = args[0];
            ::std::transform(target.begin(), target.end(), target.begin(), ::tolower);
            
            if (target == "all") {
                // Sell all unlocked fish
                auto inventory = db->get_inventory(event.msg.author.id);
                int64_t total_earned = 0;
                int fish_sold = 0;
                
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    
                    ::std::string metadata = item.metadata;
                    size_t locked_pos = metadata.find("\"locked\":");
                    bool is_locked = (locked_pos != ::std::string::npos && metadata.find("true", locked_pos) != ::std::string::npos);
                    
                    if (!is_locked) {
                        size_t value_pos = metadata.find("\"value\":");
                        if (value_pos != ::std::string::npos) {
                            size_t start = value_pos + 8;
                            size_t end = metadata.find(",", start);
                            if (end == ::std::string::npos) end = metadata.find("}", start);
                            int64_t fish_value = ::std::stoll(metadata.substr(start, end - start));
                            
                            if (db->remove_item(event.msg.author.id, item.item_id, 1)) {
                                total_earned += fish_value;
                                fish_sold++;
                            }
                        }
                    }
                }
                
                if (fish_sold > 0) {
                    db->update_wallet(event.msg.author.id, total_earned);
                    
                    // Track fish selling stats
                    db->increment_stat(event.msg.author.id, "fish_sold", fish_sold);
                    db->increment_stat(event.msg.author.id, "fish_value", total_earned);
                    db->increment_stat(event.msg.author.id, "fish_profit", total_earned);
                    ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "fish_sell_value_today", total_earned);
                    ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "coins_earned_today", total_earned);
                    
                    // Track global boss fish profit
                    global_boss::on_fish_profit(db, event.msg.author.id, total_earned);
                    
                    // Check for fish value achievements
                    achievements::check_achievements_for_stat(bot, db, event.msg.channel_id, 
                        event.msg.author.id, "fish_value");
                    
                    // Log fish sale to history
                    int64_t new_balance = db->get_wallet(event.msg.author.id);
                    std::string log_desc = "sold " + std::to_string(fish_sold) + " fish ($" + format_number(total_earned) + ")";
                    bronx::db::history_operations::log_fishing(db, event.msg.author.id, log_desc, total_earned, new_balance);
                    
                    ::std::string description = "sold " + ::std::to_string(fish_sold) + " fish for $" + format_number(total_earned);
                    auto embed = bronx::success(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                } else {
                    bronx::send_message(bot, event, bronx::error("no unlocked fish to sell"));
                }
            } else {
                // Sell specific fish by ID
                ::std::transform(target.begin(), target.end(), target.begin(), ::toupper);
                
                auto inventory = db->get_inventory(event.msg.author.id);
                bool found = false;
                
                for (const auto& item : inventory) {
                    if (item.item_id == target && item.item_type == "collectible") {
                        found = true;
                        
                        ::std::string metadata = item.metadata;
                        size_t locked_pos = metadata.find("\"locked\":");
                        bool is_locked = (locked_pos != ::std::string::npos && metadata.find("true", locked_pos) != ::std::string::npos);
                        
                        if (is_locked) {
                            bronx::send_message(bot, event,
                                bronx::error("this fish is locked! unlock it first with `lockfish " + target + "`"));
                            return;
                        }
                        
                        size_t value_pos = metadata.find("\"value\":");
                        if (value_pos != ::std::string::npos) {
                            size_t start = value_pos + 8;
                            size_t end = metadata.find(",", start);
                            if (end == ::std::string::npos) end = metadata.find("}", start);
                            int64_t fish_value = ::std::stoll(metadata.substr(start, end - start));
                            
                            if (db->remove_item(event.msg.author.id, item.item_id, 1)) {
                                db->update_wallet(event.msg.author.id, fish_value);
                                
                                // Track fish selling stats
                                db->increment_stat(event.msg.author.id, "fish_sold", 1);
                                db->increment_stat(event.msg.author.id, "fish_value", fish_value);
                                db->increment_stat(event.msg.author.id, "fish_profit", fish_value);
                                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "fish_sell_value_today", fish_value);
                                ::commands::daily_challenges::track_daily_stat(db, event.msg.author.id, "coins_earned_today", fish_value);
                                
                                // Check for fish value achievements
                                achievements::check_achievements_for_stat(bot, db, event.msg.channel_id,
                                    event.msg.author.id, "fish_value");
                                
                                // Log fish sale to history
                                int64_t new_balance = db->get_wallet(event.msg.author.id);
                                std::string log_desc = "sold fish " + target + " ($" + format_number(fish_value) + ")";
                                bronx::db::history_operations::log_fishing(db, event.msg.author.id, log_desc, fish_value, new_balance);
                                
                                ::std::string description = "sold fish `" + target + "` for $" + format_number(fish_value);
                                auto embed = bronx::success(description);
                                bronx::add_invoker_footer(embed, event.msg.author);
                                bronx::send_message(bot, event, embed);
                            } else {
                                bronx::send_message(bot, event, bronx::error("failed to sell fish"));
                            }
                        }
                        break;
                    }
                }
                
                if (!found) {
                    bronx::send_message(bot, event, bronx::error("fish not found in your inventory"));
                }
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ::std::string target;
            try {
                auto p = event.get_parameter("target");
                if (std::holds_alternative<std::string>(p)) {
                    target = std::get<std::string>(p);
                }
            } catch (...) {}
            if (target.empty()) {
                event.reply(dpp::message().add_embed(bronx::error("specify a fish ID or `all` to sell all unlocked fish")));
                return;
            }
            std::transform(target.begin(), target.end(), target.begin(), ::tolower);

            auto reply_error = [&](const std::string &msg){
                event.reply(dpp::message().add_embed(bronx::error(msg)));
            };
            auto reply_success = [&](const std::string &msg){
                event.reply(dpp::message().add_embed(bronx::success(msg)));
            };

            uint64_t uid = event.command.get_issuing_user().id;
            if (target == "all") {
                auto inventory = db->get_inventory(uid);
                int64_t total_earned = 0;
                int fish_sold = 0;
                for (const auto& item : inventory) {
                    if (item.item_type != "collectible") continue;
                    std::string metadata = item.metadata;
                    size_t locked_pos = metadata.find("\"locked\":");
                    bool is_locked = (locked_pos != std::string::npos && metadata.find("true", locked_pos) != std::string::npos);
                    if (!is_locked) {
                        size_t value_pos = metadata.find("\"value\":");
                        if (value_pos != std::string::npos) {
                            size_t start = value_pos + 8;
                            size_t end = metadata.find(",", start);
                            if (end == std::string::npos) end = metadata.find("}", start);
                            int64_t fish_value = std::stoll(metadata.substr(start, end - start));
                            if (db->remove_item(uid, item.item_id, 1)) {
                                total_earned += fish_value;
                                fish_sold++;
                            }
                        }
                    }
                }
                if (fish_sold > 0) {
                    db->update_wallet(uid, total_earned);
                    
                    // Track fish selling stats
                    db->increment_stat(uid, "fish_sold", fish_sold);
                    db->increment_stat(uid, "fish_value", total_earned);
                    db->increment_stat(uid, "fish_profit", total_earned);
                    ::commands::daily_challenges::track_daily_stat(db, uid, "fish_sell_value_today", total_earned);
                    ::commands::daily_challenges::track_daily_stat(db, uid, "coins_earned_today", total_earned);
                    
                    // Track global boss fish profit
                    global_boss::on_fish_profit(db, uid, total_earned);
                    
                    // Check for fish value achievements
                    achievements::check_achievements_for_stat(bot, db, event.command.channel_id, uid, "fish_value");
                    
                    // Log fish sale to history
                    int64_t new_balance = db->get_wallet(uid);
                    std::string log_desc = "sold " + std::to_string(fish_sold) + " fish ($" + format_number(total_earned) + ")";
                    bronx::db::history_operations::log_fishing(db, uid, log_desc, total_earned, new_balance);
                    
                    reply_success("sold " + std::to_string(fish_sold) + " fish for $" + format_number(total_earned));
                } else {
                    reply_error("no unlocked fish to sell");
                }
            } else {
                std::transform(target.begin(), target.end(), target.begin(), ::toupper);
                auto inventory = db->get_inventory(uid);
                bool found = false;
                for (const auto& item : inventory) {
                    if (item.item_id == target && item.item_type == "collectible") {
                        found = true;
                        std::string metadata = item.metadata;
                        size_t locked_pos = metadata.find("\"locked\":");
                        bool is_locked = (locked_pos != std::string::npos && metadata.find("true", locked_pos) != std::string::npos);
                        if (is_locked) {
                            reply_error("this fish is locked! unlock it first with `lockfish " + target + "`");
                            return;
                        }
                        size_t value_pos = metadata.find("\"value\":");
                        if (value_pos != std::string::npos) {
                            size_t start = value_pos + 8;
                            size_t end = metadata.find(",", start);
                            if (end == std::string::npos) end = metadata.find("}", start);
                            int64_t fish_value = std::stoll(metadata.substr(start, end - start));
                            if (db->remove_item(uid, item.item_id, 1)) {
                                db->update_wallet(uid, fish_value);
                                
                                // Track fish selling stats
                                db->increment_stat(uid, "fish_sold", 1);
                                db->increment_stat(uid, "fish_value", fish_value);
                                db->increment_stat(uid, "fish_profit", fish_value);
                                ::commands::daily_challenges::track_daily_stat(db, uid, "fish_sell_value_today", fish_value);
                                ::commands::daily_challenges::track_daily_stat(db, uid, "coins_earned_today", fish_value);
                                
                                // Check for fish value achievements
                                achievements::check_achievements_for_stat(bot, db, event.command.channel_id, uid, "fish_value");
                                
                                // Log fish sale to history
                                int64_t new_balance = db->get_wallet(uid);
                                std::string log_desc = "sold fish " + target + " ($" + format_number(fish_value) + ")";
                                bronx::db::history_operations::log_fishing(db, uid, log_desc, fish_value, new_balance);
                                
                                reply_success("sold fish `" + target + "` for $" + format_number(fish_value));
                            } else {
                                reply_error("failed to sell fish");
                            }
                        }
                        break;
                    }
                }
                if (!found) {
                    reply_error("fish not found in your inventory");
                }
            }
        },
        {
            dpp::command_option(dpp::co_string, "target", "fish ID or 'all' to sell unlocked fish", true)
        });
    
    return sellfish;
}

} // namespace fishing
} // namespace commands
