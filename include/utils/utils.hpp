#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <utility>

class CUtils {
public: // jaka glupota nmg
	static std::string urlEncode(std::string_view strInput);
	static std::string urlDecode(std::string_view strInput);
	static std::string buildUrl(std::string_view strBase, std::string_view strEndpoint);
	static std::vector<std::pair<std::string, std::string>> parseHeaders(std::string_view strHeaderString);
};