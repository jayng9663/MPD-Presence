#include "rpc.hpp"
#include "discord_ipc.hpp"

#include <charconv>
#include <chrono>
#include <string>
#include <unistd.h>
#include "logger.hpp"
#include "config.hpp"

// Presence is sent to the local Discord app through discord_ipc::Client.
// Everything here runs on the main thread: rpc_poll() (called every main-loop
// iteration) connects, reconnects and reads Discord's replies.

static discord_ipc::Client   g_ipc;
static discord_ipc::Activity g_activity;
static std::string           g_appId;

static std::string Button1Label;
static std::string Button1Url;
static std::string Button2Label;
static std::string Button2Url;

static constexpr int64_t DISCORD_RATE_LIMIT_SECONDS = 16;
static int64_t g_lastPushTime  = 0;
static bool    g_pendingUpdate = false;
static bool    g_hasPresence   = false; // false while idle/cleared
static int     g_nonce         = 0;

static int64_t nowSeconds() {
	return static_cast<int64_t>(
			std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
}

static bool sendActivity(bool clear) {
	if (!g_ipc.is_ready()) return false;
	g_activity.buttons = {{Button1Label, Button1Url}, {Button2Label, Button2Url}};
	return g_ipc.send(discord_ipc::Opcode::Frame,
			discord_ipc::set_activity_command(static_cast<int>(::getpid()),
				clear ? nullptr : &g_activity, std::to_string(++g_nonce)));
}

// Send now, or hold the update until the rate-limit window has passed
// (or until Discord is connected)
static void pushPresenceOrDefer() {
	const int64_t now = nowSeconds();

	if (!g_ipc.is_ready()) {
		LOG_DEBUG("Presence update deferred (Discord not connected)");
		g_pendingUpdate = true;
		return;
	}
	if (now - g_lastPushTime < DISCORD_RATE_LIMIT_SECONDS) {
		LOG_DEBUG("Presence update deferred (rate limit, "
				<< (DISCORD_RATE_LIMIT_SECONDS - (now - g_lastPushTime))
				<< "s remaining)");
		g_pendingUpdate = true;
		return;
	}

	g_pendingUpdate = !sendActivity(false);
	if (!g_pendingUpdate) g_lastPushTime = now;
}

static void handleMessage(const nlohmann::json& msg) {
	const std::string cmd = discord_ipc::get_string(msg, "cmd");
	const std::string evt = discord_ipc::get_string(msg, "evt");
	const nlohmann::json data = msg.is_object() && msg.contains("data") ? msg["data"] : nlohmann::json();

	if (cmd == "DISPATCH" && evt == "READY") {
		const nlohmann::json user = data.is_object() && data.contains("user") ? data["user"] : nlohmann::json();
		LOG_INFO("Discord: connected to " << discord_ipc::get_string(user, "username")
				<< " - " << discord_ipc::get_string(user, "id"));
		// Discord forgets our activity across restarts; re-send it on the
		// next main-loop flush (with fresh timestamps)
		if (g_hasPresence) g_pendingUpdate = true;
	} else if (evt == "ERROR") {
		LOG_WARN("Discord: " << (cmd.empty() ? "request" : cmd) << " failed - "
				<< discord_ipc::get_string(data, "message"));
	}
}

// -- Public API --

void rpc_setup() {
	const std::string_view id = config::DISCORD_APP_ID;
	uint64_t parsed = 0;
	auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), parsed);
	if (ec != std::errc() || ptr != id.data() + id.size() || parsed == 0)
		LOG_ERR("DISCORD_APP_ID is not a valid application ID: '" << id << "'");
	g_appId = std::string(id);

	g_activity.type                = config::DISCORD_ACTIVITY_TYPE;
	g_activity.status_display_type = 2; // show the track title (details) in the member list
	g_activity.large_image         = "mpd";
}

void rpc_initialize() {
	LOG_DEBUG("Connecting to Discord");
	rpc_poll();
}

void rpc_shutdown() {
	if (g_ipc.is_ready() && g_hasPresence) sendActivity(true);
	g_ipc.close();
}

