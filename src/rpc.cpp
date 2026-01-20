#include "rpc.hpp"
#include <discord-rpc.hpp>
#include <iostream>
#include <string>

constexpr auto APPLICATION_ID = "1343479020918014013";

static int64_t StartTime = 0;
static int64_t EndTime   = 0;

static bool SendPresence = true;
static std::string currentDetails = "Coding MPDRPC";
static std::string currentState = "DEV BY JAYNG9663";
static std::string LargeImageKey = "mpd";

static void discordSetup() {
	discord::RPCManager::get()
		.setClientID(APPLICATION_ID)
		.onReady([](discord::User const& user) {
				std::cout << "Discord: connected to user " 
				<< user.username << "#" << user.discriminator 
				<< " - " << user.id << std::endl;
				})
	.onDisconnected([](int errcode, std::string_view message) {
			std::cout << "Discord: disconnected with error code " 
			<< errcode << " - " << message << std::endl;
			})
	.onErrored([](int errcode, std::string_view message) {
			std::cout << "Discord: error with code " 
			<< errcode << " - " << message << std::endl;
			});
}

static void updatePresence() {
	auto& rpc = discord::RPCManager::get();
	if (!SendPresence) {
		rpc.clearPresence();
		return;
	}

	rpc.getPresence()
		.setDetails(currentDetails)
		.setState(currentState)
		.setActivityType(discord::ActivityType::Listening)
		.setStatusDisplayType(discord::StatusDisplayType::State)
		//.setLargeImageKey(LargeImageKey)
		.setStartTimestamp(StartTime)
		.setEndTimestamp(EndTime)
		//.setButton1("Click me!", "")
		//.setButton2("Dont click me!", "https://www.youtube.com/watch?v=dQw4w9WgXcQ")
		.refresh();
}

void rpc_setup() {
	discordSetup();
}

void rpc_initialize() {
	discord::RPCManager::get().initialize();
}

void rpc_shutdown() {
	discord::RPCManager::get().shutdown();
}

void rpc_update_presence() {
	updatePresence();
}

void rpc_set_starttime(int64_t starttime) { StartTime = starttime; }
void rpc_set_endtime(int64_t endtime)     { EndTime = endtime; }

void rpc_set_details(const char* details) {
	currentDetails = details;
}

void rpc_set_state(const char* state) {
	currentState = state;
}

void rpc_set_largeimage(const char* url) {
	LargeImageKey = url;
}
