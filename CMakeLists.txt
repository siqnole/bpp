cmake_minimum_required(VERSION 3.16)
project(discord-bot)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent module for TUI and other dependencies
include(FetchContent)

# FTXUI (Terminal User Interface) integration
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)
# Force FTXUI to be static so it is embedded in the binary
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
FetchContent_MakeAvailable(ftxui)

# ============================================================================
# BUILD PERFORMANCE: ccache — caches compiled objects between builds.
# Install with:  sudo apt install ccache
# This makes rebuilds of unchanged files near-instant.
# ============================================================================
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found — enabling compiler caching (${CCACHE_PROGRAM})")
else()
    message(STATUS "ccache NOT found — install with: sudo apt install ccache")
endif()

# Disable verbose output — printing every compile command wastes I/O time
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Release-quality optimizations (significant speedup over default -O0)
# Use CACHE FORCE so the value actually persists — the old check was silently
# failing because CMakeCache.txt already had an empty string stored.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "Build type defaulted to Release")
endif()

# -g for debug symbols (flamegraph), -fno-omit-frame-pointer for proper stack traces
# -march=x86-64-v2 ensures compatibility with older VPS CPUs (SSE4.2, but not AVX2/FMA)
set(CMAKE_CXX_FLAGS "-g -fno-omit-frame-pointer -march=x86-64-v2 -mno-avx -mno-avx2 -mno-fma -mno-f16c -mno-bmi2")
set(CMAKE_C_FLAGS "-g -fno-omit-frame-pointer -march=x86-64-v2 -mno-avx -mno-avx2 -mno-fma -mno-f16c -mno-bmi2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Force GGML to not use instructions above SSE4.2
set(GGML_AVX OFF CACHE BOOL "Disable AVX" FORCE)
set(GGML_AVX2 OFF CACHE BOOL "Disable AVX2" FORCE)
set(GGML_FMA OFF CACHE BOOL "Disable FMA" FORCE)
set(GGML_F16C OFF CACHE BOOL "Disable F16C" FORCE)
set(GGML_BMI2 OFF CACHE BOOL "Disable BMI2" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Disable native optimization" FORCE)
set(GGML_SSE42 ON CACHE BOOL "Enable SSE4.2" FORCE)

# Add command source files (matches commands/*.cpp AND commands/**/*.cpp)
file(GLOB_RECURSE COMMAND_SOURCES
    ${CMAKE_SOURCE_DIR}/commands/*.cpp
)

# Find DPP library
find_package(DPP REQUIRED)

# Find MariaDB/MySQL
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MARIADB REQUIRED libmariadb)
else()
    # Fallback to FindMySQL
    find_path(MYSQL_INCLUDE_DIR mysql/mysql.h
        PATHS /usr/include /usr/local/include
    )
    find_library(MYSQL_LIBRARY NAMES mariadb mysqlclient
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    )
    set(MARIADB_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
    set(MARIADB_LIBRARIES ${MYSQL_LIBRARY})
endif()

# libcurl for emoji steal command and API cache client (optional)
find_package(CURL QUIET)
if (CURL_FOUND)
    message(STATUS "libcurl found — building `steal` command + API cache client")
else()
    message(STATUS "libcurl NOT found — `steal` command and API cache will be disabled")
endif()

# Cairo for chart rendering (stats commands)
pkg_check_modules(CAIRO REQUIRED cairo)
if(CAIRO_FOUND)
    message(STATUS "Cairo found — enabling chart rendering for stats commands")
else()
    message(FATAL_ERROR "Cairo not found! Install: sudo apt install libcairo2-dev")
endif()

# SQLite3 for local cache database (required for hybrid performance layer)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SQLITE3 QUIET sqlite3)
endif()
if(NOT SQLITE3_FOUND)
    find_path(SQLITE3_INCLUDE_DIRS sqlite3.h PATHS /usr/include /usr/local/include)
    find_library(SQLITE3_LIBRARIES NAMES sqlite3 PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
endif()
if(SQLITE3_LIBRARIES)
    message(STATUS "SQLite3 found — enabling local cache database")
else()
    message(FATAL_ERROR "SQLite3 not found! Install: sudo apt install libsqlite3-dev")
endif()

# OpenSSL for AES-256 encryption (privacy/identity cache)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found — enabling AES-256 identity encryption")
else()
    message(FATAL_ERROR "OpenSSL not found! Install: sudo apt install libssl-dev")
endif()

# Tesseract OCR and Leptonica
pkg_check_modules(TESSERACT QUIET tesseract)
pkg_check_modules(LEPTONICA QUIET lept)
if(NOT TESSERACT_FOUND OR NOT LEPTONICA_FOUND)
    # Fallback to manual find if pkg-config fails
    find_path(TESSERACT_INCLUDE_DIRS tesseract/baseapi.h)
    find_library(TESSERACT_LIBRARIES NAMES tesseract)
    find_path(LEPTONICA_INCLUDE_DIRS leptonica/allheaders.h)
    find_library(LEPTONICA_LIBRARIES NAMES lept)
endif()

# FFmpeg for audio decoding
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

# whisper.cpp (Using local build from third_party/whisper.cpp)
option(WHISPER_BUILD_EXAMPLES OFF)
option(WHISPER_BUILD_TESTS OFF)
option(WHISPER_BUILD_SERVER OFF)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

set(WHISPER_INCLUDE_DIRS 
    "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/include"
    "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/ggml/include"
)
set(WHISPER_LIBRARIES whisper ggml ggml-cpu)

# Add RPATH so the bot finds these shared libraries at runtime
# (When installing, we might want to change this, but for now it's fine)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};/opt/bpp/build")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

if(NOT MARIADB_LIBRARIES)
    message(FATAL_ERROR "MariaDB client library not found! Install: sudo apt install libmariadb-dev")
endif()

# Add database source files
file(GLOB_RECURSE DATABASE_SOURCES
    ${CMAKE_SOURCE_DIR}/database/core/*.cpp
    ${CMAKE_SOURCE_DIR}/database/operations/**/*.cpp
    ${CMAKE_SOURCE_DIR}/database/utils/*.cpp
    ${CMAKE_SOURCE_DIR}/database/operations/maintenance_operations.cpp
)

# Add performance optimization source files
file(GLOB PERFORMANCE_SOURCES
    ${CMAKE_SOURCE_DIR}/performance/*.cpp
)

# Add events source files
file(GLOB EVENTS_SOURCES
    ${CMAKE_SOURCE_DIR}/events/*.cpp
)

# Add utility source files
file(GLOB_RECURSE UTILS_SOURCES
    ${CMAKE_SOURCE_DIR}/utils/**/*.cpp
)

# Add executable
add_executable(${PROJECT_NAME}
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${EVENTS_SOURCES}
    ${CMAKE_SOURCE_DIR}/server_logger.cpp
    ${CMAKE_SOURCE_DIR}/tui_logger.cpp
    ${COMMAND_SOURCES}
    ${DATABASE_SOURCES}
    ${PERFORMANCE_SOURCES}
    ${UTILS_SOURCES}
)

# ============================================================================
# BUILD PERFORMANCE: Precompiled Headers (PCH)
# The DPP mega-header and common STL headers are parsed once and reused across
# all 38+ translation units.  This alone can cut compile time by 30-50%.
# Requires CMake >= 3.16.
# ============================================================================
target_precompile_headers(${PROJECT_NAME} PRIVATE
    <dpp/dpp.h>
    <string>
    <vector>
    <map>
    <unordered_map>
    <chrono>
    <mutex>
    <iostream>
    <algorithm>
    <functional>
    <memory>
    <optional>
    <sstream>
    <atomic>
    <thread>
    <cstdint>
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/performance
    ${CMAKE_SOURCE_DIR}/commands/moderation
    ${CMAKE_SOURCE_DIR}/commands/quiet_moderation
    ${MARIADB_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWRESAMPLE_INCLUDE_DIRS}
    ${WHISPER_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    dpp
    ${MARIADB_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${CAIRO_LIBRARIES}
    OpenSSL::Crypto
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWRESAMPLE_LIBRARIES}
    ${WHISPER_LIBRARIES}
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# If libcurl was found, wire it up and enable the steal command at compile-time
if (CURL_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_LIBCURL=1)
endif()

# Print configuration
message(STATUS "DPP found: ${DPP_LIBRARIES}")
message(STATUS "MariaDB include: ${MARIADB_INCLUDE_DIRS}")
message(STATUS "MariaDB library: ${MARIADB_LIBRARIES}")
