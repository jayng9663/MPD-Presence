#include "doctest.h"
#include "album_art.hpp"
#include "fixture_loader.hpp"

// These tests exercise the real JSON-parsing logic (parse_release_ids_search /
// parse_release_ids_fingerprint) against *real* AcoustID / MusicBrainz
// responses captured for a genuine local track (see tests/fixtures/
// higurashi_track.hpp for details + provenance). No network access happens
// here -- the responses were saved to disk ahead of time, so these tests
// run fully offline and deterministically.

TEST_CASE("parse_release_ids_fingerprint: real AcoustID lookup response") {
	auto json = read_fixture("acoustid_lookup_higurashi.json");
	auto ids = parse_release_ids_fingerprint(json);

	// AcoustID's top (highest-confidence) match for this fingerprint listed
	// 8 release IDs, in this order.
	REQUIRE(ids.size() == 8);
	CHECK(ids[0] == "fb267625-79d7-481a-ac6b-fcc9a10dd397");
	CHECK(ids[1] == "0b9c7235-a4f2-455c-a9e1-a879c79c3105");
	CHECK(ids[4] == "8682a143-8583-4918-970f-a67d63a246bc");
	CHECK(ids[7] == "f270992e-efcc-4976-8679-fb8c60213316");
}

TEST_CASE("parse_release_ids_fingerprint: empty/garbage JSON yields no IDs") {
	CHECK(parse_release_ids_fingerprint("").empty());
	CHECK(parse_release_ids_fingerprint("not json").empty());
	CHECK(parse_release_ids_fingerprint(R"({"status":"ok","results":[]})").empty());
}

TEST_CASE("parse_release_ids_search: real MusicBrainz search response, threshold 100") {
	auto json = read_fixture("musicbrainz_search_higurashi.json");
	auto matches = parse_release_ids_search(json, 100.0);

	// KNOWN LIMITATION (documented, not fixed here): the real response
	// contains four releases tied at score 100 (fb267625..., 8682a143...,
	// 6ae55a42..., f270992e...). json_get_release_ids_search() only ever
	// keeps the single first-seen best match, so this asserts the *current*
	// behaviour rather than the ideal one. See the fingerprint test above
	// for how the richer multi-candidate AcoustID path is able to skip a
	// release with no cover art (fb267625... returns 404 from the Cover Art
	// Archive) in favour of one that has art (8682a143...) -- the search
	// path cannot currently do the same because it discards every match but
	// the first tie.
	REQUIRE(matches.size() == 1);
	CHECK(matches[0].first == "fb267625-79d7-481a-ac6b-fcc9a10dd397");
	CHECK(matches[0].second == doctest::Approx(100.0));
}

TEST_CASE("parse_release_ids_search: threshold above every candidate yields no match") {
	auto json = read_fixture("musicbrainz_search_higurashi.json");
	auto matches = parse_release_ids_search(json, 101.0);
	CHECK(matches.empty());
}

TEST_CASE("parse_release_ids_search: lower threshold still only returns the single best") {
	auto json = read_fixture("musicbrainz_search_higurashi.json");
	// Real fixture has further ties at score 47 below the top score-100
	// matches; the function should still only surface the best one.
	auto matches = parse_release_ids_search(json, 40.0);
	REQUIRE(matches.size() == 1);
	CHECK(matches[0].second == doctest::Approx(100.0));
}

TEST_CASE("parse_release_ids_search: missing 'releases' field is handled gracefully") {
	CHECK(parse_release_ids_search(R"({"count":0,"offset":0})", 50.0).empty());
}
