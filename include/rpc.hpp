#pragma once
#include <cstdint>
#include <string>

// RPC setup / teardown
void rpc_setup();
void rpc_initialize();
void rpc_shutdown();

// Restart the RPC connection if Discord has been disconnected for a while.
// Call periodically from the main loop.
void rpc_reconnect_if_needed();

// Update presence manually
void rpc_update_presence();

// Set all track metadata atomically (call once per track change)
void rpc_set_current_song(const std::string& details,
		const std::string& state,
		const std::string& largeImageText,
		int64_t startTime,
		int64_t endTime);

void rpc_set_starttime(int64_t starttime);
void rpc_set_endtime(int64_t endtime);
void rpc_set_largeimage(const std::string& url);

void rpc_load_button_settings();
void rpc_set_button1(const std::string& label, const std::string& url);
void rpc_set_button2(const std::string& label, const std::string& url);

// Clear Discord presence entirely (shows nothing in Discord)
void rpc_clear_presence();

// Retry a previously rate-limited update if the window has passed.
// Pass fresh timestamps so the timer stays accurate. Returns true if flushed.
bool rpc_flush_if_pending(int64_t newStartTime, int64_t newEndTime);
