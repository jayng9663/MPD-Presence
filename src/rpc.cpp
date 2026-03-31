#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>
#include <mutex>
#include "logger.hpp"
#include "config.hpp"

constexpr auto APPLICATION_ID = "1343479020918014013";

static int64_t     StartTime = 0;
static int64_t     EndTime   = 0;
static std::string currentDetails;
static std::string currentState;
static std::string currentImageText;
static std::string LargeImageKey = "mpd";
static std::string Button1Label;
static std::string Button1Url;
static std::string Button2Label;
static std::string Button2Url;

// Single mutex protecting ALL RPC state including reads in updatePresence()
static std::mutex rpcMutex;

static void discordSetup() {
	LOG_DEBUG("Setting up Discord RPC");
	discord::RPCManager::get()
		.setClientID(APPLICATION_ID)
		.onReady([](discord::User const& user) {
				LOG_INFO("Discord: connected to " << user.username
						<< "#" << user.discriminator << " - " << user.id);
				})
	.onDisconnected([](int errcode, std::string_view message) {
			LOG_INFO("Discord: disconnected (" << errcode << ") - " << message);
			})
	.onErrored([](int errcode, std::string_view message) {
			LOG_ERR("Discord: error (" << errcode << ") - " << message);
			});
}

// Must be called with rpcMutex held
static void updatePresenceLocked() {
	auto& rpc = discord::RPCManager::get();

	auto& presence = rpc.getPresence()
		.setDetails(currentDetails)
		.setState(currentState)
		.setLargeImageText(currentImageText)
		.setActivityType(discord::ActivityType::Listening)
		.setStatusDisplayType(discord::StatusDisplayType::Details)
		.setLargeImageKey(LargeImageKey)
		.setStartTimestamp(StartTime)
		.setEndTimestamp(EndTime);

	if (!Button1Label.empty() && !Button1Url.empty())
		presence.setButton1(Button1Label, Button1Url);

	if (!Button2Label.empty() && !Button2Url.empty())
		presence.setButton2(Button2Label, Button2Url);

	presence.refresh();
}

// Song ID of the track currently shown in Discord.
// Set by the main thread on every track change; read by art threads.
static std::atomic<int> g_rpcSongID{-1};

// Discord silently drops presence updates faster than ~15 seconds apart.
// We enforce our own gate: if a push is attempted within the window, we mark
// it as pending. The main loop checks for a pending update each tick and
// re-sends it once the window has elapsed.
static constexpr int64_t DISCORD_RATE_LIMIT_SECONDS = 16; // 1s margin over Discord's 16s
static int64_t  g_lastPushTime   = 0;   // wall-clock of last actual push
static bool     g_pendingUpdate  = false; // an update was suppressed and needs retry

// Must be called with rpcMutex held.
// Returns true if the update was sent, false if rate-limited (pending flagged).
static bool pushPresenceOrDefer() {
	int64_t now = static_cast<int64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());

	if (now - g_lastPushTime < DISCORD_RATE_LIMIT_SECONDS) {
		LOG_DEBUG("Presence update deferred (rate limit, " 
				<< (DISCORD_RATE_LIMIT_SECONDS - (now - g_lastPushTime)) 
				<< "s remaining)");
		g_pendingUpdate = true;
		return false;
	}

	updatePresenceLocked();
	g_lastPushTime  = now;
	g_pendingUpdate = false;
	return true;
}

// -- Public API --

void rpc_setup()      { discordSetup(); }
void rpc_initialize() { discord::RPCManager::get().initialize(); }
void rpc_shutdown()   { discord::RPCManager::get().shutdown(); }

// Set all track metadata and record the song ID in one locked operation.
// Must be called by the main thread before launching the art thread so that
// g_rpcSongID is always up-to-date before any art thread checks it.
void rpc_set_current_song(int songID,
		const std::string& details,
		const std::string& state,
		const std::string& largeImageText,
		int64_t startTime,
		int64_t endTime)
{
	std::lock_guard<std::mutex> lock(rpcMutex);
	g_rpcSongID.store(songID);
	currentDetails   = details;
	currentState     = state;
	currentImageText = largeImageText;
	LargeImageKey    = "mpd";   // always reset to placeholder on track change (If Imgge not found)
	StartTime        = startTime;
	EndTime          = endTime;
	LOG_DEBUG("rpc_set_current_song: id=" << songID << " details=" << details);
}

void rpc_clear_presence() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	discord::RPCManager::get().clearPresence();
	g_pendingUpdate = false;
	LOG_DEBUG("Discord presence cleared");
}

