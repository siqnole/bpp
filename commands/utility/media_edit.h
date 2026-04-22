#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "media.h"
#include <map>
#include <random>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace commands {
namespace utility {

struct MediaEffect {
    std::string name;
    std::string description;
    std::string filter;
    bool animated_only = false; // For effects that require time (t)
};

static const std::map<std::string, MediaEffect> EFFECT_REGISTRY = {
    {"blur", {"Blur", "Applies a box blur", "boxblur=5:1"}},
    {"hue", {"Hue", "Rotates the color hue", "hue=h=90"}},
    {"invert", {"Invert", "Inverts image colors", "negate"}},
    {"edge", {"Edge", "Canny edge detection", "edgedetect=mode=colormix:high=0.1:low=0.1"}},
    {"pixelate", {"Pixelate", "Mosaic/Pixelation effect", "scale=iw/10:-1,scale=iw*10:-1:flags=neighbor"}},
    {"sepia", {"Sepia", "Vintage brown tone", "colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131"}},
    {"mirror", {"Mirror", "Horizontal mirror", "split[l][r];[r]hflip[f];[l][f]hstack"}},
    {"vmirror", {"Vertical Mirror", "Vertical mirror", "split[t][b];[b]vflip[f];[t][f]vstack"}},
    {"flip", {"Flip", "Horizontal flip", "hflip"}},
    {"flop", {"Flop", "Vertical flip", "vflip"}},
    {"rgb", {"RGB Party", "Rainbow hue cycling", "hue=h=t*360", true}},
    {"deepfry", {"Deepfry", "Extreme saturation and contrast", "unsharp=7:7:2.5:7:7:2.5,eq=contrast=2:saturation=5"}},
    {"vaporwave", {"Vaporwave", "Magenta/Cyan aesthetic", "curves=vintage,hue=h=300:s=1.5"}},
    {"vintage", {"Vintage", "1920s film style", "curves=all='0/0 0.5/0.46 1/1',noise=alls=20:allf=t+u,hue=s=0"}},
    {"vhs", {"VHS", "Analog tape distortion", "chromashift=cbh=2:crv=1,noise=alls=10:allf=t,boxblur=1:1"}},
    {"cyberpunk", {"Cyberpunk", "Neon enhancement", "eq=contrast=1.5:saturation=2,hue=h=280:s=1.2"}},
    {"oilpaint", {"Oil Paint", "Artistic smudge effect", "hqdn3d=1.5:1.5:6:6,unsharp=5:5:1.0"}},
    {"acid", {"Acid Trip", "Psychedelic hue/sat oscillation", "hue=h='T*100':s='sin(T)*2+2'", true}},
    {"kaleidoscope", {"Kaleidoscope", "4-way mirrored symmetry", "split=4[v1][v2][v3][v4];[v2]hflip[v2h];[v3]vflip[v3v];[v4]hflip,vflip[v4hv];[v1][v2h]hstack[top];[v3v][v4hv]hstack[bottom];[top][bottom]vstack,scale=iw/2:ih/2"}},
    {"bulge", {"Bulge", "Fish-eye lens distortion", "vignette,lenscorrection=k1=0.2:k2=0.2"}},
    {"wave", {"Wave", "Aquatic ripples", "format=rgb24,geq=r='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':g='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':b='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))'", true}},
    {"thermal", {"Thermal", "Heatmap simulation", "format=gray,colorchannelmixer=1:0:0:0:0:1:0:0:0:0:1:0,curves=all='0/0 0.5/1 1/0',hue=h=240:s=2"}},
    {"night", {"Night Vision", "Amplified green grain", "hue=s=0,eq=brightness=0.1:contrast=1.5,colorchannelmixer=0:1:0:0:0:1:0:0:0:1:0:0,noise=alls=20:allf=t+u"}},
    {"xray", {"X-Ray", "Inverted skeletal look", "negate,format=gray,curves=all='0/0 0.5/1 1/0'"}},
    {"crt", {"CRT", "Retro TV scanlines", "geq=r='if(mod(Y,2),R,R*0.5)':g='if(mod(Y,2),G,G*0.5)':b='if(mod(Y,2),B,B*0.5)',vignette"}},
    {"pixelsort", {"Pixelsort", "Digital smear effect", "transpose,boxblur=20:1,transpose"}},
    {"hyperpixel", {"Hyperpixel", "Extreme low-res scaling", "scale=iw/20:-1,scale=iw*20:-1:flags=neighbor"}},
    {"datamosh", {"Datamosh", "Pixel bleed glitch", "mpdecimate,lagfun=decay=0.98:range=50,scale=iw/2:-1,scale=iw*2:-1:flags=neighbor", true}},
    {"melt", {"Melt", "Liquification effect", "minterpolate=fps=20:scd=none:me_mode=bidir:mi_mode=mci", true}},
    {"smear", {"Smear", "Ghostly motion trails", "lagfun=decay=0.95:range=24", true}}
};

inline void process_media_edit(dpp::cluster& bot, dpp::attachment attachment, const std::string& filter_chain, std::function<void(const dpp::message&)> responder, double target_fps = 12.0, int target_duration = 10) {
    if (attachment.size > 25 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " File is too large (max 25MB)."));
        return;
    }

    auto response = http_get_sync(attachment.url);
    if (response.status != 200) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to download attachment."));
        return;
    }

    // Generate unique filenames for this request
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string id = std::to_string(dis(gen));
    
    std::string temp_dir = "/tmp/bronx_edit_" + id;
    std::filesystem::create_directories(temp_dir);
    
    std::string in_ext = attachment.filename.substr(attachment.filename.find_last_of("."));
    // Ensure extension is safe
    if (in_ext.find("/") != std::string::npos) in_ext = ".bin";
    
    std::string in_path = temp_dir + "/input" + in_ext;
    
    std::string mime = attachment.content_type;
    bool is_video = (mime.find("video/") != std::string::npos);
    bool is_gif = (mime == "image/gif");
    bool animated = is_video || is_gif;

    // Output settings
    std::string out_ext = animated ? ".gif" : in_ext;
    if (is_video) out_ext = ".mp4";
    
    std::string out_path = temp_dir + "/output" + out_ext;

    std::ofstream out_file(in_path, std::ios::binary);
    out_file.write(response.body.data(), response.body.size());
    out_file.close();

    // Construct FFmpeg command
    std::string cmd;
    std::string fps_str = std::to_string(target_fps);
    std::string dur_str = std::to_string(target_duration);

    if (animated && out_ext == ".gif") {
        // High quality GIF encoding with palette
        cmd = "ffmpeg -y -i " + in_path + " -t " + dur_str + " -vf \"" + filter_chain + ",fps=" + fps_str + ",scale=320:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse=dither=bayer:bayer_scale=1\" " + out_path + " > /dev/null 2>&1";
    } else if (is_video) {
        // Video to video
        cmd = "ffmpeg -y -i " + in_path + " -t " + dur_str + " -vf \"" + filter_chain + "\" -c:v libx264 -pix_fmt yuv420p -profile:v baseline -level 3.0 -crf 28 -preset faster -c:a copy " + out_path + " > /dev/null 2>&1";
    } else {
        // Image to image
        cmd = "ffmpeg -y -i " + in_path + " -vf \"" + filter_chain + "\" " + out_path + " > /dev/null 2>&1";
    }
    
    int result = std::system(cmd.c_str());
    if (result != 0) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to process media. Filter might be incompatible with this file type."));
        return;
    }

    std::ifstream res_file(out_path, std::ios::binary | std::ios::ate);
    if (!res_file.is_open()) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to read processed file."));
        return;
    }

    std::streamsize res_size = res_file.tellg();
    if (res_size > 25 * 1024 * 1024) {
         std::filesystem::remove_all(temp_dir);
         responder(dpp::message(bronx::EMOJI_DENY + " Processed file is too large for Discord (max 25MB)."));
         return;
    }

    res_file.seekg(0, std::ios::beg);
    std::string res_data(res_size, '\0');
    res_file.read(&res_data[0], res_size);
    res_file.close();

    std::filesystem::remove_all(temp_dir);

    dpp::message msg;
    msg.add_file("edited" + out_ext, res_data);
    responder(msg);
}

