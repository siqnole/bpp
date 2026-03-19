#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>

namespace commands {
namespace games {

struct BlackTeaGame {
    uint64_t game_id;
    uint64_t author_id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id; // lobby message id
    bool active = false;

    ::std::vector<uint64_t> players;            // players still in lobby / game
    ::std::map<uint64_t,int> lives;             // remaining lives per player
    ::std::set<::std::string> used_words;         // words used during the entire game (lowercase sanitized)
    ::std::map<uint64_t,bool> responded;        // whether player responded this round

    bool round_active = false;                // whether currently accepting words
    ::std::string current_pattern;              // current 3-letter pattern
    int round = 0;

    bool force_stop = false;                    // admin force-stop flag
    int64_t start_timestamp = 0;               // unix timestamp when game will start (for relative discord timestamp)

    // Lobby timer reset / pause support
    ::std::set<uint64_t> ever_joined;           // all users who have ever reacted :check: (prevents re-join resets)
    ::std::atomic<uint32_t> timer_generation{0}; // bumped on each timer reset; old threads exit when their gen is stale
    bool lobby_paused = false;                  // true while author has paused the countdown
};

static ::std::map<uint64_t, BlackTeaGame> active_blacktea_games;

static ::std::string sanitize_word(const ::std::string& s) {
    ::std::string out;
    for (char c : s) {
        if (::std::isalpha((unsigned char)c)) out.push_back(::std::tolower((unsigned char)c));
    }
    return out;
}

static ::std::string pick_pattern() {
    // Common 3-letter patterns from actual English words
    static const ::std::vector<::std::string> patterns = {
        // common prefixes/roots
        "pre", "pro", "con", "com", "dis", "mis", "non", "out", "sub", "tra", "tri",
        "anti", "auto", "bio", "geo", "neo", "uni", "mon", "poly",
        // common suffixes/roots
        "ing", "ion", "tion", "ness", "ment", "ful", "less", "able", "ible", "ous",
        "ive", "ate", "ize", "ent", "ant", "ble", "ure", "ity", "age", "dom",
        // common middle patterns
        "and", "ard", "art", "ast", "eat", "ent", "est", "ight", "old", "ort",
        "ain", "ear", "ean", "eal", "ead", "oar", "oor", "air", "ait", "all",
        "ang", "ank", "ant", "ape", "are", "ark", "arm", "arn", "ash", "ask",
        "bar", "bat", "ber", "bet", "bir", "ble", "ock", "ond", "ong", "ook",
        "ool", "oom", "oon", "oot", "ope", "ord", "ore", "ork", "orm", "orn",
        "ound", "our", "ove", "own", "uck", "ude", "uff", "ump", "und", "ung",
        "unk", "unt", "urn", "ust", "ace", "ack", "act", "ade", "afe", "age",
        "ake", "ale", "ame", "ane", "ave", "aze", "ice", "ick", "ide", "ife",
        "ike", "ile", "ime", "ine", "ipe", "ire", "ise", "ite", "ive", "ize"
    };
    
    static ::std::mt19937_64 rng((uint64_t)::std::chrono::high_resolution_clock::now().time_since_epoch().count());
    ::std::uniform_int_distribution<size_t> d(0, patterns.size() - 1);
    return patterns[d(rng)];
}

// Build the standard lobby embed.
static dpp::embed build_lobby_embed(const BlackTeaGame& game) {
    ::std::string desc;
    if (game.lobby_paused) {
        desc = ":pause_button: Timer **paused** by the host. React with " + bronx::EMOJI_CHECK + " to join.";
    } else {
        ::std::string ts = "<t:" + ::std::to_string(game.start_timestamp) + ":R>";
        desc = ":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".";
    }
    return dpp::embed()
        .set_color(0x1E90FF)
        .set_title("⏰ BLACKTEA - Lobby")
        .set_description(desc)
        .add_field("Players", ::std::to_string(game.players.size()) + " players joined", true)
        .add_field("Minimum", "3 players to start", true)
        .set_footer("Each player starts with 2 lives. Words **must** be unique.", "");
}

// Edit the lobby message with the current embed + author control buttons.
// Button custom_ids encode the game_id so the handler can look up the right game.
static void edit_lobby_message(dpp::cluster& bot, const BlackTeaGame& game) {
    ::std::string gid = ::std::to_string(game.game_id);

    dpp::component pause_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_style(game.lobby_paused ? dpp::cos_success : dpp::cos_secondary)
        .set_label(game.lobby_paused ? "\u25b6 Resume" : "\u23f8 Pause")
        .set_id("bt_lobby_pause:" + gid);

    dpp::component cancel_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_label("\u2716 Cancel")
        .set_id("bt_lobby_cancel:" + gid);

    dpp::component row = dpp::component()
        .set_type(dpp::cot_action_row)
        .add_component(pause_btn)
        .add_component(cancel_btn);

    dpp::message msg;
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;
    msg.add_embed(build_lobby_embed(game));
    msg.add_component(row);
    bronx::safe_message_edit(bot, msg);
}

// Called when the lobby timer expires and the game should start (runs on background thread)
static void run_blacktea_game_loop(dpp::cluster& bot, uint64_t game_id) {
    auto it = active_blacktea_games.find(game_id);
    if (it == active_blacktea_games.end()) return;
    BlackTeaGame& game = it->second;

    // Require at least 3 players
    if (game.players.size() < 3) {
        dpp::embed e = dpp::embed().set_color(0xFF0000).set_title("BLACKTEA - Cancelled")
            .set_description("Not enough players joined (minimum 3 required). Game cancelled.");
        bot.message_create(dpp::message(game.channel_id, e));
        active_blacktea_games.erase(game_id);
        return;
    }

    // Initialize lives
    for (auto uid : game.players) game.lives[uid] = 2;
    game.active = true;

    // Announce game start
    dpp::embed start_emb = dpp::embed().set_color(0x00FF00).set_title("☕ BLACKTEA - Starting")
        .set_description("Game is starting with " + ::std::to_string(game.players.size()) + " players. Each player has 2 lives.");
    bot.message_create(dpp::message(game.channel_id, start_emb));

    // Game loop
    while (true) {
        // Check force-stop flag
        if (game.force_stop) {
            bot.message_create(dpp::message(game.channel_id,
                dpp::embed().set_color(0xFF4500).set_title("BLACKTEA - Force Ended")
                .set_description("this game was ended by a server administrator.")));
            active_blacktea_games.erase(game_id);
            return;
        }

        // Check active players
        ::std::vector<uint64_t> alive;
        for (auto uid : game.players) {
            if (game.lives[uid] > 0) alive.push_back(uid);
        }
        if (alive.size() <= 1) break; // winner (or none)

        // Setup round
        game.round++;
        game.current_pattern = pick_pattern();
        game.round_active = true;
        game.responded.clear();

        // Mark all alive players as not responded
        for (auto uid : alive) game.responded[uid] = false;

        // Announce round (short format)
        ::std::string desc = "Round " + ::std::to_string(game.round) + "\n";
        desc += "send a word containing **" + game.current_pattern + "**";

        dpp::embed round_emb = dpp::embed().set_color(0xFFD700).set_title("☕ BLACKTEA — Round " + ::std::to_string(game.round))
            .set_description(desc);
        bot.message_create(dpp::message(game.channel_id, round_emb));

        // Wait 10 seconds collecting submissions (message handler will mark responses)
        ::std::this_thread::sleep_for(::std::chrono::seconds(10));

        // Check force-stop after sleep
        if (game.force_stop) {
            bot.message_create(dpp::message(game.channel_id,
                dpp::embed().set_color(0xFF4500).set_title("BLACKTEA - Force Ended")
                .set_description("this game was ended by a server administrator.")));
            active_blacktea_games.erase(game_id);
            return;
        }

        // Evaluate results
        ::std::vector<uint64_t> eliminated_this_round;
        for (auto uid : alive) {
            if (!game.responded[uid]) {
                game.lives[uid] -= 1;
                if (game.lives[uid] <= 0) eliminated_this_round.push_back(uid);
            }
        }

        // Announce round summary
        ::std::string summary = "Round " + ::std::to_string(game.round) + " complete.\n";
        for (auto uid : alive) {
            summary += "<@" + ::std::to_string(uid) + "> — " + ::std::to_string(game.lives[uid]) + " lives\n";
        }
        if (!eliminated_this_round.empty()) {
            summary += "\nEliminated: ";
            for (auto u : eliminated_this_round) summary += "<@" + ::std::to_string(u) + "> ";
        }
        bot.message_create(dpp::message(game.channel_id, dpp::embed().set_color(0xFF4500).set_description(summary)));

        // Remove players with 0 lives from the active list (they remain in players vector but are considered dead by lives)
        bool only_one_left = true;
        for (auto uid : game.players) {
            if (game.lives[uid] > 0) {
                if (only_one_left && uid != alive.front()) only_one_left = true; // noop
                only_one_left = only_one_left && (game.lives[uid] <= 0);
            }
        }

        // Continue to next round unless someone won
        ::std::vector<uint64_t> still_alive;
        for (auto uid : game.players) if (game.lives[uid] > 0) still_alive.push_back(uid);
        if (still_alive.size() <= 1) break;

        // small pause between rounds
        ::std::this_thread::sleep_for(::std::chrono::milliseconds(800));
    }

    // Game finished — determine winners
    ::std::vector<uint64_t> final_alive;
    for (auto uid : game.players) if (game.lives[uid] > 0) final_alive.push_back(uid);

    if (final_alive.empty()) {
        bot.message_create(dpp::message(game.channel_id, dpp::embed().set_color(0x808080).set_title("BLACKTEA - Game Over").set_description("All players eliminated. No winner.")));
    } else {
        ::std::string winners;
        for (auto uid : final_alive) winners += "<@" + ::std::to_string(uid) + "> ";
        bot.message_create(dpp::message(game.channel_id, dpp::embed().set_color(0x00FF00).set_title("BLACKTEA - Winner").set_description("Winner(s): " + winners)));
    }

    // Cleanup
    active_blacktea_games.erase(game_id);
}

// Reaction join handler and message submit handler will be registered externally via register_blacktea_handlers
inline void register_blacktea_handlers(dpp::cluster& bot) {
    // Reaction join (custom :check: emoji)
    bot.on_message_reaction_add([&bot](const dpp::message_reaction_add_t& event) {
        // find any active lobby with this message id
        for (auto& kv : active_blacktea_games) {
            auto& game = kv.second;
            if (!game.active && game.message_id == event.message_id && game.channel_id == event.channel_id) {
                // Only accept :check: custom emoji
                ::std::string emoji_name = event.reacting_emoji.name;
                if (emoji_name != "check") return;

                uint64_t uid = event.reacting_user.id;
                // Ignore bots
                if (event.reacting_user.is_bot()) return;

                // Add player if not already in the lobby.
                // ever_joined tracks all users who have reacted at least once so
                // that someone can't leave + re-react to repeatedly reset the timer.
                bool first_time_join = (game.ever_joined.find(uid) == game.ever_joined.end());
                game.ever_joined.insert(uid);

                if (::std::find(game.players.begin(), game.players.end(), uid) == game.players.end()) {
                    game.players.push_back(uid);

                    // Reset the countdown only when this is a brand-new user and the
                    // lobby is not currently paused.
                    if (first_time_join && !game.lobby_paused) {
                        int64_t new_ts = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                            ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                        game.start_timestamp = new_ts;

                        // Bump generation so the old timer thread exits on its next tick.
                        uint32_t new_gen = ++game.timer_generation;
                        uint64_t gid_copy = game.game_id;
                        ::std::thread([gid_copy, new_gen, &bot]() mutable {
                            // Poll once per second so we notice a generation change quickly.
                            for (int i = 0; i < 30; ++i) {
                                ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                                auto jt = active_blacktea_games.find(gid_copy);
                                if (jt == active_blacktea_games.end()) return; // game gone
                                if (jt->second.timer_generation != new_gen) return; // superseded
                                if (jt->second.lobby_paused) { ++i; continue; } // freeze while paused (don't count this second)
                            }
                            // Re-check generation one last time before launching.
                            auto jt = active_blacktea_games.find(gid_copy);
                            if (jt == active_blacktea_games.end()) return;
                            if (jt->second.timer_generation != new_gen) return;
                            run_blacktea_game_loop(bot, gid_copy);
                        }).detach();
                    }

                    // Always update the embed when a new player joins (counter only,
                    // no per-player list, so edits stay minimal).
                    edit_lobby_message(bot, game);
                }

                // Acknowledge join via DM
                try {
                    bot.direct_message_create(uid, dpp::message("You joined the game!"));
                } catch (...) {}
                return;
            }
        }
    });

    // Author lobby control buttons: "⏸ Pause / ▶ Resume" and "✖ Cancel"
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        const ::std::string& cid = event.custom_id;

        auto parse_game_id = [&](const ::std::string& prefix) -> uint64_t {
            if (cid.rfind(prefix, 0) != 0) return 0;
            try { return ::std::stoull(cid.substr(prefix.size())); } catch (...) { return 0; }
        };

        if (uint64_t game_id = parse_game_id("bt_lobby_pause:")) {
            auto it = active_blacktea_games.find(game_id);
            if (it == active_blacktea_games.end() || it->second.active) {
                event.reply(dpp::ir_deferred_update_message, ""); return;
            }
            BlackTeaGame& game = it->second;
            // Author-only
            if (event.command.get_issuing_user().id != game.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("Only the game host can do that.").set_flags(dpp::m_ephemeral));
                return;
            }
            game.lobby_paused = !game.lobby_paused;
            if (!game.lobby_paused) {
                // Resuming: push the deadline 30 s from now and spawn a fresh timer thread.
                int64_t new_ts = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                    ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                game.start_timestamp = new_ts;
                uint32_t new_gen = ++game.timer_generation;
                uint64_t gid_copy = game_id;
                ::std::thread([gid_copy, new_gen, &bot]() mutable {
                    for (int i = 0; i < 30; ++i) {
                        ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                        auto jt = active_blacktea_games.find(gid_copy);
                        if (jt == active_blacktea_games.end()) return;
                        if (jt->second.timer_generation != new_gen) return;
                        if (jt->second.lobby_paused) { ++i; continue; }
                    }
                    auto jt = active_blacktea_games.find(gid_copy);
                    if (jt == active_blacktea_games.end()) return;
                    if (jt->second.timer_generation != new_gen) return;
                    run_blacktea_game_loop(bot, gid_copy);
                }).detach();
            }
            edit_lobby_message(bot, game);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_game_id("bt_lobby_cancel:")) {
            auto it = active_blacktea_games.find(game_id);
            if (it == active_blacktea_games.end() || it->second.active) {
                event.reply(dpp::ir_deferred_update_message, ""); return;
            }
            BlackTeaGame& game = it->second;
            if (event.command.get_issuing_user().id != game.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("Only the game host can do that.").set_flags(dpp::m_ephemeral));
                return;
            }
            // Invalidate any running timer thread, then send cancellation message.
            ++game.timer_generation;
            uint64_t channel_id = game.channel_id;
            active_blacktea_games.erase(game_id);
            bot.message_create(dpp::message(channel_id,
                dpp::embed().set_color(0xFF0000).set_title("BLACKTEA - Cancelled")
                .set_description("The lobby was cancelled by the host.")));
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }
    });

    // Message submissions (players reply with word in channel)
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (!event.msg.guild_id) return; // only in guilds
        // find an active game in this channel that is accepting responses
        for (auto& kv : active_blacktea_games) {
            auto& game = kv.second;
            if (!game.active || !game.round_active) continue;
            if (game.channel_id != event.msg.channel_id) continue;

            uint64_t uid = event.msg.author.id;
            if (event.msg.author.is_bot()) continue;

            // Only accept submissions from players who are alive
            if (game.lives.find(uid) == game.lives.end() || game.lives[uid] <= 0) continue;

            // If already responded this round, ignore
            if (game.responded.find(uid) != game.responded.end() && game.responded[uid]) continue;

            // Extract a candidate word from the message: first token with letters
            ::std::istringstream iss(event.msg.content);
            ::std::string token;
            bool accepted = false;
            while (iss >> token) {
                ::std::string cleaned = sanitize_word(token);
                if (cleaned.empty()) continue;
                // must contain the pattern as substring
                if (cleaned.find(game.current_pattern) == ::std::string::npos) continue;
                // must not have been used before game-wide
                if (game.used_words.find(cleaned) != game.used_words.end()) {
                    // inform user
                    event.reply(dpp::message().set_content("That word was already used in this game."));
                    accepted = true; // don't keep searching tokens
                    break;
                }

                // mark accepted
                game.used_words.insert(cleaned);
                game.responded[uid] = true;
                // react to their message to show acceptance (no automatic reaction added)
                accepted = true;
                break;
            }

            if (!accepted) {
                // Not a valid submission; ignore silently
            }
        }
    });
}

