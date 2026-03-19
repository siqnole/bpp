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

struct WhiteTeaGame {
    uint64_t game_id;
    uint64_t author_id;
    uint64_t guild_id;
    uint64_t channel_id;
    uint64_t message_id; // lobby message id
    bool active = false;

    ::std::vector<uint64_t> players;              // players still in lobby / game
    ::std::map<uint64_t, int> lives;              // remaining lives per player
    ::std::set<::std::string> used_words;         // words used during the entire game (lowercase sanitized)
    ::std::map<uint64_t, bool> responded;         // whether player responded this round

    bool round_active = false;
    ::std::map<uint64_t, ::std::string> player_patterns; // per-player pattern for the current round
    int round = 0;

    bool force_stop = false;
    int64_t start_timestamp = 0;

    ::std::set<uint64_t> ever_joined;
    ::std::atomic<uint32_t> timer_generation{0};
    bool lobby_paused = false;
};

static ::std::map<uint64_t, WhiteTeaGame> active_whitetea_games;

static ::std::string wt_sanitize_word(const ::std::string& s) {
    ::std::string out;
    for (char c : s) {
        if (::std::isalpha((unsigned char)c)) out.push_back(::std::tolower((unsigned char)c));
    }
    return out;
}

static ::std::string wt_pick_pattern() {
    static const ::std::vector<::std::string> patterns = {
        "pre", "pro", "con", "com", "dis", "mis", "non", "out", "sub", "tra", "tri",
        "anti", "auto", "bio", "geo", "neo", "uni", "mon", "poly",
        "ing", "ion", "tion", "ness", "ment", "ful", "less", "able", "ible", "ous",
        "ive", "ate", "ize", "ent", "ant", "ble", "ure", "ity", "age", "dom",
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

// Pick N distinct patterns (one per player). Falls back to allowing repeats if
// the pool is somehow exhausted, which is practically impossible.
static ::std::vector<::std::string> wt_pick_patterns_for(const ::std::vector<uint64_t>& alive) {
    static const ::std::vector<::std::string> patterns = {
        "pre", "pro", "con", "com", "dis", "mis", "non", "out", "sub", "tra", "tri",
        "anti", "auto", "bio", "geo", "neo", "uni", "mon", "poly",
        "ing", "ion", "tion", "ness", "ment", "ful", "less", "able", "ible", "ous",
        "ive", "ate", "ize", "ent", "ant", "ble", "ure", "ity", "age", "dom",
        "and", "ard", "art", "ast", "eat", "est", "ight", "old", "ort",
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

    // Shuffle a copy of the pool and take the first N.
    ::std::vector<::std::string> pool = patterns;
    ::std::shuffle(pool.begin(), pool.end(), rng);

    ::std::vector<::std::string> result;
    result.reserve(alive.size());
    for (size_t i = 0; i < alive.size(); ++i) {
        result.push_back(pool[i % pool.size()]);
    }
    return result;
}

static dpp::embed wt_build_lobby_embed(const WhiteTeaGame& game) {
    ::std::string desc;
    if (game.lobby_paused) {
        desc = ":pause_button: Timer **paused** by the host. React with " + bronx::EMOJI_CHECK + " to join.";
    } else {
        ::std::string ts = "<t:" + ::std::to_string(game.start_timestamp) + ":R>";
        desc = ":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".";
    }
    return dpp::embed()
        .set_color(0xF5F5DC) // warm off-white / tea colour
        .set_title("🍵 WHITETEA - Lobby")
        .set_description(desc)
        .add_field("Players", ::std::to_string(game.players.size()) + " players joined", true)
        .add_field("Minimum", "3 players to start", true)
        .set_footer("Each player starts with 2 lives. Every player gets their **OWN pattern** each round!", "");
}

static void wt_edit_lobby_message(dpp::cluster& bot, const WhiteTeaGame& game) {
    ::std::string gid = ::std::to_string(game.game_id);

    dpp::component pause_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_style(game.lobby_paused ? dpp::cos_success : dpp::cos_secondary)
        .set_label(game.lobby_paused ? "\u25b6 Resume" : "\u23f8 Pause")
        .set_id("wt_lobby_pause:" + gid);

    dpp::component cancel_btn = dpp::component()
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_label("\u2716 Cancel")
        .set_id("wt_lobby_cancel:" + gid);

    dpp::component row = dpp::component()
        .set_type(dpp::cot_action_row)
        .add_component(pause_btn)
        .add_component(cancel_btn);

    dpp::message msg;
    msg.id = game.message_id;
    msg.channel_id = game.channel_id;
    msg.add_embed(wt_build_lobby_embed(game));
    msg.add_component(row);
    bronx::safe_message_edit(bot, msg);
}

static void run_whitetea_game_loop(dpp::cluster& bot, uint64_t game_id) {
    auto it = active_whitetea_games.find(game_id);
    if (it == active_whitetea_games.end()) return;
    WhiteTeaGame& game = it->second;

    if (game.players.size() < 3) {
        dpp::embed e = dpp::embed().set_color(0xFF0000).set_title("WHITETEA - Cancelled")
            .set_description("Not enough players joined (minimum 3 required). Game cancelled.");
        bot.message_create(dpp::message(game.channel_id, e));
        active_whitetea_games.erase(game_id);
        return;
    }

    for (auto uid : game.players) game.lives[uid] = 2;
    game.active = true;

    dpp::embed start_emb = dpp::embed().set_color(0x00FF00).set_title("🍵 WHITETEA - Starting")
        .set_description("Game is starting with " + ::std::to_string(game.players.size()) +
            " players. Each player has **2 lives** and receives their **own unique pattern** every round!");
    bot.message_create(dpp::message(game.channel_id, start_emb));

    while (true) {
        if (game.force_stop) {
            bot.message_create(dpp::message(game.channel_id,
                dpp::embed().set_color(0xFF4500).set_title("WHITETEA - Force Ended")
                .set_description("This game was ended by a server administrator.")));
            active_whitetea_games.erase(game_id);
            return;
        }

        // Collect alive players
        ::std::vector<uint64_t> alive;
        for (auto uid : game.players) {
            if (game.lives[uid] > 0) alive.push_back(uid);
        }
        if (alive.size() <= 1) break;

        // Assign each alive player a distinct pattern for this round
        game.round++;
        game.player_patterns.clear();
        game.responded.clear();

        ::std::vector<::std::string> assigned = wt_pick_patterns_for(alive);
        for (size_t i = 0; i < alive.size(); ++i) {
            game.player_patterns[alive[i]] = assigned[i];
            game.responded[alive[i]] = false;
        }
        game.round_active = true;

        // Build the round announcement showing each player their own pattern
        ::std::string desc = "**Round " + ::std::to_string(game.round) + "** — each player has their own pattern!\n\n";
        for (auto uid : alive) {
            desc += "<@" + ::std::to_string(uid) + "> → **" + game.player_patterns[uid] + "**\n";
        }
        desc += "\nYou have **10 seconds** to send a word containing your pattern!";

        dpp::embed round_emb = dpp::embed()
            .set_color(0xF5F5DC)
            .set_title("🍵 WHITETEA — Round " + ::std::to_string(game.round))
            .set_description(desc);
        bot.message_create(dpp::message(game.channel_id, round_emb));

        ::std::this_thread::sleep_for(::std::chrono::seconds(10));

        if (game.force_stop) {
            bot.message_create(dpp::message(game.channel_id,
                dpp::embed().set_color(0xFF4500).set_title("WHITETEA - Force Ended")
                .set_description("This game was ended by a server administrator.")));
            active_whitetea_games.erase(game_id);
            return;
        }

        game.round_active = false;

        // Penalise players who didn't respond
        ::std::vector<uint64_t> eliminated_this_round;
        for (auto uid : alive) {
            if (!game.responded[uid]) {
                game.lives[uid] -= 1;
                if (game.lives[uid] <= 0) eliminated_this_round.push_back(uid);
            }
        }

        // Round summary
        ::std::string summary = "**Round " + ::std::to_string(game.round) + " complete.**\n\n";
        for (auto uid : alive) {
            summary += "<@" + ::std::to_string(uid) + "> — " + ::std::to_string(game.lives[uid]) + " ❤️";
            if (!game.responded[uid]) summary += " *(missed)*";
            summary += "\n";
        }
        if (!eliminated_this_round.empty()) {
            summary += "\n💀 Eliminated: ";
            for (auto u : eliminated_this_round) summary += "<@" + ::std::to_string(u) + "> ";
        }
        bot.message_create(dpp::message(game.channel_id,
            dpp::embed().set_color(0xFF4500).set_title("🍵 WHITETEA — Round Summary").set_description(summary)));

        // Check if only one survivor remains
        ::std::vector<uint64_t> still_alive;
        for (auto uid : game.players) if (game.lives[uid] > 0) still_alive.push_back(uid);
        if (still_alive.size() <= 1) break;

        ::std::this_thread::sleep_for(::std::chrono::milliseconds(800));
    }

    // Determine winner(s)
    ::std::vector<uint64_t> final_alive;
    for (auto uid : game.players) if (game.lives[uid] > 0) final_alive.push_back(uid);

    if (final_alive.empty()) {
        bot.message_create(dpp::message(game.channel_id,
            dpp::embed().set_color(0x808080).set_title("WHITETEA - Game Over")
            .set_description("All players eliminated. No winner.")));
    } else {
        ::std::string winners;
        for (auto uid : final_alive) winners += "<@" + ::std::to_string(uid) + "> ";
        bot.message_create(dpp::message(game.channel_id,
            dpp::embed().set_color(0x00FF00).set_title("🍵 WHITETEA - Winner!")
            .set_description("🏆 Winner(s): " + winners)));
    }

    active_whitetea_games.erase(game_id);
}

inline void register_whitetea_handlers(dpp::cluster& bot) {
    // ── Reaction join ────────────────────────────────────────────────────────
    bot.on_message_reaction_add([&bot](const dpp::message_reaction_add_t& event) {
        for (auto& kv : active_whitetea_games) {
            auto& game = kv.second;
            if (!game.active && game.message_id == event.message_id && game.channel_id == event.channel_id) {
                if (event.reacting_emoji.name != "check") return;
                if (event.reacting_user.is_bot()) return;

                uint64_t uid = event.reacting_user.id;
                bool first_time_join = (game.ever_joined.find(uid) == game.ever_joined.end());
                game.ever_joined.insert(uid);

                if (::std::find(game.players.begin(), game.players.end(), uid) == game.players.end()) {
                    game.players.push_back(uid);

                    if (first_time_join && !game.lobby_paused) {
                        int64_t new_ts = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                            ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                        game.start_timestamp = new_ts;

                        uint32_t new_gen = ++game.timer_generation;
                        uint64_t gid_copy = game.game_id;
                        ::std::thread([gid_copy, new_gen, &bot]() mutable {
                            for (int i = 0; i < 30; ++i) {
                                ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                                auto jt = active_whitetea_games.find(gid_copy);
                                if (jt == active_whitetea_games.end()) return;
                                if (jt->second.timer_generation != new_gen) return;
                                if (jt->second.lobby_paused) { ++i; continue; }
                            }
                            auto jt = active_whitetea_games.find(gid_copy);
                            if (jt == active_whitetea_games.end()) return;
                            if (jt->second.timer_generation != new_gen) return;
                            run_whitetea_game_loop(bot, gid_copy);
                        }).detach();
                    }

                    wt_edit_lobby_message(bot, game);
                }

                try { bot.direct_message_create(uid, dpp::message("You joined the White Tea game!")); } catch (...) {}
                return;
            }
        }
    });

    // ── Author lobby control buttons ─────────────────────────────────────────
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        const ::std::string& cid = event.custom_id;

        auto parse_game_id = [&](const ::std::string& prefix) -> uint64_t {
            if (cid.rfind(prefix, 0) != 0) return 0;
            try { return ::std::stoull(cid.substr(prefix.size())); } catch (...) { return 0; }
        };

        if (uint64_t game_id = parse_game_id("wt_lobby_pause:")) {
            auto it = active_whitetea_games.find(game_id);
            if (it == active_whitetea_games.end() || it->second.active) {
                event.reply(dpp::ir_deferred_update_message, ""); return;
            }
            WhiteTeaGame& game = it->second;
            if (event.command.get_issuing_user().id != game.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("Only the game host can do that.").set_flags(dpp::m_ephemeral));
                return;
            }
            game.lobby_paused = !game.lobby_paused;
            if (!game.lobby_paused) {
                int64_t new_ts = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                    ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;
                game.start_timestamp = new_ts;
                uint32_t new_gen = ++game.timer_generation;
                uint64_t gid_copy = game_id;
                ::std::thread([gid_copy, new_gen, &bot]() mutable {
                    for (int i = 0; i < 30; ++i) {
                        ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                        auto jt = active_whitetea_games.find(gid_copy);
                        if (jt == active_whitetea_games.end()) return;
                        if (jt->second.timer_generation != new_gen) return;
                        if (jt->second.lobby_paused) { ++i; continue; }
                    }
                    auto jt = active_whitetea_games.find(gid_copy);
                    if (jt == active_whitetea_games.end()) return;
                    if (jt->second.timer_generation != new_gen) return;
                    run_whitetea_game_loop(bot, gid_copy);
                }).detach();
            }
            wt_edit_lobby_message(bot, game);
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }

        if (uint64_t game_id = parse_game_id("wt_lobby_cancel:")) {
            auto it = active_whitetea_games.find(game_id);
            if (it == active_whitetea_games.end() || it->second.active) {
                event.reply(dpp::ir_deferred_update_message, ""); return;
            }
            WhiteTeaGame& game = it->second;
            if (event.command.get_issuing_user().id != game.author_id) {
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().set_content("Only the game host can do that.").set_flags(dpp::m_ephemeral));
                return;
            }
            ++game.timer_generation;
            uint64_t channel_id = game.channel_id;
            active_whitetea_games.erase(game_id);
            bot.message_create(dpp::message(channel_id,
                dpp::embed().set_color(0xFF0000).set_title("WHITETEA - Cancelled")
                .set_description("The lobby was cancelled by the host.")));
            event.reply(dpp::ir_deferred_update_message, "");
            return;
        }
    });

    // ── Message submissions ───────────────────────────────────────────────────
    bot.on_message_create([&bot](const dpp::message_create_t& event) {
        if (!event.msg.guild_id) return;
        for (auto& kv : active_whitetea_games) {
            auto& game = kv.second;
            if (!game.active || !game.round_active) continue;
            if (game.channel_id != event.msg.channel_id) continue;

            uint64_t uid = event.msg.author.id;
            if (event.msg.author.is_bot()) continue;

            // Only alive players who haven't responded yet
            if (game.lives.find(uid) == game.lives.end() || game.lives[uid] <= 0) continue;
            if (game.responded.find(uid) != game.responded.end() && game.responded[uid]) continue;

            // This player's specific pattern for this round
            auto pit = game.player_patterns.find(uid);
            if (pit == game.player_patterns.end()) continue;
            const ::std::string& required_pattern = pit->second;

            ::std::istringstream iss(event.msg.content);
            ::std::string token;
            while (iss >> token) {
                ::std::string cleaned = wt_sanitize_word(token);
                if (cleaned.empty()) continue;

                // Must contain THIS player's pattern
                if (cleaned.find(required_pattern) == ::std::string::npos) continue;

                // Must not be a word already used game-wide
                if (game.used_words.find(cleaned) != game.used_words.end()) {
                    event.reply(dpp::message().set_content("That word was already used in this game."));
                    break;
                }

                // Accept
                game.used_words.insert(cleaned);
                game.responded[uid] = true;
                break;
            }
        }
    });
}

