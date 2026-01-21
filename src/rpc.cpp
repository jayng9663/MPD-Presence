#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>
#include <mutex>
#include "logging.hpp"

constexpr auto APPLICATION_ID = "1343479020918014013";

static int64_t StartTime = 0;
static int64_t EndTime   = 0;

static bool SendPresence = true;
static std::string currentDetails;
static std::string currentState;
static std::string currentImageText;
static std::string LargeImageKey = "mpd";

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

	rpc.getPresence()
		.setDetails(currentDetails)
		.setState(currentState)
		.setLargeImageText(currentImageText)
		.setActivityType(discord::ActivityType::Listening)
		.setStatusDisplayType(discord::StatusDisplayType::Details)
		.setLargeImageKey(LargeImageKey)
		.setStartTimestamp(StartTime)
		.setEndTimestamp(EndTime)
		.setButton1("ListenBrainz", "https://listenbrainz.org/user/omega101/")
		.setButton2("Create by Jayng9663", "https://github.com/jayng9663/")
		.refresh();
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

