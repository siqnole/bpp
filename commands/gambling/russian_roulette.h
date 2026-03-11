#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../../database/core/database.h"
#include "../../database/operations/economy/history_operations.h"
#include "../economy_core.h"
#include "../economy/helpers.h"
#include "gambling_helpers.h"
#include <dpp/dpp.h>
#include <random>
#include <algorithm>
#include <map>
#include <set>
#include <chrono>
#include "../../log.h"

using namespace bronx::db;
using namespace bronx::db::history_operations;

namespace commands {
namespace gambling {

// ─── Data Structures ───────────────────────────────────────────────────────────

struct RussianRoulettePlayer {
    uint64_t user_id;
    ::std::string username;
    int64_t bet_amount;
    bool eliminated;
    int spins_remaining;   // each player can spin the barrel 3 times per game
};

struct RussianRouletteGame {
    uint64_t author_id;
    uint64_t message_id;
    uint64_t channel_id;
    ::std::vector<RussianRoulettePlayer> players;
    bool active;
    bool started;
    int current_turn;       // index into players vector
    int bullet_chamber;     // which chamber (0-5) holds the bullet
    int current_chamber;    // which chamber (0-5) is next to fire
    int64_t bet_amount;

    int next_alive_player(int from) const {
        int n = static_cast<int>(players.size());
        for (int i = 1; i <= n; ++i) {
            int idx = (from + i) % n;
            if (!players[idx].eliminated) return idx;
        }
        return -1;
    }

    int alive_count() const {
        int c = 0;
        for (const auto& p : players) if (!p.eliminated) ++c;
        return c;
    }

    void randomize_barrel() {
        static thread_local ::std::mt19937 gen(::std::random_device{}());
        ::std::uniform_int_distribution<> dis(0, 5);
        bullet_chamber = dis(gen);
        current_chamber = dis(gen);
    }

    // Returns true if the bullet fires
    bool pull_trigger() {
        bool fired = (current_chamber == bullet_chamber);
        current_chamber = (current_chamber + 1) % 6;
        return fired;
    }
};

static ::std::map<uint64_t, RussianRouletteGame> active_russian_roulette_games;

// ─── UI Builders ───────────────────────────────────────────────────────────────

inline dpp::message build_turn_message(const RussianRouletteGame& game, uint64_t game_id) {
    const auto& current = game.players[game.current_turn];

    ::std::string desc = "**<@" + ::std::to_string(current.user_id) + ">'s turn**\n\n";
    desc += "choose your action:\n";
    desc += "• **shoot** — pull the trigger on yourself\n";
    desc += "• **shoot player** — aim at another player\n";
    desc += "• **spin barrel** — re-randomize the chamber (" +
            ::std::to_string(current.spins_remaining) + "/3 left)\n\n";

    desc += "**players:**\n";
    for (size_t i = 0; i < game.players.size(); ++i) {
        const auto& p = game.players[i];
        ::std::string icon = p.eliminated ? "💀" : (static_cast<int>(i) == game.current_turn ? "➡️" : "🟢");
        desc += icon + " <@" + ::std::to_string(p.user_id) + ">";
        if (p.eliminated) desc += " *(eliminated)*";
        desc += "\n";
    }

    dpp::embed embed = dpp::embed()
        .set_color(0x8B0000)
        .set_title("🔫 russian roulette")
        .set_description(desc)
        .set_footer(dpp::embed_footer().set_text(
            "chamber " + ::std::to_string(game.current_chamber + 1) +
            "/6 • pot: $" + format_number(game.bet_amount * static_cast<int64_t>(game.players.size()))));

    dpp::message msg;
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;
    msg.add_embed(embed);

    // Row 1 — action buttons
    dpp::component action_row;
    action_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Shoot Yourself")
            .set_style(dpp::cos_danger)
            .set_id("rr_shoot_" + ::std::to_string(game_id)));
    action_row.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Spin Barrel (" + ::std::to_string(current.spins_remaining) + ")")
            .set_style(dpp::cos_secondary)
            .set_id("rr_spin_" + ::std::to_string(game_id))
            .set_disabled(current.spins_remaining <= 0));
    msg.add_component(action_row);

    // Row 2 — player target dropdown (only alive opponents)
    ::std::vector<dpp::select_option> opts;
    for (const auto& p : game.players) {
        if (!p.eliminated && p.user_id != current.user_id) {
            opts.push_back(dpp::select_option(p.username, ::std::to_string(p.user_id)));
        }
    }
    if (!opts.empty()) {
        dpp::component select_row;
        dpp::component menu;
        menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("🎯 Shoot a player...")
            .set_id("rr_target_" + ::std::to_string(game_id));
        for (auto& o : opts) menu.add_select_option(o);
        select_row.add_component(menu);
        msg.add_component(select_row);
    }

