#pragma once
#include <string>
#include <vector>

namespace util {

	// Split a string on a delimiter and trim leading/trailing spaces and tabs
	// from each resulting item. Empty items (e.g. from consecutive delimiters
	// or leading/trailing delimiters) are dropped.
	//
	// Shared by main.cpp for both the MPD ignore-list and album-art method
	// order parsing, which previously duplicated this logic inline.
	std::vector<std::string> split_trim(const std::string& input, char delim);

} // namespace util
