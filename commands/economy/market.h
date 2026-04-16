#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/permission_operations.h"
#include "../../database/operations/economy/market_operations.h"
#include <dpp/dpp.h>
#include <vector>
#include <ctime>

using namespace bronx::db;

namespace commands {

// parse a simple metadata JSON with string fields; we only need a couple of keys
static std::string extract_meta(const std::string& meta, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = meta.find(pattern);
    if (pos == std::string::npos) return "";
    pos = meta.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    // skip whitespace and quotes
    while (pos < meta.size() && isspace((unsigned char)meta[pos])) pos++;
    if (pos < meta.size() && meta[pos] == '"') pos++;
    std::string out;
    while (pos < meta.size() && meta[pos] != '"' && meta[pos] != ',' && meta[pos] != '}') {
        out += meta[pos++];
    }
    return out;
}

static std::vector<MarketItem> load_market_items(Database* db, uint64_t guild_id) {
    return db->get_market_items(guild_id);
}

static std::optional<MarketItem> find_market_item(Database* db, uint64_t guild_id, const std::string& token) {
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto items = load_market_items(db, guild_id);
    // 1. exact id
    for (auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (id == lower) return it;
    }
    // 2. exact name
    for (auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name == lower) return it;
    }
    // 3. id starts with
    for (auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (id.rfind(lower,0) == 0) return it;
    }
    // 4. name starts with
    for (auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.rfind(lower,0)==0) return it;
    }
    // 5. id contains
    std::optional<MarketItem> best;
    size_t bestlen = SIZE_MAX;
    for (auto &it : items) {
        std::string id = it.item_id;
        std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        auto pos = id.find(lower);
        if (pos != std::string::npos) {
            if (id.size() < bestlen) {
                bestlen = id.size(); best = it;
            }
        }
    }
    if (best) return best;
    // 6. name contains
    for (auto &it : items) {
        std::string name = it.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find(lower) != std::string::npos) return it;
    }
    return {};
}

