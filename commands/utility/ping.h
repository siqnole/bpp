#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "utility_helpers.h"
#include <dpp/dpp.h>
#include <chrono>

namespace commands {
namespace utility {

inline Command* get_ping_command() {
    static Command ping("ping", "check bot latency and response time", "utility", {"pong", "ms"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto start_time = ::std::chrono::high_resolution_clock::now();
            
            // Get websocket latency (track which shard this instance is using)
            double ws_latency = 0;
            int shard_id = -1;
            auto shards = bot.get_shards();
            if (!shards.empty()) {
                shard_id = shards.begin()->first;
                ws_latency = shards.begin()->second->websocket_ping * 1000;
            }
            
            // Create initial embed
            auto loading_embed = bronx::info("calculating latency...");
            bot.message_create(dpp::message(event.msg.channel_id, loading_embed), [&bot, event, start_time, ws_latency, shard_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) return;
                
                auto end_time = ::std::chrono::high_resolution_clock::now();
                auto round_trip_us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end_time - start_time).count();
                auto round_trip_ms = round_trip_us / 1000.0;
                
                auto sent_msg = ::std::get<dpp::message>(callback.value);
                
                // Check if user is bot owner
                bot.current_application_get([&bot, event, ws_latency, round_trip_ms, round_trip_us, sent_msg, shard_id](const dpp::confirmation_callback_t& app_callback) {
                    bool is_owner = false;
                    if (!app_callback.is_error()) {
                        auto app = ::std::get<dpp::application>(app_callback.value);
                        is_owner = (app.owner.id == event.msg.author.id);
                    }
                    
                    ::std::string description = "**websocket:** `" + ::std::to_string((int)ws_latency) + "ms`";
                    if (shard_id >= 0) {
                        description += " (shard " + ::std::to_string(shard_id) + ")";
                    }
                    description += "\n";
                    description += "**round trip:** `" + ::std::to_string(round_trip_ms).substr(0, ::std::to_string(round_trip_ms).find('.') + 3) + "ms`\n";
                    description += "**precision:** `" + ::std::to_string(round_trip_us) + "μs`\n";
                    description += "**version:** `" + get_build_version() + "`";
                    
                    if (is_owner) {
                        auto total_secs = bot.uptime().to_secs();
                        auto weeks = total_secs / (7 * 24 * 3600);
                        auto days = (total_secs % (7 * 24 * 3600)) / (24 * 3600);
                        auto hours = (total_secs % (24 * 3600)) / 3600;
                        auto minutes = (total_secs % 3600) / 60;
                        auto seconds = total_secs % 60;

                        ::std::string uptime_str;
                        if (weeks > 0) {
                            uptime_str = ::std::to_string(weeks) + "w";
                        } else if (days > 0) {
                            uptime_str = ::std::to_string(days) + "d";
                        } else if (hours > 0) {
                            uptime_str = ::std::to_string(hours) + "h";
                        } else if (minutes > 0) {
                            uptime_str = ::std::to_string(minutes) + "m";
                        } else {
                            uptime_str = ::std::to_string(seconds) + "s";
                        }

                        description += "\n\n**owner data:**\n";
                        description += "**uptime:** `" + uptime_str + "`";
                    }
                    
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    
                    dpp::message edit_msg = sent_msg;
                    edit_msg.embeds.clear();
                    edit_msg.add_embed(embed);
                    bot.message_edit(edit_msg);
                });
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto start_time = ::std::chrono::high_resolution_clock::now();
            
            // Get websocket latency (track which shard this instance is using)
            double ws_latency = 0;
            int shard_id = -1;
            auto shards = bot.get_shards();
            if (!shards.empty()) {
                shard_id = shards.begin()->first;
                ws_latency = shards.begin()->second->websocket_ping * 1000;
            }
            
            // Reply with thinking state
            event.thinking(false, [&bot, event, start_time, ws_latency, shard_id](const dpp::confirmation_callback_t& callback) {
                auto end_time = ::std::chrono::high_resolution_clock::now();
                auto round_trip_us = ::std::chrono::duration_cast<::std::chrono::microseconds>(end_time - start_time).count();
                auto round_trip_ms = round_trip_us / 1000.0;
                
                // Check if user is bot owner
                bot.current_application_get([&bot, event, ws_latency, round_trip_ms, round_trip_us, shard_id](const dpp::confirmation_callback_t& app_callback) {
                    bool is_owner = false;
                    if (!app_callback.is_error()) {
                        auto app = ::std::get<dpp::application>(app_callback.value);
                        is_owner = (app.owner.id == event.command.get_issuing_user().id);
                    }
                    
                    ::std::string description = "**websocket:** `" + ::std::to_string((int)ws_latency) + "ms`";
                    if (shard_id >= 0) {
                        description += " (shard " + ::std::to_string(shard_id) + ")";
                    }
                    description += "\n";
                    description += "**round trip:** `" + ::std::to_string(round_trip_ms).substr(0, ::std::to_string(round_trip_ms).find('.') + 3) + "ms`\n";
                    description += "**precision:** `" + ::std::to_string(round_trip_us) + "μs`\n";
                    description += "**version:** `" + get_build_version() + "`";
                    
                    if (is_owner) {
                        description += "\n\n**owner data:**\n";
                        description += "**uptime:** `" + ::std::to_string(bot.uptime().to_secs()) + "s`";
                    }
                    
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                    
                    event.edit_original_response(dpp::message().add_embed(embed));
                });
            });
        });
    
    return &ping;
}

} // namespace utility
} // namespace commands
