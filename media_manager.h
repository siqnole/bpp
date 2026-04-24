#pragma once
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <whisper.h>
#include <string>
#include <iostream>

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
            std::cerr << "\033[1;31m[MediaManager]\033[0m Could not initialize Tesseract OCR!\n";
            return false;
        }
        std::cout << "\033[1;32m[MediaManager]\033[0m Tesseract OCR initialized (eng).\n";

        // Initialize Whisper
        whisper_context_params params = whisper_context_default_params();
        params.use_gpu = false;
        params.flash_attn = false;
        std::cerr << "[MediaManager] Initializing whisper with use_gpu=" << params.use_gpu << " flash_attn=" << params.flash_attn << "\n";
        whisper_ctx = whisper_init_from_file_with_params(whisper_model_path.c_str(), params);
        if (!whisper_ctx) {
            std::cerr << "\033[1;31m[MediaManager]\033[0m Could not initialize whisper.cpp from file: " << whisper_model_path << "!\n";
            // Tesseract is already init'd, but we fail if either fails for simplicity in this bot
            return false;
        }
        std::cerr << "\033[1;32m[MediaManager]\033[0m whisper.cpp context initialized from " << whisper_model_path << ".\n";

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
