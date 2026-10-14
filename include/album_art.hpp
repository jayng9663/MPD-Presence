#pragma once

#include <string>
#include <vector>
#include <utility>

// Album Cover + Page url
struct AlbumUrls {
	std::string cover_url;
	std::string page_url;
};

AlbumUrls get_album_urls_search(
		const std::string& artist,
		const std::string& album,
		const std::string& date,
		double score);

AlbumUrls get_album_urls_fingerprint(
		int duration,
		const std::string& fingerprint,
		const std::string& acoustid_api);

// Uses MusicBrainz IDs from the file's tags (MUSICBRAINZ_ALBUMID /
// MUSICBRAINZ_RELEASEGROUPID); falls back to the release group's art when the
// release itself has none. Either ID may be empty.
AlbumUrls get_album_urls_mbid(
		const std::string& releaseId,
		const std::string& releaseGroupId);

// MusicBrainz search
std::vector<std::pair<std::string, double>>
json_get_release_ids_search(
		const std::string& artist,
		const std::string& album,
		const std::string& date,
		double score = 90.0);

// AcoustID fingerprint
std::vector<std::string>
json_get_release_ids_fingerprint(
		int duration,
		const std::string& fingerprint,
		const std::string& acoustid_api);

// -- Pure JSON parsers (no network I/O) --
//
// These contain the actual parsing logic used by json_get_release_ids_search()
// / json_get_release_ids_fingerprint() above, split out so they can be unit
// tested directly against captured real API responses without making any
// network calls.

// Parses a MusicBrainz /ws/2/release/ search response and returns the
// single best-scoring release at or above scoreThreshold (empty if none).
std::vector<std::pair<std::string, double>>
parse_release_ids_search(const std::string& json, double scoreThreshold);

// Parses an AcoustID /v2/lookup response and returns the release IDs of
// the first (highest-confidence) matching recording, in response order.
std::vector<std::string>
parse_release_ids_fingerprint(const std::string& json);

// Cover Art Archive / MusicBrainz URL helpers
bool cover_art_exists(const std::string& id);
bool image_exists(const std::string& url);
std::string get_album_art_url(const std::string& id);
std::string get_release_page_url(const std::string& id);
std::string get_release_group_art_url(const std::string& id);
std::string get_release_group_page_url(const std::string& id);
