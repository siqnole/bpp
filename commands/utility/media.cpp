#include "media.h"
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

void process_download_request(dpp::cluster& bot, const std::string& url, std::function<void(const dpp::message&)> responder) {
    std::thread([&bot, url, responder]() {
        responder(dpp::message("📥 Download for " + url + " is being restored..."));
    }).detach();
}

Command* get_ocr_command() {
    static Command ocr("ocr", "Extract text from an image", "Utility", {}, true,
        nullptr,
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("OCR command is currently being restored.");
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The image to OCR", true) }
    );
    return &ocr;
}

Command* get_transcribe_command() {
    static Command transcribe("transcribe", "Transcribe audio to text", "Utility", {}, true,
        nullptr,
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("Transcription command is currently being restored.");
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The audio to transcribe", true) }
    );
    return &transcribe;
}

Command* get_gif_command() {
    static Command gif("gif", "Convert video to GIF", "Utility", {}, true,
        nullptr,
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("GIF command is currently being restored.");
        },
        { dpp::command_option(dpp::co_attachment, "attachment", "The video to convert", true) }
    );
    return &gif;
}

Command* get_download_command() {
    static Command download("download", "Download media from URL", "Utility", {"dl"}, true,
        nullptr,
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            event.reply("Download command is currently being restored.");
        },
        { dpp::command_option(dpp::co_string, "url", "The URL to download", true) }
    );
    return &download;
}

} // namespace utility
} // namespace commands
