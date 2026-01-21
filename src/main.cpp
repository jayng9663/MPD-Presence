#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "config.hpp"
#include "rpc.hpp"
#include "mpd.hpp"
#include "album_art.hpp"
#include "logging.hpp"

std::atomic<bool> keepRunning(true);

// Global verbosity variable
Verbosity g_verbosity = Verbosity::INFO;

std::vector<std::string> artMethods;

void signalHandler(int signum) {
	LOG_INFO("Received signal " << signum << ", stopping...");
	keepRunning = false;
}

Config g_config("mpdrpc.conf");

int main() {
	std::signal(SIGINT, signalHandler); // Ctrl+C
	std::signal(SIGTERM, signalHandler); // systemd stop signal

	// Load config first
	if (!g_config.loadConfig()) {
		LOG_ERROR("Failed to load configuration file");
		return 1;
	}

	// Read verbosity from config after loading it
	std::string verboseStr = g_config.getVerbose();
	LOG_INFO("Verbose setting from config: '" << verboseStr << "'");

	// Clean the string - remove whitespace and handle case sensitivity
	std::string cleanVerbose = verboseStr;
	cleanVerbose.erase(0, cleanVerbose.find_first_not_of(" \t"));
	cleanVerbose.erase(cleanVerbose.find_last_not_of(" \t") + 1);

	LOG_INFO("Cleaned verbose setting: '" << cleanVerbose << "'");

	if (cleanVerbose == "none") {
		g_verbosity = Verbosity::NONE;
	} else if (cleanVerbose == "debug") {
		g_verbosity = Verbosity::DEBUG;
	} else {
		g_verbosity = Verbosity::INFO; // default to info
	}
	LOG_INFO("Set verbosity level to: " << 
			(g_verbosity == Verbosity::NONE ? "none" : 
			 (g_verbosity == Verbosity::DEBUG ? "debug" : "info")));

	std::string methodsStr = g_config.getAlbumArtMethodOrder();
	if (methodsStr.empty()) {
		methodsStr = "fingerprint,search"; // default fallback
	}

	std::istringstream iss(methodsStr);
	std::string method;
	artMethods.clear();

	while (std::getline(iss, method, ',')) {
		method.erase(0, method.find_first_not_of(" \t"));
		method.erase(method.find_last_not_of(" \t") + 1);
		if (!method.empty()) {
			artMethods.push_back(method);
		}
	}

	if (artMethods.empty()) {
		artMethods = {"fingerprint", "search"}; // fallback
	}

	fetchMPDInfo();

	rpc_setup();
	rpc_initialize();

	std::string lastTitle = "";
	std::string lastAlbum = "";
	std::string lastArtist = "";
	bool lastPaused = false;
	int64_t lastElapsed = 0;
	bool lastWasIdle = false;

	while (keepRunning) {
		fetchMPDInfo();
		std::string title = getMPDTitle();
		std::string album = getMPDAlbum();
		std::string artist = getMPDArtist();
		std::string date = getMPDDate();
		bool isPaused = getMPDIsPaused();
		int64_t elapsed = getMPDElapsed();
		int64_t total = getMPDTotal();

		// Check if no track is loaded
		bool isIdle = title.empty();

		// Detect what changed
		bool trackChanged = (title != lastTitle || album != lastAlbum || artist != lastArtist);
		bool pauseStateChanged = (isPaused != lastPaused);
		bool idleStateChanged = (isIdle != lastWasIdle);

		// Detect seek: jump backward or forward by more than 1 seconds
		bool seekDetected = std::abs(elapsed - lastElapsed) > 1;
		bool needsUpdate = false;

		if (isIdle) {
			// No track loaded - show idle state
			if (idleStateChanged) {
				LOG_INFO("Entering idle state");
				rpc_set_details("MPD RPC");
				rpc_set_state("");
				rpc_set_largeimage("mpd");
				rpc_set_starttime(0);
				rpc_set_endtime(0);
				needsUpdate = true;
				lastWasIdle = true;
			}
		} else {
			// Track is loaded
			// Handle track change - update everything
			if (trackChanged || idleStateChanged) {
				LOG_INFO("Changing song");
				if (g_verbosity >= Verbosity::DEBUG) {
					LOG_INFO(("Title: " + title).c_str());
					LOG_INFO(("Album: " + album).c_str());
				}
				rpc_set_details(title.c_str());
				rpc_set_state(album.c_str());

				// Set default image first (mpd)
				rpc_update_presence();

				// Create a function to handle the album art fetching
				auto fetchAlbumArt = [title, artist, album, date]() {
					LOG_DEBUG("Attempting album art lookup");

					for (const auto& method : artMethods) {
						if (method == "fingerprint") {
							LOG_DEBUG("Trying fingerprint-based lookup");
							std::string fingerprint = getMPDFingerprint();
							if (!fingerprint.empty()) {
								std::string url = get_url_fingerprint(getMPDTotal(), fingerprint, "2jFwlOUpO2");
								if (!url.empty()) {
									LOG_INFO("Using Fingerprint method");
									return url;
								}
							}
						} else if (method == "search") {
							LOG_DEBUG("Trying search-based lookup");
							if (!artist.empty() && artist != "Unknown Artist" &&
									!album.empty() && album != "Unknown Album" &&
									!date.empty() && date != "Unknown Date") {
								std::string url = get_url_search(artist, album, date, 100);
								if (!url.empty()) {
									LOG_INFO("Using Search method");
									return url;
								}
							}
						}
					}

					LOG_INFO("No album art found, defaulting to mpd image");
					return std::string("mpd");
				};

				// Run the async operation in background
				std::thread([fetchAlbumArt]() {
						std::string url = fetchAlbumArt();
						if (!url.empty()) {
						// Update RPC with actual image after a short delay to ensure it's ready
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						rpc_set_largeimage(url.c_str());
						rpc_update_presence();
						}
						}).detach();

				needsUpdate = true;
				lastTitle = title;
				lastAlbum = album;
				lastArtist = artist;
				lastWasIdle = false;
			}
			// Handle pause/play state or seek - update timestamps
			if (trackChanged || pauseStateChanged || (seekDetected && !isPaused) || idleStateChanged) {
				int64_t currentTime = std::time(nullptr);
				if (isPaused) {
					// Freeze time
					LOG_DEBUG("Setting paused timestamps");
					rpc_set_starttime(0);
					rpc_set_endtime(0);
				} else if (total > 0) {
					// Update timestamps based on current time
					LOG_DEBUG("Setting playing timestamps");
					int64_t startTime = currentTime - elapsed;
					int64_t endTime = currentTime + (total - elapsed);
					rpc_set_starttime(startTime);
					rpc_set_endtime(endTime);
				} else {
					LOG_DEBUG("Setting default timestamps");
					rpc_set_starttime(0);
					rpc_set_endtime(0);
				}
				needsUpdate = true;
				lastPaused = isPaused;
			}
		}
		// Only update presence if something actually changed
		if (needsUpdate) {
			LOG_DEBUG("Updating Discord presence");
			rpc_update_presence();
		}
		// Always track elapsed for seek detection
		lastElapsed = elapsed;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	rpc_shutdown();
	LOG_INFO("Discord RPC shutdown complete");
	return 0;
}

