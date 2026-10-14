#include "doctest.h"
#include "album_art.hpp"
#include "higurashi_track.hpp"

// Opt-in integration/smoke test: actually calls the real AcoustID,
// MusicBrainz, and Cover Art Archive APIs using the genuine fingerprint and
// metadata cloned from a real local track (see fixtures/higurashi_track.hpp
// for provenance -- only tags + fingerprint were extracted, never the audio
// file itself).
//
// Disabled by default (network access, external services, not deterministic
// CI material). Enable with:
//   cmake -DMPD_PRESENCE_ENABLE_LIVE_TESTS=ON ..
//
// This exercises the exact same production code path main.cpp uses when a
// track changes: get_album_urls_fingerprint() / get_album_urls_search().

#ifdef MPD_PRESENCE_ENABLE_LIVE_TESTS

TEST_CASE("LIVE: get_album_urls_fingerprint resolves real cover art end-to-end") {
	using namespace fixtures::higurashi;

	// "2jFwlOUpO2" is the public demo AcoustID client key already shipped in
	// include/config.hpp.example -- not a secret.
	auto urls = get_album_urls_fingerprint(DURATION_SECONDS, FINGERPRINT, "2jFwlOUpO2");

	CHECK_FALSE(urls.cover_url.empty());
	CHECK_FALSE(urls.page_url.empty());
	if (!urls.cover_url.empty())
		CHECK(urls.cover_url.find("coverartarchive.org") != std::string::npos);
	if (!urls.page_url.empty())
		CHECK(urls.page_url.find("musicbrainz.org/release/") != std::string::npos);
}

TEST_CASE("LIVE: get_album_urls_search resolves the same track via text search") {
	using namespace fixtures::higurashi;

	auto urls = get_album_urls_search(ARTIST, ALBUM, DATE, 100.0);

	// Documents the known single-best-match limitation: this specific track
	// has several score-100 ties and the first one happens to lack cover
	// art, so the text-search path is expected to come back empty even
	// though the fingerprint path above succeeds. If this ever starts
	// passing, the underlying multi-candidate fix landed -- update this
	// test to assert success instead.
	CHECK(urls.cover_url.empty());
}

#endif // MPD_PRESENCE_ENABLE_LIVE_TESTS
