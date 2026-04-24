#pragma once
#include <string>
#include <filesystem>
#include <vector>

namespace bronx {
namespace utility {

/**
 * @brief RAII guard to ensure temporary directories/files are cleaned up.
 */
struct TempDirGuard {
    std::string path;
    TempDirGuard(std::string p) : path(p) {}
    ~TempDirGuard() {
        if (!path.empty() && std::filesystem::exists(path)) {
            std::filesystem::remove_all(path);
        }
    }
    
    // Non-copyable
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;
    
    // Moveable
    TempDirGuard(TempDirGuard&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }
    TempDirGuard& operator=(TempDirGuard&& other) noexcept {
        if (this != &other) {
            if (!path.empty() && std::filesystem::exists(path)) {
                std::filesystem::remove_all(path);
            }
            path = std::move(other.path);
            other.path.clear();
        }
        return *this;
    }
};

} // namespace utility
} // namespace bronx