inline std::vector<Command*> get_market_commands(Database* db) {
    static std::vector<Command*> cmds;
    
    // market browse/purchase command
    static Command* market = new Command("market", "browse or buy from the server market", "market", {"mkt"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            uint64_t guild_id = event.msg.guild_id;
            // admin editing mode
            if (!args.empty() && args[0] == "edit") {
                // check Discord Administrator permission OR bot-assigned admin
                bool is_allowed = permission_operations::is_admin(db, event.msg.author.id, guild_id);
                if (!is_allowed) {
                    for (const auto& rid : event.msg.member.get_roles()) {
                        dpp::role* r = dpp::find_role(rid);
                        if (r && (static_cast<uint64_t>(r->permissions) & static_cast<uint64_t>(dpp::p_administrator))) {
                            is_allowed = true;
                            break;
                        }
                    }
                }
                if (!is_allowed) {
                    bronx::send_message(bot, event, bronx::error("administrator permission required"));
                    return;
                }
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `market edit help` for command reference"));
                    return;
                }
                std::string sub = args[1];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                // help subcommand
                if (sub == "help") {
                    std::string help = "**market edit commands**\n\n"
                        "**add** - create a new market item\n"
                        "`market edit add <id> <price> <type> <target_id> [options]`\n"
                        "• `id` - unique item identifier (no spaces)\n"
                        "• `price` - cost in currency\n"
                        "• `type` - `role` or `channel`\n"
                        "• `target_id` - the role/channel ID to grant\n"
                        "• options: `name=Display Name` `desc=Description` `limit=10` `expires=2024-12-31`\n\n"
                        "**update** - modify an existing item\n"
                        "`market edit update <id> field=value ...`\n"
                        "• fields: `price`, `name`, `desc`, `limit`, `expires`, `type`, `target`\n\n"
                        "**delete** - remove an item\n"
                        "`market edit delete <id>`\n\n"
                        "**examples**\n"
                        "```\nmarket edit add vip 5000 role 123456789 name=VIP desc=Get the VIP role!\n"
                        "market edit update vip price=7500 desc=Premium VIP access\n"
                        "market edit delete vip\n```";
                    auto embed = bronx::create_embed(help);
                    embed.set_title("Market Administration");
                    bronx::add_invoker_footer(embed, event.msg.author);
                    bronx::send_message(bot, event, embed);
                    return;
                }
                if (sub == "add") {
                    // expect: market edit add <id> <price> <type> <target_id> [field=value ...]
                    if (args.size() < 6) {
                        bronx::send_message(bot, event, bronx::error("usage: market edit add <id> <price> <type> <target_id> [name=...] [desc=...] [limit=N] [expires=YYYY-MM-DD]"));
                        return;
                    }
                    MarketItem it;
                    it.guild_id = guild_id;
                    it.item_id = args[2];
                    try { it.price = std::stoll(args[3]); } catch(...) { it.price = 0; }
                    it.category = args[4];
                    // metadata will hold type/target
                    std::string md = "{\"type\":\"" + args[4] + "\"";
                    md += ",\"target_id\":\"" + args[5] + "\"}";
                    it.metadata = md;
                    it.name = it.item_id;
                    it.description = "";
                    it.max_quantity = -1;
                    it.expires_at = std::nullopt;
                    // parse extra fields
                    for (size_t i = 6; i < args.size(); ++i) {
                        auto pos = args[i].find('=');
                        if (pos == std::string::npos) continue;
                        std::string key = args[i].substr(0,pos);
                        std::string val = args[i].substr(pos+1);
                        if (key == "name") it.name = val;
                        else if (key == "desc") it.description = val;
                        else if (key == "limit") {
                            try { it.max_quantity = std::stoi(val); } catch(...){}
                        } else if (key == "expires") {
                            std::tm tm = {};
                            strptime(val.c_str(), "%Y-%m-%d", &tm);
                            it.expires_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                        }
                    }
                    if (db->create_market_item(it)) {
                        bronx::send_message(bot, event, bronx::success("created market item " + it.item_id));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to create item"));
                    }
                    return;
                } else if (sub == "delete") {
                    if (args.size() < 3) {
                        bronx::send_message(bot, event, bronx::error("usage: market edit delete <id>"));
                        return;
                    }
                    if (db->delete_market_item(guild_id, args[2])) {
                        bronx::send_message(bot, event, bronx::success("deleted market item " + args[2]));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to delete item"));
                    }
                    return;
                } else if (sub == "update") {
                    if (args.size() < 4) {
                        bronx::send_message(bot, event, bronx::error("usage: market edit update <id> field=value ..."));
                        return;
                    }
                    auto maybe = db->get_market_item(guild_id, args[2]);
                    if (!maybe) {
                        bronx::send_message(bot, event, bronx::error("item not found"));
                        return;
                    }
                    MarketItem it = *maybe;
                    for (size_t i = 3; i < args.size(); ++i) {
                        auto pos = args[i].find('=');
                        if (pos == std::string::npos) continue;
                        std::string key = args[i].substr(0,pos);
                        std::string val = args[i].substr(pos+1);
                        if (key == "price") {
                            try { it.price = std::stoll(val); } catch(...){}
                        } else if (key == "name") {
                            it.name = val;
                        } else if (key == "desc") {
                            it.description = val;
                        } else if (key == "limit") {
                            try { it.max_quantity = std::stoi(val); } catch(...){}
                        } else if (key == "expires") {
                            std::tm tm = {};
                            strptime(val.c_str(), "%Y-%m-%d", &tm);
                            it.expires_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                        } else if (key == "type" || key == "target") {
                            // rebuild metadata
                            std::string type = extract_meta(it.metadata, "type");
                            std::string target = extract_meta(it.metadata, "target_id");
                            if (key == "type") type = val;
                            if (key == "target") target = val;
                            it.metadata = "{\"type\":\"" + type + "\",\"target_id\":\"" + target + "\"}";
                            it.category = type;
                        }
                    }
                    if (db->update_market_item(it)) {
                        bronx::send_message(bot, event, bronx::success("updated item " + it.item_id));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to update item"));
                    }
                    return;
                } else {
                    bronx::send_message(bot, event, bronx::error("unknown subcommand"));
                    return;
                }
            }

            // normal browse behaviour
            auto items = load_market_items(db, guild_id);
            std::string description = "**server market**\n\n";
            if (items.empty()) {
                description += "*no items available*";
            } else {
                for (const auto& it : items) {
                    description += "**" + it.name + "** (`" + it.item_id + "`) - $" + format_number(it.price);
                    if (it.max_quantity > 0) {
                        description += " *(" + std::to_string(it.max_quantity) + " left)*";
                    }
                    if (it.expires_at.has_value()) {
                        auto tt = std::chrono::system_clock::to_time_t(*it.expires_at);
                        char buf[64];
                        strftime(buf, sizeof(buf), "%Y-%m-%d", gmtime(&tt));
                        description += " *(expires " + std::string(buf) + ")*";
                    }
                    description += "\n" + it.description + "\n\n";
                }
            }
            description += "use `buy <item_id>` to purchase";
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            if (!event.command.guild_id) {
                event.reply(dpp::message().add_embed(bronx::error("this command only works in a server")).set_flags(dpp::m_ephemeral));
                return;
            }
            uint64_t guild_id = event.command.guild_id;
            std::string description;
            auto items = load_market_items(db, guild_id);
            if (items.empty()) {
                description = "*no items available*";
            } else {
                description = "**server market**\n\n";
                for (const auto& it : items) {
                    description += "**" + it.name + "** (`" + it.item_id + "`) - $" + format_number(it.price);
                    if (it.max_quantity > 0) description += " *(" + std::to_string(it.max_quantity) + " left)*";
                    if (it.expires_at.has_value()) {
                        auto tt = std::chrono::system_clock::to_time_t(*it.expires_at);
                        char buf[64];
                        strftime(buf, sizeof(buf), "%Y-%m-%d", gmtime(&tt));
                        description += " *(expires " + std::string(buf) + ")*";
                    }
                    description += "\n" + it.description + "\n\n";
                }
            }
            auto embed = bronx::create_embed(description);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            // no options for now (could add item parameter for buying via slash)
        }
    );
    cmds.push_back(market);

    // buy command for market
    static Command* buy = new Command("buy", "purchase an item from the server market", "market", {"purchase"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (!event.msg.guild_id) {
                bronx::send_message(bot, event, bronx::error("this command only works in a server"));
                return;
            }
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("specify an item to buy"));
                return;
            }
            uint64_t guild_id = event.msg.guild_id;
            std::string item_name;
            int64_t amount = 1;
            bool found_amount = false;
            if (args.size() >= 2) {
                std::string last_arg = args.back();
                std::transform(last_arg.begin(), last_arg.end(), last_arg.begin(), ::tolower);
                if (last_arg == "max" || last_arg == "all") {
                    found_amount = true;
                    for (size_t i = 0; i < args.size()-1; ++i) {
                        if (i>0) item_name += " ";
                        item_name += args[i];
                    }
                } else {
                    try {
                        amount = std::stoll(last_arg);
                        if (amount > 0) {
                            found_amount = true;
                            for (size_t i = 0; i < args.size()-1; ++i) {
                                if (i>0) item_name += " ";
                                item_name += args[i];
                            }
                        }
                    } catch(...) {}
                }
            }
            if (!found_amount) {
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i>0) item_name += " ";
                    item_name += args[i];
                }
            }
            std::transform(item_name.begin(), item_name.end(), item_name.begin(), ::tolower);
            auto maybe_item = find_market_item(db, guild_id, item_name);
            if (!maybe_item) {
                bronx::send_message(bot, event, bronx::error("item not found in market"));
                return;
            }
            MarketItem item = *maybe_item;
            if (item.expires_at.has_value()) {
                auto now = std::chrono::system_clock::now();
                if (now >= *item.expires_at) {
                    bronx::send_message(bot, event, bronx::error("that item is no longer available"));
                    return;
                }
            }
            if (item.max_quantity > 0) {
                if (item.max_quantity < amount) {
                    bronx::send_message(bot, event, bronx::error("not enough stock remaining"));
                    return;
                }
            }
            // roles/channels are not consumable; only one per purchase
            if (amount > 1) {
                bronx::send_message(bot, event, bronx::error("you can only purchase one of that item"));
                return;
            }
            int64_t cost = item.price * amount;
            auto user = db->get_user(event.msg.author.id);
            if (!user || user->wallet < cost) {
                bronx::send_message(bot, event, bronx::error("you can't afford this item"));
                return;
            }
            if (!db->update_wallet(event.msg.author.id, -cost)) {
                bronx::send_message(bot, event, bronx::error("failed to complete purchase"));
                return;
            }
            if (item.max_quantity > 0) {
                db->adjust_market_item_quantity(guild_id, item.item_id, -amount);
            }
            // perform action specified by metadata
            std::string type = extract_meta(item.metadata, "type");
            std::string target = extract_meta(item.metadata, "target_id");
            if (type == "role") {
                uint64_t rid = 0;
                try { rid = std::stoull(target); } catch(...) {}
                if (rid) {
                    bot.guild_member_add_role(guild_id, event.msg.author.id, rid, [](const dpp::confirmation_callback_t& cb){});
                }
            } else if (type == "channel") {
                uint64_t cid = 0;
                try { cid = std::stoull(target); } catch(...) {}
                if (cid) {
                    // just mention channel and/or create invite
                    dpp::message m(event.msg.channel_id, "<#" + std::to_string(cid) + ">");
                    bronx::send_message(bot, event, m);
                }
            }
            std::string desc = "purchased **" + item.name + "**";
            if (amount > 1) desc += " x" + std::to_string(amount);
            desc += " for $" + format_number(cost);
            auto embed = bronx::success(desc);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // slash buy not implemented for server market (could be added later)
            event.reply(dpp::message().add_embed(bronx::error("use message mode to purchase from server market")));
        },
        {}
    );
    cmds.push_back(buy);
    
    return cmds;
}

} // namespace commands
