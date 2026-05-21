#include "media.h"
#include "media_edit.h"
#include "../../embed_style.h"
#include "../../media_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <future>
#include <cstdio>
#include <random>
#include <filesystem>
#include <curl/curl.h>

namespace commands {
namespace utility {
namespace media {

// Implementation of download_file
bool download_file(const std::string& url, const std::string& output_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(output_path.c_str(), "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BronxBot/1.0 (MediaDownloader)");

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

// Implementation of get_video_info
VideoInfo get_video_info(const std::string& file_path) {
    VideoInfo info;
    std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,duration,bit_rate -of csv=p=0 " + file_path;
    
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return info;
    
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);

    std::stringstream ss(result);
    std::string item;
    std::vector<std::string> parts;
    while (std::getline(ss, item, ',')) {
        parts.push_back(item);
    }

    if (parts.size() >= 4) {
        try {
            info.width = std::stoi(parts[0]);
            info.height = std::stoi(parts[1]);
            info.duration = std::stod(parts[2]);
            info.bitrate = std::stoll(parts[3]);
            info.valid = true;
        } catch (...) {}
    }
    return info;
}

// Implementation of compress_video
bool compress_video(const std::string& input, const std::string& output, int target_mb) {
    VideoInfo info = get_video_info(input);
    if (!info.valid || info.duration <= 0) return false;

    long long target_bitrate = static_cast<long long>((target_mb * 8.0 * 1024 * 1024) / info.duration);
    target_bitrate = static_cast<long long>(target_bitrate * 0.85); 
    if (target_bitrate < 100000) target_bitrate = 100000;

    std::stringstream cmd;
    cmd << "ffmpeg -y -i " << input 
        << " -c:v libx264 -b:v " << target_bitrate 
        << " -pass 1 -an -f null /dev/null && "
        << "ffmpeg -y -i " << input 
        << " -c:v libx264 -b:v " << target_bitrate 
        << " -pass 2 -c:a aac -b:a 128k " << output;

    int rc = system(cmd.str().c_str());
    return rc == 0;
}

// Implementation of extract_audio
bool extract_audio(const std::string& input, const std::string& output) {
    std::string cmd = "ffmpeg -y -i " + input + " -vn -acodec libmp3lame -q:a 2 " + output;
    int rc = system(cmd.c_str());
    return rc == 0;
}

// Implementation of convert_to_gif
bool convert_to_gif(const std::string& input, const std::string& output, int fps, int width) {
    std::stringstream cmd;
    cmd << "ffmpeg -y -i " << input 
        << " -vf \"fps=" << fps << ",scale=" << width << ":-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" "
        << output;
    
    int rc = system(cmd.str().c_str());
    return rc == 0;
}

// Implementation of transcribe_audio
std::string transcribe_audio(const std::string& file_path) {
    std::string whisper_cmd = "./whisper -m models/ggml-base.en.bin -f " + file_path + " -nt";
    char buffer[256];
    std::string result = "";
    FILE* pipe = popen(whisper_cmd.c_str(), "r");
    if (!pipe) return "[Error: Transcription failed]";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) result += buffer;
    pclose(pipe);
    return result;
}

// Implementation of perform_ocr
std::string perform_ocr(const std::string& image_path) {
    std::string cmd = "tesseract " + image_path + " stdout -l eng";
    char buffer[256];
    std::string result = "";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "[Error: OCR failed]";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) result += buffer;
    pclose(pipe);
    return result;
}

} // namespace media

void process_search_request(dpp::cluster& bot, const std::string& query, const std::string& platform, 
    bool randomize, std::function<void(const dpp::message&)> responder,
    std::function<void(const std::string&)> log_callback) {
    
    if (log_callback) log_callback("Searching " + platform + " for: " + query);
    
    std::thread([&bot, query, platform, randomize, responder]() {
        responder(dpp::message("🔎 Search for " + platform + " (" + query + ") is being restored..."));
    }).detach();
}

