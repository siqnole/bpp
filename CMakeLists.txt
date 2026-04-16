cmake_minimum_required(VERSION 3.15)
project(discord-bot)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Maximum build verbosity — show full compiler/linker commands
set(CMAKE_VERBOSE_MAKEFILE ON)

# Release-quality optimizations (significant speedup over default -O0)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# -g for debug symbols (flamegraph), -fno-omit-frame-pointer for proper stack traces
# Add command source files
file(GLOB_RECURSE COMMAND_SOURCES
    ${CMAKE_SOURCE_DIR}/commands/**/*.cpp
)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -g -fno-omit-frame-pointer")

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

if(NOT MARIADB_LIBRARIES)
    message(FATAL_ERROR "MariaDB client library not found! Install: sudo apt install libmariadb-dev")
endif()

# Add database source files
file(GLOB_RECURSE DATABASE_SOURCES
    ${CMAKE_SOURCE_DIR}/database/core/*.cpp
    ${CMAKE_SOURCE_DIR}/database/operations/**/*.cpp
    ${CMAKE_SOURCE_DIR}/database/utils/*.cpp
)

# Add performance optimization source files
file(GLOB PERFORMANCE_SOURCES
    ${CMAKE_SOURCE_DIR}/performance/*.cpp
)

# Add executable
add_executable(${PROJECT_NAME}
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/server_logger.cpp
    ${COMMAND_SOURCES}
    ${DATABASE_SOURCES}
    ${PERFORMANCE_SOURCES}
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
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    dpp
    ${MARIADB_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${CAIRO_LIBRARIES}
    OpenSSL::Crypto
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