    return msg;
}

inline dpp::message build_lobby_message(const RussianRouletteGame& game, uint64_t game_id) {
    ::std::string desc = "join the game by clicking the button below!\n\n";
    desc += "**bet:** $" + format_number(game.bet_amount) + " per player\n";
    desc += "**players (" + ::std::to_string(game.players.size()) + "/16):**\n";
    for (const auto& p : game.players) {
        desc += "• <@" + ::std::to_string(p.user_id) + ">\n";
    }

    dpp::embed embed = dpp::embed()
        .set_color(0x1E90FF)
        .set_title("🔫 russian roulette — lobby")
        .set_description(desc);

    dpp::message msg;
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;
    msg.add_embed(embed);

    dpp::component row1;
    row1.add_component(
        dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Join Game")
            .set_style(dpp::cos_primary)
            .set_id("russian_roulette_join_" + ::std::to_string(game_id)));
    msg.add_component(row1);

    if (game.players.size() >= 2) {
        dpp::component row2;
        row2.add_component(
            dpp::component()
                .set_type(dpp::cot_button)
                .set_label("Start Game")
                .set_style(dpp::cos_success)
                .set_id("russian_roulette_start_" + ::std::to_string(game_id)));
        msg.add_component(row2);
    }

    return msg;
}

// ─── Shot Processing ───────────────────────────────────────────────────────────

inline void process_shot(dpp::cluster& bot, Database* db, uint64_t game_id,
                         uint64_t shooter_id, uint64_t target_id, bool self_shot) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end()) return;
    auto& game = it->second;

    bool fired = game.pull_trigger();

    if (fired) {
        // Mark target eliminated
        for (auto& p : game.players) {
            if (p.user_id == target_id) { p.eliminated = true; break; }
        }

        // Build elimination embed
        ::std::string desc;
        if (self_shot) {
            desc = "💥 **BANG!** <@" + ::std::to_string(target_id) + "> pulled the trigger and got shot!";
        } else {
            desc = "💥 **BANG!** <@" + ::std::to_string(shooter_id) + "> shot <@" + ::std::to_string(target_id) + ">!";
        }
        desc += "\n\n*" + ::std::to_string(game.alive_count()) + " players remaining*";

        dpp::message elim_msg;
        elim_msg.channel_id = game.channel_id;
        elim_msg.add_embed(dpp::embed().set_color(0xFF0000).set_title("🔫 russian roulette").set_description(desc));
        bot.message_create(elim_msg);

        // Check for game over
        if (game.alive_count() <= 1) {
            game.active = false;

            uint64_t winner_id = 0;
            int64_t total_pot = 0;
            for (const auto& p : game.players) {
                total_pot += p.bet_amount;
                if (!p.eliminated) winner_id = p.user_id;
            }

            // Award winner
            db->update_wallet(winner_id, total_pot);
            log_gambling(db, winner_id, "won russian roulette ($" + format_number(total_pot) + ")",
                         total_pot, 0);
            db->increment_stat(winner_id, "gambling_profit", total_pot - game.bet_amount);
            track_gambling_result(bot, db, game.channel_id, winner_id, true, total_pot - game.bet_amount);
            track_gambling_profit(bot, db, game.channel_id, winner_id);

            // Track losses for eliminated players
            for (const auto& p : game.players) {
                if (p.eliminated) {
                    db->increment_stat(p.user_id, "gambling_losses", p.bet_amount);
                    track_gambling_result(bot, db, game.channel_id, p.user_id, false, -p.bet_amount);
                }
            }

            // Game over embed
            ::std::string go_desc = "🏆 **<@" + ::std::to_string(winner_id) + "> is the last one standing!**\n\n";
            go_desc += "💰 winnings: **$" + format_number(total_pot) + "**\n\n";
            go_desc += "**final results:**\n";
            for (const auto& p : game.players) {
                if (p.user_id == winner_id)
                    go_desc += "🏆 <@" + ::std::to_string(p.user_id) + "> — **winner** (+$" + format_number(total_pot - p.bet_amount) + ")\n";
                else
                    go_desc += "💀 <@" + ::std::to_string(p.user_id) + "> — *eliminated* (-$" + format_number(p.bet_amount) + ")\n";
            }

            dpp::message go_msg;
            go_msg.channel_id = game.channel_id;
            go_msg.add_embed(dpp::embed().set_color(0x00FF00).set_title("🔫 russian roulette — game over!").set_description(go_desc));
            bot.message_create(go_msg);

            // Clear the original message
            dpp::message clear_msg;
            clear_msg.id = game.message_id;
            clear_msg.channel_id = game.channel_id;
            clear_msg.add_embed(dpp::embed()
                .set_color(0x00FF00)
                .set_title("🔫 russian roulette — game over!")
                .set_description("🏆 winner: <@" + ::std::to_string(winner_id) + ">"));
            clear_msg.components = {};
            bronx::safe_message_edit(bot, clear_msg);

            active_russian_roulette_games.erase(game_id);
            return;
        }

        // Advance to next alive player
        game.current_turn = game.next_alive_player(game.current_turn);
    } else {
        // Safe — click
        ::std::string safe_desc;
        if (self_shot) {
            safe_desc = "🔫 *click...* <@" + ::std::to_string(target_id) + "> pulled the trigger... **nothing happened!**";
        } else {
            safe_desc = "🔫 *click...* <@" + ::std::to_string(shooter_id) + "> aimed at <@" + ::std::to_string(target_id) + ">... **nothing happened!**";
        }

        dpp::message safe_msg;
        safe_msg.channel_id = game.channel_id;
        safe_msg.add_embed(dpp::embed().set_color(0xFFD700).set_title("🔫 russian roulette").set_description(safe_desc));
        bot.message_create(safe_msg);

        game.current_turn = game.next_alive_player(game.current_turn);
    }

    // Update the game message for the next turn
    bot.message_edit(build_turn_message(game, game_id));
}

