#pragma once
namespace clr {
    constexpr const char* RESET       = "\033[0m";
    constexpr const char* RED         = "\033[31m";
    constexpr const char* GREEN       = "\033[32m";
    constexpr const char* YELLOW      = "\033[33m";
    constexpr const char* BLUE        = "\033[34m";
    constexpr const char* MAGENTA     = "\033[35m";
    constexpr const char* CYAN        = "\033[36m";
    constexpr const char* WHITE       = "\033[37m";
    constexpr const char* DIM         = "\033[2m";
    constexpr const char* BOLD        = "\033[1m";
    constexpr const char* UNDERLINE   = "\033[4m";
    
    // Semantic Colors (Cool to Warm)
    constexpr const char* LOG_TRACE   = "\033[2;37m";   // Dim White
    constexpr const char* LOG_DEBUG   = "\033[35m";     // Magenta
    constexpr const char* LOG_INFO    = "\033[36m";     // Cyan
    constexpr const char* LOG_NOTICE  = "\033[34m";     // Blue
    constexpr const char* LOG_SUCCESS = "\033[32m";     // Green
    constexpr const char* LOG_WARN    = "\033[33m";     // Yellow
    constexpr const char* LOG_ERROR   = "\033[31m";     // Red
    constexpr const char* LOG_CRIT    = "\033[1;31m";   // Bold Red

    constexpr const char* BOLD_RED    = "\033[1;31m";
    constexpr const char* BOLD_GREEN  = "\033[1;32m";
    constexpr const char* BOLD_YELLOW = "\033[1;33m";
    constexpr const char* BOLD_BLUE   = "\033[1;34m";
    constexpr const char* BOLD_MAGENTA= "\033[1;35m";
    constexpr const char* BOLD_CYAN   = "\033[1;36m";
}
