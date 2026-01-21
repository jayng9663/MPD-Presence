cmake_minimum_required(VERSION 3.16)
project(MPD-Presence)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find MPD client library
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
pkg_check_modules(MPD REQUIRED libmpdclient)

# Add the discord-presence subdirectory properly
add_subdirectory(lib/discord-presence)

# Create your executable
add_executable(MPD-Presence
	src/main.cpp
	src/rpc.cpp
	src/mpd.cpp
	src/config.cpp
	src/album_art.cpp
)

# Link with the discord-rpc library
target_link_libraries(MPD-Presence PRIVATE
	discord-rpc
	${MPD_LIBRARIES}
	CURL::libcurl
)

target_include_directories(MPD-Presence PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/discord-presence/include
	 ${CMAKE_CURRENT_SOURCE_DIR}/src
	 ${MPD_INCLUDE_DIRS}
	 ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/include
)

if(UNIX AND NOT APPLE)
    target_link_libraries(MPD-Presence PRIVATE pthread)
endif()
