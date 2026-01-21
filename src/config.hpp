#pragma once

#include <string>
#include <map>

class Config {
	private:
		std::string configFilePath;
		std::map<std::string, std::string> settings;

	public:
		Config(const std::string& filePath);
		bool loadConfig();
		std::string getValue(const std::string& key) const;
		std::string getHost() const;
		int getPort() const;
		std::string getPassword() const;
		std::string getMusicFolder() const;
		std::string getVerbose() const;
		std::string getAlbumArtMethodOrder() const;

		std::string getButton1Label() const;
		std::string getButton1Url() const;
		std::string getButton2Label() const;
		std::string getButton2Url() const;
};

extern Config g_config;
