#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "../media_manager.h"
#include "utility_helpers.h"
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <random>
#include <dpp/dpp.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <curl/curl.h>
}

namespace commands {
namespace utility {

/**
 * libcurl helper for synchronous memory downloads.
 */
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;
    char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

struct HttpResponse {
    long status;
    ::std::string body;
};

inline HttpResponse http_get_sync(const ::std::string& url) {
    HttpResponse res = {0, ""};
    CURL *curl = curl_easy_init();
    if (!curl) return res;

    MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
        res.body.assign(chunk.memory, chunk.size);
    }

    free(chunk.memory);
    curl_easy_cleanup(curl);
    return res;
}

/**
 * Upload a file to Catbox.moe and return the result URL.
 * Uses curl via system call for simplicity with multipart/form-data.
 */
inline ::std::string upload_to_catbox(const ::std::string& file_path) {
    std::string temp_res = "/tmp/catbox_res_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";
    std::string cmd = "curl -s -F \"reqtype=fileupload\" -F \"fileToUpload=@" + file_path + "\" https://catbox.moe/user/api.php > " + temp_res + " 2>&1";
    
    int status = std::system(cmd.c_str());
    std::string url = "";
    
    if (status == 0 && std::filesystem::exists(temp_res)) {
        std::ifstream ifs(temp_res);
        std::getline(ifs, url);
        // Trim any whitespace/newlines
        url.erase(url.find_last_not_of(" \n\r\t") + 1);
    }
    
    if (std::filesystem::exists(temp_res)) std::filesystem::remove(temp_res);
    return url;
}

/**
 * FFmpeg custom AVIO context for reading from memory.
 */
struct BufferData {
    const uint8_t* ptr;
    size_t size;
};

static int read_packet(void* opaque, uint8_t* buf, int buf_size) {
    BufferData* bd = (BufferData*)opaque;
    buf_size = ::std::min((size_t)buf_size, bd->size);
    if (!buf_size) return AVERROR_EOF;
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr += buf_size;
    bd->size -= buf_size;
    return buf_size;
}

/**
 * @brief Decode audio bytes to 16kHz mono float PCM samples.
 */
inline ::std::vector<float> decode_audio(const ::std::string& bytes, double& duration) {
    ::std::vector<float> samples;
    duration = 0.0;

    AVFormatContext* fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) return samples;

