#pragma once
// Minimal client for the local Discord desktop app's IPC socket, which is what
// Rich Presence uses. Protocol: each message is a frame of
//   uint32 opcode (LE) | uint32 length (LE) | <length> bytes of JSON
// The client sends a handshake ({"v":1,"client_id":...}), waits for the READY
// dispatch, then sends SET_ACTIVITY commands. Everything is non-blocking on the
// read side and meant to be driven from a polling loop on one thread.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace discord_ipc {

enum class Opcode : uint32_t {
	Handshake = 0,
	Frame     = 1,
	Close     = 2,
	Ping      = 3,
	Pong      = 4,
};

struct Frame {
	Opcode      op = Opcode::Frame;
	std::string payload;
};

enum class DecodeResult { Ok, Incomplete, Invalid };

std::string  encode_frame(Opcode op, std::string_view payload);
// Removes one complete frame from the front of buf into out
DecodeResult decode_frame(std::string& buf, Frame& out);

// Socket paths Discord may listen on, in the order they are tried
// (plain install, Flatpak, Snap, Vesktop; discord-ipc-0..9 in each)
std::vector<std::string> socket_candidates();

// Cut s to at most maxChars UTF-8 code points, ending in "…" if it was cut
std::string truncate_utf8(std::string_view s, size_t maxChars);

// j[key] if it is a string, else "" (Discord sends "evt": null etc.)
std::string get_string(const nlohmann::json& j, const char* key);

struct Activity {
	int         type                = 0;  // 0=Playing 1=Streaming 2=Listening 3=Watching 5=Competing
	int         status_display_type = -1; // 0=Name 1=State 2=Details, -1 = omit
	std::string details;
	std::string state;
	std::string large_image; // asset key or URL
	std::string large_text;
	std::string small_image;
	std::string small_text;
	int64_t     start = 0;   // unix seconds, 0 = none
	int64_t     end   = 0;
	std::vector<std::pair<std::string, std::string>> buttons; // label, url (max 2)
};

nlohmann::json activity_json(const Activity& a);
// SET_ACTIVITY command; a == nullptr clears the presence
nlohmann::json set_activity_command(int pid, const Activity* a, const std::string& nonce);

class Client {
public:
	Client() = default;
	~Client();
	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	// Connect to one socket and send the handshake. READY arrives later via poll().
	bool connect(const std::string& path, std::string_view clientId);
	// Try every socket_candidates() path
	bool connect_any(std::string_view clientId);
	void close();

	bool is_open()  const { return fd_ >= 0; }
	bool is_ready() const { return ready_; }
	const std::string& path() const { return path_; }
	// Why the last connection ended (empty if closed by us)
	const std::string& close_reason() const { return closeReason_; }

	bool send(Opcode op, const nlohmann::json& j);
	bool send_raw(Opcode op, std::string_view payload);

	// Read whatever is available without blocking. Answers pings, handles
	// close frames and READY; returns the JSON of every received message frame.
	std::vector<nlohmann::json> poll();

private:
	int         fd_    = -1;
	bool        ready_ = false;
	std::string buf_;
	std::string path_;
	std::string closeReason_;
};

} // namespace discord_ipc
