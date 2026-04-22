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
    bool complex = false;       // Uses complex filtergraph (semicolons/stream labels) — can't be chained
};

static const std::map<std::string, MediaEffect> EFFECT_REGISTRY = {
    {"blur", {"Blur", "Applies a box blur", "boxblur=5:1"}},
    {"hue", {"Hue", "Rotates the color hue", "hue=h=90"}},
    {"invert", {"Invert", "Inverts image colors", "negate"}},
    {"edge", {"Edge", "Canny edge detection", "edgedetect=mode=colormix:high=0.1:low=0.1"}},
    {"pixelate", {"Pixelate", "Mosaic/Pixelation effect", "scale=iw/10:-1,scale=iw*10:-1:flags=neighbor"}},
    {"sepia", {"Sepia", "Vintage brown tone", "colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131"}},
    {"mirror", {"Mirror", "Horizontal mirror", "split[l][r];[r]hflip[f];[l][f]hstack", false, /*complex=*/true}},
    {"vmirror", {"Vertical Mirror", "Vertical mirror", "split[t][b];[b]vflip[f];[t][f]vstack", false, /*complex=*/true}},
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
    {"kaleidoscope", {"Kaleidoscope", "4-way mirrored symmetry", "split=4[v1][v2][v3][v4];[v2]hflip[v2h];[v3]vflip[v3v];[v4]hflip,vflip[v4hv];[v1][v2h]hstack[top];[v3v][v4hv]hstack[bottom];[top][bottom]vstack,scale=iw/2:ih/2", false, /*complex=*/true}},
    {"bulge", {"Bulge", "Fish-eye lens distortion", "vignette,lenscorrection=k1=0.2:k2=0.2"}},
    {"wave", {"Wave", "Aquatic ripples", "format=rgb24,geq=r='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':g='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':b='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))'", true, /*complex=*/true}},
    {"thermal", {"Thermal", "Heatmap simulation", "format=gray,colorchannelmixer=1:0:0:0:0:1:0:0:0:0:1:0,curves=all='0/0 0.5/1 1/0',hue=h=240:s=2"}},
    {"night", {"Night Vision", "Amplified green grain", "hue=s=0,eq=brightness=0.1:contrast=1.5,colorchannelmixer=0:1:0:0:0:1:0:0:0:1:0:0,noise=alls=20:allf=t+u"}},
    {"xray", {"X-Ray", "Inverted skeletal look", "negate,format=gray,curves=all='0/0 0.5/1 1/0'"}},
    {"crt", {"CRT", "Retro TV scanlines", "format=rgb24,noise=alls=8:allf=t,chromashift=cbh=1:crh=-1,vignette"}},
    {"pixelsort", {"Pixelsort", "Digital smear effect", "transpose,boxblur=20:1,transpose"}},
    {"hyperpixel", {"Hyperpixel", "Extreme low-res scaling", "scale=iw/20:-1,scale=iw*20:-1:flags=neighbor"}},
    {"datamosh", {"Datamosh", "Pixel bleed glitch", "mpdecimate,lagfun=decay=0.98:range=50,scale=iw/2:-1,scale=iw*2:-1:flags=neighbor", true, /*complex=*/true}},
    {"melt", {"Melt", "Liquification effect", "minterpolate=fps=20:scd=none:me_mode=bidir:mi_mode=mci", true, /*complex=*/true}},
    {"smear", {"Smear", "Ghostly motion trails", "lagfun=decay=0.95:range=24", true}},
    {"fisheye", {"Fisheye", "Fisheye lens distortion", "lenscorrection=k1=0.2:k2=0.2"}},
    {"swirl", {"Swirl", "Twisted swirl effect", "swirl=1.5", false, true}},
    {"wave", {"Wave", "Aquatic ripples", "format=rgb24,geq=r='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':g='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))':b='p(X+10*sin(6.28*(Y/100+T)),Y+10*cos(6.28*(X/100+T)))'", true, true}},
    {"paint", {"Paint", "Oil paint simulation", "oilify=10"}},
    {"neon", {"Neon", "Neon glow effect", "edgedetect=mode=colormix:high=0.1:low=0.1,curves=all='0/0 0.5/0.8 1/1',hue=h=280:s=2"}},
    {"grayscale", {"Grayscale", "Black and white", "hue=s=0"}},
    {"rainbow", {"Rainbow", "Continuous hue cycling", "hue=h=t*360", true}}
};

