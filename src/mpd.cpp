#include <iostream>
#include <string>

#include <mpd/client.h>

#include "mpd.hpp"
#include "config.hpp"

static MPDState g_mpd;

void fetchMPDInfo() {
	g_mpd = {}; // reset state

	if (!g_config.loadConfig()) {
		std::cerr << "Failed to load MPD config\n";
		return;
	}

	mpd_connection* conn = mpd_connection_new(
			g_config.getHost().c_str(),
			g_config.getPort(),
			30000
			);

	if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		std::cerr << "MPD connection failed\n";
		if (conn) mpd_connection_free(conn);
		return;
	}

	if (!g_config.getPassword().empty()) {
		if (!mpd_run_password(conn, g_config.getPassword().c_str())) {
			std::cerr << "MPD authentication failed\n";
			mpd_connection_free(conn);
			return;
		}
	}

	mpd_status* status = mpd_run_status(conn);
	if (!status) {
		mpd_connection_free(conn);
		return;
	}

	mpd_state state = mpd_status_get_state(status);
	if (state == MPD_STATE_PLAY || state == MPD_STATE_PAUSE) {
		mpd_song* song = mpd_run_current_song(conn);
		if (song) {
			g_mpd.valid = true;
			g_mpd.paused = (state == MPD_STATE_PAUSE);

			const char* v;

			v = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
			g_mpd.title = v ? v : "Unknown Title";

			v = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
			g_mpd.artist = v ? v : "Unknown Artist";

			v = mpd_song_get_tag(song, MPD_TAG_ALBUM, 0);
			g_mpd.album = v ? v : "Unknown Album";

			v = mpd_song_get_uri(song);
			if (v)
				g_mpd.filePath = g_config.getMusicFolder() + v;

			g_mpd.elapsed = mpd_status_get_elapsed_time(status);
			g_mpd.total   = mpd_status_get_total_time(status);

			char buffer[2048];
			const char* uri = mpd_song_get_uri(song);
			if (uri) {
				if (mpd_run_getfingerprint_chromaprint(conn, uri, buffer, sizeof(buffer))) {
					g_mpd.fingerprint = buffer;
				} else {
					// Handle error or set empty string
					g_mpd.fingerprint = "";
				}
			} else {
				g_mpd.fingerprint = "";
			}

			mpd_song_free(song);
		}
	}

	mpd_status_free(status);
	mpd_connection_free(conn);
}

bool mpdIsValid() {
	return g_mpd.valid;
}

bool mpdIsPaused() {
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

