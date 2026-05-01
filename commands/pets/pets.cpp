#include "pets.h"
#include <iostream>
#include <ctime>

namespace commands {
namespace pets {

bool g_pet_tables_created = false;
std::mutex g_pet_mutex;

const PetSpecies* find_species(const std::string& id) {
    for (const auto& s : PET_SPECIES) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

std::string rarity_color(const std::string& rarity) {
    if (rarity == "common")    return "\xE2\xAC\x9C"; // ⬜
    if (rarity == "uncommon")  return "\xF0\x9F\x9F\xA9"; // 🟩
    if (rarity == "rare")      return "\xF0\x9F\x9F\xA6"; // 🟦
    if (rarity == "epic")      return "\xF0\x9F\x9F\xAA"; // 🟪
    if (rarity == "legendary") return "\xF0\x9F\x9F\xA8"; // 🟨
    if (rarity == "prestige")  return "\xF0\x9F\x9F\xA5"; // 🟥
    return "\xE2\xAC\x9C";
}

std::string format_bonus_name(const std::string& bonus) {
    std::string result;
    std::string word;
    for (size_t i = 0; i <= bonus.size(); i++) {
        if (i == bonus.size() || bonus[i] == '_') {
            if (!word.empty()) {
                result += word;
                result += ' ';
                word.clear();
            }
        } else {
            word += bonus[i];
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

int calculate_current_hunger(int stored_hunger, const std::string& last_fed) {
    if (last_fed.empty()) return 0;
    
    auto now = std::chrono::system_clock::now();
    time_t tnow = std::chrono::system_clock::to_time_t(now);
    
    tm tm_fed = {};
    strptime(last_fed.c_str(), "%Y-%m-%d %H:%M:%S", &tm_fed);
    time_t t_fed = mktime(&tm_fed);
    
    int hours_elapsed = static_cast<int>((tnow - t_fed) / 3600);
    int current = stored_hunger - hours_elapsed;
    return std::max(0, std::min(100, current));
}

int xp_for_level(int level) {
    return static_cast<int>(100 * pow(1.15, level - 1));
}

double get_pet_effective_bonus(const UserPet& pet, const PetSpecies& species) {
    double base = species.base_bonus + (pet.level - 1) * species.bonus_per_level;
    int current_hunger = calculate_current_hunger(pet.hunger, pet.last_fed);
    double hunger_mult = current_hunger / 100.0;
    return base * hunger_mult;
}

std::string hunger_bar(int hunger) {
    std::string bar;
    int filled = hunger / 10;
    int empty = 10 - filled;
    for (int i = 0; i < filled; i++) bar += "\xF0\x9F\x9F\xA2"; // 🟢
    for (int i = 0; i < empty; i++)  bar += "\xE2\xAC\x9B"; // ⬛
    return bar + " " + std::to_string(hunger) + "%";
}

void ensure_pet_tables(Database* db) {
    if (g_pet_tables_created) return;
    std::lock_guard<std::mutex> lock(g_pet_mutex);
    if (g_pet_tables_created) return;
    
    db->execute(
        "CREATE TABLE IF NOT EXISTS user_pets ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  user_id BIGINT UNSIGNED NOT NULL,"
        "  species_id VARCHAR(32) NOT NULL,"
        "  nickname VARCHAR(32) NOT NULL,"
        "  level INT NOT NULL DEFAULT 1,"
        "  xp INT NOT NULL DEFAULT 0,"
        "  hunger INT NOT NULL DEFAULT 100,"
        "  equipped BOOLEAN NOT NULL DEFAULT FALSE,"
        "  adopted_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  last_fed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_user (user_id),"
        "  INDEX idx_user_equipped (user_id, equipped)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    
    g_pet_tables_created = true;
}

std::vector<UserPet> get_user_pets(Database* db, uint64_t user_id) {
    std::vector<UserPet> pets_list;
    std::string sql = "SELECT id, species_id, nickname, level, xp, hunger, equipped, adopted_at, last_fed "
                      "FROM user_pets WHERE user_id = " + std::to_string(user_id) + " ORDER BY equipped DESC, level DESC";
    MYSQL_RES* res = economy::db_select(db, sql);
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            UserPet p;
            p.pet_id = row[0] ? std::stoll(row[0]) : 0;
            p.species_id = row[1] ? row[1] : "";
            p.nickname = row[2] ? row[2] : "";
            p.level = row[3] ? std::stoi(row[3]) : 1;
            p.xp = row[4] ? std::stoi(row[4]) : 0;
            p.hunger = row[5] ? std::stoi(row[5]) : 100;
            p.equipped = row[6] && std::string(row[6]) == "1";
            p.adopted_at = row[7] ? row[7] : "";
            p.last_fed = row[8] ? row[8] : "";
            pets_list.push_back(p);
        }
        mysql_free_result(res);
    }
    return pets_list;
}

UserPet* find_user_pet(std::vector<UserPet>& pets_list, const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    for (auto& p : pets_list) {
        std::string lower_nick = p.nickname;
        std::transform(lower_nick.begin(), lower_nick.end(), lower_nick.begin(), ::tolower);
        if (lower_nick == lower_name) return &p;
        
        std::string lower_species = p.species_id;
        std::transform(lower_species.begin(), lower_species.end(), lower_species.begin(), ::tolower);
        if (lower_species == lower_name) return &p;
    }
    return nullptr;
}

bool adopt_pet(Database* db, uint64_t user_id, const std::string& species_id, const std::string& nickname) {
    std::string esc_species = economy::db_escape(db, species_id);
    std::string esc_nick = economy::db_escape(db, nickname);
    MYSQL_RES* existing = economy::db_select(db, "SELECT COUNT(*) FROM user_pets WHERE user_id = " + std::to_string(user_id));
    bool first_pet = true;
    if (existing) {
        MYSQL_ROW r = mysql_fetch_row(existing);
        if (r && r[0] && std::stoi(r[0]) > 0) first_pet = false;
        mysql_free_result(existing);
    }
    std::string sql = "INSERT INTO user_pets (user_id, species_id, nickname, level, xp, hunger, equipped) "
                      "VALUES (" + std::to_string(user_id) + ", '" + esc_species + "', '" + esc_nick + "', 1, 0, 100, " + (first_pet ? "TRUE" : "FALSE") + ")";
    return economy::db_exec(db, sql);
}

bool feed_pet(Database* db, int64_t pet_id) {
    return economy::db_exec(db, "UPDATE user_pets SET hunger = 100, last_fed = NOW() WHERE id = " + std::to_string(pet_id));
}

bool equip_pet(Database* db, uint64_t user_id, int64_t pet_id) {
    economy::db_exec(db, "UPDATE user_pets SET equipped = FALSE WHERE user_id = " + std::to_string(user_id));
    return economy::db_exec(db, "UPDATE user_pets SET equipped = TRUE WHERE id = " + std::to_string(pet_id)
                              + " AND user_id = " + std::to_string(user_id));
}

bool release_pet(Database* db, uint64_t user_id, int64_t pet_id) {
    return economy::db_exec(db, "DELETE FROM user_pets WHERE id = " + std::to_string(pet_id)
                              + " AND user_id = " + std::to_string(user_id));
}

bool rename_pet(Database* db, int64_t pet_id, const std::string& new_name) {
    std::string esc_name = economy::db_escape(db, new_name);
    return economy::db_exec(db, "UPDATE user_pets SET nickname = '" + esc_name + "' WHERE id = " + std::to_string(pet_id));
}

bool activity_matches_pet(const std::string& bonus_type, const std::string& activity) {
    if (bonus_type == "all_value_bonus" || bonus_type == "all_earnings_bonus" || bonus_type == "prestige_bonus") return true;
    if (bonus_type == "xp_bonus") return true;
    
    if (activity == "fish") {
        return bonus_type == "fish_value_bonus" || bonus_type == "rare_fish_bonus"
            || bonus_type == "legendary_fish_bonus" || bonus_type == "luck_bonus";
    }
    if (activity == "mine") {
        return bonus_type == "ore_value_bonus" || bonus_type == "luck_bonus";
    }
    if (activity == "gamble") {
        return bonus_type == "gambling_luck_bonus" || bonus_type == "luck_bonus";
    }
    if (activity == "work") {
        return bonus_type == "work_bonus" || bonus_type == "rob_protection";
    }
    if (activity == "daily") {
        return bonus_type == "daily_bonus" || bonus_type == "rob_protection";
    }
    return false;
}

bool award_pet_xp(Database* db, uint64_t user_id, const std::string& activity, int count) {
    try {
        ensure_pet_tables(db);
        
        std::string sql = "SELECT id, species_id, level, xp FROM user_pets "
                          "WHERE user_id = " + std::to_string(user_id) + " AND equipped = TRUE LIMIT 1";
        MYSQL_RES* res = economy::db_select(db, sql);
        if (!res) return false;
        
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row) { mysql_free_result(res); return false; }
        
        int64_t pet_id = std::stoll(row[0]);
        std::string species_id = row[1] ? row[1] : "";
        int level = row[2] ? std::stoi(row[2]) : 1;
        int xp = row[3] ? std::stoi(row[3]) : 0;
        mysql_free_result(res);
        
        const PetSpecies* species = find_species(species_id);
        if (!species) return false;
        
        if (!activity_matches_pet(species->bonus_type, activity)) return false;
        if (level >= species->max_level) return false;
        
        int xp_gain = species->xp_per_activity * count;
        if (xp_gain <= 0) xp_gain = 1;
        
        int new_xp = xp + xp_gain;
        int needed = xp_for_level(level);
        bool leveled_up = false;
        int new_level = level;
        
        while (new_xp >= needed && new_level < species->max_level) {
            new_xp -= needed;
            new_level++;
            needed = xp_for_level(new_level);
            leveled_up = true;
        }
        
        if (new_level >= species->max_level) {
            new_level = species->max_level;
            new_xp = 0;
        }
        
        economy::db_exec(db, "UPDATE user_pets SET xp = " + std::to_string(new_xp) +
                             ", level = " + std::to_string(new_level) +
                             " WHERE id = " + std::to_string(pet_id));
        
        return leveled_up;
    } catch (...) {
        return false;
    }
}

namespace pet_hooks {
    void on_fish(Database* db, uint64_t uid, int fish_count) {
        award_pet_xp(db, uid, "fish", fish_count);
    }
    void on_mine(Database* db, uint64_t uid, int ore_count) {
        award_pet_xp(db, uid, "mine", ore_count);
    }
    void on_gamble(Database* db, uint64_t uid) {
        award_pet_xp(db, uid, "gamble", 1);
    }
    void on_work(Database* db, uint64_t uid) {
        award_pet_xp(db, uid, "work", 1);
    }
    void on_daily(Database* db, uint64_t uid) {
        award_pet_xp(db, uid, "daily", 1);
    }
}

dpp::message build_shop_page(int page, int prestige, uint64_t user_id) {
    std::vector<const PetSpecies*> visible;
    for (const auto& s : PET_SPECIES) {
        if (s.rarity == "prestige" && prestige < 5) continue;
        visible.push_back(&s);
    }
    int total = static_cast<int>(visible.size());
    int total_pages = std::max(1, (total + PET_SHOP_PER_PAGE - 1) / PET_SHOP_PER_PAGE);
    if (page < 0) page = 0;
    if (page >= total_pages) page = total_pages - 1;
    int start = page * PET_SHOP_PER_PAGE;
    int end = std::min(start + PET_SHOP_PER_PAGE, total);

    std::string desc = "\xF0\x9F\xBE **pet shop**\n\n";
    std::string current_rarity;
    for (int i = start; i < end; i++) {
        const auto* s = visible[i];
        if (s->rarity != current_rarity) {
            current_rarity = s->rarity;
            desc += "**" + current_rarity + "**\n";
        }
        desc += rarity_color(s->rarity) + " " + s->emoji + " **" + s->name + "**";
        desc += " \xE2\x80\x94 $" + commands::economy::format_number(s->adopt_cost) + "\n";
        desc += "   " + s->description + "\n";
        desc += "   bonus: **+" + std::to_string(static_cast<int>(s->base_bonus)) + "% " + format_bonus_name(s->bonus_type) + "**\n\n";
    }
    desc += "*adopt: `b.pet adopt <type>` | `/pet adopt`*";

    auto embed = bronx::create_embed(desc);
    embed.set_title("\xF0\x9F\xBE pet shop \xE2\x80\x94 page " + std::to_string(page + 1) + "/" + std::to_string(total_pages));

    dpp::message msg;
    msg.add_embed(embed);

    if (total_pages > 1) {
        dpp::component nav_row;
        nav_row.set_type(dpp::cot_action_row);

        dpp::component prev_btn;
        prev_btn.set_type(dpp::cot_button);
        prev_btn.set_emoji("\u25c0\ufe0f");
        prev_btn.set_style(dpp::cos_secondary);
        prev_btn.set_id("pet_shop_nav_" + std::to_string(page) + "_" + std::to_string(user_id) + "_prev");
        prev_btn.set_disabled(page <= 0);
        nav_row.add_component(prev_btn);

        dpp::component page_btn;
        page_btn.set_type(dpp::cot_button);
        page_btn.set_label(std::to_string(page + 1) + "/" + std::to_string(total_pages));
        page_btn.set_style(dpp::cos_secondary);
        page_btn.set_id("pet_shop_pageinfo_" + std::to_string(user_id));
        page_btn.set_disabled(true);
        nav_row.add_component(page_btn);

        dpp::component next_btn;
        next_btn.set_type(dpp::cot_button);
        next_btn.set_emoji("\u25b6\ufe0f");
        next_btn.set_style(dpp::cos_secondary);
        next_btn.set_id("pet_shop_nav_" + std::to_string(page) + "_" + std::to_string(user_id) + "_next");
        next_btn.set_disabled(page >= total_pages - 1);
        nav_row.add_component(next_btn);

        msg.add_component(nav_row);
    }
    return msg;
}

void handle_pet_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db) {
    std::string custom_id = event.custom_id;
    
    if (custom_id.find("pet_shop_nav_") == 0) {
        std::string rem = custom_id.substr(13);
        size_t s1 = rem.find('_');
        if (s1 == std::string::npos) return;
        int current_page = std::stoi(rem.substr(0, s1));
        std::string rest = rem.substr(s1 + 1);
        size_t s2 = rest.rfind('_');
        if (s2 == std::string::npos) return;
        uint64_t target_user = std::stoull(rest.substr(0, s2));
        std::string direction = rest.substr(s2 + 1);

        if (event.command.get_issuing_user().id != target_user) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("open your own shop with `b.pet shop`!")).set_flags(dpp::m_ephemeral));
            return;
        }

