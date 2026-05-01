#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../media_manager.h"
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <random>
#include <dpp/dpp.h>
#include <curl/curl.h>

namespace commands {
namespace utility {

// --- HTTP Utilities ---

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string content_type;
};

static size_t _http_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto *buf = static_cast<std::string*>(userp);
    buf->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static size_t _http_header_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto *res = static_cast<HttpResponse*>(userp);
    std::string header(static_cast<char*>(contents), size * nmemb);
    if (header.find("Content-Type:") == 0 || header.find("content-type:") == 0) {
        size_t colon = header.find(':');
        res->content_type = header.substr(colon + 1);
        res->content_type.erase(0, res->content_type.find_first_not_of(" \t\r\n"));
        res->content_type.erase(res->content_type.find_last_not_of(" \t\r\n") + 1);
    }
    return size * nmemb;
}

inline HttpResponse http_get_sync(const std::string& url) {
    HttpResponse res;
    CURL* curl = curl_easy_init();
    if (!curl) return res;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, _http_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BronxBot/1.0");

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        res.status = static_cast<int>(http_code);
    }
    curl_easy_cleanup(curl);
    return res;
}

// --- Media Descriptor ---

struct MediaSource {
    std::string url;
    std::string filename;
    std::string content_type;
    size_t size = 0;
    bool want_gif = false;

    bool empty() const { return url.empty(); }
    bool is_animated() const {
        return content_type.find("video/") != std::string::npos || content_type == "image/gif";
    }
    bool is_video() const {
        return content_type.find("video/") != std::string::npos;
    }
};

inline MediaSource resolve_media_source(const dpp::message& msg) {
    MediaSource src;
    if (!msg.attachments.empty()) {
        const auto& a = msg.attachments[0];
        src.url = a.url;
        src.filename = a.filename;
        src.content_type = a.content_type;
        src.size = a.size;
        return src;
    }
    if (!msg.embeds.empty()) {
        const auto& e = msg.embeds[0];
        if (e.video.has_value() && !e.video->url.empty()) {
            src.url = e.video->url;
            src.filename = "video.mp4";
            src.content_type = "video/mp4";
            src.want_gif = true;
            return src;
        }
        if (e.image.has_value() && !e.image->url.empty()) {
            src.url = e.image->url;
            src.filename = "image.png";
            src.content_type = "image/png";
            return src;
        }
        if (e.thumbnail.has_value() && !e.thumbnail->url.empty()) {
            src.url = e.thumbnail->url;
            src.filename = "thumb.png";
            src.content_type = "image/png";
            return src;
        }
    }
    return src;
}

// --- Video Processing Types ---

namespace media {
    struct VideoInfo {
        int width = 0;
        int height = 0;
        double duration = 0.0;
        long long bitrate = 0;
        bool valid = false;
    };
    
    // Low-level processing (implemented in media.cpp)
    bool download_file(const std::string& url, const std::string& output_path);
    VideoInfo get_video_info(const std::string& file_path);
    bool compress_video(const std::string& input, const std::string& output, int target_mb = 25);
    bool extract_audio(const std::string& input, const std::string& output);
    bool convert_to_gif(const std::string& input, const std::string& output, int fps = 15, int width = 480);
    std::string transcribe_audio(const std::string& file_path);
    std::string perform_ocr(const std::string& image_path);
}

// --- Command Processors & Handlers ---

/**
 * @brief Search for media on a platform and return the result.
 */
void process_search_request(dpp::cluster& bot, const std::string& query, const std::string& platform, 
    bool randomize, std::function<void(const dpp::message&)> responder,
    std::function<void(const std::string&)> log_callback = nullptr);

/**
 * @brief Download media from a URL and return it as an attachment or link.
 */
void process_download_request(dpp::cluster& bot, const std::string& url, std::function<void(const dpp::message&)> responder);

// Command getters for utility.h
Command* get_ocr_command();
Command* get_transcribe_command();
Command* get_gif_command();
Command* get_download_command();

} // namespace utility
} // namespace commands
