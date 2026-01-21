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

// Cover Art Archive helpers
bool cover_art_exists(const std::string& id);
std::string get_album_art_url(const std::string& id);
