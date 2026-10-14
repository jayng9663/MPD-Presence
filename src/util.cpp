#include "util.hpp"
#include <sstream>
#include <cctype>

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

	std::string join(const std::vector<std::string>& items, const std::string& sep) {
		std::string result;
		for (size_t i = 0; i < items.size(); ++i) {
			if (i > 0) result += sep;
			result += items[i];
		}
		return result;
	}

	std::string year_from_date(const std::string& date) {
		if (date.size() < 4) return date;
		for (size_t i = 0; i < 4; ++i)
			if (!std::isdigit(static_cast<unsigned char>(date[i]))) return date;
		return date.substr(0, 4);
	}

} // namespace util
