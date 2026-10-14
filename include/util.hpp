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

	// Join items with a separator ("a", "b" + ", " -> "a, b").
	std::string join(const std::vector<std::string>& items, const std::string& sep);

	// Year part of a date tag ("2008-08-14" -> "2008"). Returns the input
	// unchanged if it doesn't start with four digits.
	std::string year_from_date(const std::string& date);

} // namespace util
