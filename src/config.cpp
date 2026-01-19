#include "config.h"
#include <fstream>
#include <unordered_map>
#include <stdexcept>

static std::string trim(const std::string& s) {
	const char* ws = " \t\n\r";
	auto start = s.find_first_not_of(ws);
	auto end   = s.find_last_not_of(ws);
	return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

Config load_config(const std::string& path) {
	std::ifstream file(path);
	if (!file)
		throw std::runtime_error("Failed to open config file: " + path);

	std::unordered_map<std::string, std::string> kv;
	std::string line;

	while (std::getline(file, line)) {
		line = trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		auto pos = line.find('=');
		if (pos == std::string::npos)
			continue;

		kv[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
	}

	Config cfg;
	cfg.discord_app_id         = kv.at("DISCORD_APP_ID");
	cfg.mpd_host               = kv.at("MPD_HOST");
	cfg.mpd_port               = std::stoi(kv.at("MPD_PORT"));
	cfg.mpd_password           = kv.at("MPD_PASSWORD");
	cfg.mpd_musicfolder_root   = kv.at("MPD_MUSICFOLDER_ROOT");
	cfg.acoustid_api_key       = kv.at("ACOUSTID_API_KEY");

	return cfg;
}