// ---------------------------------------------------------------------------
// Lightweight source descriptor — avoids dpp::attachment's non-default ctor
// ---------------------------------------------------------------------------
// Moved to media.h

// ---------------------------------------------------------------------------
// Core processing — now uses MediaSource instead of dpp::attachment
// ---------------------------------------------------------------------------
inline void process_media_edit(
    dpp::cluster& bot,
    const MediaSource& src,
    const std::string& filter_chain,
    std::function<void(const dpp::message&)> responder,
    double target_fps = 12.0,
    int    target_duration = 10)
{
    if (src.size > 25 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " File is too large (max 25MB)."));
        return;
    }

    auto response = http_get_sync(src.url);
    if (response.status != 200) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to download attachment."));
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string id = std::to_string(dis(gen));

    std::string temp_dir = "/tmp/bronx_edit_" + id;
    std::filesystem::create_directories(temp_dir);

    std::string in_ext = src.filename.substr(src.filename.find_last_of("."));
    if (in_ext.find("/") != std::string::npos) in_ext = ".bin";

    std::string in_path = temp_dir + "/input" + in_ext;

    bool is_video  = (src.content_type.find("video/") != std::string::npos);
    bool is_gif    = (src.content_type == "image/gif");
    bool animated  = is_video || is_gif;

    // If the user sees this as a GIF (direct upload or embed), output GIF.
    // This ensures Tenor embeds (delivered as MP4) still come back as GIFs.
    bool output_gif = is_gif || src.want_gif;

    std::string out_ext;
    if (output_gif) {
        out_ext = ".gif";
    } else if (is_video) {
        out_ext = ".mp4";
    } else {
        out_ext = animated ? ".gif" : in_ext;
    }

    std::string out_path = temp_dir + "/output" + out_ext;

    std::ofstream out_file(in_path, std::ios::binary);
    out_file.write(response.body.data(), response.body.size());
    out_file.close();

    std::string fps_str = std::to_string(target_fps);
    std::string dur_str = std::to_string(target_duration);
    std::string cmd;

    if (animated && out_ext == ".gif") {
        // GIF output: use palette-based encoding for quality
        // Prepend format=rgba to handle pal8 GIF input that most filters can't process
        // When source is a video (Tenor MP4), omit audio flags; ffmpeg handles it.
        cmd = "ffmpeg -y -i " + in_path + " -t " + dur_str +
              " -vf \"format=rgba," + filter_chain + ",fps=" + fps_str +
              ",scale=320:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse=dither=bayer:bayer_scale=1\""
              " " + out_path + " > /dev/null 2>&1";
    } else if (is_video) {
        cmd = "ffmpeg -y -i " + in_path + " -t " + dur_str +
              " -vf \"" + filter_chain + "\""
              " -c:v libx264 -pix_fmt yuv420p -profile:v baseline -level 3.0 -crf 28 -preset faster -c:a copy"
              " " + out_path + " > /dev/null 2>&1";
    } else {
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

/**
 * @brief Processes media using ImageMagick (convert).
 * Best for complex layouts, text rendering, and templates.
 */
inline void process_image_magick(
    dpp::cluster& bot,
    const MediaSource& src,
    const std::string& magick_args,
    std::function<void(const dpp::message&)> responder)
{
    if (src.size > 25 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " File is too large (max 25MB)."));
        return;
    }

    auto response = http_get_sync(src.url);
    if (response.status != 200) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to download attachment."));
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string id = std::to_string(dis(gen));

    std::string temp_dir = "/tmp/bronx_magick_" + id;
    std::filesystem::create_directories(temp_dir);

    size_t dot = src.filename.find_last_of(".");
    std::string in_ext = (dot == std::string::npos) ? ".bin" : src.filename.substr(dot);
    if (in_ext.find("/") != std::string::npos) in_ext = ".bin";
    
    std::string in_path = temp_dir + "/input" + in_ext;
    std::string out_path = temp_dir + "/output" + (src.is_animated() ? ".gif" : in_ext);

    std::ofstream out_file(in_path, std::ios::binary);
    out_file.write(response.body.data(), response.body.size());
    out_file.close();

    // ImageMagick on some systems lacks delegates for MP4/GIF containers or struggles with them.
    // We normalize all animated media by extracting the first frame with FFmpeg to a static PNG.
    if (src.is_animated()) {
        std::string frame_path = temp_dir + "/frame.png";
        std::string extract_cmd = "ffmpeg -y -i " + in_path + " -frames:v 1 " + frame_path + " > /dev/null 2>&1";
        if (std::system(extract_cmd.c_str()) == 0) {
            in_path = frame_path;
            // From now on, ImageMagick sees a static image.
        }
    }

    // ImageMagick 'convert' command. 
    // If it was animated, in_path now points to a static frame.
    std::string cmd = "convert " + in_path + " " + magick_args + " " + out_path + " > /dev/null 2>&1";
    
    int result = std::system(cmd.c_str());
    if (result != 0) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " ImageMagick was unable to process this image."));
        return;
    }

    std::ifstream res_file(out_path, std::ios::binary | std::ios::ate);
    if (!res_file.is_open()) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to read processed file."));
        return;
    }

    std::streamsize res_size = res_file.tellg();
    res_file.seekg(0, std::ios::beg);
    std::string res_data(res_size, '\0');
    res_file.read(&res_data[0], res_size);
    res_file.close();

    std::filesystem::remove_all(temp_dir);

    dpp::message msg;
    msg.add_file("magick_output" + (src.is_animated() ? ".gif" : in_ext), res_data);
    responder(msg);
}

