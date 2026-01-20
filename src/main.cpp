#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "config.hpp"
#include "rpc.hpp"
#include "mpd.hpp"

std::atomic<bool> keepRunning(true);

void signalHandler(int signum) {
	std::cout << "\nReceived signal " << signum << ", stopping...\n";
	keepRunning = false;
}

Config g_config("mpdrpc.conf");

int main() {
	std::signal(SIGINT, signalHandler);  // Ctrl+C
	std::signal(SIGTERM, signalHandler); // systemd stop signal

	fetchMPDInfo();

	std::cout << "Starting Discord RPC..." << std::endl;

	rpc_setup();
	rpc_initialize();

	while (keepRunning) {
		fetchMPDInfo();

		if (!mpdIsValid()) {
		}

		rpc_set_details(getMPDTitle().c_str());
		rpc_set_state(getMPDAlbum().c_str());

		std::string fingerprint = getMPDFingerprint();
		if (!fingerprint.empty()) {
			std::cout << fingerprint << std::endl;
		}

		int64_t currentTime = std::time(nullptr);
		int64_t elapsed = getMPDElapsed();
		int64_t total = getMPDTotal();

		if (total > 0) {
			int64_t startTime = currentTime - elapsed;
			int64_t endTime = currentTime + (total - elapsed);

			rpc_set_starttime(startTime);
			rpc_set_endtime(endTime);
		} else {
			rpc_set_starttime(0);
			rpc_set_endtime(0);
		}

		rpc_update_presence();
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	rpc_shutdown();
	std::cout << "Discord RPC shutdown complete" << std::endl;
}

