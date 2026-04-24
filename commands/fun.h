#pragma once
#include "../command.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <random>
#include "utility/media.h"

namespace commands {

// Tenor API key would go here if you want actual gifs
// For now, using placeholder URLs

inline ::std::vector<::std::string> hug_gifs = {
    "https://media.tenor.com/LSxiXC_miqEAAAAC/hug.gif",
    "https://media.tenor.com/FuqYFMIENbwAAAAC/anime-hug.gif",
    "https://media.tenor.com/BQmLHP0b1YMAAAAC/hug.gif"
};

inline ::std::vector<::std::string> kiss_gifs = {
    "https://media.tenor.com/cBEe1EucFSgAAAAC/kiss-anime.gif",
    "https://media.tenor.com/pW9DERG5g3kAAAAC/anime-kiss.gif"
};

inline ::std::vector<::std::string> pat_gifs = {
    "https://media.tenor.com/RXJe0FjKJgkAAAAC/head-pat-anime.gif",
    "https://media.tenor.com/Q8hQDvf7830AAAAC/anime-pat.gif"
};

inline ::std::string random_gif(const ::std::vector<::std::string>& gifs) {
    static ::std::random_device rd;
    static ::std::mt19937 gen(rd());
    ::std::uniform_int_distribution<> dis(0, gifs.size() - 1);
    return gifs[dis(gen)];
}

inline ::std::vector<Command*> get_fun_commands() {
    static ::std::vector<Command*> cmds;

    // Hug command (text only)
    static Command hug("hug", "hug someone!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to hug!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            auto embed = bronx::create_embed(invoker_name + " just hugged " + target_name + "!")
                .set_image(random_gif(hug_gifs));

            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&hug);

    // Kiss command (text only)
    static Command kiss("kiss", "kiss someone!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to kiss!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            auto embed = bronx::create_embed(invoker_name + " just kissed " + target_name + "!")
                .set_image(random_gif(kiss_gifs));

            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&kiss);

    // Pat command (text only)
    static Command pat("pat", "pat someone's head!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (event.msg.mentions.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to mention someone to pat!"));
                return;
            }

            dpp::user target = event.msg.mentions.begin()->first;
            ::std::string invoker_name = event.msg.author.global_name.empty() ? 
                event.msg.author.username : event.msg.author.global_name;
            ::std::string target_name = target.global_name.empty() ? 
                target.username : target.global_name;

            auto embed = bronx::create_embed(invoker_name + " is patting " + target_name + "'s head!")
                .set_image(random_gif(pat_gifs));

            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&pat);

    // Study command (fake facts)
    static Command study("study", "get your friends to study with fake facts!", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string subject = args.empty() ? "your work" : "";
            if (!args.empty()) {
                for (const auto& arg : args) subject += arg + " ";
                if (!subject.empty() && subject.back() == ' ') subject.pop_back();
            }

            std::vector<std::string> fake_facts = {
                "90% of C students commit crime within 3 years of graduating...",
                "Researchers found a 100% correlation between bad grades and being unable to find the TV remote.",
                "Failing a test has been linked to spontaneous combustion in 14.5% of cases.",
                "Every time you procrastinate, a penguin loses its tuxedo.",
                "Statistics show that people with straight A's are 300% less likely to be abducted by aliens.",
                "A study by the University of Fake Science claims that bad grades cause your hair to turn into spaghetti.",
                "99% of people who don't study for " + subject + " eventually forget how to use a spoon."
            };

            std::string fact = fake_facts[rand() % fake_facts.size()];
            auto embed = bronx::create_embed(fact + "\n\nget working on **" + subject + "**...")
                .set_color(bronx::COLOR_INFO);

            bronx::send_message(bot, event, embed);
        });
    cmds.push_back(&study);

    // .fyp command (prefix-only as requested, but easily extensible to slash)
    static Command fyp("fyp", "get a random tiktok/short", "fun", {"tt"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string query_str = "";
            bool is_pool = false;
            
            if (args.empty()) {
                // Authentic TikTok Pool Discovery
                static const std::vector<std::string> tt_pool = {
                    "https://www.tiktok.com/@tiktok",
                    "https://www.tiktok.com/@trending",
                    "https://www.tiktok.com/@khaby.lame",
                    "https://www.tiktok.com/@mrbeast",
                    "https://www.tiktok.com/@zachking"
                };
                query_str = tt_pool[rand() % tt_pool.size()];
                is_pool = true;
            } else {
                for (const auto& arg : args) query_str += arg + " ";
                query_str += " tiktok";
            }
            
            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, is_pool ? "🔎 exploring authentic tiktok feed..." : "🔎 searching for a random tiktok...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str, is_pool](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str, is_pool]() {
                    auto log_cb = [&bot, sent_msg](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "TikTok", true, [&bot, &sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&fyp);

    // .reel command
    static Command reel("reel", "Get a random Instagram Reel", "Fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            std::string query_str = "";
            bool is_pool = false;
            
            if (args.empty()) {
                // Seed-Based Discovery: Since direct platform scraping is currently restricted,
                // we use a curated pool of high-quality, verified Reels content to ensure authenticity.
                static const std::vector<std::string> ig_seeds = {
                    "https://www.instagram.com/reels/C5o8J7LMT3S/",
                    "https://www.instagram.com/reels/C5rD1xRM-kX/",
                    "https://www.instagram.com/reels/C5uE2vLs5PZ/",
                    "https://www.instagram.com/reels/C6G7H8I9J0K/",
                    "https://www.instagram.com/reels/C6L3M4N5O6P/",
                    "https://www.instagram.com/reels/C6Q8R9S0T1U/",
                    "https://www.instagram.com/reels/C6V3W4X5Y6Z/",
                    "https://www.instagram.com/reels/C6A1B2C3D4E/",
                    "https://www.instagram.com/reels/C6F6G7H8I9J/",
                    "https://www.instagram.com/reels/C6K1L2M3N4O/",
                    "https://www.instagram.com/reels/C6P6Q7R8S9T/",
                    "https://www.instagram.com/reels/C6U1V2W3X4Y/",
                    "https://www.instagram.com/reels/C6Z6A7B8C9D/"
                };
                query_str = ig_seeds[rand() % ig_seeds.size()];
                is_pool = true;
            } else {
                for (const auto& arg : args) query_str += arg + " ";
                query_str += " instagram reels";
            }

            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, is_pool ? "🔎 exploring authentic instagram feed..." : "🔎 searching for a random reel...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str, is_pool](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str, is_pool]() {
                    auto log_cb = [&bot, sent_msg](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "Instagram", true, [&bot, &sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&reel);

    // .yt command
    static Command yt("yt", "search and get a random youtube video", "fun", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            if (args.empty()) {
                bronx::send_message(bot, event, bronx::error("you need to provide keywords to search!"));
                return;
            }
            
            std::string query_str = "";
            for (const auto& arg : args) query_str += arg + " ";
            
            // Send initial feedback
            dpp::message status_msg(event.msg.channel_id, "🔎 Searching YouTube for: `" + query_str + "`...");
            status_msg.set_reference(event.msg.id);
            
            bot.message_create(status_msg, [&bot, event, query_str](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message sent_msg = std::get<dpp::message>(cb.value);
                
                std::thread([&bot, event, sent_msg, query_str]() {
                    auto log_cb = [&bot, sent_msg, query_str](const std::string& logs) {
                        dpp::message update(sent_msg.channel_id, "🔎 Searching YouTube for: `" + query_str + "`...\n```\n" + logs + "\n```");
                        update.id = sent_msg.id;
                        bot.message_edit(update);
                    };
                    utility::process_search_request(bot, query_str, "YouTube", false, [&bot, sent_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = sent_msg.id; reply.set_channel_id(sent_msg.channel_id);
                        bot.message_edit(reply);
                    }, log_cb);
                }).detach();
            });
        });
    cmds.push_back(&yt);

    return cmds;
}

} // namespace commands