// Moved to media.h

// ---------------------------------------------------------------------------
// .speed command
// ---------------------------------------------------------------------------
inline void handle_speed_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id,
            bronx::EMOJI_DENY + " Please provide a speed (e.g., `2x`, `0.5x`, `15fps`, `1fps`).").set_reference(event.msg.id));
        return;
    }

    std::string speed_arg = args[0];
    double target_fps = 12.0;
    std::string filter = "setpts=PTS";
    std::string label  = "Speed";

    try {
        if (speed_arg.find("fps") != std::string::npos) {
            double fps = std::stod(speed_arg.substr(0, speed_arg.find("fps")));
            if (fps <= 0 || fps > 120) throw std::invalid_argument("FPS out of range (0.01-120)");
            target_fps = fps;
            // Scale timestamps relative to the 12fps baseline so the fps filter has
            // enough frames to sample from.  This is equivalent to the multiplier
            // mode but expressed as an absolute rate:
            //   0.3fps → 0.3/12 = 0.025x speed  →  setpts = PTS * (12/0.3) = PTS*40
            //   100fps → 100/12 ≈ 8.33x speed   →  setpts = PTS / (100/12) = PTS/8.33
            double ratio = fps / 12.0;   // > 1 = faster, < 1 = slower
            if (ratio >= 1.0) {
                filter = "setpts=PTS/" + std::to_string(ratio);
            } else {
                filter = "setpts=PTS*" + std::to_string(1.0 / ratio);
            }
            // Clean label: strip trailing zeros
            std::string fps_label = std::to_string(fps);
            fps_label.erase(fps_label.find_last_not_of('0') + 1);
            if (fps_label.back() == '.') fps_label.pop_back();
            label = fps_label + " FPS";
        } else if (speed_arg.back() == 'x') {
            double mul = std::stod(speed_arg.substr(0, speed_arg.size() - 1));
            if (mul <= 0 || mul > 50) throw std::invalid_argument("Multiplier out of range");
            target_fps = 12.0 * mul;
            filter     = "setpts=PTS/" + std::to_string(mul);
            label      = speed_arg;
        } else {
            double mul = std::stod(speed_arg);
            if (mul <= 0 || mul > 50) throw std::invalid_argument("Invalid speed");
            target_fps = 12.0 * mul;
            filter     = "setpts=PTS/" + std::to_string(mul);
            label      = speed_arg + "x";
        }
    } catch (...) {
        bot.message_create(dpp::message(event.msg.channel_id,
            bronx::EMOJI_DENY + " Invalid speed. Examples: `2x`, `0.5x`, `30fps`, `0.5fps`.").set_reference(event.msg.id));
        return;
    }

    auto do_process = [&bot, event, filter, target_fps, label](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id,
                bronx::EMOJI_DENY + " No media found. Reply to an uploaded GIF/Video or a Tenor/Giphy embed.").set_reference(event.msg.id));
            return;
        }

        std::string status_text = src.content_type.find("video/") != std::string::npos
            ? "⏳ Adjusting speed to **" + label + "** (embed)..."
            : "⏳ Adjusting speed to **" + label + "**...";

        bot.message_create(dpp::message(event.msg.channel_id, status_text).set_reference(event.msg.id),
            [&bot, src, filter, target_fps](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, filter, target_fps, status_msg]() {
                    process_media_edit(bot, src, filter, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.set_channel_id(status_msg.channel_id);
                        bot.message_edit(reply);
                    }, target_fps);
                }).detach();
            });
    };

    dpp::message msg = event.msg;
    if (!msg.attachments.empty() || !msg.embeds.empty()) {
        do_process(msg);
    } else if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id,
            [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::EMOJI_DENY + " Failed to fetch the replied message.").set_reference(event.msg.id));
                    return;
                }
                do_process(::std::get<dpp::message>(cb.value));
            });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id,
            bronx::EMOJI_DENY + " Please attach a GIF/Video or reply to one.").set_reference(event.msg.id));
    }
}