    size_t avio_ctx_buffer_size = 4096;
    uint8_t* avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    BufferData bd = { (const uint8_t*)bytes.data(), bytes.size() };
    AVIOContext* avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size, 0, &bd, &read_packet, nullptr, nullptr);
    if (!avio_ctx) {
        avformat_free_context(fmt_ctx);
        return samples;
    }
    fmt_ctx->pb = avio_ctx;

    if (avformat_open_input(&fmt_ctx, nullptr, nullptr, nullptr) < 0) {
        av_free(avio_ctx->buffer);
        avio_context_free(&avio_ctx);
        avformat_free_context(fmt_ctx);
        return samples;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        avio_context_free(&avio_ctx);
        return samples;
    }

    int stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&fmt_ctx);
        avio_context_free(&avio_ctx);
        return samples;
    }

    AVStream* stream = fmt_ctx->streams[stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        avio_context_free(&avio_ctx);
        return samples;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        avio_context_free(&avio_ctx);
        return samples;
    }

    duration = (double)stream->duration * av_q2d(stream->time_base);
    if (duration <= 0 && fmt_ctx->duration > 0) {
        duration = (double)fmt_ctx->duration / AV_TIME_BASE;
    }

    SwrContext* swr = swr_alloc();
    av_opt_set_chlayout(swr, "in_chlayout", &codec_ctx->ch_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codec_ctx->sample_fmt, 0);
    
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, 1); // Mono
    av_opt_set_chlayout(swr, "out_chlayout", &out_ch_layout, 0);
    av_opt_set_int(swr, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    swr_init(swr);

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == stream_index) {
            if (avcodec_send_packet(codec_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    uint8_t* out_data[1] = { nullptr };
                    int out_samples = swr_get_out_samples(swr, frame->nb_samples);
                    float* pcm_buf = (float*)av_malloc(out_samples * sizeof(float));
                    out_data[0] = (uint8_t*)pcm_buf;
                    
                    int converted = swr_convert(swr, out_data, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    if (converted > 0) {
                        samples.insert(samples.end(), pcm_buf, pcm_buf + converted);
                    }
                    av_free(pcm_buf);
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    avio_context_free(&avio_ctx);

    return samples;
}

inline void process_ocr_request(dpp::cluster& bot, dpp::attachment attachment, std::function<void(const dpp::message&)> responder) {
    if (attachment.size > 8 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " Image is too large (max 8MB)."));
        return;
    }

    ::std::string mime = attachment.content_type;
    if (mime != "image/png" && mime != "image/jpeg" && mime != "image/webp") {
        responder(dpp::message(bronx::EMOJI_DENY + " Unsupported file type. Please use PNG, JPEG, or WebP."));
        return;
    }

    auto response = http_get_sync(attachment.url);
    if (response.status != 200) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to download image."));
        return;
    }

    auto& manager = bronx::get_media_manager();
    if (!manager.ocr_api) {
        responder(dpp::message(bronx::EMOJI_DENY + " OCR service is not initialized."));
        return;
    }

    Pix* pix = pixReadMem((const unsigned char*)response.body.data(), response.body.size());
    if (!pix) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to process image memory."));
        return;
    }

    manager.ocr_api->SetImage(pix);
    char* outText = manager.ocr_api->GetUTF8Text();
    ::std::string text(outText);
    delete[] outText;
    pixDestroy(&pix);

    text.erase(::std::remove(text.begin(), text.end(), '\0'), text.end());
    text.erase(0, text.find_first_not_of(" \n\r\t"));
    text.erase(text.find_last_not_of(" \n\r\t") + 1);

    if (text.empty()) {
        responder(dpp::message(bronx::EMOJI_DENY + " No text detected."));
        return;
    }

    ::std::vector<::std::string> pages;
    for (size_t i = 0; i < text.size(); i += 1900) { 
        pages.push_back(text.substr(i, 1900));
    }

    for (size_t i = 0; i < pages.size(); ++i) {
        auto embed = bronx::create_embed("```\n" + pages[i] + "\n```")
            .set_title("OCR Result")
            .set_footer(dpp::embed_footer().set_text("Page " + ::std::to_string(i + 1) + "/" + ::std::to_string(pages.size())));
        
        responder(dpp::message().add_embed(embed));
    }
}

inline void process_transcribe_request(dpp::cluster& bot, dpp::attachment attachment, ::std::string language, std::function<void(const dpp::message&)> responder) {
    if (attachment.size > 25 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " Audio file is too large (max 25MB)."));
        return;
    }

    ::std::string mime = attachment.content_type;
    if (mime != "audio/ogg" && mime != "audio/mpeg" && mime != "audio/wav" && mime != "audio/flac" && mime != "application/ogg") {
        if (attachment.filename.find(".ogg") == ::std::string::npos) {
            responder(dpp::message(bronx::EMOJI_DENY + " Unsupported file type. Please use OGG, MP3, WAV, or FLAC."));
            return;
        }
    }

    auto response = http_get_sync(attachment.url);
    if (response.status != 200) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to download audio."));
        return;
    }

    double duration = 0.0;
    ::std::vector<float> pcm = decode_audio(response.body, duration);

    if (pcm.empty()) {
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to decode audio or file is empty."));
        return;
    }

    auto& manager = bronx::get_media_manager();
    if (!manager.whisper_ctx) {
        responder(dpp::message(bronx::EMOJI_DENY + " Transcription service is not initialized."));
        return;
    }

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = language.c_str();
    params.translate = false;
    params.no_context = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;

    if (whisper_full(manager.whisper_ctx, params, pcm.data(), pcm.size()) != 0) {
        responder(dpp::message(bronx::EMOJI_DENY + " Transcription failed."));
        return;
    }

    ::std::string transcript;
    int n_segments = whisper_full_n_segments(manager.whisper_ctx);
    for (int i = 0; i < n_segments; ++i) {
        transcript += whisper_full_get_segment_text(manager.whisper_ctx, i);
    }

    if (transcript.empty()) {
        responder(dpp::message(bronx::EMOJI_DENY + " No speech detected in audio."));
        return;
    }

    const char* detected_lang = whisper_lang_str(whisper_full_lang_id(manager.whisper_ctx));
    auto embed = bronx::create_embed("```\n" + transcript + "\n```")
        .set_title("Audio Transcription")
        .add_field("Detected Language", detected_lang ? detected_lang : "Unknown", true)
        .add_field("Duration", ::std::to_string(duration) + "s", true);

    responder(dpp::message().add_embed(embed));
}

