// C++ Standard Library Headers
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <exception>

// POSIX / Unix System Headers
#include <csignal>
#include <sys/stat.h>

// External Library Headers
#include <curl/curl.h>
#include <sqlite3.h>
#include <mpd/client.h>
#include <chromaprint.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libavutil/log.h>
}

// Project Headers
#include "./config.h"
#include "../include/discord_rpc.h"
#include "../include/json.hpp"

Config g_cfg;

// Easier to read and write JSON using nlohmann::json
using json = nlohmann::json;

// Database path for caching fingerprints and album art
const std::string DB_PATH = "/tmp/mpdrpc_cache.db";

// Global database connection pointer
sqlite3* db = nullptr;

// Current song information
std::string currentSong;
std::string currentArtURL;

// Album art fetching thread control
bool artThreadRunning = false;
std::mutex artMutex;
std::thread artThread;

// Atomic flag for main loop control
std::atomic<bool> running(true);

struct PresenceState {
	std::string filePath;
	bool isPaused = true;
	int elapsed = -1;
	std::time_t currentTime;
	int64_t initialStartTimestamp = 0;
	int64_t lastInitialStartTimestamp = 0;
	std::string lastAlbumArtURL;
};

static PresenceState lastPresence;

// Signal handler for graceful shutdown on Ctrl+C
void signal_handler(int signum) {
	if (signum == SIGINT) {
		running = false;
	}
}

// RAII wrapper for database connection
class DatabaseGuard {
	private:
		sqlite3* db_;
	public:
		explicit DatabaseGuard(sqlite3* db) : db_(db) {}
		~DatabaseGuard() { 
			if (db_) {
				sqlite3_close(db_);
				db_ = nullptr;
			}
		}
		DatabaseGuard(const DatabaseGuard&) = delete;
		DatabaseGuard& operator=(const DatabaseGuard&) = delete;
};

// Initialize SQLite database and create required tables
void initDatabase() {
	// Ensure directory exists
	mkdir("/tmp", 0755);

	if (sqlite3_open(DB_PATH.c_str(), &db)) {
		std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
		sqlite3_close(db);
		db = nullptr;
		return;
	}

	const char* createFingerprintTable =
		"CREATE TABLE IF NOT EXISTS fingerprint_cache "
		"(filepath TEXT PRIMARY KEY, release_ids TEXT);";

	const char* createAlbumArtTable =
		"CREATE TABLE IF NOT EXISTS albumart_cache "
		"(filepath TEXT PRIMARY KEY, url TEXT);";

	if (sqlite3_exec(db, createAlbumArtTable, nullptr, nullptr, nullptr) != SQLITE_OK) {
		std::cerr << "Failed to create albumart_cache table: " << sqlite3_errmsg(db) << std::endl;
	}

	if (sqlite3_exec(db, createFingerprintTable, nullptr, nullptr, nullptr) != SQLITE_OK) {
		std::cerr << "Failed to create fingerprint_cache table: " << sqlite3_errmsg(db) << std::endl;
	}
}

// Retrieve release IDs from database for a given file path
std::vector<std::string> getReleaseIDsFromDB(const std::string& filePath) {
	std::vector<std::string> releaseIDs;
	if (!db) return releaseIDs;

	const char* query = "SELECT release_ids FROM fingerprint_cache WHERE filepath = ?;";
	sqlite3_stmt* stmt = nullptr;

	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

		if (sqlite3_step(stmt) == SQLITE_ROW) {
			std::string releaseJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
			try {
				releaseIDs = json::parse(releaseJson).get<std::vector<std::string>>();
			} catch (const std::exception& e) {
				std::cerr << "Failed to parse release IDs JSON: " << e.what() << std::endl;
			}
		}
	} else {
		std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
	}

	sqlite3_finalize(stmt);
	return releaseIDs;
}

// Retrieve album art URL from database for a given file path
std::string getAlbumArtURLFromDB(const std::string& filePath) {
	std::string url;
	if (!db) return url;

	const char* query = "SELECT url FROM albumart_cache WHERE filepath = ?;";
	sqlite3_stmt* stmt = nullptr;

	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

		if (sqlite3_step(stmt) == SQLITE_ROW) {
			url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
		}
	} else {
		std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
	}

	sqlite3_finalize(stmt);
	return url;
}

// Save release IDs to database for a given file path
void saveFingerprintToDB(const std::string& filePath, const std::vector<std::string>& releaseIDs) {
	if (!db) return;

	std::string releaseJson = json(releaseIDs).dump();
	const char* query = "INSERT OR REPLACE INTO fingerprint_cache (filepath, release_ids) VALUES (?, ?);";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, releaseJson.c_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) != SQLITE_DONE) {
			std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
		}
	} else {
		std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
	}
	sqlite3_finalize(stmt);
}