// ---------------------------------------------------------------------------
// Effect commands (.blur, .hue, .random, etc.)
// ---------------------------------------------------------------------------
inline void handle_media_edit_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args, const std::string& effect_key) {
    auto it = EFFECT_REGISTRY.find(effect_key);
    if (it == EFFECT_REGISTRY.end() && effect_key != "random") return;

    dpp::message msg = event.msg;

    auto do_process = [&bot, event, it, effect_key](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id,
                bronx::EMOJI_DENY + " No media found. Reply to an uploaded GIF/Video or a Tenor/Giphy embed.").set_reference(event.msg.id));
            return;
        }

        bool is_animated = src.is_animated();
        std::string filter;
        std::string final_effect_name;

        if (effect_key == "random") {
            std::vector<std::string> keys;
            for (auto const& [key, val] : EFFECT_REGISTRY) {
                if (!is_animated && val.animated_only) continue;
                if (val.complex) continue; // Complex filtergraphs can't be comma-chained
                keys.push_back(key);
            }
            if (keys.size() < 3) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::EMOJI_DENY + " Not enough compatible effects found for this file type.").set_reference(event.msg.id));
                return;
            }
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(keys.begin(), keys.end(), g);
            // Normalize pixel format between effects to prevent format mismatches
            filter = EFFECT_REGISTRY.at(keys[0]).filter + ",format=rgba," +
                     EFFECT_REGISTRY.at(keys[1]).filter + ",format=rgba," +
                     EFFECT_REGISTRY.at(keys[2]).filter;
            final_effect_name = "Random (" + EFFECT_REGISTRY.at(keys[0]).name + " + " +
                                              EFFECT_REGISTRY.at(keys[1]).name + " + " +
                                              EFFECT_REGISTRY.at(keys[2]).name + ")";
        } else {
            if (!is_animated && it->second.animated_only) {
                bot.message_create(dpp::message(event.msg.channel_id,
                    bronx::EMOJI_DENY + " The **" + it->second.name + "** effect only works on animated GIFs or Videos.").set_reference(event.msg.id));
                return;
            }
            filter = it->second.filter;
            final_effect_name = it->second.name;
        }

        bot.message_create(dpp::message(event.msg.channel_id,
            "✨ Applying **" + final_effect_name + "**...").set_reference(event.msg.id),
            [&bot, src, filter](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, filter, status_msg]() {
                    process_media_edit(bot, src, filter, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.set_channel_id(status_msg.channel_id);
                        bot.message_edit(reply);
                    });
                }).detach();
            });
    };

    if (!msg.attachments.empty() || !msg.embeds.empty()) {
        do_process(msg);
        return;
    }

    if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id,
            [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) {
                    bot.message_create(dpp::message(event.msg.channel_id,
                        bronx::EMOJI_DENY + " Failed to fetch replied message.").set_reference(event.msg.id));
                    return;
                }
                do_process(::std::get<dpp::message>(cb.value));
            });
        return;
    }

    bot.message_create(dpp::message(event.msg.channel_id,
        bronx::EMOJI_DENY + " Please attach an image/video or reply to one.").set_reference(event.msg.id));
}

