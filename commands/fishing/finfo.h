#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../economy_core.h"
#include "fishing_helpers.h"
#include <dpp/dpp.h>
#include <algorithm>
#include <sstream>

using namespace bronx::db;

namespace commands {
namespace fishing {

inline Command* get_finfo_command(Database* db) {
    static Command* finfo = new Command("finfo", "view detailed information about a fish", "fishing", {"fishinfo", "fi"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // helper to extract fish name from metadata
            auto parse_name = [](const ::std::string& metadata) {
                ::std::string fish_name = "Unknown";
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos != ::std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start, end - start);
                }
                return fish_name;
            };

            // build list of collectible fish items
            auto inventory = db->get_inventory(event.msg.author.id);
            ::std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) {
                if (item.item_type == "collectible") {
                    fish_items.push_back(item);
                }
            }

            if (fish_items.empty()) {
                bronx::send_message(bot, event, bronx::error("you haven't caught any fish yet"));
                return;
            }

            // page rendering lambda (captures by reference)
            auto render_page = [&](int page, bool update=false, const dpp::message& orig_msg = dpp::message()) {
                int per_page = 10;
                int total = fish_items.size();
                int pages = (total + per_page - 1) / per_page;
                if (page < 1) page = 1;
                if (page > pages) page = pages;
                int start = (page - 1) * per_page;
                int end = std::min(start + per_page, total);

                ::std::string desc;
                for (int i = start; i < end; ++i) {
                    auto& item = fish_items[i];
                    ::std::string name = parse_name(item.metadata);
                    desc += ::std::to_string(i+1) + ". `" + item.item_id + "` " + name + "\n";
                }
                desc += "\n_page " + ::std::to_string(page) + " of " + ::std::to_string(pages) + "_";

                auto embed = bronx::create_embed(desc);
                bronx::add_invoker_footer(embed, event.msg.author);

                // build components
                std::vector<dpp::component> rows;
                // navigation buttons
                dpp::component nav_row;
                if (page > 1) {
                      dpp::component prev_btn;
                      prev_btn.set_type(dpp::cot_button);
                      prev_btn.set_label("◀ prev");
                      prev_btn.set_style(dpp::cos_secondary);
                      prev_btn.set_id("finfo_nav_" + ::std::to_string(page) + "_" + ::std::to_string(event.msg.author.id) + "_prev");
                      nav_row.add_component(prev_btn);
                  }
                  if (page < pages) {
                      dpp::component next_btn;
                      next_btn.set_type(dpp::cot_button);
                      next_btn.set_label("next ▶");
                      next_btn.set_style(dpp::cos_secondary);
                    next_btn.set_id("finfo_nav_" + ::std::to_string(page) + "_" + ::std::to_string(event.msg.author.id) + "_next");
                    nav_row.add_component(next_btn);
                }
                if (!nav_row.components.empty()) rows.push_back(nav_row);

                // select menu for current page
                dpp::component sel_row;
                dpp::component select_menu;
                select_menu.set_type(dpp::cot_selectmenu)
                    .set_placeholder("select a fish to view details")
                    .set_id("finfo_select_" + ::std::to_string(event.msg.author.id) + "_" + ::std::to_string(page));
                for (int i = start; i < end; ++i) {
                    auto& item = fish_items[i];
                    ::std::string name = parse_name(item.metadata);
                    select_menu.add_select_option(dpp::select_option(name + " (" + item.item_id + ")", item.item_id));
                }
                sel_row.add_component(select_menu);
                rows.push_back(sel_row);

                dpp::message msg;
                msg.add_embed(embed);
                for (auto &r : rows) msg.add_component(r);

                if (update) {
                    event.reply(msg);
                } else {
                    bronx::send_message(bot, event, msg);
                }
            };

            if (args.empty()) {
                render_page(1);
                return;
            }

            // existing behaviour follows, but add back button on info page
            ::std::string fish_id = args[0];
            ::std::transform(fish_id.begin(), fish_id.end(), fish_id.begin(), ::toupper);
            
            bool found = false;
            
