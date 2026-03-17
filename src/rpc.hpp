#pragma once
#include <cstdint>
#include <string>

// RPC setup / teardown
void rpc_setup();
void rpc_initialize();
void rpc_shutdown();

// Update presence manually
void rpc_update_presence();

// Set all track metadata atomically (call once per track change)
void rpc_set_current_song(int songID,
		const std::string& details,
		const std::string& state,
		const std::string& largeImageText,
		int64_t startTime,
		int64_t endTime);

// Change details / state manually
void rpc_set_details(const char* details);
void rpc_set_state(const char* state);
void rpc_set_largeimagetext(const char* imagetext);
void rpc_set_starttime(int64_t starttime);
void rpc_set_endtime(int64_t endtime);
void rpc_set_largeimage(const std::string& url);

void rpc_load_button_settings();
void rpc_set_button1(const std::string& label, const std::string& url);
void rpc_set_button2(const std::string& label, const std::string& url);

// Retry a previously rate-limited update if the window has passed.
// Pass fresh timestamps so the timer stays accurate. Returns true if flushed.
bool rpc_flush_if_pending(int64_t newStartTime, int64_t newEndTime);

// Returns the current song ID — for stale checks inside the art thread.
int rpc_get_current_song_id();

// Atomically apply art + timestamps + push. No-op (returns false) if song changed.
bool rpc_apply_art_if_current(int songID,
		const std::string& cover_url,
		const std::string& page_url,
		const std::string& btn1_label,
		const std::string& btn1_url,
		int64_t startTime,
		int64_t endTime);