inline void handle_speed_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please provide a speed (e.g., `2x`, `0.5x`, `15fps`, `1fps`).").set_reference(event.msg.id));
        return;
    }

    std::string speed_arg = args[0];
    double target_fps = 12.0;
    std::string filter = "setpts=PTS"; // Default identity
    std::string label = "Speed";

    try {
        if (speed_arg.find("fps") != std::string::npos) {
            double fps = std::stod(speed_arg.substr(0, speed_arg.find("fps")));
            if (fps <= 0 || fps > 120) throw std::invalid_argument("FPS out of range (0.1 - 120)");
            target_fps = fps;
            label = std::to_string(fps) + " FPS";
        } else if (speed_arg.back() == 'x') {
            double multiplier = std::stod(speed_arg.substr(0, speed_arg.size() - 1));
            if (multiplier <= 0 || multiplier > 50) throw std::invalid_argument("Multiplier out of range (0.01x - 50x)");
            target_fps = 12.0 * multiplier;
            filter = "setpts=PTS/" + std::to_string(multiplier);
            label = speed_arg;
        } else {
             // Treat as multiplier if no suffix
             double multiplier = std::stod(speed_arg);
             if (multiplier <= 0 || multiplier > 50) throw std::invalid_argument("Invalid speed format");
             target_fps = 12.0 * multiplier;
             filter = "setpts=PTS/" + std::to_string(multiplier);
             label = speed_arg + "x";
        }
    } catch (...) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Invalid speed. Examples: `2x`, `0.5x`, `30fps`, `0.5fps`.").set_reference(event.msg.id));
        return;
    }

    dpp::message msg = event.msg;
    
    auto process_msg = [&bot, event, filter, target_fps, label](const dpp::message& target_msg) {
        if (target_msg.attachments.empty()) {
             bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No media found to edit.").set_reference(event.msg.id));
             return;
        }
        
        const dpp::attachment* attachment = nullptr;
        for (const auto& a : target_msg.attachments) {
            if (a.content_type.find("image/") != std::string::npos || a.content_type.find("video/") != std::string::npos) {
                attachment = &a;
                break;
            }
        }
        
        if (!attachment) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No supported media found.").set_reference(event.msg.id));
            return;
        }

        dpp::attachment att = *attachment;
        bot.message_create(dpp::message(event.msg.channel_id, "⏳ Adjusting speed to **" + label + "**...").set_reference(event.msg.id), [&bot, event, att, filter, target_fps](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            dpp::message status_msg = std::get<dpp::message>(cb.value);

            ::std::thread([&bot, event, att, filter, target_fps, status_msg]() {
                process_media_edit(bot, att, filter, [&bot, &status_msg](const dpp::message& m) {
                    dpp::message reply = m;
                    reply.id = status_msg.id;
                    reply.set_channel_id(status_msg.channel_id);
                    bot.message_edit(reply);
                }, target_fps);
            }).detach();
        });
    };

    if (!msg.attachments.empty()) {
        process_msg(msg);
    } else if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id, [process_msg, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            process_msg(::std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please attach a GIF/Video or reply to one.").set_reference(event.msg.id));
    }
}

