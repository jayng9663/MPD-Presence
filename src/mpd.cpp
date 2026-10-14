#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cerrno>

#include <mpd/client.h>

#include "mpd.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "util.hpp"

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

	// Retry forever, backing off up to 30s between attempts. Never sleep here:
	// the main loop keeps running (and stays responsive to Ctrl+C) meanwhile.
	using clock = std::chrono::steady_clock;
	static constexpr auto MIN_DELAY = std::chrono::seconds(2);
	static constexpr auto MAX_DELAY = std::chrono::seconds(30);
	static auto nextAttempt = clock::time_point{};
	static auto delay       = MIN_DELAY;
	static int  failures    = 0;

	if (clock::now() < nextAttempt) return false;

	g_conn = mpd_connection_new(
			std::string(config::MPD_HOST).c_str(),
			config::MPD_PORT,
			30000
			);

	if (!g_conn || mpd_connection_get_error(g_conn) != MPD_ERROR_SUCCESS) {
		const std::string err = g_conn
			? mpd_connection_get_error_message(g_conn) : "out of memory";
		if (failures == 0)
			LOG_ERR("MPD connection failed: " << err << " -- retrying in background");
		else
			LOG_DEBUG("MPD connection failed (" << failures << "): " << err
					<< ", next attempt in " << delay.count() << "s");
		if (g_conn) {
			mpd_connection_free(g_conn);
			g_conn = nullptr;
		}

		failures++;
		nextAttempt = clock::now() + delay;
		delay = std::min(delay * 2, MAX_DELAY);
		return false;
	}

	failures    = 0;
	delay       = MIN_DELAY;
	nextAttempt = {};

	if (!config::MPD_PASSWORD.empty()) {
		if (!mpd_run_password(g_conn, std::string(config::MPD_PASSWORD).c_str())) {
			LOG_ERR("MPD authentication failed");
			mpd_connection_free(g_conn);
			g_conn = nullptr;
			nextAttempt = clock::now() + MAX_DELAY;
			return false;
		}
	}

	LOG_INFO("MPD connected to " << config::MPD_HOST << ":" << config::MPD_PORT);
	return true;
}

static std::string getTag(const mpd_song* song, mpd_tag_type type) {
	const char* v = mpd_song_get_tag(song, type, 0);
	return v ? v : "";
}

void fetchMPDInfo() {
	if (!ensureConnected()) {
		g_mpd = {};
		return;
	}

	mpd_status* status = mpd_run_status(g_conn);
	if (!status) {
		LOG_ERR("Failed to get MPD status -- dropping connection");
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

			g_mpd.title = getTag(song, MPD_TAG_TITLE);
			if (g_mpd.title.empty()) g_mpd.title = "Unknown Title";

			std::vector<std::string> artists;
			for (unsigned i = 0; const char* v = mpd_song_get_tag(song, MPD_TAG_ARTIST, i); ++i)
				artists.emplace_back(v);
			g_mpd.artist      = artists.empty() ? "Unknown Artist" : util::join(artists, ", ");
			g_mpd.firstArtist = artists.empty() ? "Unknown Artist" : artists[0];

			g_mpd.album = getTag(song, MPD_TAG_ALBUM);
			if (g_mpd.album.empty()) g_mpd.album = "Unknown Album";

			g_mpd.date      = getTag(song, MPD_TAG_DATE);
			g_mpd.mbAlbumId = getTag(song, MPD_TAG_MUSICBRAINZ_ALBUMID);
#if LIBMPDCLIENT_CHECK_VERSION(2, 22, 0)
			g_mpd.mbReleaseGroupId = getTag(song, MPD_TAG_MUSICBRAINZ_RELEASEGROUPID);
#endif

			const char* v = mpd_song_get_uri(song);
			g_mpd.uri      = v ? v : "";
			g_mpd.filePath = g_mpd.uri.empty() ? "" : std::string(config::MUSIC_FOLDER) + g_mpd.uri;

			g_mpd.SongID   = mpd_status_get_song_id(status);
			g_mpd.elapsed  = mpd_status_get_elapsed_time(status);
			g_mpd.total    = mpd_status_get_total_time(status);

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

std::string getMPDFingerprint() {
	// Chromaprint fingerprinting decodes the whole file, so compute it once per
	// file and cache the result (even on failure, so we don't retry every call).
	static std::string cachedUri;
	static std::string cachedFingerprint;

	const std::string& uri = g_mpd.uri;
	if (!g_mpd.valid || uri.empty()) return {};
	if (uri == cachedUri) return cachedFingerprint;
	if (!ensureConnected()) return {};

	std::vector<char> buffer(8192);
	std::string fingerprint;

	while (true) {
		const char* fp = mpd_run_getfingerprint_chromaprint(
				g_conn, uri.c_str(), buffer.data(), buffer.size());
		if (fp) {
			fingerprint = fp;
			break;
		} else if (errno == ERANGE) {
			buffer.resize(buffer.size() * 2);
		} else {
			LOG_ERR("Error getting fingerprint");
			// Clear the error so the persistent connection stays usable
			mpd_connection_clear_error(g_conn);
			break;
		}
	}

	cachedUri         = uri;
	cachedFingerprint = fingerprint;
	return fingerprint;
}

bool        getMPDIsPaused()        { return g_mpd.paused; }
std::string getMPDTitle()           { return g_mpd.title; }
std::string getMPDArtist()          { return g_mpd.artist; }
std::string getMPDFirstArtist()     { return g_mpd.firstArtist; }
std::string getMPDAlbum()           { return g_mpd.album; }
std::string getMPDDate()            { return g_mpd.date; }
std::string getMPDFilePath()        { return g_mpd.filePath; }
std::string getMPDAlbumId()         { return g_mpd.mbAlbumId; }
std::string getMPDReleaseGroupId()  { return g_mpd.mbReleaseGroupId; }
int         getMPDSongID()          { return g_mpd.SongID; }
int64_t     getMPDElapsed()         { return g_mpd.elapsed; }
int64_t     getMPDTotal()           { return g_mpd.total; }
bool        getMPDIsValid()         { return g_mpd.valid; }