// Save album art URL to database for a given file path
void saveAlbumArtToDB(const std::string& filePath, const std::string& url) {
	if (!db) return;

	const char* query = "INSERT OR REPLACE INTO albumart_cache (filepath, url) VALUES (?, ?);";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
		if (sqlite3_step(stmt) != SQLITE_DONE) {
			std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
		}
	} else {
		std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
	}
	sqlite3_finalize(stmt);
}

// Close database connection
void closeDatabase() {
	if (db) {
		sqlite3_close(db);
		db = nullptr;
	}
}

// Structure to hold HTTP response data
struct HttpResponse {
	int statusCode;
	std::string body;
};

// Callback function for curl to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t totalSize = size * nmemb;
	if (userp) {
		static_cast<std::string*>(userp)->append(static_cast<char*>(contents), totalSize);
	}
	return totalSize;
}

// Perform HTTP POST request
std::string httpPost(const std::string& url, const std::string& postData) {
	CURL* curl = curl_easy_init();
	std::string response;

	if (!curl) {
		std::cerr << "Failed to initialize curl" << std::endl;
		return "";
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.size());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0,MPDRPC/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // Add timeout
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // Add connect timeout

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
	}

	curl_easy_cleanup(curl);
	return response;
}

// Perform HTTP GET request
HttpResponse httpGet(const std::string& url) {
	CURL* curl = curl_easy_init();
	std::string response;
	long response_code = 0;

	if (!curl) {
		std::cerr << "Failed to initialize curl" << std::endl;
		return { -1, "" };
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0,MPDRPC");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // Add timeout
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // Add connect timeout

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	} else {
		std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
	}

	curl_easy_cleanup(curl);
	return { static_cast<int>(response_code), response };
}

// Audio data reader for Chromaprint
class AudioReader {
	private:
		AVFormatContext* format_ctx_ = nullptr;
		AVCodecContext* codec_ctx_ = nullptr;
		SwrContext* swr_ctx_ = nullptr;
		int stream_index_ = -1;
		bool initialized_ = false;

	public:
		~AudioReader() {
			cleanup();
		}

		bool initialize(const std::string& filepath) {
			avformat_network_init();

			if (avformat_open_input(&format_ctx_, filepath.c_str(), nullptr, nullptr) < 0)
				return false;

			if (avformat_find_stream_info(format_ctx_, nullptr) < 0)
				return false;

			stream_index_ = av_find_best_stream(
					format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0
					);
			if (stream_index_ < 0)
				return false;

			AVStream* stream = format_ctx_->streams[stream_index_];

			const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
			if (!codec)
				return false;

			codec_ctx_ = avcodec_alloc_context3(codec);
			if (!codec_ctx_)
				return false;

			if (avcodec_parameters_to_context(codec_ctx_, stream->codecpar) < 0)
				return false;

			if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
				return false;

			// SWR SETUP
			AVChannelLayout out_layout;
			av_channel_layout_default(&out_layout, 1); // mono

			swr_ctx_ = swr_alloc();

			swr_alloc_set_opts2(
					&swr_ctx_,
					&out_layout,
					AV_SAMPLE_FMT_S16,
					codec_ctx_->sample_rate,
					&codec_ctx_->ch_layout,
					codec_ctx_->sample_fmt,
					codec_ctx_->sample_rate,
					0,
					nullptr
					);

			av_channel_layout_uninit(&out_layout);

			if (!swr_ctx_ || swr_init(swr_ctx_) < 0)
				return false;

			initialized_ = true;
			return true;
		}

		bool readSamples(int16_t* buffer, int buffer_size) {
			if (!initialized_) return false;

			AVPacket* packet = av_packet_alloc();
			AVFrame* frame = av_frame_alloc();

			int samples_written = 0;

			while (av_read_frame(format_ctx_, packet) >= 0) {
				if (packet->stream_index != stream_index_) {
					av_packet_unref(packet);
					continue;
				}

				if (avcodec_send_packet(codec_ctx_, packet) < 0) {
					av_packet_unref(packet);
					continue;
				}

				while (avcodec_receive_frame(codec_ctx_, frame) >= 0) {
					uint8_t* out_buf[1] = {
						reinterpret_cast<uint8_t*>(buffer + samples_written)
					};

					int out_samples = swr_convert(
							swr_ctx_,
							out_buf,
							buffer_size - samples_written,
							(const uint8_t**)frame->data,
							frame->nb_samples
							);

					if (out_samples > 0)
						samples_written += out_samples;

					if (samples_written >= buffer_size) {
						av_frame_unref(frame);
						av_packet_unref(packet);
						av_frame_free(&frame);
						av_packet_free(&packet);
						return true;
					}
				}

				av_packet_unref(packet);
			}

			av_frame_free(&frame);
			av_packet_free(&packet);

			return samples_written > 0;
		}

