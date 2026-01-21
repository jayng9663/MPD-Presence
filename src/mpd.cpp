#include <string>
#include <vector>
#include <thread>
#include <csignal>

#include <mpd/client.h>

#include "mpd.hpp"
#include "config.hpp"
#include "logging.hpp"

static MPDState g_mpd;

void fetchMPDInfo() {
	g_mpd = {}; // reset state

	//LOG_DEBUG("Fetching MPD info");
	if (!g_config.loadConfig()) {
		LOG_ERROR("Failed to load MPD config");
		return;
	}

	mpd_connection* conn = mpd_connection_new(
			g_config.getHost().c_str(),
			g_config.getPort(),
			30000
			);

	static int retryCount = 0;
	const int maxRetries = 20;

	if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		LOG_ERROR("MPD connection failed (" << retryCount << "/" << maxRetries << ")");
		std::this_thread::sleep_for(std::chrono::seconds(2));
		if (conn) mpd_connection_free(conn);

		retryCount++;
		if (retryCount >= maxRetries) {
			LOG_ERROR("Max retries reached. Sending quit signal.");
			std::raise(SIGTERM); // stops the app
		}

		return;
	}

	retryCount = 0;

	if (!g_config.getPassword().empty()) {
		//LOG_DEBUG("Attempting authentication");
		if (!mpd_run_password(conn, g_config.getPassword().c_str())) {
			LOG_ERROR("MPD authentication failed");
			mpd_connection_free(conn);
			return;
		}
	}

	mpd_status* status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("Failed to get MPD status");
		mpd_connection_free(conn);
		return;
	}

	mpd_state state = mpd_status_get_state(status);
	if (state == MPD_STATE_PLAY || state == MPD_STATE_PAUSE) {
		//LOG_DEBUG("Found playing/paused state");
		mpd_song* song = mpd_run_current_song(conn);
		if (song) {
			g_mpd.paused = (state == MPD_STATE_PAUSE);

			const char* v;

			v = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
			g_mpd.title = v ? v : "Unknown Title";

			v = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
			g_mpd.artist = v ? v : "Unknown Artist";

			v = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
			g_mpd.album = v ? v : "Unknown Album";

			v = mpd_song_get_tag(song, MPD_TAG_DATE, 0);
			g_mpd.date = v ? v : "";

			v = mpd_song_get_uri(song);
			if (v)
				g_mpd.filePath = g_config.getMusicFolder() + v;

			g_mpd.elapsed = mpd_status_get_elapsed_time(status);
			g_mpd.total   = mpd_status_get_total_time(status);

			const char* uri = mpd_song_get_uri(song);

			if (uri) {
				//LOG_DEBUG("Getting fingerprint for URI: " << uri);
				size_t bufsize = 8192;
				std::vector<char> buffer(bufsize);

				while (true) {
					const char* fp = mpd_run_getfingerprint_chromaprint(conn, uri, buffer.data(), buffer.size());
					if (fp) {
						g_mpd.fingerprint = fp;
						//LOG_DEBUG("Fingerprint obtained: " << g_mpd.fingerprint.substr(0, 10) << "...");
						break;
					} else if (errno == ERANGE) {
						// Buffer too small, double the size
						bufsize *= 2;
						buffer.resize(bufsize);
					} else {
						// Some other error
						g_mpd.fingerprint.clear();
						LOG_ERROR("Error getting fingerprint");
						break;
					}
				}
			} else {
				LOG_DEBUG("No URI found for song");
				g_mpd.fingerprint.clear();
			}

			mpd_song_free(song);
		}
	} else {
		LOG_DEBUG("MPD state is not playing or paused");
	}

	mpd_status_free(status);
	mpd_connection_free(conn);
}

bool getMPDIsPaused() {
	return g_mpd.paused;
}

std::string getMPDTitle() {
	return g_mpd.title;
}

std::string getMPDArtist() {
	return g_mpd.artist;
}

std::string getMPDAlbum() {
	return g_mpd.album;
}

std::string getMPDDate() {
	return g_mpd.date;
}

std::string getMPDFilePath() {
	return g_mpd.filePath;
}

int64_t getMPDElapsed() {
	return g_mpd.elapsed;
}

int64_t getMPDTotal() {
	return g_mpd.total;
}

std::string getMPDFingerprint() {
	return g_mpd.fingerprint;
}

