#include "config.hpp"
#include <fstream>
#include <iostream>

Config::Config(const std::string& filePath) : configFilePath(filePath) {}

bool Config::loadConfig() {
	std::ifstream file(configFilePath);
	if (!file.is_open()) {
		std::cerr << "Failed to open config file: " << configFilePath << std::endl;
		return false;
	}

	std::string line;
	std::string currentSection = "";

	while (std::getline(file, line)) {
		// Remove whitespace
		line.erase(0, line.find_first_not_of(" \t"));
		line.erase(line.find_last_not_of(" \t") + 1);

		// Skip empty lines and comments
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}

		// Check for section headers
		if (line.front() == '[' && line.back() == ']') {
			currentSection = line.substr(1, line.length() - 2);
			continue;
		}

		// Parse key-value pairs
		size_t pos = line.find('=');
		if (pos != std::string::npos) {
			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);

			// Remove whitespace from key and value
			key.erase(0, key.find_first_not_of(" \t"));
			key.erase(key.find_last_not_of(" \t") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);

			// Remove surrounding quotes
			if ((value.front() == '"' || value.front() == '\'') &&
					(value.back() == '"' || value.back() == '\'')) {
				value = value.substr(1, value.length() - 2);
			}

			// Store in map
			settings[key] = value;
		}
	}

	file.close();
	return true;
}

std::string Config::getValue(const std::string& key) const {
	auto it = settings.find(key);
	if (it != settings.end()) {
		return it->second;
	}
	return "";
}

std::string Config::getHost() const {
	return getValue("host");
}

int Config::getPort() const {
	std::string portStr = getValue("port");
	return portStr.empty() ? 0 : std::stoi(portStr);
}

std::string Config::getPassword() const {
	return getValue("password");
}

std::string Config::getMusicFolder() const {
	return getValue("music_folder");
}