// ---------------------------------------------------------------------------
// .caption command
// ---------------------------------------------------------------------------
inline void handle_caption_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please provide text for the caption.").set_reference(event.msg.id));
        return;
    }

    std::string text = "";
    for (const auto& arg : args) text += arg + " ";
    if (!text.empty()) text.pop_back();

    // Sanitize for shell
    std::string sanitized_text = text;
    size_t pos = 0;
    while ((pos = sanitized_text.find("\"", pos)) != std::string::npos) {
        sanitized_text.replace(pos, 1, "\\\"");
        pos += 2;
    }

    auto do_process = [&bot, event, sanitized_text](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No image found.").set_reference(event.msg.id));
            return;
        }

        std::string font_path = "/home/siqnole/Documents/code/bpp/site/fonts/blinkmacsystemfont-bold.ttf";
        // ImageMagick caption logic:
        // 1. Create a white background with black text wrapped to the image width.
        // 2. Append it to the top of the image.
        std::string magick_args = "-colorspace sRGB ( -background white -fill black -font " + font_path + " -size %wx caption:\"" + sanitized_text + "\" ) +swap -append";

        bot.message_create(dpp::message(event.msg.channel_id, "⏳ Generating caption...").set_reference(event.msg.id),
            [&bot, src, magick_args](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, magick_args, status_msg]() {
                    process_image_magick(bot, src, magick_args, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.set_channel_id(status_msg.channel_id);
                        bot.message_edit(reply);
                    });
                }).detach();
            });
    };

    if (!event.msg.attachments.empty() || !event.msg.embeds.empty()) {
        do_process(event.msg);
    } else if (event.msg.message_reference.message_id != 0) {
        bot.message_get(event.msg.message_reference.message_id, event.msg.channel_id, [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            do_process(std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please attach an image or reply to one.").set_reference(event.msg.id));
    }
}

// ---------------------------------------------------------------------------
// .meme command
// ---------------------------------------------------------------------------
inline void handle_meme_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Use: `.meme \"top text\" \"bottom text\"`").set_reference(event.msg.id));
        return;
    }

    std::string top = args[0];
    std::string bottom = (args.size() > 1) ? args[1] : "";

    auto sanitize = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        size_t pos = 0;
        while ((pos = s.find("\"", pos)) != std::string::npos) {
            s.replace(pos, 1, "\\\"");
            pos += 2;
        }
        return s;
    };

    top = sanitize(top);
    bottom = sanitize(bottom);

    auto do_process = [&bot, event, top, bottom](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No image found.").set_reference(event.msg.id));
            return;
        }

        std::string font_path = "/home/siqnole/Documents/code/bpp/DPP/fonts/segoe-ui-black.ttf";
        // Impact meme logic:
        // 1. Draw top text with outline.
        // 2. Draw bottom text with outline.
        std::string draw_top = top.empty() ? "" : "-draw \"gravity north text 0,10 '" + top + "'\"";
        std::string draw_bottom = bottom.empty() ? "" : "-draw \"gravity south text 0,10 '" + bottom + "'\"";

        // We use -annotate for better control over wrapping or just raw -draw for simplicity as found in most bot implementations
        // Standard Impact meme: white text, black stroke
        std::string magick_args = "-colorspace sRGB -font " + font_path + " -fill white -stroke black -strokewidth 2 -pointsize 50 " + draw_top + " " + draw_bottom;

        bot.message_create(dpp::message(event.msg.channel_id, "⏳ Generating meme...").set_reference(event.msg.id),
            [&bot, src, magick_args](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, magick_args, status_msg]() {
                    process_image_magick(bot, src, magick_args, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.set_channel_id(status_msg.channel_id);
                        bot.message_edit(reply);
                    });
                }).detach();
            });
    };

    if (!event.msg.attachments.empty() || !event.msg.embeds.empty()) {
        do_process(event.msg);
    } else if (event.msg.message_reference.message_id != 0) {
        bot.message_get(event.msg.message_reference.message_id, event.msg.channel_id, [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            do_process(std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please attach an image or reply to one.").set_reference(event.msg.id));
    }
}

// ---------------------------------------------------------------------------
// .motivate command
// ---------------------------------------------------------------------------
inline void handle_motivate_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Use: `.motivate \"title\" \"description\"`").set_reference(event.msg.id));
        return;
    }

    std::string title = args[0];
    std::string desc = (args.size() > 1) ? args[1] : "";

    auto sanitize = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        size_t pos = 0;
        while ((pos = s.find("\"", pos)) != std::string::npos) {
            s.replace(pos, 1, "\\\"");
            pos += 2;
        }
        return s;
    };

    title = sanitize(title);
    desc = sanitize(desc);

    auto do_process = [&bot, event, title, desc](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No image found.").set_reference(event.msg.id));
            return;
        }

        std::string font_path = "/home/siqnole/Documents/code/bpp/DPP/fonts/segoe-ui-black.ttf";
        // Motivational poster logic:
        // 1. Add black border. 2. Add title. 3. Add description.
        std::string magick_args = "-colorspace sRGB -bordercolor black -border 10%x10% "
                                  "-font " + font_path + " -fill white "
                                  "-draw \"gravity south text 0,60 '" + title + "'\" ";
        
        if (!desc.empty()) {
            magick_args += "-pointsize 20 -draw \"gravity south text 0,30 '" + desc + "'\" ";
        }

        bot.message_create(dpp::message(event.msg.channel_id, "⏳ Generating motivational poster...").set_reference(event.msg.id),
            [&bot, src, magick_args](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, magick_args, status_msg]() {
                    process_image_magick(bot, src, magick_args, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.set_channel_id(status_msg.channel_id);
                        bot.message_edit(reply);
                    });
                }).detach();
            });
    };

    if (!event.msg.attachments.empty() || !event.msg.embeds.empty()) {
        do_process(event.msg);
    } else if (event.msg.message_reference.message_id != 0) {
        bot.message_get(event.msg.message_reference.message_id, event.msg.channel_id, [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            do_process(std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please attach an image or reply to one.").set_reference(event.msg.id));
    }
}

// ---------------------------------------------------------------------------
// Command exports
// ---------------------------------------------------------------------------
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

    for (auto const& [key, effect] : EFFECT_REGISTRY) {
        auto* cmd = new Command(key, effect.description, "Media", {}, false,
            [key](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
                handle_media_edit_text(bot, event, args, key);
            });
        cmds.push_back(cmd);
    }

    cmds.push_back(get_speed_command());

    static Command caption_cmd("caption", "Add a top caption to an image", "Media", {"cap"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_caption_text(bot, event, args);
        });
    cmds.push_back(&caption_cmd);

    static Command meme_cmd("meme", "Create an Impact-style meme", "Media", {"impact"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_meme_text(bot, event, args);
        });
    cmds.push_back(&meme_cmd);

    static Command motivate_cmd("motivate", "Create a motivational poster", "Media", {"poster"}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_motivate_text(bot, event, args);
        });
    cmds.push_back(&motivate_cmd);

    static Command random_cmd("random", "Apply 3 random effects to media", "Media", {}, false,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_media_edit_text(bot, event, args, "random");
        });
    cmds.push_back(&random_cmd);

    return cmds;
}

} // namespace utility
} // namespace commands