inline void handle_media_edit_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args, const std::string& effect_key) {
    auto it = EFFECT_REGISTRY.find(effect_key);
    if (it == EFFECT_REGISTRY.end() && effect_key != "random") return;

    dpp::message msg = event.msg;
    
    auto process_msg = [&bot, event, it, effect_key](const dpp::message& target_msg) {
        if (target_msg.attachments.empty()) {
             bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No media found to edit.").set_reference(event.msg.id));
             return;
        }
        
        // Find first image/video
        const dpp::attachment* found_attachment = nullptr;
        for (const auto& a : target_msg.attachments) {
            if (a.content_type.find("image/") != std::string::npos || a.content_type.find("video/") != std::string::npos) {
                found_attachment = &a;
                break;
            }
        }
        
        if (!found_attachment) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No supported media found.").set_reference(event.msg.id));
            return;
        }

        bool is_animated = (found_attachment->content_type == "image/gif" || found_attachment->content_type.find("video/") != std::string::npos);
        
        std::string filter;
        std::string final_effect_name;

        // Selection Logic
        if (effect_key == "random") {
            std::vector<std::string> keys;
            for (auto const& [key, val] : EFFECT_REGISTRY) {
                // If static, skip animated_only effects
                if (!is_animated && val.animated_only) continue;
                keys.push_back(key);
            }

            if (keys.size() < 3) {
                 bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Not enough compatible effects found for this file type.").set_reference(event.msg.id));
                 return;
            }

            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(keys.begin(), keys.end(), g);
            
            filter = EFFECT_REGISTRY.at(keys[0]).filter + "," + EFFECT_REGISTRY.at(keys[1]).filter + "," + EFFECT_REGISTRY.at(keys[2]).filter;
            final_effect_name = "Random (" + EFFECT_REGISTRY.at(keys[0]).name + " + " + EFFECT_REGISTRY.at(keys[1]).name + " + " + EFFECT_REGISTRY.at(keys[2]).name + ")";
        } else {
            if (!is_animated && it->second.animated_only) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " The **" + it->second.name + "** effect only works on animated GIFs or Videos.").set_reference(event.msg.id));
                return;
            }
            filter = it->second.filter;
            final_effect_name = it->second.name;
        }

        dpp::attachment attachment = *found_attachment;
        bot.message_create(dpp::message(event.msg.channel_id, "✨ Applying **" + final_effect_name + "**...").set_reference(event.msg.id), [&bot, event, attachment, filter](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            dpp::message status_msg = std::get<dpp::message>(cb.value);

            ::std::thread([&bot, event, attachment, filter, status_msg]() {
                process_media_edit(bot, attachment, filter, [&bot, &status_msg](const dpp::message& m) {
                    dpp::message reply = m;
                    reply.id = status_msg.id;
                    reply.set_channel_id(status_msg.channel_id);
                    bot.message_edit(reply);
                });
            }).detach();
        });
    };

    if (!msg.attachments.empty()) {
        process_msg(msg);
        return;
    }

    if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id, [process_msg, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Failed to fetch replied message.").set_reference(event.msg.id));
                return;
            }
            process_msg(::std::get<dpp::message>(cb.value));
        });
        return;
    }

    bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please attach an image/video or reply to one.").set_reference(event.msg.id));
}

inline Command* get_speed_command() {
    static Command speed_cmd("speed", "Change the speed of a GIF or Video (e.g. 2x, 0.5x, 30fps)", "Media", {"velocity", "fps"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_speed_text(bot, event, args);
        });
    return &speed_cmd;
}

inline std::vector<Command*> get_media_edit_commands() {
    static std::vector<Command*> cmds;
    if (!cmds.empty()) return cmds;

    // Individual commands
    for (auto const& [key, effect] : EFFECT_REGISTRY) {
        auto* cmd = new Command(key, effect.description, "Media", {}, false,
            [key](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
                handle_media_edit_text(bot, event, args, key);
            });
        cmds.push_back(cmd);
    }

    // Speed command
    cmds.push_back(get_speed_command());

    // Random command
    static Command random_cmd("random", "Apply 3 random effects to media", "Media", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_media_edit_text(bot, event, args, "random");
        });
    cmds.push_back(&random_cmd);

    return cmds;
}

} // namespace utility
} // namespace commands
