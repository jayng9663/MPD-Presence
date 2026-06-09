#pragma once
#include <string_view>

/** Central configuration — edit this file to customise all daemon behaviour. **/
namespace config {

	/** Discord application ID. **/
	inline constexpr std::string_view DISCORD_APP_ID = "1343479020918014013";

	/** Activity type **/
	inline constexpr int DISCORD_ACTIVITY_TYPE = 2; // 0=Game, 1=Streaming, 2=Listening, 3=Watching

	/** MPD Host **/
	inline constexpr std::string_view MPD_HOST = "localhost";

	/** MPD port **/
	inline constexpr int MPD_PORT = 6650;

	/** MPD Password **/
	inline constexpr std::string_view MPD_PASSWORD = "";

	/** Local music folder — must end with '/' **/
	inline constexpr std::string_view MUSIC_FOLDER = "/home/user/Music/";

	/** MPD Ignore list (comma-separated paths) **/
	inline constexpr std::string_view MPD_IGNORE = "";

	/** Album art method order (comma-separated) **/
	inline constexpr std::string_view ALBUM_ART_METHOD_ORDER = "fingerprint";
	// inline constexpr std::string_view ALBUM_ART_METHOD_ORDER = "fingerprint,search";

	/** Button 1 **/
	inline constexpr std::string_view BUTTON1LABEL = "";
	inline constexpr std::string_view BUTTON1URL   = "";

	/** Button 2 (empty = disabled) **/
	inline constexpr std::string_view BUTTON2LABEL = "";
	inline constexpr std::string_view BUTTON2URL   = "";

} // namespace config
