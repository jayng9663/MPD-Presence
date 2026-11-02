// Tests for the Discord IPC client: framing, payloads, and a full handshake /
// SET_ACTIVITY / ping / disconnect exchange against a fake Discord socket.
#include "doctest.h"
#include "discord_ipc.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace discord_ipc;
using nlohmann::json;

TEST_CASE("frames round-trip and decode incrementally") {
	std::string buf = encode_frame(Opcode::Frame, R"({"a":1})") + encode_frame(Opcode::Ping, "x");
	CHECK(buf.size() == 8 + 7 + 8 + 1);
	CHECK(buf.substr(0, 8) == std::string("\x01\0\0\0\x07\0\0\0", 8));

	Frame f;
	std::string partial = buf.substr(0, 10);
	CHECK(decode_frame(partial, f) == DecodeResult::Incomplete);

	REQUIRE(decode_frame(buf, f) == DecodeResult::Ok);
	CHECK(f.op == Opcode::Frame);
	CHECK(f.payload == R"({"a":1})");
	REQUIRE(decode_frame(buf, f) == DecodeResult::Ok);
	CHECK(f.op == Opcode::Ping);
	CHECK(buf.empty());

	std::string bad = encode_frame(static_cast<Opcode>(9), "");
	CHECK(decode_frame(bad, f) == DecodeResult::Invalid);
}

TEST_CASE("truncate_utf8 counts code points") {
	CHECK(truncate_utf8("hello", 5) == "hello");
	CHECK(truncate_utf8("hello!", 5) == "hell\u2026");
	CHECK(truncate_utf8("\u3072\u3050\u3089\u3057", 4) == "\u3072\u3050\u3089\u3057");
	CHECK(truncate_utf8("\u3072\u3050\u3089\u3057", 3) == "\u3072\u3050\u2026");
}

TEST_CASE("activity_json omits empty fields and limits buttons") {
	Activity a;
	a.type = 2;
	a.status_display_type = 2;
	a.details = "Title";
	a.large_image = "mpd";
	a.start = 100;
	a.end   = 300;
	a.buttons = {{"", ""}, {"View Album", "https://example.com/a"},
	             {"Two", "https://example.com/b"}, {"Three", "https://example.com/c"}};

	const json j = activity_json(a);
	CHECK(j["type"] == 2);
	CHECK(j["status_display_type"] == 2);
	CHECK(j["details"] == "Title");
	CHECK_FALSE(j.contains("state"));
	CHECK(j["assets"] == json{{"large_image", "mpd"}});
	CHECK(j["timestamps"] == json{{"start", 100}, {"end", 300}});
	REQUIRE(j["buttons"].size() == 2);
	CHECK(j["buttons"][0]["label"] == "View Album");

	Activity paused;
	CHECK_FALSE(activity_json(paused).contains("timestamps"));
	CHECK_FALSE(activity_json(paused).contains("buttons"));

	CHECK_FALSE(set_activity_command(42, nullptr, "1")["args"].contains("activity"));
	CHECK(set_activity_command(42, &a, "1")["args"]["pid"] == 42);
}

TEST_CASE("socket candidates start with XDG_RUNTIME_DIR") {
	::setenv("XDG_RUNTIME_DIR", "/run/user/test/", 1);
	const auto c = socket_candidates();
	REQUIRE(c.size() >= 10);
	CHECK(c[0] == "/run/user/test/discord-ipc-0");
	CHECK(c[9] == "/run/user/test/discord-ipc-9");
}

namespace {
	// Blocking read of one frame on the fake-server side
	bool readFrame(int fd, std::string& buf, Frame& f) {
		char tmp[4096];
		while (decode_frame(buf, f) != DecodeResult::Ok) {
			const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
			if (n <= 0) return false;
			buf.append(tmp, static_cast<size_t>(n));
		}
		return true;
	}
	void writeFrame(int fd, Opcode op, const std::string& payload) {
		const std::string fr = encode_frame(op, payload);
		(void)!::send(fd, fr.data(), fr.size(), MSG_NOSIGNAL);
	}
	template <class Pred>
	bool pollUntil(Client& c, Pred done, std::vector<json>* got = nullptr) {
		for (int i = 0; i < 200; ++i) {
			for (auto& m : c.poll()) if (got) got->push_back(m);
			if (done()) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return false;
	}
}

TEST_CASE("client handshake, SET_ACTIVITY, ping and disconnect against a fake Discord") {
	char dir[] = "/tmp/mpdp-ipc-XXXXXX";
	REQUIRE(::mkdtemp(dir) != nullptr);
	const std::string path = std::string(dir) + "/discord-ipc-0";

	const int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
	REQUIRE(srv >= 0);
	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
	REQUIRE(::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
	REQUIRE(::listen(srv, 1) == 0);

	// Fake Discord: check the handshake, send READY, read one command, ping, close
	Frame handshake, command, pong;
	bool serverOk = false;
	std::thread server([&] {
		const int c = ::accept(srv, nullptr, nullptr);
		if (c < 0) return;
		std::string buf;
		if (!readFrame(c, buf, handshake)) { ::close(c); return; }
		writeFrame(c, Opcode::Frame,
			R"({"cmd":"DISPATCH","evt":"READY","data":{"v":1,"user":{"id":"1","username":"tester"}},"nonce":null})");
		if (!readFrame(c, buf, command)) { ::close(c); return; }
		writeFrame(c, Opcode::Ping, "ping-payload");
		if (!readFrame(c, buf, pong)) { ::close(c); return; }
		serverOk = true;
		writeFrame(c, Opcode::Close, R"({"code":1000,"message":"bye"})");
		::close(c);
	});

	Client client;
	REQUIRE(client.connect(path, "1234"));
	std::vector<json> got;
	REQUIRE(pollUntil(client, [&] { return client.is_ready(); }, &got));
	REQUIRE(got.size() == 1);
	CHECK(get_string(got[0], "evt") == "READY");

	Activity a;
	a.type = 2;
	a.details = "Song";
	REQUIRE(client.send(Opcode::Frame, set_activity_command(7, &a, "1")));
	REQUIRE(pollUntil(client, [&] { return !client.is_open(); }));
	server.join();
	::close(srv);
	::unlink(path.c_str());
	::rmdir(dir);

	CHECK(serverOk);
	CHECK(handshake.op == Opcode::Handshake);
	const json hs = json::parse(handshake.payload);
	CHECK(hs["v"] == 1);
	CHECK(hs["client_id"] == "1234");

	const json cmd = json::parse(command.payload);
	CHECK(cmd["cmd"] == "SET_ACTIVITY");
	CHECK(cmd["args"]["activity"]["details"] == "Song");

	CHECK(pong.op == Opcode::Pong);
	CHECK(pong.payload == "ping-payload");

	CHECK_FALSE(client.is_ready());
	CHECK(client.close_reason() == "Discord closed the connection: bye (code 1000)");
}

TEST_CASE("connect fails cleanly when nothing is listening") {
	Client client;
	CHECK_FALSE(client.connect("/tmp/mpdp-ipc-does-not-exist/discord-ipc-0", "1"));
	CHECK_FALSE(client.is_open());
}
