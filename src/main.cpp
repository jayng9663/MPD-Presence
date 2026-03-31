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

std::atomic<bool> keepRunning(true);

std::vector<std::string> artMethods;

void signalHandler(int signum) {
	LOG_INFO("Received signal " << signum << ", stopping...");
	keepRunning = false;
}

Config g_config("MPD-Presence.conf");

int main(int argc, char* argv[]) {
	std::signal(SIGINT,  signalHandler);
	std::signal(SIGTERM, signalHandler);

	bool verbose = false;
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

	// Verbosity
	if (verbose) Logger::get().setLevel(LogLevel::DEBUG);

	if (!g_config.loadConfig()) {
		LOG_ERR("Failed to load configuration file");
		return 1;
	}

	// Album art method order
	std::string methodsStr = g_config.getAlbumArtMethodOrder();
	if (methodsStr.empty()) methodsStr = "fingerprint,search";

	{
		std::istringstream iss(methodsStr);
		std::string method;
		while (std::getline(iss, method, ',')) {
			method.erase(0, method.find_first_not_of(" \t"));
			method.erase(method.find_last_not_of(" \t") + 1);
			if (!method.empty()) artMethods.push_back(method);
		}
	}
	if (artMethods.empty()) artMethods = {"fingerprint", "search"};

	// Initial MPD fetch so we have a valid state before RPC init
	fetchMPDInfo();

	rpc_setup();
	rpc_load_button_settings();
	rpc_initialize();

	int     lastSongID          = -1;
	bool    lastPaused          = false;
	int64_t lastElapsed         = 0;
	bool    lastWasIdle         = true;

	while (keepRunning) {
		fetchMPDInfo();

		const std::string title   = getMPDTitle();
		const std::string album   = getMPDAlbum();
		const std::string artist  = getMPDArtist();
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
				const std::string& mf = g_config.getMusicFolder();
				for (const auto& p : g_config.getIgnoreList()) {
					// Strip leading slash so "/AMSR" matches "music_folder/AMSR/..."
					const std::string rel = (!p.empty() && p[0] == '/') ? p.substr(1) : p;
					if (fp.find(mf + rel) == 0) return true;
				}
				return false;
			}();

		const bool trackChanged      = (songID != lastSongID);
		const bool pauseStateChanged = (paused != lastPaused);
		const bool idleStateChanged  = (isIdle != lastWasIdle);

		const bool seekDetected = !isIdle && !paused && !trackChanged &&
			std::abs(elapsed - lastElapsed) > 3;

		int64_t now = static_cast<int64_t>(std::time(nullptr));

		bool needsUpdate = false;

		if (isIdle) {
			if (idleStateChanged) {
				LOG_INFO("Entering idle state — clearing Discord presence");
				rpc_clear_presence();
				needsUpdate   = false; // clear already sent, no further push needed
				lastWasIdle   = true;
				lastSongID    = -1;
			}
		} else {
			// Track changed (or returning from idle)
			if (trackChanged || idleStateChanged) {
				LOG_INFO("Track changed: " << title << " — " << artist);

				const std::string fingerprint = getMPDFingerprint();
				const int         trackTotal  = static_cast<int>(total);

				int64_t startTime = 0, endTime = 0;
				if (!paused && total > 0) {
					startTime = now - elapsed;
					endTime   = now + (total - elapsed);
				}

				// Fetch album art synchronously before pushing presence
				AlbumUrls urls;
				for (const auto& method : artMethods) {
					if (method == "fingerprint") {
						if (!fingerprint.empty()) {
							urls = get_album_urls_fingerprint(
									trackTotal, fingerprint, "2jFwlOUpO2");
							if (!urls.cover_url.empty()) {
								LOG_INFO("Album art: fingerprint succeeded");
								break;
							}
						}
					} else if (method == "search") {
						if (!artist.empty() && artist != "Unknown Artist" &&
								!album.empty()  && album  != "Unknown Album"  &&
								!date.empty()   && date   != "Unknown Date") {
							urls = get_album_urls_search(artist, album, date, 100);
							if (!urls.cover_url.empty()) {
								LOG_INFO("Album art: search succeeded");
								break;
							}
						}
					}
				}

				// When playing, show "View Album" only if config Button1 is set and art was found
				std::string btnLabel, btnUrl;
				{
					std::string cfgLabel = g_config.getButton1Label();
					std::string cfgUrl   = g_config.getButton1Url();
					if (!cfgLabel.empty() && !cfgUrl.empty() && !urls.page_url.empty()) {
						btnLabel = "View Album";
						btnUrl   = urls.page_url;
					}
				}

				// Set all metadata + art in one go, then push once
				rpc_set_current_song(songID,
						title,
						date.empty() ? artist : artist + " - " + date,
						album,
						startTime,
						endTime);

				rpc_set_largeimage(urls.cover_url.empty() ? "mpd" : urls.cover_url);

				if (!btnLabel.empty() && !btnUrl.empty())
					rpc_set_button1(btnLabel, btnUrl);

				rpc_update_presence();

				lastSongID          = songID;
				lastWasIdle         = false;
				lastPaused          = paused;
				lastElapsed         = elapsed;
				continue;
			}

			// Pause/resume or seek
			if (pauseStateChanged || seekDetected) {
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
			// Re-send any rate-limited update. When paused or idle the
			// timestamps are 0; when playing they reflect the current position.
			int64_t freshStart = (!isIdle && !paused && total > 0) ? now - elapsed : 0;
			int64_t freshEnd   = (!isIdle && !paused && total > 0) ? now + (total - elapsed) : 0;
			rpc_flush_if_pending(freshStart, freshEnd);
		}

		lastElapsed = elapsed;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	rpc_shutdown();
	LOG_INFO("Discord RPC shutdown complete");
	return 0;
}
