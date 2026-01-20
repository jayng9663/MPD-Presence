#pragma once
#include <string>

struct MPDState {
	bool valid = false;
	bool paused = false;

	std::string title;
	std::string artist;
	std::string album;
	std::string filePath;
	std::string fingerprint;

	int64_t elapsed = 0;
	int64_t total = 0;
};

// Fetch / update MPD
void fetchMPDInfo();

// MPD getters
bool mpdIsValid();
bool mpdIsPaused();

std::string getMPDTitle();
std::string getMPDArtist();
std::string getMPDAlbum();
std::string getMPDFilePath();
std::string getMPDFingerprint();

int64_t getMPDElapsed();
int64_t getMPDTotal();
