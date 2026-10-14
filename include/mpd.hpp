#pragma once
#include <string>

struct MPDState {
	bool valid = false;
	bool paused = false;

	std::string title;
	std::string artist;       // all ARTIST tags, joined with ", "
	std::string firstArtist;  // first ARTIST tag only (for MusicBrainz search)
	std::string album;
	std::string date;
	std::string uri;          // song URI relative to the MPD music directory
	std::string filePath;
	std::string mbAlbumId;        // MUSICBRAINZ_ALBUMID tag (release)
	std::string mbReleaseGroupId; // MUSICBRAINZ_RELEASEGROUPID tag
	int SongID = -1;

	int64_t elapsed = 0;
	int64_t total = 0;
};

// Fetch / update MPD
void fetchMPDInfo();

// MPD getters
bool getMPDIsValid();
bool getMPDIsPaused();

std::string getMPDTitle();
std::string getMPDArtist();
std::string getMPDFirstArtist();
std::string getMPDAlbum();
std::string getMPDDate();
std::string getMPDFilePath();
std::string getMPDAlbumId();
std::string getMPDReleaseGroupId();
int getMPDSongID();

int64_t getMPDElapsed();
int64_t getMPDTotal();

// Chromaprint fingerprint of the current song. Expensive (MPD decodes the
// audio), so it is computed on first call per song and cached.
std::string getMPDFingerprint();