inline void process_gif_request(dpp::cluster& bot, dpp::attachment attachment, std::function<void(const dpp::message&)> responder) {
    if (attachment.size > 25 * 1024 * 1024) {
        responder(dpp::message(bronx::EMOJI_DENY + " File is too large (max 25MB for conversion)."));
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
    
    std::string temp_dir = "/tmp/bronx_gif_" + id;
    std::filesystem::create_directories(temp_dir);
    
    std::string in_ext = attachment.filename.substr(attachment.filename.find_last_of("."));
    std::string in_path = temp_dir + "/input" + in_ext;
    std::string out_path = temp_dir + "/output.gif";

    std::ofstream out_file(in_path, std::ios::binary);
    out_file.write(response.body.data(), response.body.size());
    out_file.close();

    // ffmpeg -i input -vf "fps=15,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" output.gif
    // This provides high-quality GIFs by generating a custom palette for the video.
    std::string cmd = "ffmpeg -y -i " + in_path + " -vf \"fps=15,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" " + out_path + " > /dev/null 2>&1";
    
    int result = std::system(cmd.c_str());
    if (result != 0) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to convert media to GIF. Check if the file is a valid video/image."));
        return;
    }

    std::ifstream gif_file(out_path, std::ios::binary | std::ios::ate);
    if (!gif_file.is_open()) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to read converted GIF."));
        return;
    }

    std::streamsize gif_size = gif_file.tellg();
    if (gif_size > 25 * 1024 * 1024) {
         std::filesystem::remove_all(temp_dir);
         responder(dpp::message(bronx::EMOJI_DENY + " Converted GIF is too large for Discord (max 25MB)."));
         return;
    }

    gif_file.seekg(0, std::ios::beg);
    std::string gif_data(gif_size, '\0');
    gif_file.read(&gif_data[0], gif_size);
    gif_file.close();

    std::filesystem::remove_all(temp_dir);

    dpp::message msg;
    msg.add_file("converted.gif", gif_data);
    responder(msg);
}

inline void handle_gif(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);
    ::std::thread([&bot, event]() {
        auto attachment_id_param = event.get_parameter("attachment");
        if (!::std::holds_alternative<dpp::snowflake>(attachment_id_param)) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Attachment not provided."));
            return;
        }
        auto attachment = event.command.get_resolved_attachment(::std::get<dpp::snowflake>(attachment_id_param));
        
        process_gif_request(bot, attachment, [&event](const dpp::message& m) {
            event.edit_original_response(m);
        });
    }).detach();
}

inline void handle_gif_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    dpp::message msg = event.msg;
    
    auto process_msg = [&bot, event](const dpp::message& target_msg) {
        if (target_msg.attachments.empty()) {
             bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No attachment found.").set_reference(event.msg.id));
             return;
        }
        ::std::thread([&bot, event, target_msg]() {
            for (const auto& attachment : target_msg.attachments) {
                // Try to find a video or image
                std::string mime = attachment.content_type;
                if (mime.find("video/") != std::string::npos || mime.find("image/") != std::string::npos) {
                    process_gif_request(bot, attachment, [&bot, &event](const dpp::message& m) {
                        dpp::message reply = m;
                        reply.set_channel_id(event.msg.channel_id).set_reference(event.msg.id);
                        bot.message_create(reply);
                    });
                    return;
                }
            }
            bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No video or image attachment found.").set_reference(event.msg.id));
        }).detach();
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

    bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No media found. Please attach a file or reply to one.").set_reference(event.msg.id));
}

inline Command* get_gif_command() {
    static Command gif("gif", "Convert a video or image to a GIF", "Utility", {"to-gif"}, true, 
        handle_gif_text, handle_gif, {
        dpp::command_option(dpp::co_attachment, "attachment", "The video or image to convert", false)
    });
    return &gif;
}