            for (const auto& item : fish_items) {
                if (item.item_id == fish_id) {
                    found = true;
// parse metadata and build description
                      ::std::string metadata = item.metadata;
                      ::std::string fish_name;
                      int64_t fish_value = 0;
                      bool is_locked = false;

                      size_t name_pos = metadata.find("\"name\":\"");
                      if (name_pos != ::std::string::npos) {
                          size_t start = name_pos + 8;
                          size_t end = metadata.find("\"", start);
                          fish_name = metadata.substr(start, end - start);
                      }

                      size_t value_pos = metadata.find("\"value\":");
                      if (value_pos != ::std::string::npos) {
                          size_t start = value_pos + 8;
                          size_t end = metadata.find(",", start);
                          if (end == ::std::string::npos) end = metadata.find("}", start);
                          fish_value = ::std::stoll(metadata.substr(start, end - start));
                      }

                      size_t locked_pos = metadata.find("\"locked\":");
                      if (locked_pos != ::std::string::npos) {
                          is_locked = (metadata.find("true", locked_pos) != ::std::string::npos);
                      }

                      // Find fish type
                      FishType* fish_type = nullptr;
                      for (auto& ft : fish_types) {
                          if (ft.name == fish_name) {
                              fish_type = &ft;
                              break;
                          }
                      }
                      if (!fish_type) {
                          bronx::send_message(bot, event, bronx::error("unknown fish type"));
                          return;
                      }

                      int total_weight = 0;
                      for (const auto& ft : fish_types) total_weight += ft.weight;
                      double catch_odds = (double)fish_type->weight / total_weight * 100.0;
                      ::std::string odds_str = ::std::to_string(catch_odds);
                      odds_str = odds_str.substr(0, odds_str.find(".") + 3) + "%";

                      // Calculate realistic value range based on actual fish value and possible multipliers
                      // Fish can be affected by luck bonuses, bait effects, fish effects, etc.
                      // If fish value is way above base max, estimate the realistic range
                      int64_t display_min_value = fish_type->min_value;
                      int64_t display_max_value = fish_type->max_value;
                      
                      if (fish_value > fish_type->max_value * 1.5) {
                          // Fish value is significantly higher than base max, estimate actual range
                          // Assume the fish was caught with multipliers, estimate the range
                          double estimated_multiplier = (double)fish_value / ((fish_type->min_value + fish_type->max_value) / 2.0);
                          display_min_value = (int64_t)(fish_type->min_value * std::max(1.0, estimated_multiplier * 0.3));
                          display_max_value = (int64_t)(fish_type->max_value * std::max(1.0, estimated_multiplier * 1.2));
                      } else if (fish_value < fish_type->min_value * 0.8) {
                          // Fish is below expected minimum, keep base range but adjust for display
                          display_min_value = std::min(fish_value, fish_type->min_value);
                      }
                      
                      int64_t value_range = display_max_value - display_min_value;
                      int64_t value_offset = fish_value - display_min_value;
                      double value_percentile = value_range > 0 ? (double)value_offset / value_range * 100.0 : 50.0;
                      // Clamp percentile to reasonable bounds
                      if (value_percentile > 100.0) value_percentile = 99.9;
                      if (value_percentile < 0.0) value_percentile = 0.1;

                      ::std::string description = fish_type->emoji + " **" + fish_name + "** `" + fish_id + "`";
                      // include any extra flavour text
                      if (!fish_type->description.empty()) {
                          description += "\n" + fish_type->description + "\n";
                      }
                      if (is_locked) description += " ❤️";
                      description += "\n\n";
                      description += "**rarity:** " + fish_name;
                      if (catch_odds < 1.0) description += " 🌟";
                      description += "\n**catch odds:** " + odds_str;
                      if (catch_odds < 1.0) {
                          description += " — *extremely rare!*";
                      } else if (catch_odds < 5.0) {
                          description += " — *very rare*";
                      } else if (catch_odds < 15.0) {
                          description += " — *uncommon*";
                      }
                      description += "\n\n";
                      description += "**value:** $" + format_number(fish_value) + "\n";
                      if (display_min_value != fish_type->min_value || display_max_value != fish_type->max_value) {
                          description += "**value range:** $" + format_number(display_min_value) + " - $" + format_number(display_max_value) + " *(with effects)*\n";
                      } else {
                          description += "**value range:** $" + format_number(fish_type->min_value) + " - $" + format_number(fish_type->max_value) + "\n";
                      }
                      if (value_percentile >= 99.0) {
                          description += "**value tier:** top 1% 🏆 *god roll!*\n";
                      } else if (value_percentile >= 95.0) {
                          description += "**value tier:** top 5% ✨ *excellent*\n";
                      } else if (value_percentile >= 75.0) {
                          description += "**value tier:** top 25% 💎 *great*\n";
                      } else if (value_percentile >= 50.0) {
                          description += "**value tier:** top 50% 📈 *above average*\n";
                      } else if (value_percentile >= 25.0) {
                          description += "**value tier:** bottom 50% 📉 *below average*\n";
                      } else if (value_percentile >= 5.0) {
                          description += "**value tier:** bottom 25% 🪨 *low*\n";
                      } else if (value_percentile >= 1.0) {
                          description += "**value tier:** bottom 5% 💩 *poor*\n";
                      } else {
                          description += "**value tier:** bottom 1% ⚰️ *trash tier*\n";
                      }
                      double overall_rarity = (100.0 - catch_odds) * (value_percentile / 100.0);
                      description += "\n**overall score:** ";
                      if (overall_rarity > 98.0) {
                          description += "🌈 *legendary specimen*";
                      } else if (overall_rarity > 90.0) {
                          description += "🌟 *exceptional*";
                      } else if (overall_rarity > 75.0) {
                          description += "✨ *impressive*";
                      } else if (overall_rarity > 50.0) {
                          description += bronx::EMOJI_STAR + " *solid*";
                      } else {
                          description += "📊 *standard*";
                      }
                    
                    auto embed = bronx::create_embed(description);
                    bronx::add_invoker_footer(embed, event.msg.author);
                    
                    // add back button to embed
                    dpp::component btnrow;
dpp::component back_btn;
                      back_btn.set_type(dpp::cot_button);
                      back_btn.set_label("🔙 back to inventory");
                      back_btn.set_style(dpp::cos_secondary);
                    back_btn.set_id("finfo_back_1_" + ::std::to_string(event.msg.author.id));
                    btnrow.add_component(back_btn);
                    
                    dpp::message out;
                    out.add_embed(embed);
                    out.add_component(btnrow);
                    
                    bronx::send_message(bot, event, out);
                    break;
                }
            }
            
