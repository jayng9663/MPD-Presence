#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

// Small helper shared by test files: read a fixture file under
// tests/fixtures/ into a string. MPD_PRESENCE_FIXTURES_DIR is injected by
// tests/CMakeLists.txt via target_compile_definitions.
inline std::string read_fixture(const std::string& filename) {
#ifndef MPD_PRESENCE_FIXTURES_DIR
#error "MPD_PRESENCE_FIXTURES_DIR must be defined by the build system"
#endif
	const std::string path = std::string(MPD_PRESENCE_FIXTURES_DIR) + "/" + filename;
	std::ifstream f(path, std::ios::binary);
	if (!f) throw std::runtime_error("Could not open fixture: " + path);

	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}
