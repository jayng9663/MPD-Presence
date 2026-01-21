#pragma once

#include <string>
#include <map>

class Config {
	private:
		std::string configFilePath;
		std::map<std::string, std::string> settings;

	public:
		Config(const std::string& filePath = "mpdrpc.conf");
		bool loadConfig();
		std::string getValue(const std::string& key) const;
		std::string getHost() const;
		int getPort() const;
		std::string getPassword() const;
		std::string getMusicFolder() const;
		std::string getVerbose() const;
		std::string getAlbumArtMethodOrder() const;
};

extern Config g_config;
