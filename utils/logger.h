#pragma once
#include "colors.h"
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>

namespace bronx {
namespace logger {

inline std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

enum class Level {
    TRACE,
    DEBUG,
    INFO,
    NOTICE,
    SUCCESS,
    WARN,
    ERR,
    CRIT
};

inline void log(Level level, const std::string& component, const std::string& message) {
    const char* color = clr::LOG_INFO;
    const char* level_name = "INFO";
    
    switch (level) {
        case Level::TRACE:   color = clr::LOG_TRACE;   level_name = "TRACE"; break;
        case Level::DEBUG:   color = clr::LOG_DEBUG;   level_name = "DEBUG"; break;
        case Level::INFO:    color = clr::LOG_INFO;    level_name = "INFO";  break;
        case Level::NOTICE:  color = clr::LOG_NOTICE;  level_name = "NOTE";  break;
        case Level::SUCCESS: color = clr::LOG_SUCCESS; level_name = "OK";    break;
        case Level::WARN:    color = clr::LOG_WARN;    level_name = "WARN";  break;
        case Level::ERR:     color = clr::LOG_ERROR;   level_name = "ERROR"; break;
        case Level::CRIT:    color = clr::LOG_CRIT;    level_name = "FATAL"; break;
    }

    std::cout << clr::DIM << "[" << get_timestamp() << "] " 
              << "[" << component << "] " << clr::RESET
              << color << message << clr::RESET << std::endl;
}

// Convenience macros/functions
inline void info(const std::string& comp, const std::string& msg) { log(Level::INFO, comp, msg); }
inline void success(const std::string& comp, const std::string& msg) { log(Level::SUCCESS, comp, msg); }
inline void warn(const std::string& comp, const std::string& msg) { log(Level::WARN, comp, msg); }
inline void error(const std::string& comp, const std::string& msg) { log(Level::ERR, comp, msg); }
inline void debug(const std::string& comp, const std::string& msg) { log(Level::DEBUG, comp, msg); }
inline void trace(const std::string& comp, const std::string& msg) { log(Level::TRACE, comp, msg); }
inline void notice(const std::string& comp, const std::string& msg) { log(Level::NOTICE, comp, msg); }
inline void critical(const std::string& comp, const std::string& msg) { log(Level::CRIT, comp, msg); }

} // namespace logger
} // namespace bronx