            if (!found) {
                bronx::send_message(bot, event, bronx::error("fish not found in your inventory"));
            }
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Query user's fish inventory
            auto inventory = db->get_inventory(event.command.get_issuing_user().id);
            
            ::std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) {
                if (item.item_type == "collectible") {
                    fish_items.push_back(item);
                }
            }
            
            if (fish_items.empty()) {
                event.reply(dpp::message().add_embed(
                    bronx::error("you haven't caught any fish yet")));
                return;
            }
            
            // Create select menu with fish options
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a fish to view details")
                .set_id("finfo_select_" + ::std::to_string(event.command.get_issuing_user().id));
            
            // Limit to 25 fish (Discord select menu limit)
            int count = 0;
            for (const auto& item : fish_items) {
                if (count >= 25) break;
                
                // Parse fish name from metadata
                ::std::string fish_name = "Unknown";
                size_t name_pos = item.metadata.find("\"name\":\"");
                if (name_pos != ::std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = item.metadata.find("\"", start);
                    fish_name = item.metadata.substr(start, end - start);
                }
                
                // Find emoji
                ::std::string fish_emoji = "🐟";
                for (const auto& fish_type : fish_types) {
                    if (fish_type.name == fish_name) {
                        fish_emoji = fish_type.emoji;
                        break;
                    }
                }
                
                select_menu.add_select_option(
                    dpp::select_option(item.item_id + " - " + fish_name, item.item_id, fish_emoji)
                );
                count++;
            }
            
            auto embed = bronx::create_embed("**select a fish**\n\nchoose a fish from the dropdown below to view detailed information");
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            
            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(dpp::component().add_component(select_menu));
            
            event.reply(msg);
        });
    
    return finfo;
}

