cmake_minimum_required(VERSION 3.16)
project(mpdrpc)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find MPD client library
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPD REQUIRED libmpdclient)

# Add the discord-presence subdirectory properly
add_subdirectory(lib/discord-presence)

# Create your executable
add_executable(mpdrpc 
	src/main.cpp
	src/rpc.cpp
	src/mpd.cpp
	src/config.cpp
)

# Link with the discord-rpc library
target_link_libraries(mpdrpc PRIVATE
	discord-rpc
	${MPD_LIBRARIES}
)

target_include_directories(mpdrpc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/discord-presence/include
	 ${CMAKE_CURRENT_SOURCE_DIR}/src
	 ${MPD_INCLUDE_DIRS}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(mpdrpc PRIVATE pthread)
endif()
