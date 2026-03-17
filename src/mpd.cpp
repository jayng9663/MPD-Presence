#include <string>
#include <vector>
#include <thread>
#include <csignal>

#include <mpd/client.h>

#include "mpd.hpp"
#include "config.hpp"
#include "logging.hpp"

static MPDState g_mpd;

// Persistent connection — reconnect only on failure
static mpd_connection* g_conn = nullptr;

static bool ensureConnected() {
	// If we have a live connection, reuse it
	if (g_conn && mpd_connection_get_error(g_conn) == MPD_ERROR_SUCCESS) {
		return true;
	}

	// Clean up broken connection
	if (g_conn) {
		mpd_connection_free(g_conn);
		g_conn = nullptr;
	}

	static int retryCount = 0;
	const int maxRetries = 20;

	g_conn = mpd_connection_new(
			g_config.getHost().c_str(),
			g_config.getPort(),
			30000
			);

	if (!g_conn || mpd_connection_get_error(g_conn) != MPD_ERROR_SUCCESS) {
		LOG_ERROR("MPD connection failed (" << retryCount << "/" << maxRetries << ")");
		if (g_conn) {
			mpd_connection_free(g_conn);
			g_conn = nullptr;
		}
		std::this_thread::sleep_for(std::chrono::seconds(2));

		retryCount++;
		if (retryCount >= maxRetries) {
			LOG_ERROR("Max retries reached. Sending quit signal.");
			std::raise(SIGTERM);
		}
		return false;
	}

	retryCount = 0;

	if (!g_config.getPassword().empty()) {
		if (!mpd_run_password(g_conn, g_config.getPassword().c_str())) {
			LOG_ERROR("MPD authentication failed");
			mpd_connection_free(g_conn);
			g_conn = nullptr;
			return false;
		}
	}

	LOG_INFO("MPD connected to " << g_config.getHost() << ":" << g_config.getPort());
	return true;
}

void fetchMPDInfo() {
	if (!ensureConnected()) {
		LOG_ERROR("Failed to connect to MPD.");
		g_mpd = {};
		return;
	}

	mpd_status* status = mpd_run_status(g_conn);
	if (!status) {
		LOG_ERROR("Failed to get MPD status -- dropping connection");
		mpd_connection_free(g_conn);
		g_conn = nullptr;
		g_mpd = {};
		return;
	}

	mpd_state state = mpd_status_get_state(status);
	if (state == MPD_STATE_PLAY || state == MPD_STATE_PAUSE) {
		mpd_song* song = mpd_run_current_song(g_conn);
		if (song) {
			g_mpd.valid  = true;
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

			g_mpd.SongID   = mpd_status_get_song_id(status);
			g_mpd.elapsed  = mpd_status_get_elapsed_time(status);
			g_mpd.total    = mpd_status_get_total_time(status);

			const char* uri = mpd_song_get_uri(song);
			if (uri) {
				size_t bufsize = 8192;
				std::vector<char> buffer(bufsize);

				while (true) {
					const char* fp = mpd_run_getfingerprint_chromaprint(g_conn, uri, buffer.data(), buffer.size());
					if (fp) {
						g_mpd.fingerprint = fp;
						break;
					} else if (errno == ERANGE) {
						bufsize *= 2;
						buffer.resize(bufsize);
					} else {
						g_mpd.fingerprint.clear();
						LOG_ERROR("Error getting fingerprint");
						break;
					}
				}
			} else {
				g_mpd.fingerprint.clear();
			}

			mpd_song_free(song);
		}
	} else {
		// Stopped / unknown -- treat as idle
		if (g_mpd.valid) {
			LOG_DEBUG("MPD state is not playing or paused");
		}
		g_mpd.valid  = false;
		g_mpd.title  = "";
		g_mpd.artist = "";
		g_mpd.album  = "";
	}

	mpd_status_free(status);
	// NOTE: do NOT free g_conn here -- it is persistent
}

bool        getMPDIsPaused()    { return g_mpd.paused; }
std::string getMPDTitle()       { return g_mpd.title; }
std::string getMPDArtist()      { return g_mpd.artist; }
std::string getMPDAlbum()       { return g_mpd.album; }
std::string getMPDDate()        { return g_mpd.date; }
std::string getMPDFilePath()    { return g_mpd.filePath; }
int         getMPDSongID()      { return g_mpd.SongID; }
int64_t     getMPDElapsed()     { return g_mpd.elapsed; }
int64_t     getMPDTotal()       { return g_mpd.total; }
std::string getMPDFingerprint() { return g_mpd.fingerprint; }
bool        getMPDIsValid()     { return g_mpd.valid; }