// ─── Interaction Handlers ──────────────────────────────────────────────────────

inline void handle_rr_shoot(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t game_id) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end() || !it->second.active || !it->second.started) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("this game is no longer active.").set_flags(dpp::m_ephemeral));
        return;
    }
    auto& game = it->second;
    uint64_t uid = event.command.get_issuing_user().id;

    if (game.players[game.current_turn].user_id != uid) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("it's not your turn!").set_flags(dpp::m_ephemeral));
        return;
    }

    event.reply(dpp::ir_deferred_update_message, dpp::message());
    process_shot(bot, db, game_id, uid, uid, true);
}

inline void handle_rr_spin(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t game_id) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end() || !it->second.active || !it->second.started) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("this game is no longer active.").set_flags(dpp::m_ephemeral));
        return;
    }
    auto& game = it->second;
    uint64_t uid = event.command.get_issuing_user().id;

    if (game.players[game.current_turn].user_id != uid) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("it's not your turn!").set_flags(dpp::m_ephemeral));
        return;
    }

    auto& player = game.players[game.current_turn];
    if (player.spins_remaining <= 0) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("you have no spins remaining!").set_flags(dpp::m_ephemeral));
        return;
    }

    player.spins_remaining--;
    game.randomize_barrel();

    // Notify channel
    dpp::message spin_msg;
    spin_msg.channel_id = game.channel_id;
    spin_msg.add_embed(dpp::embed()
        .set_color(0x1E90FF)
        .set_title("🔫 russian roulette")
        .set_description("🔄 <@" + ::std::to_string(uid) + "> spun the barrel! (" +
                          ::std::to_string(player.spins_remaining) + " spins left)"));
    bot.message_create(spin_msg);

    // Advance turn
    game.current_turn = game.next_alive_player(game.current_turn);

    event.reply(dpp::ir_deferred_update_message, dpp::message());
    bot.message_edit(build_turn_message(game, game_id));
}

inline void handle_rr_target(dpp::cluster& bot, Database* db, const dpp::select_click_t& event, uint64_t game_id) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end() || !it->second.active || !it->second.started) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("this game is no longer active.").set_flags(dpp::m_ephemeral));
        return;
    }
    auto& game = it->second;
    uint64_t uid = event.command.get_issuing_user().id;

    if (game.players[game.current_turn].user_id != uid) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("it's not your turn!").set_flags(dpp::m_ephemeral));
        return;
    }

    if (event.values.empty()) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("select a player.").set_flags(dpp::m_ephemeral));
        return;
    }

    uint64_t target_id = ::std::stoull(event.values[0]);

    // Verify target is alive and not the shooter
    bool valid = false;
    for (const auto& p : game.players) {
        if (p.user_id == target_id && !p.eliminated && p.user_id != uid) { valid = true; break; }
    }
    if (!valid) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("invalid target!").set_flags(dpp::m_ephemeral));
        return;
    }

    event.reply(dpp::ir_deferred_update_message, dpp::message());
    process_shot(bot, db, game_id, uid, target_id, false);
}

// ─── Lobby Handlers ────────────────────────────────────────────────────────────