inline void handle_ocr(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);
    ::std::thread([&bot, event]() {
        auto attachment_id_param = event.get_parameter("attachment");
        if (!::std::holds_alternative<dpp::snowflake>(attachment_id_param)) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Attachment not provided."));
            return;
        }
        auto attachment = event.command.get_resolved_attachment(::std::get<dpp::snowflake>(attachment_id_param));
        
        bool sent_first = false;
        process_ocr_request(bot, attachment, [&event, &sent_first](const dpp::message& m) {
            if (!sent_first) {
                event.edit_original_response(m);
                sent_first = true;
            } else {
                dpp::message follow_up = m;
                follow_up.set_channel_id(event.command.channel_id);
                event.from()->creator->message_create(follow_up);
            }
        });
    }).detach();
}

inline void handle_ocr_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    dpp::message msg = event.msg;
    
    // Check current message first
    if (!msg.attachments.empty()) {
        ::std::thread([&bot, event, msg]() {
            bool sent_first = false;
            process_ocr_request(bot, msg.attachments[0], [&bot, &event, &sent_first](const dpp::message& m) {
                dpp::message reply = m;
                reply.set_channel_id(event.msg.channel_id);
                if (!sent_first) {
                    reply.set_reference(event.msg.id);
                    sent_first = true;
                }
                bot.message_create(reply);
            });
        }).detach();
        return;
    }

    // Check message reference (reply)
    if (msg.message_reference.message_id != 0) {
        bot.message_get(msg.message_reference.message_id, msg.channel_id, [&bot, event](const dpp::confirmation_callback_t& cb) {
            if (cb.is_error()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Failed to fetch replied message.").set_reference(event.msg.id));
                return;
            }
            dpp::message ref_msg = ::std::get<dpp::message>(cb.value);
            if (ref_msg.attachments.empty()) {
                bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Replied message has no attachments.").set_reference(event.msg.id));
                return;
            }
            
            ::std::thread([&bot, event, ref_msg]() {
                bool sent_first = false;
                process_ocr_request(bot, ref_msg.attachments[0], [&bot, &event, &sent_first](const dpp::message& m) {
                    dpp::message reply = m;
                    reply.set_channel_id(event.msg.channel_id);
                    if (!sent_first) {
                        reply.set_reference(event.msg.id);
                        sent_first = true;
                    }
                    bot.message_create(reply);
                });
            }).detach();
        });
        return;
    }

    bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No attachment found. Please attach an image or reply to one.").set_reference(event.msg.id));
}

inline void handle_transcribe(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);
    ::std::thread([&bot, event]() {
        auto attachment_id_param = event.get_parameter("attachment");
        if (!::std::holds_alternative<dpp::snowflake>(attachment_id_param)) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Attachment not provided."));
            return;
        }
        auto attachment = event.command.get_resolved_attachment(::std::get<dpp::snowflake>(attachment_id_param));
        
        auto lang_param = event.get_parameter("language");
        auto language = ::std::holds_alternative<::std::string>(lang_param) ? ::std::get<::std::string>(lang_param) : "auto";

        process_transcribe_request(bot, attachment, language, [&event](const dpp::message& m) {
            event.edit_original_response(m);
        });
    }).detach();
}

inline void handle_transcribe_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    dpp::message msg = event.msg;
    ::std::string language = (args.size() > 0) ? args[0] : "auto";
    
    auto process_msg = [&bot, event, language](const dpp::message& target_msg) {
        if (target_msg.attachments.empty()) {
             bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No audio attachment found.").set_reference(event.msg.id));
             return;
        }
        ::std::thread([&bot, event, target_msg, language]() {
            process_transcribe_request(bot, target_msg.attachments[0], language, [&bot, &event](const dpp::message& m) {
                dpp::message reply = m;
                reply.set_channel_id(event.msg.channel_id).set_reference(event.msg.id);
                bot.message_create(reply);
            });
        }).detach();
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

    bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " No audio found. Please attach a file or reply to one.").set_reference(event.msg.id));
}

inline Command* get_ocr_command() {
    static Command ocr("ocr", "Extract text from an image attachment", "Utility", {}, true, 
        handle_ocr_text, handle_ocr, {
        dpp::command_option(dpp::co_attachment, "attachment", "The image to extract text from", false)
    });
    return &ocr;
}

