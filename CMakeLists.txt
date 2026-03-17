cmake_minimum_required(VERSION 3.21)
project(MPD-Presence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type default ──
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Dependencies ──

find_package(PkgConfig REQUIRED)

# libmpdclient
pkg_check_modules(MPDCLIENT REQUIRED libmpdclient)

# libcurl
find_package(CURL REQUIRED)

# nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    pkg_check_modules(NLOHMANN_JSON nlohmann_json)
endif()

# discord-presence — let the subproject fetch its own deps (fmt, glaze) via CPM
set(DISCORD_RPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/discord-presence)

# ── Main executable ──
add_executable(MPD-Presence
    src/main.cpp
    src/rpc.cpp
    src/mpd.cpp
    src/album_art.cpp
    src/config.cpp
)

target_include_directories(MPD-Presence PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${MPDCLIENT_INCLUDE_DIRS}
)

target_link_libraries(MPD-Presence PRIVATE
    discord-rpc
    CURL::libcurl
    ${MPDCLIENT_LIBRARIES}
)

target_compile_options(MPD-Presence PRIVATE ${MPDCLIENT_CFLAGS_OTHER})

if(nlohmann_json_FOUND)
    target_link_libraries(MPD-Presence PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_FOUND)
    target_include_directories(MPD-Presence PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
else()
    message(STATUS "nlohmann_json not found via package managers — looking in third_party/")
    target_include_directories(MPD-Presence PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
endif()

# ── Compiler warnings ──
target_compile_options(MPD-Presence PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Debug>:-g3 -fsanitize=address,undefined>
    $<$<CONFIG:Release>:-O2>
)
target_link_options(MPD-Presence PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ── Install ──
install(TARGETS MPD-Presence RUNTIME DESTINATION bin)
install(FILES MPD-Presence.conf.example
        DESTINATION share/MPD-Presence
        OPTIONAL)