// Register fish info select menu handler
inline void register_finfo_interactions(dpp::cluster& bot, Database* db) {
    bot.on_select_click([db, &bot](const dpp::select_click_t& event) {
        if (event.custom_id.find("finfo_select_") != 0) return;
        
        // Extract user ID and page from custom_id (format finfo_select_<uid>_<page>)
        ::std::string rest = event.custom_id.substr(13); // after prefix
        ::std::vector<std::string> cid_parts;
        ::std::stringstream css(rest);
        ::std::string part;
        while (::std::getline(css, part, '_')) cid_parts.push_back(part);
        if (cid_parts.size() < 1) return;
        dpp::snowflake expected_user_id = ::std::stoull(cid_parts[0]);
        int current_page = 1;
        if (cid_parts.size() >= 2) current_page = ::std::stoi(cid_parts[1]);
        
        // Verify the user clicking is the one who invoked the command
        if (event.command.get_issuing_user().id != expected_user_id) {
            event.reply(dpp::ir_channel_message_with_source, 
                dpp::message().add_embed(bronx::error("this menu isn't for you")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        ::std::string fish_id = event.values[0];
        
        // Query inventory and find the fish
        auto inventory = db->get_inventory(event.command.get_issuing_user().id);
        bool found = false;
        
        for (const auto& item : inventory) {
            if (item.item_id == fish_id && item.item_type == "collectible") {
                found = true;
                
                // Parse JSON metadata
                ::std::string metadata = item.metadata;
                ::std::string fish_name = "";
                int64_t fish_value = 0;
                bool is_locked = false;
                
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos != ::std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start, end - start);
                }
                
                size_t value_pos = metadata.find("\"value\":");
                if (value_pos != ::std::string::npos) {
                    size_t start = value_pos + 8;
                    size_t end = metadata.find(",", start);
                    if (end == ::std::string::npos) end = metadata.find("}", start);
                    fish_value = ::std::stoll(metadata.substr(start, end - start));
                }
                
                size_t locked_pos = metadata.find("\"locked\":");
                if (locked_pos != ::std::string::npos) {
                    is_locked = (metadata.find("true", locked_pos) != ::std::string::npos);
                }
                
                // Find fish type and calculate stats
                FishType* fish_type = nullptr;
                for (auto& ft : fish_types) {
                    if (ft.name == fish_name) {
                        fish_type = &ft;
                        break;
                    }
                }
                
                if (!fish_type) {
                      event.reply(dpp::message().add_embed(bronx::error("unknown fish type")));
                    return;
                }
                
                // Calculate total weight for odds
                int total_weight = 0;
                for (const auto& ft : fish_types) {
                    total_weight += ft.weight;
                }
                
                // Calculate catch odds
                double catch_odds = (double)fish_type->weight / total_weight * 100.0;
                ::std::string odds_str = ::std::to_string(catch_odds);
                odds_str = odds_str.substr(0, odds_str.find(".") + 3) + "%";
                
                // Calculate realistic value range based on actual fish value and possible multipliers
                int64_t display_min_value = fish_type->min_value;
                int64_t display_max_value = fish_type->max_value;
                
                if (fish_value > fish_type->max_value * 1.5) {
                    // Fish value is significantly higher, estimate actual range
                    double estimated_multiplier = (double)fish_value / ((fish_type->min_value + fish_type->max_value) / 2.0);
                    display_min_value = (int64_t)(fish_type->min_value * std::max(1.0, estimated_multiplier * 0.3));
                    display_max_value = (int64_t)(fish_type->max_value * std::max(1.0, estimated_multiplier * 1.2));
                } else if (fish_value < fish_type->min_value * 0.8) {
                    display_min_value = std::min(fish_value, fish_type->min_value);
                }
                
                int64_t value_range = display_max_value - display_min_value;
                int64_t value_offset = fish_value - display_min_value;
                double value_percentile = value_range > 0 ? (double)value_offset / value_range * 100.0 : 50.0;
                if (value_percentile > 100.0) value_percentile = 99.9;
                if (value_percentile < 0.0) value_percentile = 0.1;
                
                // Build description
                ::std::string description = fish_type->emoji + " **" + fish_name + "** `" + fish_id + "`";
                if (is_locked) description += " ❤️";
                description += "\n\n";
                
                // Rarity info
                description += "**rarity:** " + fish_name;
                if (catch_odds < 1.0) {
                    description += " 🌟";
                }
                description += "\n**catch odds:** " + odds_str;
                
                // Add funky odds note
                if (catch_odds < 1.0) {
                    description += " — *extremely rare!*";
                } else if (catch_odds < 5.0) {
                    description += " — *very rare*";
                } else if (catch_odds < 15.0) {
                    description += " — *uncommon*";
                }
                description += "\n\n";
                
                // Value info
                description += "**value:** $" + format_number(fish_value) + "\n";
                if (display_min_value != fish_type->min_value || display_max_value != fish_type->max_value) {
                    description += "**value range:** $" + format_number(display_min_value) + " - $" + format_number(display_max_value) + " *(with effects)*\n";
                } else {
                    description += "**value range:** $" + format_number(fish_type->min_value) + " - $" + format_number(fish_type->max_value) + "\n";
                }
                
                // Value percentile with special indicators
                if (value_percentile >= 99.0) {
                    description += "**value tier:** top 1% 🏆 *god roll!*\n";
                } else if (value_percentile >= 95.0) {
                    description += "**value tier:** top 5% ✨ *excellent*\n";
                } else if (value_percentile >= 75.0) {
                    description += "**value tier:** top 25% 💎 *great*\n";
                } else if (value_percentile >= 50.0) {
                    description += "**value tier:** top 50% 📈 *above average*\n";
                } else if (value_percentile >= 25.0) {
                    description += "**value tier:** bottom 50% 📉 *below average*\n";
                } else if (value_percentile >= 5.0) {
                    description += "**value tier:** bottom 25% 🪨 *low*\n";
                } else if (value_percentile >= 1.0) {
                    description += "**value tier:** bottom 5% 💩 *poor*\n";
                } else {
                    description += "**value tier:** bottom 1% ⚰️ *trash tier*\n";
                }
                
                // Combined rarity score
                double overall_rarity = (100.0 - catch_odds) * (value_percentile / 100.0);
                description += "\n**overall score:** ";
                if (overall_rarity > 98.0) {
                    description += "🌈 *legendary specimen*";
                } else if (overall_rarity > 90.0) {
                    description += "🌟 *exceptional*";
                } else if (overall_rarity > 75.0) {
                    description += "✨ *impressive*";
                } else if (overall_rarity > 50.0) {
                    description += bronx::EMOJI_STAR + " *solid*";
                } else {
                    description += "📊 *standard*";
                }
                
                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                
                event.reply(dpp::message().add_embed(embed));
                break;
            }
        }
        
        if (!found) {
              event.reply(dpp::message().add_embed(bronx::error("fish not found in your inventory")));
        }
    });

    // Handle navigation and back buttons for inventory
    bot.on_button_click([db, &bot](const dpp::button_click_t& event) {
        std::string cid = event.custom_id;
        // expected formats: finfo_nav_<page>_<uid>_<dir>, finfo_back_<page>_<uid>
        if (cid.rfind("finfo_nav_", 0) == 0) {
            // parse
            std::vector<std::string> parts;
            std::stringstream ss(cid);
            std::string part;
            while (std::getline(ss, part, '_')) parts.push_back(part);
            if (parts.size() != 5) return;
            int page = std::stoi(parts[2]);
            uint64_t uid = std::stoull(parts[3]);
            std::string dir = parts[4];
            if (event.command.get_issuing_user().id != uid) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::error("these buttons aren't for you")).set_flags(dpp::m_ephemeral));
                return;
            }
            // rebuild fish list
            auto inventory = db->get_inventory(uid);
            std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) if (item.item_type == "collectible") fish_items.push_back(item);
            if (fish_items.empty()) return;
            int per_page = 10;
            int total = fish_items.size();
            int pages = (total + per_page - 1) / per_page;
            if (dir == "prev" && page > 1) page--;
            else if (dir == "next" && page < pages) page++;
            // use same rendering code as text handler
            // copy render_page lambda here
            auto parse_name = [&](const std::string& metadata) {
                std::string fish_name = "Unknown";
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos != std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start, end - start);
                }
                return fish_name;
            };
            int start = (page - 1) * per_page;
            int end = std::min(start + per_page, total);
            std::string desc;
            for (int i = start; i < end; ++i) {
                auto& item = fish_items[i];
                std::string name = parse_name(item.metadata);
                desc += std::to_string(i+1) + ". `" + item.item_id + "` " + name + "\n";
            }
            desc += "\n_page " + std::to_string(page) + " of " + std::to_string(pages) + "_";
            dpp::embed embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            std::vector<dpp::component> rows;
            dpp::component nav_row;
            if (page > 1) {
                dpp::component prev_btn;
                  prev_btn.set_type(dpp::cot_button);
                prev_btn.set_label("◀ prev"); prev_btn.set_style(dpp::cos_secondary);
                prev_btn.set_id("finfo_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_prev");
                nav_row.add_component(prev_btn);
            }
            if (page < pages) {
                dpp::component next_btn;
                  next_btn.set_type(dpp::cot_button);
                next_btn.set_label("next ▶"); next_btn.set_style(dpp::cos_secondary);
                next_btn.set_id("finfo_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_next");
                nav_row.add_component(next_btn);
            }
            if (!nav_row.components.empty()) rows.push_back(nav_row);
            dpp::component sel_row;
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a fish to view details")
                .set_id("finfo_select_" + std::to_string(uid) + "_" + std::to_string(page));
            for (int i = start; i < end; ++i) {
                auto& item = fish_items[i];
                std::string name = parse_name(item.metadata);
                select_menu.add_select_option(dpp::select_option(name + " (" + item.item_id + ")", item.item_id));
            }
            sel_row.add_component(select_menu);
            rows.push_back(sel_row);
            dpp::message msg;
            msg.add_embed(embed);
            for (auto &r : rows) msg.add_component(r);
            event.reply(msg);
            return;
        } else if (cid.rfind("finfo_back_",0) == 0) {
            // parse page and uid
            std::vector<std::string> parts;
            std::stringstream ss(cid);
            std::string part;
            while (std::getline(ss, part, '_')) parts.push_back(part);
            if (parts.size() != 4) return;
            int page = std::stoi(parts[2]);
            uint64_t uid = std::stoull(parts[3]);
            if (event.command.get_issuing_user().id != uid) return;
            // render that page below
            auto inventory = db->get_inventory(uid);
            std::vector<InventoryItem> fish_items;
            for (const auto& item : inventory) if (item.item_type == "collectible") fish_items.push_back(item);
            if (fish_items.empty()) return;
            int per_page = 10;
            int total = fish_items.size();
            int pages = (total + per_page - 1) / per_page;
            if (page < 1) page = 1;
            if (page > pages) page = pages;
            auto parse_name = [&](const std::string& metadata) {
                std::string fish_name = "Unknown";
                size_t name_pos = metadata.find("\"name\":\"");
                if (name_pos != std::string::npos) {
                    size_t start = name_pos + 8;
                    size_t end = metadata.find("\"", start);
                    fish_name = metadata.substr(start, end - start);
                }
                return fish_name;
            };
            int start = (page - 1) * per_page;
            int end = std::min(start + per_page, total);
            std::string desc;
            for (int i = start; i < end; ++i) {
                auto& item = fish_items[i];
                std::string name = parse_name(item.metadata);
                desc += std::to_string(i+1) + ". `" + item.item_id + "` " + name + "\n";
            }
            desc += "\n_page " + std::to_string(page) + " of " + std::to_string(pages) + "_";
            dpp::embed embed = bronx::create_embed(desc);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            std::vector<dpp::component> rows;
            dpp::component nav_row;
            if (page > 1) {
                dpp::component prev_btn;
                  prev_btn.set_type(dpp::cot_button);
                prev_btn.set_label("◀ prev"); prev_btn.set_style(dpp::cos_secondary);
                prev_btn.set_id("finfo_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_prev");
                nav_row.add_component(prev_btn);
            }
            if (page < pages) {
                dpp::component next_btn;
                  next_btn.set_type(dpp::cot_button);
                next_btn.set_label("next ▶"); next_btn.set_style(dpp::cos_secondary);
                next_btn.set_id("finfo_nav_" + std::to_string(page) + "_" + std::to_string(uid) + "_next");
                nav_row.add_component(next_btn);
            }
            if (!nav_row.components.empty()) rows.push_back(nav_row);
            dpp::component sel_row;
            dpp::component select_menu;
            select_menu.set_type(dpp::cot_selectmenu)
                .set_placeholder("select a fish to view details")
                .set_id("finfo_select_" + std::to_string(uid) + "_" + std::to_string(page));
            for (int i = start; i < end; ++i) {
                auto& item = fish_items[i];
                std::string name = parse_name(item.metadata);
                select_menu.add_select_option(dpp::select_option(name + " (" + item.item_id + ")", item.item_id));
            }
            sel_row.add_component(select_menu);
            rows.push_back(sel_row);
            dpp::message msg;
            msg.add_embed(embed);
            for (auto &r : rows) msg.add_component(r);
            event.reply(msg);
            return;
        }
    });
}

} // namespace fishing
} // namespace commands
