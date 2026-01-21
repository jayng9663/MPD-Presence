#include "album_art.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <vector>
#include <cctype>
#include <unordered_map>
#include <string>
#include "logging.hpp"

namespace {

	size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* out)
	{
		size_t totalSize = size * nmemb;
		out->append(static_cast<char*>(contents), totalSize);
		return totalSize;
	}

	// Optimized URL encoding
	std::string url_encode(const std::string& input) {
		static const char* hex = "0123456789ABCDEF";
		std::string encoded;
		encoded.reserve(input.size() * 3); // worst case

		for (unsigned char c : input) {
			if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
				encoded += c;
			} else {
				encoded += '%';
				encoded += hex[c >> 4];
				encoded += hex[c & 15];
			}
		}

		return encoded;
	}

	// Static curl handle for reuse
	static CURL* curl = nullptr;

	void init_curl() {
		if (!curl) {
			curl = curl_easy_init();
		}
	}

	std::string get_response(const std::string& url) {
		init_curl();

		std::string response;
		curl_easy_reset(curl);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "MPD-Presence");
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // Faster timeout

		LOG_DEBUG("Making request to: " << url);

		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			LOG_ERROR("cURL request failed for URL: " << url 
					<< " - Error: " << curl_easy_strerror(res));
			return {};
		}

		// Get response code for debugging
		long response_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		LOG_DEBUG("Response code: " << response_code);

		return response;
	}

	// Simple cache for search results
	static std::unordered_map<std::string, std::vector<std::pair<std::string, double>>> search_cache;

	// Cache for cover art existence checks
	static std::unordered_map<std::string, bool> cover_art_cache;

} // anonymous namespace

// MusicBrainz search with caching
std::vector<std::pair<std::string, double>> json_get_release_ids_search(
		const std::string& artist,
		const std::string& album,
		const std::string& date,
		double scoreThreshold)
{
	std::string cache_key = "mb:" + artist + ":" + album + ":" + date;

	// Check if result is cached
	auto cached = search_cache.find(cache_key);
	if (cached != search_cache.end()) {
		LOG_DEBUG("Using cached MusicBrainz results for: " << cache_key);
		return cached->second;
	}

	std::string url =
		"https://musicbrainz.org/ws/2/release/?query=artist:" +
		url_encode(artist) + "%20release:" +
		url_encode(album) + "%20date:" +
		url_encode(date) + "&fmt=json";

	std::string response = get_response(url);
	if (response.empty()) {
		LOG_ERROR("Empty response from MusicBrainz for: " << url);
		return {};
	}

	std::vector<std::pair<std::string, double>> releaseIds;

	try {
		auto root = nlohmann::json::parse(response);

		// Find the release with the highest score above the threshold
		double bestScore = -1.0;
		std::string bestId;

		if (root.contains("releases")) {
			for (const auto& r : root["releases"]) {
				// Safely check if score exists
				if (r.contains("score") && r.contains("id")) {
					double s = r["score"];
					if (s >= scoreThreshold && s > bestScore) {
						bestScore = s;
						bestId = r["id"];
					}
				}
			}

			if (bestScore >= scoreThreshold && !bestId.empty()) {
				releaseIds.emplace_back(bestId, bestScore);
				LOG_DEBUG("Found best match with ID: " << bestId 
						<< " and score: " << bestScore);
			} else {
				LOG_DEBUG("No valid matches found above threshold");
			}
		} else {
			LOG_DEBUG("No 'releases' field in response");
		}

	} catch (const std::exception& e) {
		LOG_ERROR("JSON parsing error: " << e.what());
	}

	// Cache result
	search_cache[cache_key] = releaseIds;
	return releaseIds;
}

