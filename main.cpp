#include <mpd/client.h>
#include <chromaprint.h>
#include "./include/discord_rpc.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <fstream>

std::string currentSong;
std::string currentArtURL;

#define DISCORD_APP_ID "1343479020918014013"

#define MPD_HOST "localhost"
#define MPD_PORT 16650
#define MPD_PASSWORD "14231423"

#define ACOUSTID_API_KEY "CPmwR0LOkp"

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return response;
}

std::vector<std::string> getMusicBrainzReleaseIDs(const std::string& filePath, int duration) {
    std::vector<std::string> releaseIDs;
    ChromaprintContext* ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    if (!ctx) return releaseIDs;
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        chromaprint_free(ctx);
        return releaseIDs;
    }
    
    std::vector<int16_t> audioBuffer(44100 * 10);
    file.read(reinterpret_cast<char*>(audioBuffer.data()), audioBuffer.size() * sizeof(int16_t));
    size_t bytesRead = file.gcount();
    file.close();
    
    if (bytesRead == 0) {
        chromaprint_free(ctx);
        return releaseIDs;
    }
    
    chromaprint_start(ctx, 44100, 1);
    chromaprint_feed(ctx, audioBuffer.data(), bytesRead / sizeof(int16_t));
    chromaprint_finish(ctx);
    
    char* fingerprint = nullptr;
    chromaprint_get_fingerprint(ctx, &fingerprint);
    
    std::string apiURL = "https://api.acoustid.org/v2/lookup?client=" + std::string(ACOUSTID_API_KEY) +
                          "&meta=releaseids&fingerprint=" + fingerprint +
                          "&duration=" + std::to_string(duration);
    
	 std::cout << apiURL << std::endl;
    std::string jsonResponse = httpGet(apiURL);
    
    std::istringstream iss(jsonResponse);
    std::string releaseID;
    while (iss >> releaseID) {
        releaseIDs.push_back(releaseID);
    }
    
    chromaprint_dealloc(fingerprint);
    chromaprint_free(ctx);
    return releaseIDs;
}

std::string getAlbumArtURL(const std::string& filePath, int duration) {
    std::vector<std::string> releaseIDs = getMusicBrainzReleaseIDs(filePath, duration);
    if (releaseIDs.empty()) return "mpd";

    for (const auto& releaseID : releaseIDs) {
        std::string coverArtURL = "https://coverartarchive.org/release/" + releaseID + "/front";
		  std::cout << coverArtURL << std::endl;
        if (httpGet(coverArtURL).size() > 0) {
            currentArtURL = coverArtURL;
            return coverArtURL;
        }
    }
    return "mpd";
}

void updateDiscordPresence(const std::string& title, const std::string& artist, const std::string& filePath, int elapsed, int total, bool isPaused, int playlistSize) {
    DiscordRichPresence discordPresence;
    memset(&discordPresence, 0, sizeof(discordPresence));
    std::time_t currentTime = std::time(nullptr);
    
    std::string albumArtURL = (currentSong == filePath) ? currentArtURL : getAlbumArtURL(filePath, total);
    discordPresence.startTimestamp = isPaused ? 0 : currentTime - elapsed;
    discordPresence.state = (isPaused ? "Paused" : artist.c_str());
    discordPresence.details = title.c_str();
    discordPresence.largeImageKey = albumArtURL.c_str();
    discordPresence.largeImageText = "Music Player Daemon";
    discordPresence.smallImageKey = isPaused ? "pause" : "play";
    discordPresence.smallImageText = isPaused ? "Paused" : "Playing";
    
    discordPresence.partySize = 1;
    discordPresence.partyMax = playlistSize;
    discordPresence.instance = 1;
    
    Discord_UpdatePresence(&discordPresence);
}

void fetchMPDInfo() {
    struct mpd_connection* conn = mpd_connection_new(MPD_HOST, MPD_PORT, 0);
    if (!conn || mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
        std::cerr << "Could not connect to MPD" << std::endl;
        if (conn) mpd_connection_free(conn);
        return;
    }
    
    if (std::string(MPD_PASSWORD) != "") {
        if (!mpd_run_password(conn, MPD_PASSWORD)) {
            std::cerr << "Failed to authenticate with MPD" << std::endl;
            mpd_connection_free(conn);
            return;
        }
    }

    struct mpd_status* status = mpd_run_status(conn);
    if (!status) {
        mpd_connection_free(conn);
        return;
    }
    
    bool isPaused = mpd_status_get_state(status) == MPD_STATE_PAUSE;
    int playlistSize = mpd_status_get_queue_length(status);

    if (mpd_status_get_state(status) == MPD_STATE_PLAY || isPaused) {
        struct mpd_song* song = mpd_run_current_song(conn);
        if (song) {
            std::string title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0) ? mpd_song_get_tag(song, MPD_TAG_TITLE, 0) : "Unknown Title";
            std::string artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0) ? mpd_song_get_tag(song, MPD_TAG_ARTIST, 0) : "Unknown Artist";
            std::string filePath = "/home/jay/Music/" + std::string(mpd_song_get_uri(song));
            int elapsed = mpd_status_get_elapsed_time(status);
            int total = mpd_status_get_total_time(status);
            updateDiscordPresence(title, artist, filePath, elapsed, total, isPaused, playlistSize);
            mpd_song_free(song);
        }
    }

    mpd_status_free(status);
    mpd_connection_free(conn);
}

int main() {
    DiscordEventHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    Discord_Initialize(DISCORD_APP_ID, &handlers, 1, nullptr);
    
    while (true) {
        fetchMPDInfo();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    
    Discord_Shutdown();
    return 0;
}