// Try cobalt API instances in order, return the direct download URL on success.
// Returns empty string if all instances fail or the URL isn't supported.
static std::string try_cobalt(const std::string& url) {
    static const std::vector<std::string> COBALT_INSTANCES = {
        "https://cobaltapi.squair.xyz",
        "https://cobaltapi.kittycat.boo",
        "https://fox.kittycat.boo",
        "https://api.dl.woof.monster",
        "https://api.cobalt.liubquanti.click",
    };

    const std::string body = "{\"url\":\"" + url + "\",\"videoQuality\":\"1080\",\"downloadMode\":\"auto\"}";

    for (const auto& instance : COBALT_INSTANCES) {
        CURL* curl = curl_easy_init();
        if (!curl) continue;

        std::string response;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, (instance + "/").c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _http_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK || response.empty()) continue;

        // Parse status and url from JSON (lightweight, no dependency)
        auto extract = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = response.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = response.find('"', pos);
            if (end == std::string::npos) return "";
            return response.substr(pos, end - pos);
        };

        std::string status = extract("status");
        if (status == "redirect" || status == "tunnel") {
            std::string dl_url = extract("url");
            if (!dl_url.empty()) return dl_url;
        }
        // error or unsupported — try next instance
    }
    return "";
}

void process_download_request(dpp::cluster& bot, const std::string& url, std::function<void(const dpp::message&)> responder) {
    std::thread([&bot, url, responder]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100000, 999999);
        std::string id = std::to_string(dis(gen));
        std::string temp_dir = "/tmp/bronx_dl_" + id;
        std::filesystem::create_directories(temp_dir);

        std::string downloaded_file = "";

        // --- Step 1: Try cobalt API (handles Instagram, TikTok, Twitter, etc.) ---
        std::string cobalt_url = try_cobalt(url);
        if (!cobalt_url.empty()) {
            // Cobalt gave us a direct CDN URL — download it with curl
            std::string out_path = temp_dir + "/video.mp4";
            CURL* curl = curl_easy_init();
            if (curl) {
                FILE* fp = fopen(out_path.c_str(), "wb");
                if (fp) {
                    curl_easy_setopt(curl, CURLOPT_URL, cobalt_url.c_str());
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
                    CURLcode res = curl_easy_perform(curl);
                    fclose(fp);

                    // Verify size <= 25MB
                    auto fsize = std::filesystem::file_size(out_path);
                    if (res == CURLE_OK && fsize > 0 && fsize <= 26214400ULL) {
                        downloaded_file = out_path;
                    } else if (fsize > 26214400ULL) {
                        std::filesystem::remove(out_path);
                        curl_easy_cleanup(curl);
                        responder(dpp::message(bronx::EMOJI_DENY + " file is too large to send on Discord (>25MB)."));
                        std::filesystem::remove_all(temp_dir);
                        return;
                    }
                }
                curl_easy_cleanup(curl);
            }
        }

        // --- Step 2: Fall back to yt-dlp if cobalt didn't work ---
        if (downloaded_file.empty()) {
            std::stringstream cmd;
            cmd << "yt-dlp -o \"" << temp_dir << "/%(title).100s.%(ext)s\" "
                << "--max-filesize 25M --no-playlist "
                << "--merge-output-format mp4 "
                << "--ppa \"Merger+ffmpeg_o:-movflags +faststart\" "
                << "-f \"best[ext=mp4][filesize<25M]/best[filesize<25M]/bestvideo[ext=mp4]+bestaudio[ext=m4a]/best\" "
                << "\"" << url << "\" 2>&1";

            int rc = system(cmd.str().c_str());
            if (rc != 0) {
                responder(dpp::message(bronx::EMOJI_DENY + " download failed. the file might be too large (>25MB) or the URL is invalid."));
                std::filesystem::remove_all(temp_dir);
                return;
            }

            for (const auto& entry : std::filesystem::directory_iterator(temp_dir)) {
                downloaded_file = entry.path().string();
                break;
            }
        }


        if (downloaded_file.empty()) {
            responder(dpp::message(bronx::EMOJI_DENY + " could not find downloaded file."));
            std::filesystem::remove_all(temp_dir);
            return;
        }

        std::ifstream file(downloaded_file, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            responder(dpp::message(bronx::EMOJI_DENY + " failed to read downloaded file."));
            std::filesystem::remove_all(temp_dir);
            return;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string data(size, '\0');
        file.read(&data[0], size);
        file.close();

        dpp::message msg;
        msg.add_file(std::filesystem::path(downloaded_file).filename().string(), data);
        responder(msg);

        std::filesystem::remove_all(temp_dir);
    }).detach();
}