        int new_page = (direction == "next") ? current_page + 1 : current_page - 1;
        int prestige = db->get_prestige(target_user);
        dpp::message msg = build_shop_page(new_page, prestige, target_user);
        event.reply(dpp::ir_update_message, msg);
        return;
    }

    if (custom_id.find("pet_feed_") == 0) {
        std::string remainder = custom_id.substr(9);
        size_t sep = remainder.find('_');
        if (sep == std::string::npos) return;
        
        int64_t pet_id = std::stoll(remainder.substr(0, sep));
        uint64_t target_user = std::stoull(remainder.substr(sep + 1));
        
        if (event.command.get_issuing_user().id != target_user) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::error("that's not your pet!")).set_flags(dpp::m_ephemeral));
            return;
        }
        
        if (feed_pet(db, pet_id)) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().add_embed(bronx::success("fed your pet! hunger restored to 100%.")).set_flags(dpp::m_ephemeral));
        }
    }
}

Command* create_pet_command(Database* db) {
    static Command* cmd = new Command("pet", "manage your pets and their bonuses", "economy", {"pets"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            ensure_pet_tables(db);
            uint64_t user_id = event.msg.author.id;
            db->ensure_user_exists(user_id);
            
            std::string action = args.empty() ? "view" : args[0];
            
            if (action == "shop" || action == "store") {
                int prestige = db->get_prestige(user_id);
                dpp::message msg = build_shop_page(0, prestige, user_id);
                if (!msg.embeds.empty()) bronx::add_invoker_footer(msg.embeds[0], event.msg.author);
                bronx::send_message(bot, event, msg);
                return;
            }
            
            if (action == "adopt" || action == "buy") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `b.pet adopt <type>` — see `b.pet shop` for available pets"));
                    return;
                }
                
                std::string species_id = args[1];
                std::transform(species_id.begin(), species_id.end(), species_id.begin(), ::tolower);
                
                const PetSpecies* species = find_species(species_id);
                if (!species) {
                    for (const auto& s : PET_SPECIES) {
                        std::string lower_name = s.name;
                        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                        if (lower_name == species_id || s.id.find(species_id) != std::string::npos) {
                            species = &s;
                            break;
                        }
                    }
                }
                
                if (!species) {
                    bronx::send_message(bot, event, bronx::error("unknown pet type! use `b.pet shop` to see available pets."));
                    return;
                }
                
                if (species->rarity == "prestige" && db->get_prestige(user_id) < 5) {
                    bronx::send_message(bot, event, bronx::error("you need Prestige 5+ to adopt prestige pets!"));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                if (user_pets_list.size() >= 5) {
                    bronx::send_message(bot, event, bronx::error("you can only have 5 pets! release one first with `b.pet release <name>`"));
                    return;
                }
                
                for (const auto& p : user_pets_list) {
                    if (p.species_id == species->id) {
                        bronx::send_message(bot, event, bronx::error("you already have a **" + species->name + "**!"));
                        return;
                    }
                }
                
                int64_t wallet = db->get_wallet(user_id);
                if (wallet < species->adopt_cost) {
                    bronx::send_message(bot, event, bronx::error("you need $" + commands::economy::format_number(species->adopt_cost) + " but only have $" + commands::economy::format_number(wallet) + "!"));
                    return;
                }
                
                db->update_wallet(user_id, -species->adopt_cost);
                log_balance_change(db, user_id, "adopted pet " + species->name + " -$" + commands::economy::format_number(species->adopt_cost));
                
                std::string nickname = species->name;
                if (args.size() >= 3) {
                    nickname = args[2];
                    if (nickname.length() > 20) nickname = nickname.substr(0, 20);
                }
                
                adopt_pet(db, user_id, species->id, nickname);
                
                std::string desc = species->emoji + " **You adopted a " + species->name + "!**\n\n";
                desc += "name: **" + nickname + "**\n";
                desc += "bonus: **+" + std::to_string(static_cast<int>(species->base_bonus)) + "% " + format_bonus_name(species->bonus_type) + "**\n";
                desc += "cost: **$" + commands::economy::format_number(species->adopt_cost) + "**\n\n";
                desc += "*Equip with `b.pet equip " + nickname + "` to activate the bonus!*";
                
                auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                embed.set_title("\xF0\x9F\xBE new pet!");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (action == "feed") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `b.pet feed <name>`"));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, args[1]);
                if (!pet) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pet named **" + args[1] + "**!"));
                    return;
                }
                
                int current_hunger = calculate_current_hunger(pet->hunger, pet->last_fed);
                if (current_hunger >= 90) {
                    bronx::send_message(bot, event, bronx::error("**" + pet->nickname + "** isn't hungry! (hunger: " + std::to_string(current_hunger) + "%)"));
                    return;
                }
                
                int64_t networth = db->get_networth(user_id);
                int64_t feed_cost = static_cast<int64_t>(networth * 0.01);
                if (feed_cost < 1000) feed_cost = 1000;
                if (feed_cost > 5000000) feed_cost = 5000000;
                
                int64_t wallet = db->get_wallet(user_id);
                if (wallet < feed_cost) {
                    bronx::send_message(bot, event, bronx::error("feeding costs $" + commands::economy::format_number(feed_cost) + " but you only have $" + commands::economy::format_number(wallet) + "!"));
                    return;
                }
                
                db->update_wallet(user_id, -feed_cost);
                feed_pet(db, pet->pet_id);
                
                const auto* species = find_species(pet->species_id);
                std::string emoji = species ? species->emoji : "\xF0\x9F\xBE";
                
                auto embed = bronx::success(emoji + " **" + pet->nickname + "** has been fed! hunger restored to 100%\ncost: $" + commands::economy::format_number(feed_cost));
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (action == "equip") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `b.pet equip <name>`"));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, args[1]);
                if (!pet) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pet named **" + args[1] + "**!"));
                    return;
                }
                
                equip_pet(db, user_id, pet->pet_id);
                
                const auto* species = find_species(pet->species_id);
                std::string emoji = species ? species->emoji : "\xF0\x9F\xBE";
                double bonus = species ? get_pet_effective_bonus(*pet, *species) : 0;
                
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(1);
                oss << bonus;
                
                auto embed = bronx::success(emoji + " **" + pet->nickname + "** is now your active pet!\nbonus: **+" + oss.str() + "% " + (species ? format_bonus_name(species->bonus_type) : "") + "**");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            if (action == "release" || action == "remove") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `b.pet release <name>`"));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, args[1]);
                if (!pet) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pet named **" + args[1] + "**!"));
                    return;
                }
                
                if (args.size() >= 3 && args[2] == "confirm") {
                    const auto* species = find_species(pet->species_id);
                    std::string emoji = species ? species->emoji : "\xF0\x9F\xBE";
                    std::string name = pet->nickname;
                    release_pet(db, user_id, pet->pet_id);
                    
                    bronx::send_message(bot, event, bronx::success(emoji + " **" + name + "** has been released. goodbye!"));
                } else {
                    bronx::send_message(bot, event, bronx::create_embed(
                        "\xE2\x9A\xA0\xEF\xB8\x8F **Are you sure?**\nReleasing **" + pet->nickname + "** is permanent!\n\nUse `b.pet release " + args[1] + " confirm` to proceed.",
                        bronx::COLOR_WARNING));
                }
                return;
            }
            
            if (action == "rename") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event, bronx::error("usage: `b.pet rename <current_name> <new_name>`"));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, args[1]);
                if (!pet) {
                    bronx::send_message(bot, event, bronx::error("you don't have a pet named **" + args[1] + "**!"));
                    return;
                }
                
                std::string new_name = args[2];
                if (new_name.length() > 20) new_name = new_name.substr(0, 20);
                
                rename_pet(db, pet->pet_id, new_name);
                bronx::send_message(bot, event, bronx::success("renamed **" + pet->nickname + "** to **" + new_name + "**!"));
                return;
            }
            
            auto user_pets_list = get_user_pets(db, user_id);
            
            if (user_pets_list.empty()) {
                std::string desc = "\xF0\x9F\xBE **You don't have any pets yet!**\n\n";
                desc += "Visit the pet shop to adopt your first companion:\n`b.pet shop`";
                
                auto embed = bronx::create_embed(desc);
                embed.set_title("\xF0\x9F\xBE my pets (");
                bronx::add_invoker_footer(embed, event.msg.author);
                bronx::send_message(bot, event, embed);
                return;
            }
            
            std::string desc = "";
            
            for (const auto& pet : user_pets_list) {
                const auto* species = find_species(pet.species_id);
                if (!species) continue;
                
                int hunger = calculate_current_hunger(pet.hunger, pet.last_fed);
                double bonus = get_pet_effective_bonus(pet, *species);
                
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(1);
                oss << bonus;
                
                desc += (pet.equipped ? "\xF0\x9F\x94\xB9 " : "\xE2\xAC\x9C ") + species->emoji + " **" + pet.nickname + "**";
                if (pet.equipped) desc += " *(equipped)*";
                desc += "\n";
                
                desc += "   " + rarity_color(species->rarity) + " " + species->rarity + " " + species->name + "\n";
                desc += "   level **" + std::to_string(pet.level) + "** ";
                desc += "(xp: " + std::to_string(pet.xp) + "/" + std::to_string(xp_for_level(pet.level)) + ")\n";
                desc += "   hunger: " + hunger_bar(hunger) + "\n";
                desc += "   bonus: **+" + oss.str() + "% " + format_bonus_name(species->bonus_type) + "**";
                if (hunger < 50) desc += " \xE2\x9A\xA0\xEF\xB8\x8F";
                if (hunger == 0) desc += " *(inactive — feed me!)*";
                desc += "\n\n";
            }
            
            desc += "*commands: `b.pet feed/equip/rename/release/shop`*";
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\xBE my pets (" + std::to_string(user_pets_list.size()) + "/5)");
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ensure_pet_tables(db);
            uint64_t user_id = event.command.get_issuing_user().id;
            db->ensure_user_exists(user_id);
            
            std::string action = "view";
            if (event.get_parameter("action").index() != 0) {
                action = std::get<std::string>(event.get_parameter("action"));
            }
            
            std::string name = "";
            if (event.get_parameter("name").index() != 0) {
                name = std::get<std::string>(event.get_parameter("name"));
            }
            
            if (action == "shop") {
                int prestige = db->get_prestige(user_id);
                dpp::message msg = build_shop_page(0, prestige, user_id);
                event.reply(msg);
                return;
            }
            
            if (action == "adopt") {
                if (name.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("specify a pet type to adopt!")));
                    return;
                }
                
                std::string species_id = name;
                std::transform(species_id.begin(), species_id.end(), species_id.begin(), ::tolower);
                const PetSpecies* species = find_species(species_id);
                if (!species) {
                    for (const auto& s : PET_SPECIES) {
                        std::string ln = s.name;
                        std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
                        if (ln == species_id || s.id.find(species_id) != std::string::npos) {
                            species = &s;
                            break;
                        }
                    }
                }
                if (!species) {
                    event.reply(dpp::message().add_embed(bronx::error("unknown pet type!")));
                    return;
                }
                
                if (species->rarity == "prestige" && db->get_prestige(user_id) < 5) {
                    event.reply(dpp::message().add_embed(bronx::error("you need Prestige 5+ for prestige pets!")));
                    return;
                }
                
                auto user_pets_list = get_user_pets(db, user_id);
                if (user_pets_list.size() >= 5) {
                    event.reply(dpp::message().add_embed(bronx::error("max 5 pets! release one first.")));
                    return;
                }
                for (const auto& p : user_pets_list) {
                    if (p.species_id == species->id) {
                        event.reply(dpp::message().add_embed(bronx::error("you already have a **" + species->name + "**!")));
                        return;
                    }
                }
                
                int64_t wallet = db->get_wallet(user_id);
                if (wallet < species->adopt_cost) {
                    event.reply(dpp::message().add_embed(bronx::error("need $" + commands::economy::format_number(species->adopt_cost) + ", have $" + commands::economy::format_number(wallet))));
                    return;
                }
                
                db->update_wallet(user_id, -species->adopt_cost);
                log_balance_change(db, user_id, "adopted pet " + species->name + " -$" + commands::economy::format_number(species->adopt_cost));
                adopt_pet(db, user_id, species->id, species->name);
                
                std::string desc = species->emoji + " **You adopted a " + species->name + "!**\n\n";
                desc += "bonus: **+" + std::to_string(static_cast<int>(species->base_bonus)) + "% " + format_bonus_name(species->bonus_type) + "**\n";
                desc += "cost: **$" + commands::economy::format_number(species->adopt_cost) + "**\n\n";
                desc += "*Equip with `/pet equip`!*";
                
                auto embed = bronx::create_embed(desc, bronx::COLOR_SUCCESS);
                embed.set_title("\xF0\x9F\xBE new pet!");
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            if (action == "feed") {
                if (name.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("specify which pet to feed!")));
                    return;
                }
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, name);
                if (!pet) {
                    event.reply(dpp::message().add_embed(bronx::error("no pet named **" + name + "**!")));
                    return;
                }
                
                int current_hunger = calculate_current_hunger(pet->hunger, pet->last_fed);
                if (current_hunger >= 90) {
                    event.reply(dpp::message().add_embed(bronx::error("**" + pet->nickname + "** isn't hungry!")));
                    return;
                }
                
                int64_t networth = db->get_networth(user_id);
                int64_t feed_cost = static_cast<int64_t>(networth * 0.01);
                if (feed_cost < 1000) feed_cost = 1000;
                if (feed_cost > 5000000) feed_cost = 5000000;
                
                int64_t wallet = db->get_wallet(user_id);
                if (wallet < feed_cost) {
                    event.reply(dpp::message().add_embed(bronx::error("feeding costs $" + commands::economy::format_number(feed_cost) + "!")));
                    return;
                }
                
                db->update_wallet(user_id, -feed_cost);
                feed_pet(db, pet->pet_id);
                
                const auto* species = find_species(pet->species_id);
                std::string emoji = species ? species->emoji : "\xF0\x9F\xBE";
                event.reply(dpp::message().add_embed(bronx::success(emoji + " **" + pet->nickname + "** fed! $" + commands::economy::format_number(feed_cost))));
                return;
            }
            
            if (action == "equip") {
                if (name.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("specify which pet to equip!")));
                    return;
                }
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, name);
                if (!pet) {
                    event.reply(dpp::message().add_embed(bronx::error("no pet named **" + name + "**!")));
                    return;
                }
                
                equip_pet(db, user_id, pet->pet_id);
                const auto* species = find_species(pet->species_id);
                std::string emoji = species ? species->emoji : "\xF0\x9F\xBE";
                event.reply(dpp::message().add_embed(bronx::success(emoji + " **" + pet->nickname + "** equipped!")));
                return;
            }
            
            if (action == "release") {
                if (name.empty()) {
                    event.reply(dpp::message().add_embed(bronx::error("specify which pet to release!")));
                    return;
                }
                auto user_pets_list = get_user_pets(db, user_id);
                auto* pet = find_user_pet(user_pets_list, name);
                if (!pet) {
                    event.reply(dpp::message().add_embed(bronx::error("no pet named **" + name + "**!")));
                    return;
                }
                
                std::string pname = pet->nickname;
                release_pet(db, user_id, pet->pet_id);
                event.reply(dpp::message().add_embed(bronx::success("**" + pname + "** has been released. goodbye!")));
                return;
            }
            
            auto user_pets_list = get_user_pets(db, user_id);
            if (user_pets_list.empty()) {
                auto embed = bronx::create_embed("\xF0\x9F\xBE **No pets yet!**\n\nUse `/pet shop` to browse available pets.");
                embed.set_title("\xF0\x9F\xBE my pets");
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            std::string desc;
            for (const auto& pet : user_pets_list) {
                const auto* species = find_species(pet.species_id);
                if (!species) continue;
                
                int hunger = calculate_current_hunger(pet.hunger, pet.last_fed);
                double bonus = get_pet_effective_bonus(pet, *species);
                std::ostringstream oss;
                oss << std::fixed;
                oss.precision(1);
                oss << bonus;
                
                desc += (pet.equipped ? "\xF0\x9F\x94\xB9 " : "\xE2\xAC\x9C ") + species->emoji + " **" + pet.nickname + "**";
                if (pet.equipped) desc += " *(equipped)*";
                desc += "\n";
                desc += "   " + rarity_color(species->rarity) + " " + species->rarity + " | lv." + std::to_string(pet.level);
                if (pet.level < species->max_level) {
                    desc += " (xp: " + std::to_string(pet.xp) + "/" + std::to_string(xp_for_level(pet.level)) + ")";
                } else {
                    desc += " *(max)*";
                }
                desc += "\n";
                desc += "   hunger: " + hunger_bar(hunger) + "\n";
                desc += "   bonus: **+" + oss.str() + "% " + format_bonus_name(species->bonus_type) + "**\n\n";
            }
            
            auto embed = bronx::create_embed(desc);
            embed.set_title("\xF0\x9F\xBE my pets (" + std::to_string(user_pets_list.size()) + "/5)");
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_string, "action", "what to do", false)
                .add_choice(dpp::command_option_choice("view", std::string("view")))
                .add_choice(dpp::command_option_choice("shop", std::string("shop")))
                .add_choice(dpp::command_option_choice("adopt", std::string("adopt")))
                .add_choice(dpp::command_option_choice("feed", std::string("feed")))
                .add_choice(dpp::command_option_choice("equip", std::string("equip")))
                .add_choice(dpp::command_option_choice("release", std::string("release"))),
            dpp::command_option(dpp::co_string, "name", "pet type or pet name", false)
        }
    );
    
    cmd->extended_description = "Adopt and care for pets that provide passive bonuses. "
                                "Feed your pets to keep their bonuses active (hunger decays over time).";
    cmd->subcommands = {
        {"view", "View your pets and their status"},
        {"shop", "Browse available pets for adoption"},
        {"adopt <type>", "Adopt a new pet from the shop"},
        {"feed <name>", "Feed a pet to restore hunger"},
        {"equip <name>", "Set a pet as your active pet"},
        {"rename <old> <new>", "Rename a pet"},
        {"release <name>", "Release a pet (permanent)"}
    };
    cmd->examples = {"b.pet", "b.pet shop", "b.pet adopt cat", "b.pet feed Cat", "b.pet equip Cat"};
    
    return cmd;
}

} // namespace pets
} // namespace commands