std::vector<std::string> json_get_release_ids_fingerprint(
		int duration,
		const std::string& fingerprint,
		const std::string& acoustid_api)
{
	std::string url =
		"https://api.acoustid.org/v2/lookup?client=" + acoustid_api +
		"&meta=releaseids&duration=" + std::to_string(duration) +
		"&fingerprint=" + fingerprint;

	std::string response = get_response(url);
	if (response.empty()) {
		LOG_ERROR("Empty response from AcoustID for: " << url);
		return {};
	}

	std::vector<std::string> releaseIds;

	try {
		auto root = nlohmann::json::parse(response);

		// More robust parsing
		if (root.contains("results") && 
				root["results"].is_array() && 
				!root["results"].empty()) {

			const auto& result = root["results"][0];
			if (result.contains("releases") && 
					result["releases"].is_array()) {

				LOG_DEBUG("Found " << result["releases"].size() 
						<< " releases in AcoustID response");

				for (const auto& r : result["releases"]) {
					if (r.contains("id")) {
						releaseIds.push_back(r["id"]);
						LOG_DEBUG("Added release ID: " << r["id"]);
					}
				}
			} else {
				LOG_DEBUG("No 'releases' field in AcoustID result");
			}
		} else {
			LOG_DEBUG("No valid results found in AcoustID response");
		}

	} catch (const std::exception& e) {
		LOG_ERROR("JSON parsing error in AcoustID: " << e.what());
	}

	return releaseIds;
}

// Over Art Archive
bool cover_art_exists(const std::string& id)
{
	// Check cache first
	auto cached = cover_art_cache.find(id);
	if (cached != cover_art_cache.end()) {
		LOG_DEBUG("Using cached cover art check for ID: " << id 
				<< " (result: " << (cached->second ? "true" : "false") << ")");
		return cached->second;
	}

	std::string art_url = get_album_art_url(id);

	LOG_DEBUG("Checking cover art existence for ID: " << id 
			<< " at URL: " << art_url);

	CURL* curl = curl_easy_init();
	if (!curl) {
		cover_art_cache[id] = false;
		return false;
	}

	curl_easy_setopt(curl, CURLOPT_URL, art_url.c_str());
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "MPD-Presence");

	CURLcode res = curl_easy_perform(curl);
	long code = 0;
	if (res == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	}

	curl_easy_cleanup(curl);

	bool exists = (res == CURLE_OK && code == 200);
	cover_art_cache[id] = exists; // Cache result
	LOG_DEBUG("Cover art check for ID " << id 
			<< " returned: " << (exists ? "true" : "false"));
	return exists;
}

std::string get_album_art_url(const std::string& id)
{
	return "https://coverartarchive.org/release/" + id + "/front-500";
}

std::string get_release_page_url(const std::string& id)
{
	return "https://musicbrainz.org/release/" + id;
}

AlbumUrls get_album_urls_search(
		const std::string& artist,
		const std::string& album,
		const std::string& date,
		double score)
{
	LOG_DEBUG("Starting search for artist: " << artist 
			<< ", album: " << album << ", date: " << date);

	auto releases = json_get_release_ids_search(artist, album, date, score);

	LOG_DEBUG("Found " << releases.size() << " releases from MusicBrainz");

	AlbumUrls result = {};

	for (const auto& [id, _] : releases) {
		LOG_DEBUG("Checking cover art for release ID: " << id);
		if (cover_art_exists(id)) {
			result.cover_url = get_album_art_url(id);
			result.page_url = get_release_page_url(id);
			LOG_INFO("Found album art URL: " << result.cover_url);
			LOG_INFO("Found release page URL: " << result.page_url);
			return result;
		}
	}

	LOG_DEBUG("No valid cover art found for search query");
	return {};
}

AlbumUrls get_album_urls_fingerprint(
		int duration,
		const std::string& fingerprint,
		const std::string& acoustid_api)
{
	LOG_DEBUG("Starting fingerprint lookup with duration: " << duration 
			<< ", fingerprint: " << fingerprint.substr(0, 10) << "...");

	auto releases = json_get_release_ids_fingerprint(duration, fingerprint, acoustid_api);

	LOG_DEBUG("Found " << releases.size() << " releases from AcoustID");

	AlbumUrls result = {};

	for (const auto& id : releases) {
		LOG_DEBUG("Checking cover art for release ID: " << id);
		if (cover_art_exists(id)) {
			result.cover_url = get_album_art_url(id);
			result.page_url = get_release_page_url(id);
			LOG_INFO("Found album art URL: " << result.cover_url);
			LOG_INFO("Found release page URL: " << result.page_url);
			return result;
		}
	}

	LOG_DEBUG("No valid cover art found for fingerprint");
	return {};
}