void rpc_poll() {
	using clock = std::chrono::steady_clock;
	static constexpr auto RETRY_INTERVAL    = std::chrono::seconds(2);
	static constexpr auto HANDSHAKE_TIMEOUT = std::chrono::seconds(10);
	static clock::time_point lastAttempt{};
	static bool wasOpen = false;

	const auto now = clock::now();

	if (!g_ipc.is_open()) {
		if (now - lastAttempt < RETRY_INTERVAL) return;
		lastAttempt = now;
		if (!g_ipc.connect_any(g_appId)) return;
		LOG_DEBUG("Discord: opened " << g_ipc.path() << ", waiting for READY");
		wasOpen = true;
	}

	for (const auto& msg : g_ipc.poll()) handleMessage(msg);

	if (g_ipc.is_open() && !g_ipc.is_ready() && now - lastAttempt >= HANDSHAKE_TIMEOUT) {
		LOG_WARN("Discord: no READY from " << g_ipc.path() << ", reconnecting");
		g_ipc.close();
	}

	if (!g_ipc.is_open() && wasOpen) {
		wasOpen = false;
		const std::string& why = g_ipc.close_reason();
		LOG_INFO("Discord: disconnected" << (why.empty() ? "" : " - " + why));
		if (g_hasPresence) g_pendingUpdate = true;
		lastAttempt = now;
	}
}

void rpc_set_current_song(const std::string& details,
		const std::string& state,
		const std::string& largeImageText,
		int64_t startTime,
		int64_t endTime)
{
	g_activity.details     = details;
	g_activity.state       = state;
	g_activity.large_text  = largeImageText;
	g_activity.large_image = "mpd";
	g_activity.start       = startTime;
	g_activity.end         = endTime;
	g_hasPresence          = true;
	LOG_DEBUG("rpc_set_current_song: details=" << details);
}

void rpc_clear_presence() {
	sendActivity(true);
	g_pendingUpdate = false;
	g_hasPresence   = false;
	LOG_DEBUG("Discord presence cleared");
}

void rpc_update_presence() {
	LOG_DEBUG("Updating Discord presence");
	pushPresenceOrDefer();
}

void rpc_set_starttime(int64_t v) {
	g_activity.start = v;
	LOG_DEBUG("StartTime = " << v);
}

void rpc_set_endtime(int64_t v) {
	g_activity.end = v;
	LOG_DEBUG("EndTime = " << v);
}

void rpc_set_largeimage(const std::string& v) {
	g_activity.large_image = v;
	LOG_DEBUG("LargeImage = " << v);
}

void rpc_set_paused(bool paused) {
	g_activity.small_image = std::string(paused ? config::SMALL_IMAGE_PAUSED : config::SMALL_IMAGE_PLAYING);
	g_activity.small_text  = g_activity.small_image.empty() ? "" : (paused ? "Paused" : "Playing");
	LOG_DEBUG("SmallImage = " << g_activity.small_image);
}

void rpc_set_button1(const std::string& label, const std::string& url) {
	Button1Label = label;
	Button1Url   = url;
	LOG_DEBUG("Button1 = " << label << " (" << url << ")");
}

void rpc_set_button2(const std::string& label, const std::string& url) {
	Button2Label = label;
	Button2Url   = url;
	LOG_DEBUG("Button2 = " << label << " (" << url << ")");
}

bool rpc_flush_if_pending(int64_t newStartTime, int64_t newEndTime) {
	if (!g_pendingUpdate || !g_hasPresence || !g_ipc.is_ready()) return false;

	const int64_t now = nowSeconds();
	if (now - g_lastPushTime < DISCORD_RATE_LIMIT_SECONDS) return false;

	g_activity.start = newStartTime;
	g_activity.end   = newEndTime;

	if (!sendActivity(false)) return false;
	g_lastPushTime  = now;
	g_pendingUpdate = false;
	LOG_DEBUG("Flushed pending presence update");
	return true;
}

void rpc_load_button_settings() {
	if (!config::BUTTON1LABEL.empty()) Button1Label = std::string(config::BUTTON1LABEL);
	if (!config::BUTTON1URL.empty())   Button1Url   = std::string(config::BUTTON1URL);
	if (!config::BUTTON2LABEL.empty()) Button2Label = std::string(config::BUTTON2LABEL);
	if (!config::BUTTON2URL.empty())   Button2Url   = std::string(config::BUTTON2URL);

	LOG_DEBUG("Button1: '" << Button1Label << "' -> '" << Button1Url << "'");
	LOG_DEBUG("Button2: '" << Button2Label << "' -> '" << Button2Url << "'");
}
