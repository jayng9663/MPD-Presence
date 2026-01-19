#pragma once
#include <string>

struct Config {
    std::string discord_app_id;

    std::string mpd_host;
    int         mpd_port = 0;
    std::string mpd_password;
    std::string mpd_musicfolder_root;

    std::string acoustid_api_key;
};

Config load_config(const std::string& path);