void rpc_update_presence() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	LOG_DEBUG("Updating Discord presence");
	pushPresenceOrDefer();
}

void rpc_set_starttime(int64_t v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	StartTime = v;
	LOG_DEBUG("StartTime = " << v);
}

void rpc_set_endtime(int64_t v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	EndTime = v;
	LOG_DEBUG("EndTime = " << v);
}

void rpc_set_details(const char* v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentDetails = v;
	LOG_DEBUG("Details = " << v);
}

void rpc_set_state(const char* v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentState = v;
	LOG_DEBUG("State = " << v);
}

void rpc_set_largeimagetext(const char* v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentImageText = v;
	LOG_DEBUG("LargeImageText = " << v);
}

void rpc_set_largeimage(const std::string& v) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	LargeImageKey = v;
	LOG_DEBUG("LargeImage = " << v);
}

void rpc_set_button1(const std::string& label, const std::string& url) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	Button1Label = label;
	Button1Url   = url;
	LOG_DEBUG("Button1 = " << label << " (" << url << ")");
}

void rpc_set_button2(const std::string& label, const std::string& url) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	Button2Label = label;
	Button2Url   = url;
	LOG_DEBUG("Button2 = " << label << " (" << url << ")");
}

std::string rpc_get_details()       { std::lock_guard<std::mutex> l(rpcMutex); return currentDetails; }
std::string rpc_get_state()         { std::lock_guard<std::mutex> l(rpcMutex); return currentState; }
std::string rpc_get_largeimagetext(){ std::lock_guard<std::mutex> l(rpcMutex); return currentImageText; }
std::string rpc_get_largeimage()    { std::lock_guard<std::mutex> l(rpcMutex); return LargeImageKey; }

// Atomically: check song ID is still current, apply art, push to Discord.
// Holding the mutex for the entire operation means two art threads can never
// interleave their writes — whichever acquires the lock first either applies
// its result (if still current) or bails out (if already superseded).
// Returns the current song ID (for stale checks in the art thread)
int rpc_get_current_song_id() {
	return g_rpcSongID.load();
}

bool rpc_apply_art_if_current(int songID,
		const std::string& cover_url,
		const std::string& btn1_label,
		const std::string& btn1_url,
		int64_t startTime,
		int64_t endTime)
{
	std::lock_guard<std::mutex> lock(rpcMutex);

	if (g_rpcSongID.load() != songID) {
		LOG_DEBUG("rpc_apply_art_if_current: songID mismatch ("
				<< songID << " vs " << g_rpcSongID.load() << "), discarding");
		return false;
	}

	if (!cover_url.empty()) {
		LargeImageKey = cover_url;
		LOG_DEBUG("Applied cover art: " << cover_url);
	} else {
		LargeImageKey = "mpd";
	}

	if (!btn1_label.empty() && !btn1_url.empty()) {
		Button1Label = btn1_label;
		Button1Url   = btn1_url;
	}

	StartTime = startTime;
	EndTime   = endTime;

	pushPresenceOrDefer();
	return true;
}

// Called by the main loop every tick: if a previous update was rate-limited
// and the window has now expired, re-send the current presence state.
// The timestamps are recalculated fresh so the timer is always accurate.
bool rpc_flush_if_pending(int64_t newStartTime, int64_t newEndTime) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	if (!g_pendingUpdate) return false;

	int64_t now = static_cast<int64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());

	if (now - g_lastPushTime < DISCORD_RATE_LIMIT_SECONDS) return false;

	// Update timestamps to current time before re-sending
	StartTime = newStartTime;
	EndTime   = newEndTime;

	updatePresenceLocked();
	g_lastPushTime  = now;
	g_pendingUpdate = false;
	LOG_DEBUG("Flushed pending presence update");
	return true;
}

void rpc_load_button_settings() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	std::string l1 = g_config.getButton1Label();
	std::string u1 = g_config.getButton1Url();
	std::string l2 = g_config.getButton2Label();
	std::string u2 = g_config.getButton2Url();

	if (!l1.empty()) Button1Label = l1;
	if (!u1.empty()) Button1Url   = u1;
	if (!l2.empty()) Button2Label = l2;
	if (!u2.empty()) Button2Url   = u2;

	LOG_DEBUG("Button1: '" << Button1Label << "' -> '" << Button1Url << "'");
	LOG_DEBUG("Button2: '" << Button2Label << "' -> '" << Button2Url << "'");
}
