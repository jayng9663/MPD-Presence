#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>

#include "config.hpp"
#include "rpc.hpp"
#include "mpd.hpp"
#include "album_art.hpp"
#include "logger.hpp"
#include "util.hpp"

std::atomic<bool> keepRunning(true);
std::atomic<int>  stopSignal(0);

std::vector<std::string> artMethods;

// Only async-signal-safe work here: the logger takes a mutex, so log after the loop exits
void signalHandler(int signum) {
	stopSignal  = signum;
	keepRunning = false;
}

int main(int argc, char* argv[]) {
	std::signal(SIGINT,  signalHandler);
	std::signal(SIGTERM, signalHandler);

	bool verbose = false;
	std::string logFile = "mpd-presence.log";
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--verbose" || arg == "-v")   verbose = true;
		else if (arg == "--help" || arg == "-h") {
			std::cout <<
				"Usage: MPD-Presence [OPTIONS]\n\n"
				"Options:\n"
				"  -v, --verbose  Enable DEBUG-level logging\n"
				"  -h, --help     Show this message\n\n";
			return 0;
		}
	}

	if (verbose) Logger::get().setLevel(LogLevel::DEBUG);
	if (!logFile.empty() && !Logger::get().setFile(logFile.c_str()))
		LOG_WARN("Could not open log file: " << logFile);

	// Album art method order
	artMethods = util::split_trim(std::string(config::ALBUM_ART_METHOD_ORDER), ',');
	if (artMethods.empty()) artMethods = {"tag", "fingerprint", "search"};

	// Initial MPD fetch so we have a valid state before RPC init
	fetchMPDInfo();

	rpc_setup();
	rpc_load_button_settings();
	rpc_initialize();

	int     lastSongID          = -1;
	bool    lastPaused          = false;
	int64_t lastElapsed         = 0;
	int64_t lastPollTime        = 0;
	bool    lastWasIdle         = true;

	const std::vector<std::string> ignoreList =
		util::split_trim(std::string(config::MPD_IGNORE), ',');
	const std::string musicFolder(config::MUSIC_FOLDER);

	while (keepRunning) {
		fetchMPDInfo();

		const std::string title   = getMPDTitle();
		const std::string album   = getMPDAlbum();
		const std::string artist  = getMPDArtist();
		const std::string firstArtist = getMPDFirstArtist();
		const std::string date    = getMPDDate();
		const int         songID  = getMPDSongID();
		const bool        paused  = getMPDIsPaused();
		const int64_t     elapsed = getMPDElapsed();
		const int64_t     total   = getMPDTotal();

		const bool isIdle = !getMPDIsValid()
			|| title  == "Unknown Title"
			|| artist == "Unknown Artist"
			|| [&]() {
				const std::string& fp = getMPDFilePath();
				for (const auto& p : ignoreList) {
					// Strip leading slash so "/ASMR" matches "music_folder/ASMR/..."
					const std::string rel = (!p.empty() && p[0] == '/') ? p.substr(1) : p;
					if (fp.find(musicFolder + rel) == 0) return true;
				}
				return false;
			}();

		const bool trackChanged      = (songID != lastSongID);
		const bool pauseStateChanged = (paused != lastPaused);
		const bool idleStateChanged  = (isIdle != lastWasIdle);

		int64_t now = static_cast<int64_t>(std::time(nullptr));

		// Compare against where playback should be by now, so a slow iteration
		// (e.g. the album art lookup) isn't mistaken for a seek
		const int64_t expectedElapsed = lastElapsed + (lastPaused ? 0 : now - lastPollTime);
		const bool seekDetected = !isIdle && !paused && !trackChanged &&
			std::abs(elapsed - expectedElapsed) > 3;

		bool needsUpdate = false;

		if (isIdle) {
			if (idleStateChanged) {
				LOG_INFO("Entering idle state — clearing Discord presence");
				rpc_clear_presence();
				needsUpdate   = false;
				lastWasIdle   = true;
				lastSongID    = -1;
			}
		} else {
			// Track changed (or returning from idle)
			if (trackChanged || idleStateChanged) {
				LOG_INFO("Track changed: " << title << " — " << artist);

				const int trackTotal = static_cast<int>(total);

				int64_t startTime = 0, endTime = 0;
				if (!paused && total > 0) {
					startTime = now - elapsed;
					endTime   = now + (total - elapsed);
				}

				// Fetch album art synchronously before pushing presence
				AlbumUrls urls;
				for (const auto& method : artMethods) {
					if (method == "tag") {
						const std::string albumId        = getMPDAlbumId();
						const std::string releaseGroupId = getMPDReleaseGroupId();
						if (!albumId.empty() || !releaseGroupId.empty()) {
							urls = get_album_urls_mbid(albumId, releaseGroupId);
							if (!urls.cover_url.empty()) {
								LOG_INFO("Album art: tag succeeded");
								break;
							}
						}
					} else if (method == "fingerprint") {
						// Computed only here: Chromaprint makes MPD decode the file
						const std::string fingerprint = getMPDFingerprint();
						if (!fingerprint.empty()) {
							urls = get_album_urls_fingerprint(
									trackTotal, fingerprint,
									std::string(config::ACOUSTID_API_KEY));
							if (!urls.cover_url.empty()) {
								LOG_INFO("Album art: fingerprint succeeded");
								break;
							}
						}
					} else if (method == "search") {
						if (!firstArtist.empty() && firstArtist != "Unknown Artist" &&
								!album.empty()  && album  != "Unknown Album"  &&
								!date.empty()   && date   != "Unknown Date") {
							urls = get_album_urls_search(firstArtist, album, date, 100);
							if (!urls.cover_url.empty()) {
								LOG_INFO("Album art: search succeeded");
								break;
							}
						}
					}
				}

				// If config Button1 is set, it becomes "View Album" while the track has
				// a release page, and reverts to the configured button otherwise
				const std::string cfgLabel(config::BUTTON1LABEL);
				const std::string cfgUrl(config::BUTTON1URL);
				if (!cfgLabel.empty() && !cfgUrl.empty() && !urls.page_url.empty())
					rpc_set_button1("View Album", urls.page_url);
				else
					rpc_set_button1(cfgLabel, cfgUrl);

				// Set all metadata + art in one go, then push once
				const std::string year = util::year_from_date(date);
				rpc_set_current_song(title,
						year.empty() ? artist : artist + " - " + year,
						album,
						startTime,
						endTime);

				rpc_set_largeimage(urls.cover_url.empty() ? "mpd" : urls.cover_url);
				rpc_set_paused(paused);

				rpc_update_presence();

				lastSongID          = songID;
				lastWasIdle         = false;
				lastPaused          = paused;
				lastElapsed         = elapsed;
				lastPollTime        = now;
				continue;
			}

			// Pause/resume or seek
			if (pauseStateChanged || seekDetected) {
				rpc_set_paused(paused);
				if (paused || total == 0) {
					rpc_set_starttime(0);
					rpc_set_endtime(0);
				} else {
					rpc_set_starttime(now - elapsed);
					rpc_set_endtime(now + (total - elapsed));
				}
				needsUpdate          = true;
				lastPaused           = paused;
				lastElapsed          = elapsed;
			}
		}

		if (needsUpdate) {
			LOG_DEBUG("Updating Discord presence");
			rpc_update_presence();
		} else {
			int64_t freshStart = (!isIdle && !paused && total > 0) ? now - elapsed : 0;
			int64_t freshEnd   = (!isIdle && !paused && total > 0) ? now + (total - elapsed) : 0;
			rpc_flush_if_pending(freshStart, freshEnd);
		}

		rpc_reconnect_if_needed();

		lastElapsed  = elapsed;
		lastPaused   = paused;
		lastPollTime = now;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	LOG_INFO("Received signal " << stopSignal << ", stopping...");
	rpc_shutdown();
	LOG_INFO("Discord RPC shutdown complete");
	return 0;
}
