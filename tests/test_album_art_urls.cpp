#include "doctest.h"
#include "album_art.hpp"

TEST_CASE("get_album_art_url: builds Cover Art Archive front-500 URL") {
	auto url = get_album_art_url("fb267625-79d7-481a-ac6b-fcc9a10dd397");
	CHECK(url == "https://coverartarchive.org/release/fb267625-79d7-481a-ac6b-fcc9a10dd397/front-500");
}

TEST_CASE("get_release_page_url: builds MusicBrainz release page URL") {
	auto url = get_release_page_url("fb267625-79d7-481a-ac6b-fcc9a10dd397");
	CHECK(url == "https://musicbrainz.org/release/fb267625-79d7-481a-ac6b-fcc9a10dd397");
}
