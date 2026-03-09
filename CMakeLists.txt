cmake_minimum_required(VERSION 3.15)
project(discord-bot)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release-quality optimizations (significant speedup over default -O0)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# -g for debug symbols (flamegraph), -fno-omit-frame-pointer for proper stack traces
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

# libcurl for emoji steal command (optional)
find_package(CURL QUIET)
if (CURL_FOUND)
    message(STATUS "libcurl found — building `steal` command")
else()
    message(STATUS "libcurl NOT found — `steal` command will be disabled")
endif()

if(NOT MARIADB_LIBRARIES)
    message(FATAL_ERROR "MariaDB client library not found! Install: sudo apt install libmariadb-dev")
endif()

# Add database source files
set(DATABASE_SOURCES
    database/core/database.cpp
    database/core/connection_pool.cpp
    database/operations/user/user_operations.cpp
    database/operations/economy/loan_operations.cpp
    database/operations/user/cooldown_operations.cpp
    database/operations/economy/inventory_operations.cpp
    database/operations/economy/shop_operations.cpp
    database/operations/economy/market_operations.cpp
    database/operations/leveling/leaderboard_operations.cpp
    database/operations/moderation/reaction_role_operations.cpp
    database/operations/moderation/autopurge_operations.cpp
    database/operations/moderation/abuse_operations.cpp
    database/operations/community/suggestion_operations.cpp
    database/operations/user/prefix_operations.cpp
    database/operations/moderation/permission_operations.cpp
    database/operations/economy/server_economy_operations.cpp
    database/operations/economy/server_fishing_operations.cpp
    database/operations/economy/history_operations.cpp
    database/operations/economy/jackpot_world_events.cpp
    database/operations/community/bugreport_operations.cpp
    database/operations/leveling/leveling_operations.cpp
    database/operations/community/patch_operations.cpp
    database/operations/leveling/xp_blacklist_operations.cpp
    database/utils/database_utility.cpp
)

# Add performance optimization source files
set(PERFORMANCE_SOURCES
    performance/cache_manager.cpp
)

# Add executable
add_executable(${PROJECT_NAME} 
    main.cpp
    commands/quiet_moderation/antispam_handler.cpp
    commands/quiet_moderation/antispam_command.cpp
    commands/quiet_moderation/text_filter_handler.cpp
    commands/quiet_moderation/text_filter_command.cpp
    ${DATABASE_SOURCES}
    ${PERFORMANCE_SOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/performance
    ${MARIADB_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    dpp
    ${MARIADB_LIBRARIES}
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

