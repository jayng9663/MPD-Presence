#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class Verbosity {
	NONE,
	INFO,
	DEBUG
};

extern Verbosity g_verbosity;

// Get current timestamp
inline std::string get_timestamp() {
	auto now = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()) % 1000;

	std::stringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
	ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
	return ss.str();
}

// Extract just the filename after "src/" for cleaner logging
inline std::string get_clean_filename(const std::string& full_path) {
	size_t src_pos = full_path.find("/src/");
	if (src_pos != std::string::npos) {
		return full_path.substr(src_pos + 1); // +1 to skip the '/'
	}
	return full_path; // fallback to full path
}

// ANSI color codes
#define COLOR_RESET "\033[0m"
#define COLOR_DEBUG "\033[36m"    // Cyan
#define COLOR_INFO "\033[32m"    // Green
#define COLOR_ERROR "\033[31m"   // Red
#define COLOR_TIMESTAMP "\033[33m" // Yellow

#define LOG_DEBUG(msg) \
	do { \
		if (g_verbosity >= Verbosity::DEBUG) { \
			std::string clean_file = get_clean_filename(__FILE__); \
			std::cout << "[" << COLOR_TIMESTAMP << get_timestamp() << COLOR_RESET << "] " \
			<< COLOR_DEBUG << "[DEBUG] " << COLOR_RESET \
			<< clean_file << ":" << __LINE__ << " " \
			<< msg << std::endl; \
		} \
	} while(0)

#define LOG_INFO(msg) \
	do { \
		if (g_verbosity >= Verbosity::INFO) { \
			std::cout << "[" << COLOR_TIMESTAMP << get_timestamp() << COLOR_RESET << "] " \
			<< COLOR_INFO << "[INFO] " << COLOR_RESET \
			<< msg << std::endl; \
		} \
	} while(0)

#define LOG_ERROR(msg) \
	do { \
		std::cerr << "[" << COLOR_TIMESTAMP << get_timestamp() << COLOR_RESET << "] " \
		<< COLOR_ERROR << "[ERROR] " << COLOR_RESET \
		<< msg << std::endl; \
	} while(0)