template<typename F>
void handle_media_command_text(dpp::cluster& bot, const dpp::message_create_t& event, const std::string& loading_text, F processor) {
    dpp::message msg = event.msg;
    auto do_process = [&bot, event, loading_text, processor](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " no media found.").set_reference(event.msg.id));
            return;
        }
        
        bot.message_create(dpp::message(event.msg.channel_id, "⏳ " + loading_text + "...").set_reference(event.msg.id),
            [&bot, src, processor](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, status_msg, processor]() {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::string id = std::to_string(gen());
                    std::string temp_path = "/tmp/bronx_media_" + id + "_" + src.filename;
                    
                    if (!media::download_file(src.url, temp_path)) {
                        dpp::message error_msg(status_msg.channel_id, bronx::EMOJI_DENY + " failed to download file.");
                        error_msg.id = status_msg.id;
                        bot.message_edit(error_msg);
                        return;
                    }

                    std::string result = processor(temp_path);
                    std::filesystem::remove(temp_path);

                    if (result.empty()) result = "[No text found]";
                    if (result.length() > 1900) result = result.substr(0, 1900) + "...";

                    dpp::message reply;
                    reply.id = status_msg.id;
                    reply.channel_id = status_msg.channel_id;
                    reply.set_content("✅ **Result:**\n```\n" + result + "\n```");
                    bot.message_edit(reply);
                }).detach();
            });
    };

    if (!msg.attachments.empty() || !msg.embeds.empty()) {
        do_process(msg);
    } else if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id, [do_process](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) return;
            do_process(std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " please attach media or reply to a message with media.").set_reference(event.msg.id));
    }
}

Command* get_ocr_command() {
    static Command ocr("ocr", "Extract text from an image", "Utility", {}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_media_command_text(bot, event, "performing OCR", [](const std::string& path) {
                return media::perform_ocr(path);
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto attachment_id = event.get_parameter("attachment");
            dpp::attachment a = event.command.get_resolved_attachment(std::get<dpp::snowflake>(attachment_id));
            
            event.reply("⏳ performing OCR...");
            
            ::std::thread([&bot, event, a]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::string id = std::to_string(gen());
                std::string temp_path = "/tmp/bronx_ocr_" + id + "_" + a.filename;
                
                if (!media::download_file(a.url, temp_path)) {
                    event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " failed to download image."));
                    return;
                }

                std::string result = media::perform_ocr(temp_path);
                std::filesystem::remove(temp_path);

                if (result.empty()) result = "[No text found]";
                if (result.length() > 1900) result = result.substr(0, 1900) + "...";

                event.edit_original_response(dpp::message("✅ **Result:**\n```\n" + result + "\n```"));
            }).detach();
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The image to OCR", true) }
    );
    return &ocr;
}

