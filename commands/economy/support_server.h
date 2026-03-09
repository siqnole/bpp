#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "helpers.h"
#include <dpp/dpp.h>
#include <vector>

using namespace bronx::db;

namespace commands {
namespace support_server {

// Support server ID
static const uint64_t SUPPORT_SERVER_ID = 1259717095382319215ULL;

// Helper to check if command is in support server
inline bool is_support_server(uint64_t guild_id) {
    return guild_id == SUPPORT_SERVER_ID;
}

// Exclusive support server command: Special daily reward
inline Command* get_support_daily_command(Database* db) {
    static Command* support_daily = new Command("supportdaily", "claim your special support server daily reward", "economy", {"sdaily"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            if (!is_support_server(guild_id)) {
                bronx::send_message(bot, event, bronx::error("this command is exclusive to the official support server!"));
                return;
            }
            
            uint64_t uid = event.msg.author.id;
            
            // Check cooldown (24 hours)
            if (db->is_on_cooldown(uid, "support_daily")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "support_daily")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    bronx::send_message(bot, event, bronx::error("you already claimed your support daily! come back <t:" + std::to_string(timestamp) + ":R>"));
                }
                return;
            }
            
            // Generous reward for support server members
            int64_t reward = 50000;
            db->update_wallet(uid, reward);
            db->set_cooldown(uid, "support_daily", 86400); // 24 hours
            
            std::string desc = "🎁 **support server exclusive reward**\n\n";
            desc += "thank you for being part of our community!\n";
            desc += "you received **$" + economy::format_number(reward) + "**\n\n";
            desc += "*come back tomorrow for another reward!*";
            
            auto embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            if (!is_support_server(guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("this command is exclusive to the official support server!")));
                return;
            }
            
            uint64_t uid = event.command.get_issuing_user().id;
            
            // Check cooldown (24 hours)
            if (db->is_on_cooldown(uid, "support_daily")) {
                if (auto expiry = db->get_cooldown_expiry(uid, "support_daily")) {
                    auto timestamp = std::chrono::system_clock::to_time_t(expiry.value());
                    event.reply(dpp::message().add_embed(bronx::error("you already claimed your support daily! come back <t:" + std::to_string(timestamp) + ":R>")));
                }
                return;
            }
            
            // Generous reward for support server members
            int64_t reward = 50000;
            db->update_wallet(uid, reward);
            db->set_cooldown(uid, "support_daily", 86400); // 24 hours
            
            std::string desc = "🎁 **support server exclusive reward**\n\n";
            desc += "thank you for being part of our community!\n";
            desc += "you received **$" + economy::format_number(reward) + "**\n\n";
            desc += "*come back tomorrow for another reward!*";
            
            auto embed = bronx::create_embed(desc);
            event.reply(dpp::message().add_embed(embed));
        });
    return support_daily;
}

// Exclusive support server command: VIP shop with special items
inline Command* get_support_shop_command(Database* db) {
    static Command* support_shop = new Command("supportshop", "access the exclusive support server shop", "economy", {"sshop"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            uint64_t guild_id = event.msg.guild_id;
            if (!is_support_server(guild_id)) {
                bronx::send_message(bot, event, bronx::error("this command is exclusive to the official support server!"));
                return;
            }
            
            std::string desc = "🏪 **support server exclusive shop**\n\n";
            desc += "special items only available here:\n\n";
            desc += "🌟 **supporter badge** - $100,000\n";
            desc += "   *show your support with this exclusive badge*\n\n";
            desc += "🎣 **legendary fishing rod** - $500,000\n";
            desc += "   *the best rod in the game - support server only*\n\n";
            desc += "💎 **premium booster pack** - $250,000\n";
            desc += "   *contains 5 random high-tier items*\n\n";
            desc += "use `/buy <item>` to purchase items";
            
            auto embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            uint64_t guild_id = event.command.guild_id;
            if (!is_support_server(guild_id)) {
                event.reply(dpp::message().add_embed(bronx::error("this command is exclusive to the official support server!")));
                return;
            }
            
            std::string desc = "🏪 **support server exclusive shop**\n\n";
            desc += "special items only available here:\n\n";
            desc += "🌟 **supporter badge** - $100,000\n";
            desc += "   *show your support with this exclusive badge*\n\n";
            desc += "🎣 **legendary fishing rod** - $500,000\n";
            desc += "   *the best rod in the game - support server only*\n\n";
            desc += "💎 **premium booster pack** - $250,000\n";
            desc += "   *contains 5 random high-tier items*\n\n";
            desc += "use `/buy <item>` to purchase items";
            
            auto embed = bronx::create_embed(desc);
            event.reply(dpp::message().add_embed(embed));
        });
    return support_shop;
}

// Get all support server commands
inline std::vector<Command*> get_support_server_commands(Database* db) {
    static std::vector<Command*> cmds;
    static bool initialized = false;
    
    if (!initialized) {
        cmds.push_back(get_support_daily_command(db));
        cmds.push_back(get_support_shop_command(db));
        initialized = true;
    }
    
    return cmds;
}

} // namespace support_server
} // namespace commands
