#include "util.hpp"
#include <sstream>

namespace util {

	std::vector<std::string> split_trim(const std::string& input, char delim) {
		std::vector<std::string> result;
		std::istringstream iss(input);
		std::string item;

		while (std::getline(iss, item, delim)) {
			const auto start = item.find_first_not_of(" \t");
			if (start == std::string::npos) continue; // all whitespace / empty

			const auto end = item.find_last_not_of(" \t");
			result.push_back(item.substr(start, end - start + 1));
		}

		return result;
	}

} // namespace util