void handle_gif_text(dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
    dpp::message msg = event.msg;
    auto do_process = [&bot, event](const dpp::message& target_msg) {
        MediaSource src = resolve_media_source(target_msg);
        if (src.empty()) {
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " no media found. reply to an uploaded image/video or a tenor/giphy embed.").set_reference(event.msg.id));
            return;
        }
        
        src.want_gif = true; // Force GIF output

        bot.message_create(dpp::message(event.msg.channel_id, "⏳ converting to GIF...").set_reference(event.msg.id),
            [&bot, src](const dpp::confirmation_callback_t& cb) {
                if (cb.is_error()) return;
                dpp::message status_msg = std::get<dpp::message>(cb.value);
                ::std::thread([&bot, src, status_msg]() {
                    process_media_edit(bot, src, "scale=iw:-1", [&bot, status_msg](const dpp::message& m) {
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
    } else if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id, [do_process, &bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " failed to fetch replied message.").set_reference(event.msg.id));
                return;
            }
            do_process(std::get<dpp::message>(cb.value));
        });
    } else {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " please attach an image/video or reply to one.").set_reference(event.msg.id));
    }
}

Command* get_transcribe_command() {
    static Command transcribe("transcribe", "Transcribe audio to text", "Utility", {}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_media_command_text(bot, event, "transcribing audio", [](const std::string& path) {
                return media::transcribe_audio(path);
            });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto attachment_id = event.get_parameter("attachment");
            dpp::attachment a = event.command.get_resolved_attachment(std::get<dpp::snowflake>(attachment_id));
            
            event.reply("⏳ transcribing audio...");
            
            ::std::thread([&bot, event, a]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::string id = std::to_string(gen());
                std::string temp_path = "/tmp/bronx_transcribe_" + id + "_" + a.filename;
                
                if (!media::download_file(a.url, temp_path)) {
                    event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " failed to download audio."));
                    return;
                }

                std::string result = media::transcribe_audio(temp_path);
                std::filesystem::remove(temp_path);

                if (result.empty()) result = "[No transcription found]";
                if (result.length() > 1900) result = result.substr(0, 1900) + "...";

                event.edit_original_response(dpp::message("✅ **Result:**\n```\n" + result + "\n```"));
            }).detach();
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The audio to transcribe", true) }
    );
    return &transcribe;
}

Command* get_gif_command() {
    static Command gif("gif", "Convert image/video to GIF", "Utility", {}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            handle_gif_text(bot, event, args);
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            auto attachment_id = event.get_parameter("attachment");
            dpp::attachment a = event.command.get_resolved_attachment(std::get<dpp::snowflake>(attachment_id));
            
            MediaSource src;
            src.url = a.url;
            src.filename = a.filename;
            src.content_type = a.content_type;
            src.size = a.size;
            src.want_gif = true;

            event.reply("⏳ converting to GIF...");
            
            ::std::thread([&bot, event, src]() {
                process_media_edit(bot, src, "scale=iw:-1", [&bot, event](const dpp::message& m) {
                    event.edit_original_response(m);
                });
            }).detach();
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The image/video to convert", true) }
    );
    return &gif;
}

Command* get_download_command() {
    static Command download("download", "Download media from URL", "Utility", {"dl"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            if (args.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " please provide a URL.").set_reference(event.msg.id));
                return;
            }
            std::string url = args[0];
            bot.message_create(dpp::message(event.msg.channel_id, "⏳ downloading media...").set_reference(event.msg.id),
                [&bot, url](const dpp::confirmation_callback_t& cb) {
                    if (cb.is_error()) return;
                    dpp::message status_msg = std::get<dpp::message>(cb.value);
                    process_download_request(bot, url, [&bot, status_msg](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.id = status_msg.id;
                        reply.channel_id = status_msg.channel_id;
                        bot.message_edit(reply);
                    });
                });
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            std::string url = std::get<std::string>(event.get_parameter("url"));
            event.reply("⏳ downloading media...");
            process_download_request(bot, url, [&bot, event](const dpp::message& m) {
                event.edit_original_response(m);
            });
        },
        { dpp::command_option(dpp::co_string, "url", "The URL to download", true) }
    );
    return &download;
}

} // namespace utility
} // namespace commands
