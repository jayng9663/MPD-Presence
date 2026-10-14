# Unit test suite for MPD-Presence.
#
# Builds a single test binary (doctest, vendored single-header) that links
# against the same source files as the main executable (excluding main.cpp's
# entry point) so tests exercise real production code, not reimplementations.
#
# Network-dependent "live" tests that exercise the real AcoustID/MusicBrainz/
# Cover Art Archive APIs are opt-in via -DMPD_PRESENCE_ENABLE_LIVE_TESTS=ON
# (off by default so `ctest` works offline/in CI).

option(MPD_PRESENCE_ENABLE_LIVE_TESTS
    "Also run tests that hit the real AcoustID/MusicBrainz/CAA network APIs"
    OFF)

add_executable(mpd-presence-tests
    test_main.cpp
    test_util.cpp
    test_album_art_urls.cpp
    test_album_art_parsing.cpp
    test_live_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/src/util.cpp
    ${CMAKE_SOURCE_DIR}/src/album_art.cpp
)

target_include_directories(mpd-presence-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/cpp-logger
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)

target_link_libraries(mpd-presence-tests PRIVATE CURL::libcurl)

if(nlohmann_json_FOUND)
    target_link_libraries(mpd-presence-tests PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_FOUND)
    target_include_directories(mpd-presence-tests PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
else()
    target_include_directories(mpd-presence-tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
endif()

target_compile_definitions(mpd-presence-tests PRIVATE
    MPD_PRESENCE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    $<$<BOOL:${MPD_PRESENCE_ENABLE_LIVE_TESTS}>:MPD_PRESENCE_ENABLE_LIVE_TESTS>
)

target_compile_options(mpd-presence-tests PRIVATE -Wall -Wextra)

include(CTest)
add_test(NAME mpd-presence-tests COMMAND mpd-presence-tests)
