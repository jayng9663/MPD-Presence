#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include "logger.hpp"
#include "config.hpp"
// Private header of discord-presence, used to reset its reconnect backoff (see below)
#include "../lib/discord-presence/src/backoff.hpp"

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

static constexpr int64_t DISCORD_RATE_LIMIT_SECONDS = 16;
static int64_t  g_lastPushTime   = 0;
static bool     g_pendingUpdate  = false;
static bool     g_hasPresence    = false; // false while idle/cleared
static std::atomic<bool> g_connected{false};
static std::atomic<bool> g_lostConnection{false};

static void discordSetup() {
	LOG_DEBUG("Setting up Discord RPC");
	discord::RPCManager::get()
		.setClientID(config::DISCORD_APP_ID.data())
		.onReady([](discord::User const& user) {
				LOG_INFO("Discord: connected to " << user.username
						<< "#" << user.discriminator << " - " << user.id);
				g_connected = true;
				// Discord forgets our activity across restarts; re-send it
				// on the next main-loop flush (with fresh timestamps)
				std::lock_guard<std::mutex> lock(rpcMutex);
				if (g_hasPresence) g_pendingUpdate = true;
				})
	.onDisconnected([](int errcode, std::string_view message) {
			LOG_INFO("Discord: disconnected (" << errcode << ") - " << message);
			g_connected = false;
			g_lostConnection = true;
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

// Mirrors discord-presence's socket search paths (platform/unix.hpp)
static bool discordSocketExists() {
	namespace fs = std::filesystem;
	const char* xdg = std::getenv("XDG_RUNTIME_DIR");
	fs::path base = xdg ? xdg : "/run/user/" + std::to_string(::getuid());

	for (const char* sub : {"", "snap.discord", "app/com.discordapp.Discord",
			"app/com.discordapp.DiscordCanary"}) {
		for (int i = 0; i < 10; ++i) {
			std::error_code ec;
			if (fs::is_socket(base / sub / ("discord-ipc-" + std::to_string(i)), ec))
				return true;
		}
	}
	return false;
}

// discord-presence can't reconnect on its own: a failed connect attempt leaves
// junk in its socket path buffer, and its backoff (which also delays reading the
// handshake reply) grows to 60s and never resets. So once the connection drops we
// shut the manager down, wait for Discord's socket to show up, then start a fresh
// manager with the backoff reset.
// Must not be called with rpcMutex held (shutdown joins the IO thread, whose
// onReady callback takes rpcMutex).
void rpc_reconnect_if_needed() {
	using clock = std::chrono::steady_clock;
	static constexpr auto POLL_INTERVAL    = std::chrono::seconds(2);
	static constexpr auto CONNECT_TIMEOUT  = std::chrono::seconds(15);
	static bool running = true;   // rpc_initialize() is called at startup
	static auto since   = clock::now();

	auto now = clock::now();
	auto& rpc = discord::RPCManager::get();

	if (running) {
		if (g_connected) {
			since = now;
			return;
		}
		// Connection dropped, or a connect attempt never completed
		if (g_lostConnection.exchange(false) || now - since >= CONNECT_TIMEOUT) {
			LOG_DEBUG("Discord not connected, stopping RPC manager");
			rpc.shutdown();
			running = false;
			since   = now;
		}
		return;
	}

	if (now - since < POLL_INTERVAL) return;
	since = now;
	if (!discordSocketExists()) return;

	LOG_DEBUG("Discord socket found, starting RPC manager");
	discord::Backoff::get().reset();
	g_lostConnection = false;
	rpc.initialize();
	running = true;
}

void rpc_set_current_song(const std::string& details,
		const std::string& state,
		const std::string& largeImageText,
		int64_t startTime,
		int64_t endTime)
{
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentDetails   = details;
	currentState     = state;
	currentImageText = largeImageText;
	LargeImageKey    = "mpd";
	StartTime        = startTime;
	EndTime          = endTime;
	g_hasPresence    = true;
	LOG_DEBUG("rpc_set_current_song: details=" << details);
}

void rpc_clear_presence() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	discord::RPCManager::get().clearPresence();
	g_pendingUpdate = false;
	g_hasPresence   = false;
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
