#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>
#include <mutex>
#include "logger.hpp"
#include "config.hpp"

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
		.setClientID(config::DISCORD_APP_ID.data())
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

static std::atomic<int> g_rpcSongID{-1};

static constexpr int64_t DISCORD_RATE_LIMIT_SECONDS = 16;
static int64_t  g_lastPushTime   = 0;
static bool     g_pendingUpdate  = false;

// Must be called with rpcMutex held.
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
	LargeImageKey    = "mpd";
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

bool rpc_flush_if_pending(int64_t newStartTime, int64_t newEndTime) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	if (!g_pendingUpdate) return false;

	int64_t now = static_cast<int64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());

	if (now - g_lastPushTime < DISCORD_RATE_LIMIT_SECONDS) return false;

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

	if (!config::BUTTON1LABEL.empty()) Button1Label = std::string(config::BUTTON1LABEL);
	if (!config::BUTTON1URL.empty())   Button1Url   = std::string(config::BUTTON1URL);
	if (!config::BUTTON2LABEL.empty()) Button2Label = std::string(config::BUTTON2LABEL);
	if (!config::BUTTON2URL.empty())   Button2Url   = std::string(config::BUTTON2URL);

	LOG_DEBUG("Button1: '" << Button1Label << "' -> '" << Button1Url << "'");
	LOG_DEBUG("Button2: '" << Button2Label << "' -> '" << Button2Url << "'");
}
