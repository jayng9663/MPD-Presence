#include "discord_ipc.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace discord_ipc {

using nlohmann::json;

// Discord's own messages are small; anything bigger means we lost framing
static constexpr uint32_t MAX_FRAME_SIZE = 64 * 1024;

std::string encode_frame(Opcode op, std::string_view payload) {
	std::string out;
	out.reserve(8 + payload.size());
	auto put32 = [&out](uint32_t v) {
		for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
	};
	put32(static_cast<uint32_t>(op));
	put32(static_cast<uint32_t>(payload.size()));
	out.append(payload);
	return out;
}

DecodeResult decode_frame(std::string& buf, Frame& out) {
	if (buf.size() < 8) return DecodeResult::Incomplete;
	auto get32 = [&buf](size_t off) {
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i)
			v |= static_cast<uint32_t>(static_cast<uint8_t>(buf[off + i])) << (8 * i);
		return v;
	};
	const uint32_t op  = get32(0);
	const uint32_t len = get32(4);
	if (op > static_cast<uint32_t>(Opcode::Pong) || len > MAX_FRAME_SIZE)
		return DecodeResult::Invalid;
	if (buf.size() < 8 + static_cast<size_t>(len)) return DecodeResult::Incomplete;

	out.op = static_cast<Opcode>(op);
	out.payload.assign(buf, 8, len);
	buf.erase(0, 8 + static_cast<size_t>(len));
	return DecodeResult::Ok;
}

std::vector<std::string> socket_candidates() {
	std::vector<std::string> bases;
	auto addBase = [&bases](std::string b) {
		while (b.size() > 1 && b.back() == '/') b.pop_back();
		for (const auto& e : bases) if (e == b) return;
		bases.push_back(std::move(b));
	};
	for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
		if (const char* v = std::getenv(var); v && *v) addBase(v);
	addBase("/run/user/" + std::to_string(::getuid()));
	addBase("/tmp");

	static constexpr const char* subdirs[] = {
		"",
		"app/com.discordapp.Discord/",
		"app/com.discordapp.DiscordCanary/",
		"app/com.discordapp.DiscordPTB/",
		"app/dev.vencord.Vesktop/",
		".flatpak/dev.vencord.Vesktop/xdg-run/",
		"snap.discord/",
		"snap.discord-canary/",
	};

	std::vector<std::string> out;
	for (const auto& base : bases)
		for (const char* sub : subdirs)
			for (int i = 0; i < 10; ++i)
				out.push_back(base + "/" + sub + "discord-ipc-" + std::to_string(i));
	return out;
}

std::string truncate_utf8(std::string_view s, size_t maxChars) {
	if (maxChars == 0) return {};
	auto isLead = [](char c) { return (static_cast<uint8_t>(c) & 0xC0) != 0x80; };

	size_t chars = 0;
	for (char c : s) if (isLead(c)) ++chars;
	if (chars <= maxChars) return std::string(s);

	// Keep maxChars - 1 code points and add an ellipsis
	size_t kept = 0;
	for (size_t i = 0; i < s.size(); ++i) {
		if (isLead(s[i])) {
			if (kept == maxChars - 1) return std::string(s.substr(0, i)) + "\u2026";
			++kept;
		}
	}
	return std::string(s);
}

