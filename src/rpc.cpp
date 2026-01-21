#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>
#include <mutex>
#include "logging.hpp"
#include "config.hpp"

constexpr auto APPLICATION_ID = "1343479020918014013";

static int64_t StartTime = 0;
static int64_t EndTime   = 0;

static bool SendPresence = true;
static std::string currentDetails;
static std::string currentState;
static std::string currentImageText;
static std::string LargeImageKey = "mpd";

// Configurable button settings
static std::string Button1Label;
static std::string Button1Url;
static std::string Button2Label;
static std::string Button2Url;

// Thread-safe access to RPC variables
static std::mutex rpcMutex;

static void discordSetup() {
	LOG_DEBUG("Setting up Discord RPC");
	discord::RPCManager::get()
		.setClientID(APPLICATION_ID)
		.onReady([](discord::User const& user) {
				LOG_INFO("Discord: connected to user " 
						<< user.username << "#" << user.discriminator 
						<< " - " << user.id);
				})
	.onDisconnected([](int errcode, std::string_view message) {
			LOG_INFO("Discord: disconnected with error code " 
					<< errcode << " - " << message);
			})
	.onErrored([](int errcode, std::string_view message) {
			LOG_ERROR("Discord: error with code " 
					<< errcode << " - " << message);
			});
}

static void updatePresence() {
	LOG_DEBUG("Updating Discord presence");
	auto& rpc = discord::RPCManager::get();
	if (!SendPresence) {
		LOG_DEBUG("Clearing presence");
		rpc.clearPresence();
		return;
	}

	auto& presence = rpc.getPresence()
		.setDetails(currentDetails)
		.setState(currentState)
		.setLargeImageText(currentImageText)
		.setActivityType(discord::ActivityType::Listening)
		.setStatusDisplayType(discord::StatusDisplayType::Details)
		.setLargeImageKey(LargeImageKey)
		.setStartTimestamp(StartTime)
		.setEndTimestamp(EndTime);

	if (!Button1Label.empty() && !Button1Url.empty()) {
		presence.setButton1(Button1Label, Button1Url);
	}

	if (!Button2Label.empty() && !Button2Url.empty()) {
		presence.setButton2(Button2Label, Button2Url);
	}

	presence.refresh();
}

void rpc_setup() {
	LOG_DEBUG("RPC setup called");
	discordSetup();
}

void rpc_initialize() {
	LOG_DEBUG("Initializing RPC");
	discord::RPCManager::get().initialize();
}

void rpc_shutdown() {
	LOG_DEBUG("Shutting down RPC");
	discord::RPCManager::get().shutdown();
}

void rpc_update_presence() {
	LOG_DEBUG("Updating presence");
	updatePresence();
}

void rpc_set_starttime(int64_t starttime) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	StartTime = starttime;
	LOG_DEBUG("Set start time to: " << starttime);
}

void rpc_set_endtime(int64_t endtime) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	EndTime = endtime;
	LOG_DEBUG("Set end time to: " << endtime);
}

void rpc_set_details(const char* details) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentDetails = details;
	LOG_DEBUG("Set details to: " << details);
}

void rpc_set_state(const char* state) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentState = state;
	LOG_DEBUG("Set state to: " << state);
}

void rpc_set_largeimagetext(const char* imagetext)
{
	std::lock_guard<std::mutex> lock(rpcMutex);
	currentImageText = imagetext;
	LOG_DEBUG("set image text to: " << imagetext);
}

void rpc_set_largeimage(const std::string& url)
{
	std::lock_guard<std::mutex> lock(rpcMutex);
	LargeImageKey = url;
	LOG_DEBUG("Set large image to: " << url);
}

std::string rpc_get_details() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	return currentDetails;
}

std::string rpc_get_state() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	return currentState;
}

std::string rpc_get_largeimagetext() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	return currentImageText;
}

std::string rpc_get_largeimage() {
	std::lock_guard<std::mutex> lock(rpcMutex);
	return LargeImageKey;
}

void rpc_set_button1(const std::string& label, const std::string& url) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	Button1Label = label;
	Button1Url = url;
	LOG_DEBUG("Set button1 to: " << label << " (" << url << ")");
}

void rpc_set_button2(const std::string& label, const std::string& url) {
	std::lock_guard<std::mutex> lock(rpcMutex);
	Button2Label = label;
	Button2Url = url;
	LOG_DEBUG("Set button2 to: " << label << " (" << url << ")");
}

void rpc_load_button_settings() {
	std::string configButton1Label = g_config.getButton1Label();
	std::string configButton1Url = g_config.getButton1Url();
	std::string configButton2Label = g_config.getButton2Label();
	std::string configButton2Url = g_config.getButton2Url();

	if (!configButton1Label.empty()) {
		Button1Label = configButton1Label;
	}
	if (!configButton1Url.empty()) {
		Button1Url = configButton1Url;
	}
	if (!configButton2Label.empty()) {
		Button2Label = configButton2Label;
	}
	if (!configButton2Url.empty()) {
		Button2Url = configButton2Url;
	}

	LOG_DEBUG("Loaded button settings - Button1: '" << Button1Label << "' '" << Button1Url << "'");
	LOG_DEBUG("Loaded button settings - Button2: '" << Button2Label << "' '" << Button2Url << "'");
}
