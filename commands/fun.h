#pragma once
#include "../command.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <vector>
#include <random>

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

    return cmds;
}

} // namespace commands