inline Command* get_transcribe_command() {
    static Command transcribe("transcribe", "Transcribe audio to text", "Utility", {"ts"}, true, 
        handle_transcribe_text, handle_transcribe, {
        dpp::command_option(dpp::co_attachment, "attachment", "The audio to transcribe", false),
        dpp::command_option(dpp::co_string, "language", "ISO-639-1 language code (default: auto)", false)
    });
    return &transcribe;
}

struct MediaMetadata {
    std::string title = "Unknown Title";
    std::string description = "";
    std::string views = "0";
    std::string likes = "0";
    std::string comments = "0";
    std::string thumbnail = "";
    std::string uploader = "Unknown";
    bool success = false;
};

inline MediaMetadata fetch_metadata(const std::string& url, const std::string& cookie_path) {
    MediaMetadata meta;
    std::string ytdlp_path = "/home/siqnole/Documents/code/bpp/bin/yt-dlp";
    std::string base_path = "/home/siqnole/Documents/code/bpp/";
    std::string cookie_flag = "";
    if (std::filesystem::exists(base_path + "data/cookies.txt")) {
        cookie_flag = "--cookies " + base_path + "data/cookies.txt";
    } else if (std::filesystem::exists(base_path + "data/ytcookies.txt")) {
        cookie_flag = "--cookies " + base_path + "data/ytcookies.txt";
    }
    
    // We use a specific print format to get all metadata in one go
    // title|view_count|like_count|comment_count|thumbnail|uploader|description
    std::string print_format = "%(title)s|%(view_count)s|%(like_count)s|%(comment_count)s|%(thumbnail)s|%(uploader)s|%(description).1000s";
    std::string cmd = ytdlp_path + " " + cookie_flag + " --cache-dir /tmp/yt-dlp-cache --no-check-certificates --js-runtimes \"node:/usr/bin/node\" --print \"" + print_format + "\" \"" + url + "\"";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return meta;
    
    char buffer[2048];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    
    if (result.empty()) return meta;
    
    std::stringstream ss(result);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '|')) {
        parts.push_back(segment);
    }
    
    if (parts.size() >= 6) {
        meta.title = parts[0];
        meta.views = (parts[1] == "NA" || parts[1].empty()) ? "0" : parts[1];
        meta.likes = (parts[2] == "NA" || parts[2].empty()) ? "0" : parts[2];
        meta.comments = (parts[3] == "NA" || parts[3].empty()) ? "0" : parts[3];
        meta.thumbnail = parts[4];
        meta.uploader = parts[5];
        if (parts.size() >= 7) meta.description = parts[6];
        meta.success = true;
    }
    
    return meta;
}