		int getSampleRate() const {
			return codec_ctx_ ? codec_ctx_->sample_rate : 0;
		}

		int getChannels() const {
			return 1; // ALWAYS mono for Chromaprint
		}

		void cleanup() {
			if (swr_ctx_) {
				swr_free(&swr_ctx_);
			}
			if (codec_ctx_) {
				avcodec_free_context(&codec_ctx_);
			}
			if (format_ctx_) {
				avformat_close_input(&format_ctx_);
			}
			avformat_network_deinit();
			initialized_ = false;
		}
};

// Get MusicBrainz release IDs using AcoustID API
std::vector<std::string> getMusicBrainzReleaseIDs(const std::string& filePath, int duration) {
	std::vector<std::string> releaseIDs;

	// Check database first for cached results
	releaseIDs = getReleaseIDsFromDB(filePath);
	if (!releaseIDs.empty()) {
		std::cout << "\n=== Found cached release IDs in database ===\n";
		for (const auto& id : releaseIDs) {
			std::cout << id << "\n";
		}
		return releaseIDs;
	}

	// Create Chromaprint context
	ChromaprintContext* ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
	if (!ctx) {
		std::cerr << "Failed to create chromaprint context" << std::endl;
		return releaseIDs;
	}

	AudioReader reader;
	if (!reader.initialize(filePath)) {
		std::cerr << "Failed to initialize audio reader for: " << filePath << std::endl;
		chromaprint_free(ctx);
		return releaseIDs;
	}

	const int sampleRate = reader.getSampleRate();
	// Use frame->channels instead of codec_ctx_->channels
	int channels = 1; // mono since Acoust work in mono

	if (sampleRate <= 0) {
		std::cerr << "Invalid audio format for: " << filePath << std::endl;
		chromaprint_free(ctx);
		return releaseIDs;
	}

	// Start Chromaprint fingerprinting
	if (!chromaprint_start(ctx, sampleRate, channels)) {
		std::cerr << "Failed to start Chromaprint" << std::endl;
		chromaprint_free(ctx);
		return releaseIDs;
	}

	// Read audio data and feed into Chromaprint
	const int bufferSize = 44100;
	std::vector<int16_t> buffer(bufferSize);

	while (true) {
		if (!reader.readSamples(buffer.data(), bufferSize)) {
			break;
		}

		if (!chromaprint_feed(ctx, buffer.data(), bufferSize)) {
			std::cerr << "Chromaprint feed failed" << std::endl;
			chromaprint_free(ctx);
			return releaseIDs;
		}
	}

	// Finalize fingerprint calculation
	if (!chromaprint_finish(ctx)) {
		std::cerr << "Chromaprint finish failed" << std::endl;
		chromaprint_free(ctx);
		return releaseIDs;
	}

	// Get fingerprint string
	char* raw_fp = nullptr;
	if (!chromaprint_get_fingerprint(ctx, &raw_fp)) {
		std::cerr << "Failed to get fingerprint" << std::endl;
		chromaprint_free(ctx);
		return releaseIDs;
	}

	std::string fingerprint(raw_fp);
	chromaprint_dealloc(raw_fp);
	chromaprint_free(ctx);

	// Build API request
	std::string apiURL = "https://api.acoustid.org/v2/lookup";
	std::string postData = "client=" + g_cfg.acoustid_api_key +
		"&meta=releaseids" +
		"&fingerprint=" + curl_easy_escape(nullptr, fingerprint.c_str(), 0) +
		"&duration=" + std::to_string(duration);

	std::string jsonResponse = httpPost(apiURL, postData);

	if (jsonResponse.empty()) {
		std::cerr << "AcoustID API returned empty response" << std::endl;
		return releaseIDs;
	}

	std::cout << "\n=== AcoustID JSON Response ===\n" << jsonResponse << "\n";

	// Parse JSON response
	try {
		auto data = json::parse(jsonResponse);
		if (data.contains("results")) {
			for (const auto& result : data["results"]) {
				if (result.contains("releases")) {
					for (const auto& release : result["releases"]) {
						if (release.contains("id"))
							releaseIDs.push_back(release["id"].get<std::string>());
					}
				}
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "Failed to parse AcoustID response: " << e.what() << std::endl;
	}

	// Save to database instead of cache
	saveFingerprintToDB(filePath, releaseIDs);
	return releaseIDs;
}

// Get album art URL for a given file path
std::string getAlbumArtURL(const std::string& filePath, int duration) {
	// Check database first for cached results
	std::string url = getAlbumArtURLFromDB(filePath);
	if (!url.empty()) {
		std::cout << "----Found cached album art in database: " << url << " ----" << std::endl;
		return url;
	}

	std::vector<std::string> releaseIDs = getMusicBrainzReleaseIDs(filePath, duration);
	if (releaseIDs.empty()) return "mpd";

	for (const auto& releaseID : releaseIDs) {
		std::string metadataURL = "https://coverartarchive.org/release/" + releaseID;
		std::cout << "\n>> Checking Cover Art: " << metadataURL << "\n";

		HttpResponse result = httpGet(metadataURL);
		if (result.statusCode == 200 && !result.body.empty()) {
			try {
				auto data = json::parse(result.body);
				if (data.contains("images") && data["images"].is_array()) {
					for (const auto& image : data["images"]) {
						if (image.contains("types") && image["types"].is_array()) {
							for (const auto& type : image["types"]) {
								if (type == "Front" && image.contains("thumbnails") && image["thumbnails"].contains("large")) {
									std::string imageURL = image["thumbnails"]["large"].get<std::string>();
									std::cout << " Found Front Cover (Large): " << imageURL << "\n";
									saveAlbumArtToDB(filePath, imageURL);
									return imageURL;
								}
							}
						}
					}
				}
			} catch (const std::exception& e) {
				std::cerr << "Failed to parse cover art JSON: " << e.what() << std::endl;
			}
		}
	}
	return "mpd";
}

// Static variables for tracking current state
static std::string currentTrack;
static int64_t initialStartTimestamp = 0;
static bool wasPaused = true;

// Update Discord Rich Presence with current track information
void updateDiscordPresence(const std::string& title, const std::string& artist, const std::string& album,
		const std::string& filePath, int elapsed, int total, bool isPaused,
		int currentId, int playlistSize) {
	DiscordRichPresence discordPresence;
	memset(&discordPresence, 0, sizeof(discordPresence));
	std::time_t currentTime = std::time(nullptr);

	// Update initial timestamp when track changes or resumes
	if (std::abs(initialStartTimestamp - (currentTime - elapsed)) > 1) {
		initialStartTimestamp = currentTime - elapsed;
	}

	// Check if track has changed or resumed from pause
	if (currentTrack != filePath || (wasPaused && !isPaused)) {
		initialStartTimestamp = currentTime - elapsed;
		currentTrack = filePath;

		// Wait for previous thread to finish and start new one
		if (artThread.joinable()) artThread.join();
		artThreadRunning = true;
		artThread = std::thread([filePath, total]() {
				try {
				std::string artURL = getAlbumArtURL(filePath, total);
				{
				std::lock_guard<std::mutex> lock(artMutex);
				currentArtURL = artURL;
				}
				} catch (const std::exception& e) {
				std::cerr << "Error in album art thread: " << e.what() << std::endl;
				}
				artThreadRunning = false;
				});
	}

	// Get album art URL with thread safety
	std::string albumArtURL;
	{
		std::lock_guard<std::mutex> lock(artMutex);
		albumArtURL = currentArtURL.empty() ? "mpd" : currentArtURL;
	}

	// Set start timestamp (0 for paused)
	discordPresence.startTimestamp = isPaused ? 0 : initialStartTimestamp;

	// Build presence details
	std::string detailsStr = "by " + artist + " | MPDRPC by jayng9663";
	discordPresence.state = (isPaused ? "Paused" : detailsStr.c_str());
	discordPresence.details = title.c_str();
	discordPresence.largeImageKey = albumArtURL.c_str();
	discordPresence.largeImageText = album.c_str();
	discordPresence.smallImageKey = isPaused ? "pause" : "play";
	discordPresence.smallImageText = isPaused ? "Paused" : "Playing";

	// Debug output
	std::cout << "\n=== Discord Presence Update ===\n";
	std::cout << "Title      : " << title << "\n";
	std::cout << "Artist     : " << artist << "\n";
	std::cout << "Album      : " << album << "\n";
	std::cout << "Paused     : " << (isPaused ? "Yes" : "No") << "\n";
	std::cout << "UNIX Current Time: " << currentTime << "\n";
	std::cout << "UNIX Initial Time: " << initialStartTimestamp << "\n";
	std::cout << "Elapsed (sec): " << elapsed << "\n";
	std::cout << "Album Art  : " << albumArtURL << "\n";
	std::cout << "===============================\n";

	// Update Discord presence
	Discord_UpdatePresence(&discordPresence);
	wasPaused = isPaused;
}

bool presenceChanged(const PresenceState& now) {
	if (now.filePath != lastPresence.filePath)
		return true;

	if (now.isPaused != lastPresence.isPaused)
		return true;

	if (now.initialStartTimestamp != lastPresence.initialStartTimestamp)
		return true;

	if (now.lastAlbumArtURL != lastPresence.lastAlbumArtURL)
		return true;

	return false;
}

// Fetch current MPD information and update Discord presence
void fetchMPDInfo() {
	struct mpd_connection* conn = mpd_connection_new(g_cfg.mpd_host.c_str(), g_cfg.mpd_port, 0);
	if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		std::cerr << "Could not connect to MPD" << std::endl;
		if (conn) mpd_connection_free(conn);
		return;
	}

	// Authenticate with password if set
	if (!g_cfg.mpd_password.empty()) {
		if (!mpd_run_password(conn, g_cfg.mpd_password.c_str())) {
			std::cerr << "Failed to authenticate with MPD\n";
			mpd_connection_free(conn);
			return;
		}
	}

	// Get current status
	struct mpd_status* status = mpd_run_status(conn);
	if (!status) {
		mpd_connection_free(conn);
		return;
	}

	bool isPaused = mpd_status_get_state(status) == MPD_STATE_PAUSE;
	int currentId = mpd_status_get_song_pos(status);
	int playlistSize = mpd_status_get_queue_length(status);

	// Process play/pause/stop states
	mpd_state state = mpd_status_get_state(status);
	if (state == MPD_STATE_PLAY || isPaused) {
		struct mpd_song* song = mpd_run_current_song(conn);
		if (song) {
			std::string title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0) ? 
				mpd_song_get_tag(song, MPD_TAG_TITLE, 0) : "Unknown Title";
			std::string artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0) ? 
				mpd_song_get_tag(song, MPD_TAG_ARTIST, 0) : "Unknown Artist";
			std::string album = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0) ? 
				mpd_song_get_tag(song, MPD_TAG_ALBUM, 0) : "Unknown Album";
			std::string filePath = g_cfg.mpd_musicfolder_root + std::string(mpd_song_get_uri(song));
			int elapsed = mpd_status_get_elapsed_time(status);
			int total = mpd_status_get_total_time(status);

			PresenceState now;
			now.filePath = filePath;
			now.isPaused = isPaused;
			now.elapsed = elapsed;
			now.currentTime = std::time(nullptr);

			now.initialStartTimestamp = now.currentTime - elapsed;
			now.lastInitialStartTimestamp = lastPresence.initialStartTimestamp;

			{
				std::lock_guard<std::mutex> lock(artMutex);
				now.lastAlbumArtURL = currentArtURL.empty() ? "mpd" : currentArtURL;
			}

			if (presenceChanged(now)) {
				updateDiscordPresence(
						title,
						artist,
						album,
						filePath,
						elapsed,
						total,
						isPaused,
						currentId,
						playlistSize
						);
				lastPresence = now;
			}

			mpd_song_free(song);
		}
	} else if (state == MPD_STATE_STOP) {
		if (!lastPresence.filePath.empty()) {
			Discord_ClearPresence();
			lastPresence = {};
		}
	}

	mpd_status_free(status);
	mpd_connection_free(conn);
	std::this_thread::sleep_for(std::chrono::seconds(1));
}

std::string ensureTrailingSlash(const std::string& path) {
	if (path.empty()) return "./";
	if (path.back() == '/') return path; // already ends with /
	return path + "/";
}

// Main program entry point
int main() {
	int count = 0;

	av_log_set_level(AV_LOG_ERROR);

	signal(SIGINT, signal_handler);

	g_cfg = load_config("setting.conf");
	g_cfg.mpd_musicfolder_root = ensureTrailingSlash(g_cfg.mpd_musicfolder_root);

	std::cout << "MPD root: " << g_cfg.mpd_musicfolder_root << "\n";

	initDatabase();
	std::cout << "Starting MPD Discord Rich Presence..." << std::endl;

	// Initialize Discord RPC
	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));
	Discord_Initialize(g_cfg.discord_app_id.c_str(), &handlers, 1, nullptr);

	// Main loop
	while (running) {
		//std::cout << "\n===== Update Cycle: " << count++ << " =====" << std::endl;
		fetchMPDInfo();
	}

	// Cleanup
	if (artThread.joinable()) {
		artThread.join();
	}
	closeDatabase();
	Discord_Shutdown();

	std::cout << "Program exited cleanly.\n";

	return 0;
}

