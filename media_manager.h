#pragma once
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <whisper.h>
#include <string>
#include <iostream>
#include "utils/logger.h"

namespace bronx {

/**
 * @brief Manages open-source media processing contexts (Tesseract and whisper.cpp).
 * These are expensive to initialize, so we keep them alive for the bot's lifetime.
 */
struct MediaManager {
    tesseract::TessBaseAPI* ocr_api = nullptr;
    whisper_context* whisper_ctx = nullptr;

    MediaManager() = default;

    /**
     * @brief Initialize contexts.
     * @param whisper_model_path Path to the whisper GGML model file.
     * @return true if successful.
     */
    bool init(const std::string& whisper_model_path = "ggml-base.en.bin") {
        // Initialize Tesseract
        ocr_api = new tesseract::TessBaseAPI();
        if (ocr_api->Init(nullptr, "eng")) {
            bronx::logger::error("media manager", "could not initialize Tesseract OCR!");
            return false;
        }
        bronx::logger::success("media manager", "Tesseract OCR initialized (eng).");

        // Initialize Whisper
        whisper_context_params params = whisper_context_default_params();
        params.use_gpu = false;
        params.flash_attn = false;
        bronx::logger::debug("media manager", "initializing whisper with use_gpu=" + std::to_string(params.use_gpu) + " flash_attn=" + std::to_string(params.flash_attn));
        whisper_ctx = whisper_init_from_file_with_params(whisper_model_path.c_str(), params);
        if (!whisper_ctx) {
            bronx::logger::error("media manager", "could not initialize whisper.cpp from file: " + whisper_model_path + "!");
            // Tesseract is already init'd, but we fail if either fails for simplicity in this bot
            return false;
        }
        bronx::logger::success("media manager", "whisper.cpp context initialized from " + whisper_model_path + ".");

        return true;
    }

    void cleanup() {
        if (ocr_api) {
            ocr_api->End();
            delete ocr_api;
            ocr_api = nullptr;
        }
        if (whisper_ctx) {
            whisper_free(whisper_ctx);
            whisper_ctx = nullptr;
        }
    }

    ~MediaManager() {
        cleanup();
    }
};

// Singleton access
inline MediaManager& get_media_manager() {
    static MediaManager instance;
    return instance;
}

}