inline Command* get_whitetea_command() {
    static Command* whitetea = new Command("whitetea", "Play White Tea — each player gets their own pattern per round!", "games", {"wt", "white-tea"}, true,
        // ── Prefix command ───────────────────────────────────────────────────
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            WhiteTeaGame game;
            game.author_id = event.msg.author.id;
            game.channel_id = event.msg.channel_id;
            game.guild_id = event.msg.guild_id;
            game.active = false;

            uint64_t game_id = (uint64_t)::std::chrono::system_clock::now().time_since_epoch().count() + event.msg.author.id;
            game.game_id = game_id;
            game.start_timestamp = (int64_t)::std::chrono::duration_cast<::std::chrono::seconds>(
                ::std::chrono::system_clock::now().time_since_epoch()).count() + 30;

            ::std::string ts = "<t:" + ::std::to_string(game.start_timestamp) + ":R>";
            dpp::embed embed = dpp::embed()
                .set_color(0xF5F5DC)
                .set_title("🍵 WHITETEA - Lobby")
                .set_description(":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".")
                .add_field("Players", "0 players joined", true)
                .add_field("Minimum", "3 players to start", true)
                .set_footer("Each player starts with 2 lives. Every player gets their OWN pattern each round!", "");

            ::std::string gid_str = ::std::to_string(game_id);
            dpp::component pause_btn = dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_secondary)
                .set_label("\u23f8 Pause").set_id("wt_lobby_pause:" + gid_str);
            dpp::component cancel_btn = dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger)
                .set_label("\u2716 Cancel").set_id("wt_lobby_cancel:" + gid_str);
            dpp::component row = dpp::component().set_type(dpp::cot_action_row)
                .add_component(pause_btn).add_component(cancel_btn);

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(row);

            active_whitetea_games.emplace(::std::piecewise_construct,
                ::std::forward_as_tuple(game_id), ::std::tuple<>());
            {
                auto& g = active_whitetea_games[game_id];
                g.game_id         = game.game_id;
                g.author_id       = game.author_id;
                g.guild_id        = game.guild_id;
                g.channel_id      = game.channel_id;
                g.active          = game.active;
                g.start_timestamp = game.start_timestamp;
            }

            bot.message_create(msg.set_channel_id(event.msg.channel_id),
                [game_id, &bot](const dpp::confirmation_callback_t& callback) mutable {
                    if (callback.is_error()) { active_whitetea_games.erase(game_id); return; }

                    auto sent_msg = callback.get<dpp::message>();
                    active_whitetea_games[game_id].message_id = sent_msg.id;

                    try { bot.message_add_reaction(sent_msg.id, sent_msg.channel_id, "check:1476703556428890132"); } catch (...) {}

                    ::std::thread([game_id, &bot]() mutable {
                        uint32_t my_gen = active_whitetea_games.count(game_id)
                            ? active_whitetea_games[game_id].timer_generation.load() : 0;
                        for (int i = 0; i < 30; ++i) {
                            ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                            auto jt = active_whitetea_games.find(game_id);
                            if (jt == active_whitetea_games.end()) return;
                            if (jt->second.timer_generation != my_gen) return;
                            if (jt->second.lobby_paused) { ++i; continue; }
                        }
                        auto jt = active_whitetea_games.find(game_id);
                        if (jt == active_whitetea_games.end()) return;
                        if (jt->second.timer_generation != my_gen) return;
                        run_whitetea_game_loop(bot, game_id);
                    }).detach();
                });
        },
        // ── Slash command ────────────────────────────────────────────────────
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            WhiteTeaGame game;
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
                .set_color(0xF5F5DC)
                .set_title("🍵 WHITETEA - Lobby")
                .set_description(":alarm_clock: Waiting for players, react with " + bronx::EMOJI_CHECK + " to join. The game will begin " + ts + ".")
                .add_field("Players", "0 players joined", true)
                .add_field("Minimum", "3 players to start", true)
                .set_footer("Each player starts with 2 lives. Every player gets their OWN pattern each round!", "");

            ::std::string gid_str = ::std::to_string(game_id);
            dpp::component pause_btn = dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_secondary)
                .set_label("\u23f8 Pause").set_id("wt_lobby_pause:" + gid_str);
            dpp::component cancel_btn = dpp::component().set_type(dpp::cot_button).set_style(dpp::cos_danger)
                .set_label("\u2716 Cancel").set_id("wt_lobby_cancel:" + gid_str);
            dpp::component row = dpp::component().set_type(dpp::cot_action_row)
                .add_component(pause_btn).add_component(cancel_btn);

            dpp::message msg;
            msg.add_embed(embed);
            msg.add_component(row);

            active_whitetea_games.emplace(::std::piecewise_construct,
                ::std::forward_as_tuple(game_id), ::std::tuple<>());
            {
                auto& g = active_whitetea_games[game_id];
                g.game_id         = game.game_id;
                g.author_id       = game.author_id;
                g.guild_id        = game.guild_id;
                g.channel_id      = game.channel_id;
                g.active          = game.active;
                g.start_timestamp = game.start_timestamp;
            }

            auto interaction_token = event.command.token;
            event.reply(msg, [game_id, &bot, channel_id = event.command.channel_id, interaction_token](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) { active_whitetea_games.erase(game_id); return; }

                bot.interaction_response_get_original(interaction_token,
                    [game_id, &bot, channel_id](const dpp::confirmation_callback_t& get_callback) {
                        if (!get_callback.is_error()) {
                            try {
                                auto m = get_callback.get<dpp::message>();
                                active_whitetea_games[game_id].message_id = m.id;
                                active_whitetea_games[game_id].channel_id = channel_id;
                                try { bot.message_add_reaction(m.id, channel_id, "check:1476703556428890132"); } catch (...) {}
                            } catch (...) {}
                        }
                    });

                ::std::thread([game_id, &bot]() mutable {
                    uint32_t my_gen = active_whitetea_games.count(game_id)
                        ? active_whitetea_games[game_id].timer_generation.load() : 0;
                    for (int i = 0; i < 30; ++i) {
                        ::std::this_thread::sleep_for(::std::chrono::seconds(1));
                        auto jt = active_whitetea_games.find(game_id);
                        if (jt == active_whitetea_games.end()) return;
                        if (jt->second.timer_generation != my_gen) return;
                        if (jt->second.lobby_paused) { ++i; continue; }
                    }
                    auto jt = active_whitetea_games.find(game_id);
                    if (jt == active_whitetea_games.end()) return;
                    if (jt->second.timer_generation != my_gen) return;
                    run_whitetea_game_loop(bot, game_id);
                }).detach();
            });
        },
        {});

    return whitetea;
}

} // namespace games
} // namespace commands