inline void process_download_request(dpp::cluster& bot, const ::std::string& url, 
    std::function<void(const dpp::message&)> responder, 
    std::function<void(const std::string&)> log_callback = nullptr) {
    // Generate unique filenames for this request
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string id = std::to_string(dis(gen));
    
    std::string temp_dir = "/tmp/bronx_dl_" + id;
    std::filesystem::create_directories(temp_dir);
    
    // We target mp4/m4a for best compatibility across Discord clients
    std::string out_path = temp_dir + "/video.mp4";
    
    // Path to the latest yt-dlp binary downloaded to the project's bin folder
    std::string ytdlp_path = "/home/siqnole/Documents/code/bpp/bin/yt-dlp";
    
    // Cookie support: Determine which cookie file to use based on the platform
    // Using absolute paths to ensure reliability regardless of CWD
    std::string base_path = "/home/siqnole/Documents/code/bpp/";
    std::string cookie_path = "";
    bool is_youtube = (url.find("youtube.com") != std::string::npos || url.find("youtu.be") != std::string::npos);
    
    if (is_youtube && std::filesystem::exists(base_path + "data/ytcookies.txt")) {
        cookie_path = base_path + "data/ytcookies.txt";
    } else if (std::filesystem::exists(base_path + "data/cookies.txt")) {
        cookie_path = base_path + "data/cookies.txt";
    } else if (std::filesystem::exists(base_path + "cookies.txt")) {
        cookie_path = base_path + "cookies.txt";
    }
    
    std::string cookie_flag = "";
    if (!cookie_path.empty()) {
        cookie_flag = "--cookies " + cookie_path;
    }

    // 1. Fetch metadata first to build a rich embed
    if (log_callback) log_callback("[Metadata] Fetching video details...");
    MediaMetadata meta = fetch_metadata(url, cookie_path);
    
    // yt-dlp flags:
    // -f: format selection. Multi-stage strategy:
    // 1. Try to fit 21M video + 4M audio separately (safe 25MB goal).
    // 2. Try to find any combined/separate streams under 23MB total.
    // 3. Fallback to 'best' (will be hosted on Catbox if > 23MB).
    std::string format_sel = "\"(bv*[filesize<21M]+ba*[filesize<4M])/(bv*+ba*)[filesize<23M]/b[filesize<23M]/best\"";
    
    // Sort preference: prefer filesizes that are around our new safer target (23M)
    std::string sort_pref = "-S \"filesize~23M,res,ext\"";
    
    // Determine if we should use specific extractor arguments (primarily for YouTube)
    // Hybrid Strategy: ios (cookieless but bypasses signatures) -> web_embedded/tv (cookie-aware)
    // Critical: Explicitly enable Node.js runtime for signature decryption (n-challenge solver)
    std::string extractor_args = "--no-check-certificates --js-runtimes \"node:/usr/bin/node\"";
    if (is_youtube) {
        // ALWAYS try ios first because it bypasses the n-challenge solver requirements
        // Then fallback to web_embedded and tv which support cookies for age-gated videos
        extractor_args += " --extractor-args \"youtube:player-client=ios,web_embedded,tv\"";
    }

    std::string error_log_path = temp_dir + "/error.log";

    // Try a few times with different binaries/settings
    auto run_ytdlp = [&](const std::string& binary, const std::string& formats, const std::string& args) {
        // Clear log file first
        { std::ofstream ofs(error_log_path, std::ios::trunc); }

        // Start watcher thread if callback provided
        std::atomic<bool> done{false};
        std::thread watcher;
        if (log_callback) {
            watcher = std::thread([&]() {
                while (!done) {
                    if (std::filesystem::exists(error_log_path)) {
                        std::ifstream log_file(error_log_path);
                        std::vector<std::string> lines;
                        std::string line;
                        while (std::getline(log_file, line)) {
                            if (!line.empty()) lines.push_back(line);
                        }
                        
                        if (!lines.empty()) {
                            // Take last 15 lines only
                            size_t start = lines.size() > 15 ? lines.size() - 15 : 0;
                            std::string tail = "";
                            for (size_t i = start; i < lines.size(); ++i) {
                                tail += lines[i] + "\n";
                            }
                            log_callback(tail);
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                }
            });
        }

        // Redirect stderr to file for diagnostics
        // Use /tmp for cache to avoid read-only filesystem issues
        std::string full_cmd = binary + " " + cookie_flag + " " + sort_pref + " --cache-dir /tmp/yt-dlp-cache " + args + " -f " + formats + " --no-playlist --merge-output-format mp4 -o " + out_path + " " + url + " 2> " + error_log_path;
        int status = std::system(full_cmd.c_str());
        
        done = true;
        if (watcher.joinable()) watcher.join();

        if (status != 0 && std::filesystem::exists(error_log_path)) {
            // Final check on error log
            std::ifstream log_file(error_log_path);
            std::string line;
            std::cout << "[DOWNLOAD_ERROR] Command failed with status " << status << ". Details:\n";
            while (std::getline(log_file, line)) {
                std::cout << "  > " << line << "\n";
            }
        }
        return status;
    };

    // Attempt 1: Optimal mix based on cookie availability
    int result = run_ytdlp(ytdlp_path, format_sel, extractor_args);
    
    if (result != 0 && is_youtube) {
        if (!cookie_path.empty()) {
            // Attempt 2: Try specific "tv" client which is generally most resilient with cookies
            std::cout << "[RETRY] Attempting with 'tv' client spoofing (Cookie support enabled)...\n";
            result = run_ytdlp(ytdlp_path, format_sel, "--extractor-args \"youtube:player-client=tv\"");
            
            if (result != 0) {
                // Attempt 3: Try "web" client alone
                std::cout << "[RETRY] Attempting with 'web' client spoofing...\n";
                result = run_ytdlp(ytdlp_path, format_sel, "--extractor-args \"youtube:player-client=web\"");
            }
        } else {
            // Attempt 2: Try specific "ios" client which is often most resilient without cookies
            std::cout << "[RETRY] Attempting with 'ios' client spoofing (No cookies)...\n";
            result = run_ytdlp(ytdlp_path, format_sel, "--extractor-args \"youtube:player-client=ios\"");
            
            if (result != 0) {
                // Attempt 3: Try "android" client
                std::cout << "[RETRY] Attempting with 'android' client spoofing...\n";
                result = run_ytdlp(ytdlp_path, "best", "--extractor-args \"youtube:player-client=android\"");
            }
        }
    }
    
    if (result != 0) {
        std::filesystem::remove_all(temp_dir);
        std::string error_msg = bronx::EMOJI_DENY + " Failed to download media.";
        
        if (url.find("instagram.com") != std::string::npos) {
            error_msg += " Instagram often requires authentication. Please ensure a `cookies.txt` is available on the server.";
        } else {
            error_msg += " The URL may be invalid or the platform is currently unsupported.";
        }
        
        responder(dpp::message(error_msg));
        return;
    }

    // Build the rich embed if metadata was fetched successfully
    dpp::embed embed;
    if (meta.success) {
        std::string desc = meta.description;
        if (desc.length() > 500) desc = desc.substr(0, 497) + "...";
        
        embed = bronx::create_embed(desc)
            .set_title(meta.title)
            .set_author(meta.uploader, "", "")
            .set_image(meta.thumbnail)
            .add_field("👁️ Views", meta.views, true)
            .add_field("❤️ Likes", meta.likes, true)
            .add_field("💬 Comments", meta.comments, true);
    }

    std::ifstream vid_file(out_path, std::ios::binary | std::ios::ate);
    if (!vid_file.is_open()) {
        std::filesystem::remove_all(temp_dir);
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to locate the downloaded file."));
        return;
    }

    std::streamsize vid_size = vid_file.tellg();
    // Discord technically allows 25MB, but measurements are inconsistent. 23MB is the safe 'Action' threshold.
    if (vid_size > 23 * 1024 * 1024) {
        if (log_callback) log_callback("[CDN] File > 23MB. Uploading to Catbox.moe...");
        std::string hosted_url = upload_to_catbox(out_path);
        vid_file.close();
        std::filesystem::remove_all(temp_dir);

        if (hosted_url.empty() || hosted_url.find("http") == std::string::npos) {
            responder(dpp::message(bronx::EMOJI_DENY + " File is too large for Discord (max 25MB) and CDN upload failed."));
            return;
        }

        // Send as a link embed with the video integrated
        dpp::embed hosted_embed = embed;
        hosted_embed.set_url(hosted_url);
        hosted_embed.set_video(hosted_url); // Integrate the video INTO the embed box
        hosted_embed.set_footer(dpp::embed_footer().set_text("hosted on catbox.moe"));
        
        dpp::message msg;
        msg.set_content(bronx::EMOJI_CHECK + " **Video is too large for Discord direct upload!**\nView/Download here: " + hosted_url);
        msg.add_embed(hosted_embed);
        responder(msg);
        return;
    }

    vid_file.seekg(0, std::ios::beg);
    std::string vid_data(vid_size, '\0');
    vid_file.read(&vid_data[0], vid_size);
    vid_file.close();

    std::filesystem::remove_all(temp_dir);

    dpp::message msg;
    if (meta.success) msg.add_embed(embed);
    msg.add_file("video.mp4", vid_data);
    responder(msg);
}

inline void handle_download(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);
    ::std::thread([&bot, event]() {
        auto url_param = event.get_parameter("url");
        if (!::std::holds_alternative<::std::string>(url_param)) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " URL not provided."));
            return;
        }
        std::string url = ::std::get<::std::string>(url_param);
        
        process_download_request(bot, url, [&event](const dpp::message& m) {
            event.edit_original_response(m);
        });
    }).detach();
}

