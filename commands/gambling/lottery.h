#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include <dpp/dpp.h>
#include <random>

using namespace bronx::db;

namespace commands {
namespace gambling {

inline Command* get_lottery_command(Database* db) {
    static Command* lottery = new Command("lottery",
        "buy lottery tickets; pool starts at 30,000,000 and increases by 30% of ticket costs",
        "gambling", {"lotto"}, true,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            auto user = db->get_user(event.msg.author.id);
            if (!user) return;

            // helper to read/write pool value
            auto read_pool = [&]() -> int64_t {
                int64_t pool = 30000000; // base amount
                if (auto s = db->get_ml_setting("lottery_pool"); s && !s->empty()) {
                    try {
                        pool = std::stoll(*s);
                    } catch (...) {}
                }
                return pool;
            };
            auto write_pool = [&](int64_t newval) {
                db->set_ml_setting("lottery_pool", ::std::to_string(newval));
            };

            if (args.empty() || args[0] == "info" || args[0] == "pool" || args[0] == "status") {
                int64_t pool = read_pool();
                int64_t users = db->get_lottery_user_count();
                int64_t tickets = db->get_lottery_total_tickets();
                double avg_share = tickets > 0 ? (100.0 / tickets) : 0.0;
                std::ostringstream oss;
                oss << "Current lottery pool: **$" << format_number(pool) << "**\n";
                oss << "Participants: **" << users << "**\n";
                oss << "Total tickets: **" << tickets << "**\n";
                oss << "Avg share per ticket: **" << std::fixed << std::setprecision(4) << avg_share << "%**\n";
                oss << "Ticket price range: **$300–$1,000**";
                auto embed = ::bronx::create_embed(oss.str());
                ::bronx::add_invoker_footer(embed, event.msg.author);
                ::bronx::send_message(bot, event, embed);
                return;
            }

            // parse ticket count (support numbers or "max"/"all")
            int64_t count = 1;
            if (!args.empty()) {
                if (args[0] == "max" || args[0] == "all") {
                    count = INT64_MAX/2; // effectively unlimited, loop will stop by wallet
                } else {
                    try {
                        count = std::stoll(args[0]);
                    } catch (...) {
                        ::bronx::send_message(bot, event, ::bronx::error("invalid ticket count"));
                        return;
                    }
                }
            }
            if (count < 1) {
                ::bronx::send_message(bot, event, ::bronx::error("ticket count must be at least 1"));
                return;
            }

            int64_t wallet = user->wallet;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int64_t> cost_dis(300, 1000);

            int64_t tickets_bought = 0;
            int64_t total_spent = 0;
            int64_t pool_added = 0;

            for (int64_t i = 0; i < count; ++i) {
                int64_t cost = cost_dis(gen);
                if (wallet < cost) break;
                wallet -= cost;
                total_spent += cost;
                pool_added += (cost * 30) / 100; // 30% contribution
                ++tickets_bought;
            }

            if (tickets_bought == 0) {
                ::bronx::send_message(bot, event, ::bronx::error("you can't afford any tickets (each ticket costs between $300 and $1,000)"));
                return;
            }

            // apply changes
            db->update_wallet(event.msg.author.id, -total_spent);
            ::bronx::db::history_operations::log_gambling(db, event.msg.author.id, "bought " + ::std::to_string(tickets_bought) + " lottery ticket(s) for $" + format_number(total_spent));
            db->update_lottery_tickets(event.msg.author.id, tickets_bought);
            int64_t pool = read_pool();
            pool += pool_added;
            write_pool(pool);

            ::std::string desc = "you purchased **" + ::std::to_string(tickets_bought) + "** ticket";
            if (tickets_bought != 1) desc += "s";
            desc += " for a total of **$" + format_number(total_spent) + "**.\n";
            desc += "30% of the ticket cost ($" + format_number(pool_added) + ") was added to the pool.\n";
            desc += "\n**current pool:** $" + format_number(pool);

            auto embed = ::bronx::success(desc);
            ::bronx::add_invoker_footer(embed, event.msg.author);
            ::bronx::send_message(bot, event, embed);
        },
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto user = db->get_user(event.command.get_issuing_user().id);
            if (!user) {
                event.reply(dpp::message().add_embed(::bronx::error("user not found")));
                return;
            }

            // read pool helper
            auto read_pool = [&]() -> int64_t {
                int64_t pool = 30000000;
                if (auto s = db->get_ml_setting("lottery_pool"); s && !s->empty()) {
                    try { pool = std::stoll(*s); } catch(...) {}
                }
                return pool;
            };
            auto write_pool = [&](int64_t newval) {
                db->set_ml_setting("lottery_pool", ::std::to_string(newval));
            };

            // check for status via slash (no count or count==0)
            // Discord slash can't easily send strings, so using count==0 as info trigger
            if (event.get_parameter("count").index() == 0) {
                int64_t pool = read_pool();
                int64_t users = db->get_lottery_user_count();
                int64_t tickets = db->get_lottery_total_tickets();
                double avg_share = tickets > 0 ? (100.0 / tickets) : 0.0;
                std::ostringstream oss;
                oss << "Current lottery pool: **$" << format_number(pool) << "**\n";
                oss << "Participants: **" << users << "**\n";
                oss << "Total tickets: **" << tickets << "**\n";
                double avg_user_share = users > 0 ? (100.0 / users) : 0.0;
                oss << "Avg share per ticket: **" << std::fixed << std::setprecision(4) << avg_share << "%** (chance per ticket)\n";
                oss << "Avg share per user: **" << std::fixed << std::setprecision(4) << avg_user_share << "%**\n";
                oss << "Ticket price range: **$300–$1,000**";
                auto embed = ::bronx::create_embed(oss.str());
                ::bronx::add_invoker_footer(embed, event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            int64_t count = 1;
            if (event.get_parameter("count").index() == 1) {
                // slash option is integer, cannot be max/all; still allow special logic above later if needed
                count = std::get<int64_t>(event.get_parameter("count"));
            }
            if (count < 1) {
                event.reply(dpp::message().add_embed(::bronx::error("ticket count must be at least 1")));
                return;
            }

            int64_t wallet = user->wallet;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int64_t> cost_dis(300, 1000);

            int64_t tickets_bought = 0;
            int64_t total_spent = 0;
            int64_t pool_added = 0;

            for (int64_t i = 0; i < count; ++i) {
                int64_t cost = cost_dis(gen);
                if (wallet < cost) break;
                wallet -= cost;
                total_spent += cost;
                pool_added += (cost * 30) / 100;
                ++tickets_bought;
            }

            if (tickets_bought == 0) {
                event.reply(dpp::message().add_embed(::bronx::error("you can't afford any tickets (each ticket costs between $300 and $1,000)")));
                return;
            }

            db->update_wallet(event.command.get_issuing_user().id, -total_spent);
            ::bronx::db::history_operations::log_gambling(db, event.command.get_issuing_user().id, "bought " + ::std::to_string(tickets_bought) + " lottery ticket(s) for $" + format_number(total_spent));
            db->update_lottery_tickets(event.command.get_issuing_user().id, tickets_bought);
            int64_t pool = read_pool();
            pool += pool_added;
            write_pool(pool);

            ::std::string desc = "you purchased **" + ::std::to_string(tickets_bought) + "** ticket";
            if (tickets_bought != 1) desc += "s";
            desc += " for a total of **$" + format_number(total_spent) + "**.\n";
            desc += "30% of the ticket cost ($" + format_number(pool_added) + ") was added to the pool.\n";
            desc += "\n**current pool:** $" + format_number(pool);

            auto embed = ::bronx::success(desc);
            ::bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        },
        {
            dpp::command_option(dpp::co_integer, "count", "number of tickets to buy (optional)", false)
        }
    );

    return lottery;
}

} // namespace gambling
} // namespace commands
