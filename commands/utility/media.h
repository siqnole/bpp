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

inline void handle_ocr(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);

    ::std::thread([&bot, event]() {
        auto attachment_id = event.get_parameter("attachment");
        auto attachment = event.command.get_resolved_attachment(::std::get<dpp::snowflake>(attachment_id));

        if (attachment.id == 0) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Attachment not found."));
            return;
        }

        if (attachment.size > 8 * 1024 * 1024) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Image is too large (max 8MB).").set_flags(dpp::m_ephemeral));
            return;
        }

        ::std::string mime = attachment.content_type;
        if (mime != "image/png" && mime != "image/jpeg" && mime != "image/webp") {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Unsupported file type. Please use PNG, JPEG, or WebP.").set_flags(dpp::m_ephemeral));
            return;
        }

        auto response = http_get_sync(attachment.url);
        
        if (response.status != 200) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Failed to download image."));
            return;
        }

        auto& manager = bronx::get_media_manager();
        if (!manager.ocr_api) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " OCR service is not initialized."));
            return;
        }

        Pix* pix = pixReadMem((const unsigned char*)response.body.data(), response.body.size());
        if (!pix) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Failed to process image memory."));
            return;
        }

        manager.ocr_api->SetImage(pix);
        char* outText = manager.ocr_api->GetUTF8Text();
        ::std::string text(outText);
        delete[] outText;
        pixDestroy(&pix);

        // Trim whitespace and null bytes
        text.erase(::std::remove(text.begin(), text.end(), '\0'), text.end());
        text.erase(0, text.find_first_not_of(" \n\r\t"));
        text.erase(text.find_last_not_of(" \n\r\t") + 1);

        if (text.empty()) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " No text detected.").set_flags(dpp::m_ephemeral));
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
            
            if (i == 0) {
                event.edit_original_response(dpp::message().add_embed(embed));
            } else {
                bot.message_create(dpp::message(event.command.channel_id, "").add_embed(embed));
            }
        }
    }).detach();
}

inline void handle_transcribe(dpp::cluster& bot, const dpp::slashcommand_t& event) {
    event.thinking(true);

    ::std::thread([&bot, event]() {
        auto attachment_id = event.get_parameter("attachment");
        auto lang_param = event.get_parameter("language");
        auto language = ::std::holds_alternative<::std::string>(lang_param) ? ::std::get<::std::string>(lang_param) : "auto";
        auto attachment = event.command.get_resolved_attachment(::std::get<dpp::snowflake>(attachment_id));

        if (attachment.id == 0) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Attachment not found."));
            return;
        }

        if (attachment.size > 25 * 1024 * 1024) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Audio file is too large (max 25MB).").set_flags(dpp::m_ephemeral));
            return;
        }

        ::std::string mime = attachment.content_type;
        if (mime != "audio/ogg" && mime != "audio/mpeg" && mime != "audio/wav" && mime != "audio/flac" && mime != "application/ogg") {
            if (attachment.filename.find(".ogg") == ::std::string::npos) {
                event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Unsupported file type. Please use OGG, MP3, WAV, or FLAC.").set_flags(dpp::m_ephemeral));
                return;
            }
        }

        auto response = http_get_sync(attachment.url);
        if (response.status != 200) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Failed to download audio."));
            return;
        }

        double duration = 0.0;
        ::std::vector<float> pcm = decode_audio(response.body, duration);

        if (pcm.empty()) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Failed to decode audio or file is empty."));
            return;
        }

        auto& manager = bronx::get_media_manager();
        if (!manager.whisper_ctx) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Transcription service is not initialized."));
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
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " Transcription failed."));
            return;
        }

        ::std::string transcript;
        int n_segments = whisper_full_n_segments(manager.whisper_ctx);
        for (int i = 0; i < n_segments; ++i) {
            transcript += whisper_full_get_segment_text(manager.whisper_ctx, i);
        }

        if (transcript.empty()) {
            event.edit_original_response(dpp::message(bronx::EMOJI_DENY + " No speech detected in audio.").set_flags(dpp::m_ephemeral));
            return;
        }

        const char* detected_lang = whisper_lang_str(whisper_full_lang_id(manager.whisper_ctx));
        
        auto embed = bronx::create_embed("```\n" + transcript + "\n```")
            .set_title("Audio Transcription")
            .add_field("Detected Language", detected_lang ? detected_lang : "Unknown", true)
            .add_field("Duration", ::std::to_string(duration) + "s", true);

        event.edit_original_response(dpp::message().add_embed(embed));
    }).detach();
}

inline Command* get_ocr_command() {
    static Command ocr("ocr", "Extract text from an image attachment", "Utility", {}, true, nullptr, handle_ocr, {
        dpp::command_option(dpp::co_attachment, "attachment", "The image to extract text from", true)
    });
    return &ocr;
}

inline Command* get_transcribe_command() {
    static Command transcribe("transcribe", "Transcribe audio to text", "Utility", {}, true, nullptr, handle_transcribe, {
        dpp::command_option(dpp::co_attachment, "attachment", "The audio to transcribe", true),
        dpp::command_option(dpp::co_string, "language", "ISO-639-1 language code (default: auto)", false)
    });
    return &transcribe;
}

} // namespace utility
} // namespace commands