std::string get_string(const json& j, const char* key) {
	if (!j.is_object()) return {};
	auto it = j.find(key);
	return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

json activity_json(const Activity& a) {
	// Discord's limits: 128 chars for text fields, 32 for button labels
	json j = {{"type", a.type}};
	if (a.status_display_type >= 0) j["status_display_type"] = a.status_display_type;
	if (!a.details.empty()) j["details"] = truncate_utf8(a.details, 128);
	if (!a.state.empty())   j["state"]   = truncate_utf8(a.state, 128);

	json assets = json::object();
	if (!a.large_image.empty()) assets["large_image"] = a.large_image;
	if (!a.large_text.empty())  assets["large_text"]  = truncate_utf8(a.large_text, 128);
	if (!a.small_image.empty()) assets["small_image"] = a.small_image;
	if (!a.small_text.empty())  assets["small_text"]  = truncate_utf8(a.small_text, 128);
	if (!assets.empty()) j["assets"] = std::move(assets);

	// Seconds, as MPD-Presence has always sent; the client accepts them
	json ts = json::object();
	if (a.start > 0) ts["start"] = a.start;
	if (a.end   > 0) ts["end"]   = a.end;
	if (!ts.empty()) j["timestamps"] = std::move(ts);

	json buttons = json::array();
	for (const auto& [label, url] : a.buttons) {
		if (label.empty() || url.empty() || buttons.size() == 2) continue;
		buttons.push_back({{"label", truncate_utf8(label, 32)}, {"url", url}});
	}
	if (!buttons.empty()) j["buttons"] = std::move(buttons);

	return j;
}

json set_activity_command(int pid, const Activity* a, const std::string& nonce) {
	json args = {{"pid", pid}};
	if (a) args["activity"] = activity_json(*a);
	return {{"cmd", "SET_ACTIVITY"}, {"args", std::move(args)}, {"nonce", nonce}};
}

// -- Client --

Client::~Client() { close(); }

void Client::close() {
	if (fd_ >= 0) ::close(fd_);
	fd_    = -1;
	ready_ = false;
	buf_.clear();
}

bool Client::connect(const std::string& path, std::string_view clientId) {
	close();
	closeReason_.clear();

	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	if (path.size() >= sizeof(addr.sun_path)) return false;
	std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

	const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return false;
	if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		::close(fd);
		return false;
	}

	fd_   = fd;
	path_ = path;
	return send(Opcode::Handshake, {{"v", 1}, {"client_id", std::string(clientId)}});
}

bool Client::connect_any(std::string_view clientId) {
	for (const auto& p : socket_candidates())
		if (connect(p, clientId)) return true;
	return false;
}

bool Client::send(Opcode op, const json& j) {
	// Invalid UTF-8 in tags must not throw; replace it with U+FFFD
	return send_raw(op, j.dump(-1, ' ', false, json::error_handler_t::replace));
}

bool Client::send_raw(Opcode op, std::string_view payload) {
	if (fd_ < 0) return false;
	const std::string frame = encode_frame(op, payload);
	size_t off = 0;
	while (off < frame.size()) {
		const ssize_t n = ::send(fd_, frame.data() + off, frame.size() - off, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) {
			closeReason_ = std::string("write failed: ") + std::strerror(errno);
			close();
			return false;
		}
		off += static_cast<size_t>(n);
	}
	return true;
}

std::vector<json> Client::poll() {
	std::vector<json> out;
	if (fd_ < 0) return out;

	bool lost = false;
	char tmp[4096];
	for (;;) {
		const ssize_t n = ::recv(fd_, tmp, sizeof(tmp), MSG_DONTWAIT);
		if (n > 0) { buf_.append(tmp, static_cast<size_t>(n)); continue; }
		if (n == 0) { closeReason_ = "connection closed by Discord"; lost = true; break; }
		if (errno == EINTR) continue;
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			closeReason_ = std::string("read failed: ") + std::strerror(errno);
			lost = true;
		}
		break;
	}

	Frame f;
	for (;;) {
		const DecodeResult r = decode_frame(buf_, f);
		if (r == DecodeResult::Incomplete) break;
		if (r == DecodeResult::Invalid) {
			closeReason_ = "invalid frame from Discord";
			lost = true;
			break;
		}
		if (f.op == Opcode::Ping) {
			if (!send_raw(Opcode::Pong, f.payload)) return out;
			continue;
		}
		if (f.op == Opcode::Close) {
			const json j = json::parse(f.payload, nullptr, false);
			std::string msg = get_string(j, "message");
			if (j.is_object() && j.contains("code") && j["code"].is_number_integer())
				msg += " (code " + std::to_string(j["code"].get<int>()) + ")";
			closeReason_ = "Discord closed the connection: " + msg;
			lost = true;
			break;
		}
		if (f.op != Opcode::Frame) continue;

		json j = json::parse(f.payload, nullptr, false);
		if (j.is_discarded()) continue;
		if (get_string(j, "cmd") == "DISPATCH" && get_string(j, "evt") == "READY")
			ready_ = true;
		out.push_back(std::move(j));
	}

	if (lost) close();
	return out;
}

} // namespace discord_ipc