inline Command* get_blacktea_command() {
    static Command* blacktea = new Command("blacktea", "Play Black Tea — multiplayer word game", "games", {"bt", "black-tea"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Create game lobby
            BlackTeaGame game;
            game.author_id = event.msg.author.id;
            game.channel_id = event.msg.channel_id;
            game.guild_id = event.msg.guild_id;
            game.active = false; // lobby state until started

            uint64_t game_id = (uint64_t)::std::chrono::system_clock::now().time_since_epoch().count() + event.msg.author.id;
            game.game_id = game_id;
            game.start_timestamp = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;

            // initial embed
            ::std::string ts = "<t:" + ::std::to_string(game.start_timestamp) + ":R>";
            dpp::embed embed = dpp::embed()
                .set_color(0x1E90FF)
                .set_title("⏰ BLACKTEA - Lobby")
                .set_description(":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".")
                .add_field("Players", "0 players joined", true)
                .add_field("Minimum", "3 players to start", true)
                .set_footer("Each player starts with 2 lives. Words must be unique.", "");

            // Author control buttons (Pause / Cancel) — added to the initial message.
            ::std::string gid_str = ::std::to_string(game_id);
            dpp::component pause_btn_init = dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_label("\u23f8 Pause")
                .set_id("bt_lobby_pause:" + gid_str);
            dpp::component cancel_btn_init = dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_label("\u2716 Cancel")
                .set_id("bt_lobby_cancel:" + gid_str);
            dpp::component row_init = dpp::component()
                .set_type(dpp::cot_action_row)
                .add_component(pause_btn_init)
                .add_component(cancel_btn_init);

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(row_init);

            // store game before creating message so reaction handler can find it
            // std::atomic is non-copyable; emplace a default-constructed entry then
            // move the copyable fields in manually.
            active_blacktea_games.emplace(::std::piecewise_construct,
                ::std::forward_as_tuple(game_id), ::std::tuple<>());
            {
                auto& g = active_blacktea_games[game_id];
                g.game_id        = game.game_id;
                g.author_id      = game.author_id;
                g.guild_id       = game.guild_id;
                g.channel_id     = game.channel_id;
                g.active         = game.active;
                g.start_timestamp = game.start_timestamp;
            }

            // send lobby message and add reaction
            bot.message_create(msg.set_channel_id(event.msg.channel_id), [game_id, &bot](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) {
                    active_blacktea_games.erase(game_id);
                    return;
                }

                auto sent_msg = callback.get<dpp::message>();
                active_blacktea_games[game_id].message_id = sent_msg.id;

                // add :check: reaction so users can join quickly
                try { bot.message_add_reaction(sent_msg.id, sent_msg.channel_id, "check:1476703556428890132"); } catch (...) {}

                // start lobby timer on background thread (generation-aware so joins can reset it)
                ::std::thread([game_id, &bot]() mutable {
                    uint32_t my_gen = active_blacktea_games.count(game_id)
                        ? active_blacktea_games[game_id].timer_generation.load() : 0;
                    for (int i = 0; i < 30; ++i) {
                        ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                        auto jt = active_blacktea_games.find(game_id);
                        if (jt == active_blacktea_games.end()) return;
                        if (jt->second.timer_generation != my_gen) return; // reset by a join
                        if (jt->second.lobby_paused) { ++i; continue; }   // freeze while paused
                    }
                    auto jt = active_blacktea_games.find(game_id);
                    if (jt == active_blacktea_games.end()) return;
                    if (jt->second.timer_generation != my_gen) return;
                    run_blacktea_game_loop(bot, game_id);
                }).detach();
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // slash: create lobby similarly
            BlackTeaGame game;
            game.author_id = event.command.get_issuing_user().id;
            game.channel_id = event.command.channel_id;
            game.guild_id = event.command.guild_id;
            game.active = false;

            uint64_t game_id = (uint64_t)::std::chrono::system_clock::now().time_since_epoch().count() + event.command.get_issuing_user().id;
            game.game_id = game_id;
            game.start_timestamp = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;

            ::std::string ts = "<t:" + ::std::to_string(game.start_timestamp) + ":R>";
            dpp::embed embed = dpp::embed()
                .set_color(0x1E90FF)
                .set_title("⏰ BLACKTEA - Lobby")
                .set_description(":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".")
                .add_field("Players", "0 players joined", true)
                .add_field("Minimum", "3 players to start", true)
                .set_footer("Each player starts with 2 lives. Words must be unique.", "");

            // Author control buttons for slash command lobby.
            ::std::string gid_str_sl = ::std::to_string(game_id);
            dpp::component pause_btn_sl = dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_label("\u23f8 Pause")
                .set_id("bt_lobby_pause:" + gid_str_sl);
            dpp::component cancel_btn_sl = dpp::component()
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_label("\u2716 Cancel")
                .set_id("bt_lobby_cancel:" + gid_str_sl);
            dpp::component row_sl = dpp::component()
                .set_type(dpp::cot_action_row)
                .add_component(pause_btn_sl)
                .add_component(cancel_btn_sl);

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(row_sl);

            // std::atomic is non-copyable; emplace a default-constructed entry then
            // move the copyable fields in manually.
            active_blacktea_games.emplace(::std::piecewise_construct,
                ::std::forward_as_tuple(game_id), ::std::tuple<>());
            {
                auto& g = active_blacktea_games[game_id];
                g.game_id        = game.game_id;
                g.author_id      = game.author_id;
                g.guild_id       = game.guild_id;
                g.channel_id     = game.channel_id;
                g.active         = game.active;
                g.start_timestamp = game.start_timestamp;
            }

            // Capture the interaction details for later use
            auto interaction_token = event.command.token;
            
            event.reply(msg, [game_id, &bot, channel_id = event.command.channel_id, interaction_token](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) {
                    active_blacktea_games.erase(game_id);
                    return;
                }

                // Get the original interaction response message using the interaction token
                bot.interaction_response_get_original(interaction_token, [game_id, &bot, channel_id](const dpp::confirmation_callback_t& get_callback) {
                    if (!get_callback.is_error()) {
                        try {
                            auto msg = get_callback.get<dpp::message>();
                            active_blacktea_games[game_id].message_id = msg.id;
                            active_blacktea_games[game_id].channel_id = channel_id;
                            
                            // add :check: reaction so users can join quickly
                            try { bot.message_add_reaction(msg.id, channel_id, "check:1476703556428890132"); } catch (...) {}
                        } catch (...) {}
                    }
                });

                // Start lobby timer regardless (generation-aware so joins can reset it)
                ::std::thread([game_id, &bot]() mutable {
                    uint32_t my_gen = active_blacktea_games.count(game_id)
                        ? active_blacktea_games[game_id].timer_generation.load() : 0;
                    for (int i = 0; i < 30; ++i) {
                        ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                        auto jt = active_blacktea_games.find(game_id);
                        if (jt == active_blacktea_games.end()) return;
                        if (jt->second.timer_generation != my_gen) return;
                        if (jt->second.lobby_paused) { ++i; continue; }
                    }
                    auto jt = active_blacktea_games.find(game_id);
                    if (jt == active_blacktea_games.end()) return;
                    if (jt->second.timer_generation != my_gen) return;
                    run_blacktea_game_loop(bot, game_id);
                }).detach();
            });
        },
        {});

    return blacktea;
}

} // namespace games
} // namespace commands