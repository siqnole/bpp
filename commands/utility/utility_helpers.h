#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <chrono>

namespace commands {
namespace utility {

// Generate dynamic version based on build size and time
inline ::std::string get_build_version() {
    // Get executable size
    struct stat stat_buf;
    long file_size = 0;
    
    // Try different possible paths to the executable
    ::std::vector<::std::string> exe_paths = {
        "/proc/self/exe",  // Linux self-reference
        "discord-bot",
        "./discord-bot",
        "build/discord-bot",
        "../discord-bot"
    };
    
    for (const auto& path : exe_paths) {
        if (stat(path.c_str(), &stat_buf) == 0) {
            file_size = stat_buf.st_size;
            break;
        }
    }
    
    // Use logarithmic compression to condense file size to ~4-5 digits
    // log10(file_size) gives us a nice compressed value
    int size_component = 0;
    if (file_size > 0) {
        // Scale: log10(1MB) ≈ 6, log10(10MB) ≈ 7
        // Multiply by 10000 to get good granularity
        size_component = static_cast<int>(::std::log10(file_size) * 10000);
    }
    
    // Get current time - minute of day (0-1439)
    auto now = ::std::chrono::system_clock::now();
    auto now_time_t = ::std::chrono::system_clock::to_time_t(now);
    auto now_tm = *::std::localtime(&now_time_t);
    int minute_of_day = now_tm.tm_hour * 60 + now_tm.tm_min;
    
    // Format: 1.v[size_component][minute_of_day]
    // Example: 1.v423041022 (size: 42304, time: 10:22 = 622 minutes)
    ::std::string version = "1.v" + ::std::to_string(size_component) + ::std::to_string(minute_of_day);
    
    return version;
}

// Helper function to create ASCII progress bar
inline ::std::string create_progress_bar(int percentage, int length = 10) {
    int filled = (percentage * length) / 100;
    ::std::string bar = "[";
    for (int i = 0; i < length; i++) {
        if (i < filled) {
            bar += "█";
        } else {
            bar += "░";
        }
    }
    bar += "] " + ::std::to_string(percentage) + "%";
    return bar;
}

} // namespace utility
} // namespace commands