inline void handle_download_text(dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
    if (args.empty()) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Please provide a URL to download.").set_reference(event.msg.id));
        return;
    }

    std::string url = args[0];
    
    // Basic URL validation
    if (url.find("http") == std::string::npos) {
        bot.message_create(dpp::message(event.msg.channel_id, bronx::EMOJI_DENY + " Invalid URL provided.").set_reference(event.msg.id));
        return;
    }

    ::std::thread([&bot, event, url]() {
        process_download_request(bot, url, [&bot, &event](const dpp::message& m) {
            dpp::message reply = m;
            reply.set_channel_id(event.msg.channel_id).set_reference(event.msg.id);
            bot.message_create(reply);
        });
    }).detach();
}

inline Command* get_download_command() {
    static Command download("download", "Download media from social platforms", "Utility", {"dl"}, true, 
        handle_download_text, handle_download, {
        dpp::command_option(dpp::co_string, "url", "The URL of the video to download", true)
    });
    return &download;
}


inline void process_search_request(dpp::cluster& bot, const std::string& query, const std::string& platform, 
    bool randomize,
    std::function<void(const dpp::message&)> responder,
    std::function<void(const std::string&)> log_callback = nullptr) {
    // Sanitize query for shell
    std::string sanitized_query = query;
    size_t pos = 0;
    while ((pos = sanitized_query.find("\"", pos)) != std::string::npos) {
        sanitized_query.replace(pos, 1, "\\\"");
        pos += 2;
    }

    std::string ytdlp_path = "/home/siqnole/Documents/code/bpp/bin/yt-dlp";
    std::string base_path = "/home/siqnole/Documents/code/bpp/";
    
    // Cookie support for search: prefer data/cookies.txt as it has broader platform session data
    std::string cookie_flag = "";
    if (std::filesystem::exists(base_path + "data/cookies.txt")) {
        cookie_flag = "--cookies " + base_path + "data/cookies.txt";
    } else if (std::filesystem::exists(base_path + "data/ytcookies.txt")) {
        cookie_flag = "--cookies " + base_path + "data/ytcookies.txt";
    }

    // We use a temp file to capture search status/ids
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    std::string id = std::to_string(dis(gen));
    std::string search_log = "/tmp/bronx_search_" + id + ".log";
    
    // Construct search command. We distinguish between Pool Mode (direct profile URL)
    // and Search Mode (keyword lookup via ytsearch).
    std::string search_cmd;
    if (sanitized_query.find("tiktok.com") != std::string::npos || sanitized_query.find("instagram.com") != std::string::npos) {
        // Pool Mode: Randomized extraction from a playlist/profile
        if (log_callback) log_callback("[Pool] Exploring authentic platform feed...");
        search_cmd = ytdlp_path + " " + cookie_flag + " --cache-dir /tmp/yt-dlp-cache --print webpage_url --playlist-items 1 --playlist-random --no-check-certificates --js-runtimes \"node:/usr/bin/node\" \"" + sanitized_query + "\" > " + search_log + " 2>&1";
    } else {
        // Search Mode: Keyword-based lookup
        std::string count = randomize ? "25" : "1";
        search_cmd = ytdlp_path + " " + cookie_flag + " --cache-dir /tmp/yt-dlp-cache --print webpage_url --flat-playlist --no-playlist --no-check-certificates --js-runtimes \"node:/usr/bin/node\" \"ytsearch" + count + ":" + sanitized_query + "\" > " + search_log + " 2>&1";
    }
    
    if (log_callback) log_callback("[Search] Querying " + platform + " for results...");
    int status = std::system(search_cmd.c_str());
    
    if (status == 0) {
        std::ifstream file(search_log);
        std::vector<std::string> ids;
        std::string line;
        while (std::getline(file, line)) {
            // We want lines that are valid URLs
            if (!line.empty() && line.find("https://") != std::string::npos) {
                ids.push_back(line);
            }
        }
        
        if (!ids.empty()) {
            std::string final_url;
            if (randomize) {
                std::uniform_int_distribution<> id_dis(0, (int)ids.size() - 1);
                final_url = ids[id_dis(gen)];
            } else {
                final_url = ids[0];
            }
            
            if (log_callback) log_callback("[Search] Found video: " + final_url + ". Starting download...");
            process_download_request(bot, final_url, responder, log_callback);
        } else {
            if (log_callback) log_callback("[Search] No " + platform + " results found.");
            responder(dpp::message(bronx::EMOJI_DENY + " No " + platform + " results found for: `" + query + "`"));
        }
    } else {
        if (log_callback) log_callback("[Search] Failed to query platform.");
        responder(dpp::message(bronx::EMOJI_DENY + " Failed to search " + platform + "."));
    }
    
    if (std::filesystem::exists(search_log)) std::filesystem::remove(search_log);
}

} // namespace utility
} // namespace commands