inline void handle_russian_roulette_join(dpp::cluster& bot, Database* db, const dpp::button_click_t& event, uint64_t game_id) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end() || !it->second.active) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("this game is no longer active.").set_flags(dpp::m_ephemeral));
        return;
    }
    auto& game = it->second;

    if (game.started) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("the game has already started!").set_flags(dpp::m_ephemeral));
        return;
    }

    uint64_t uid = event.command.get_issuing_user().id;

    for (const auto& p : game.players) {
        if (p.user_id == uid) {
            event.reply(dpp::ir_channel_message_with_source,
                dpp::message().set_content("you're already in the game!").set_flags(dpp::m_ephemeral));
            return;
        }
    }

    if (game.players.size() >= 16) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("the game is full! (16/16)").set_flags(dpp::m_ephemeral));
        return;
    }

    // Check balance
    auto user = db->get_user(uid);
    if (!user || user->wallet < game.bet_amount) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("you don't have enough money! need $" + format_number(game.bet_amount)).set_flags(dpp::m_ephemeral));
        return;
    }

    // Deduct bet
    db->update_wallet(uid, -game.bet_amount);

    RussianRoulettePlayer player;
    player.user_id = uid;
    player.username = event.command.get_issuing_user().username;
    player.bet_amount = game.bet_amount;
    player.eliminated = false;
    player.spins_remaining = 3;
    game.players.push_back(player);

    bot.message_edit(build_lobby_message(game, game_id));

    event.reply(dpp::ir_channel_message_with_source,
        dpp::message().set_content("you joined the game! ($" + format_number(game.bet_amount) + " deducted)").set_flags(dpp::m_ephemeral));
}

inline void handle_russian_roulette_start(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t game_id) {
    auto it = active_russian_roulette_games.find(game_id);
    if (it == active_russian_roulette_games.end() || !it->second.active) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("this game is no longer active.").set_flags(dpp::m_ephemeral));
        return;
    }
    auto& game = it->second;

    if (game.started) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("the game has already started!").set_flags(dpp::m_ephemeral));
        return;
    }

    if (event.command.get_issuing_user().id != game.author_id) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("only the game creator can start!").set_flags(dpp::m_ephemeral));
        return;
    }

    if (game.players.size() < 2) {
        event.reply(dpp::ir_channel_message_with_source,
            dpp::message().set_content("need at least 2 players to start!").set_flags(dpp::m_ephemeral));
        return;
    }

    game.started = true;
    game.current_turn = 0;
    game.randomize_barrel();

    event.reply(dpp::ir_deferred_update_message, dpp::message());
    bot.message_edit(build_turn_message(game, game_id));
}

// ─── Command Registration ──────────────────────────────────────────────────────

inline Command* get_russian_roulette_command(Database* db) {
    static Command* russian_roulette = new Command("russian_roulette",
        "Play Russian Roulette with up to 16 players!", "gambling", {"rusrou"}, true,
        // ── prefix command handler ──
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Parse bet amount (default 100, min 10, max 1M)
            int64_t bet = 100;
            if (!args.empty()) {
                try {
                    auto user = db->get_user(event.msg.author.id);
                    bet = parse_amount(args[0], user ? user->wallet : 0);
                    if (bet < 10) bet = 10;
                    if (bet > 1000000) bet = 1000000;
                } catch (...) {
                    bet = 100;
                }
            }

            // Check balance
            auto user = db->get_user(event.msg.author.id);
            if (!user || user->wallet < bet) {
                bronx::send_message(bot, event, bronx::error("you don't have enough money! need $" + format_number(bet)));
                return;
            }

            // Deduct bet for creator
            db->update_wallet(event.msg.author.id, -bet);

            // Create game
            RussianRouletteGame game;
            game.author_id = event.msg.author.id;
            game.channel_id = event.msg.channel_id;
            game.active = true;
            game.started = false;
            game.current_turn = 0;
            game.bullet_chamber = 0;
            game.current_chamber = 0;
            game.bet_amount = bet;

            // Auto-add creator
            RussianRoulettePlayer creator;
            creator.user_id = event.msg.author.id;
            creator.username = event.msg.author.username;
            creator.bet_amount = bet;
            creator.eliminated = false;
            creator.spins_remaining = 3;
            game.players.push_back(creator);

            uint64_t game_id = static_cast<uint64_t>(
                ::std::chrono::system_clock::now().time_since_epoch().count()) + event.msg.author.id;

            active_russian_roulette_games[game_id] = game;

            dpp::message msg = build_lobby_message(game, game_id);
            msg.id = dpp::snowflake(0); // new message, not an edit
            msg.channel_id = event.msg.channel_id;

            bot.message_create(msg, [game_id](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    active_russian_roulette_games.erase(game_id);
                    return;
                }
                auto sent = callback.get<dpp::message>();
                if (active_russian_roulette_games.count(game_id)) {
                    active_russian_roulette_games[game_id].message_id = sent.id;
                }
            });
        },
        // ── slash command handler ──
        [db](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // TODO: slash command support
        },
        {});

    return russian_roulette;
}

} // namespace gambling
} // namespace commands