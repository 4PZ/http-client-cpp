#include "utils/utils.hpp"

#include <sstream>
#include <cctype>
#include <iomanip>

std::string CUtils::urlEncode(std::string_view strInput) {
	std::ostringstream streamEncoded;
	streamEncoded.fill('0');
	streamEncoded << std::hex;

	for (char c : strInput) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			streamEncoded << c;
		} else {
			streamEncoded << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
		}
	}

	return streamEncoded.str();
}

std::string CUtils::urlDecode(std::string_view strInput) {
	std::string strResult;
	strResult.reserve(strInput.length());

	for (size_t i = 0; i < strInput.length(); ++i) {
		if (strInput[i] == '%' && i + 2 < strInput.length()) {
			std::string strHex = std::string(strInput.substr(i + 1, 2));
			char* pEnd;
			long iValue = std::strtol(strHex.c_str(), &pEnd, 16);
			if (*pEnd == '\0') {
				strResult += static_cast<char>(iValue);
				i += 2;
			} else {
				strResult += strInput[i];
			}
		} else if (strInput[i] == '+') {
			strResult += ' ';
		} else {
			strResult += strInput[i];
		}
	}

	return strResult;
}

std::string CUtils::buildUrl(std::string_view strBase, std::string_view strEndpoint) {
	std::string strResult(strBase);

	if (!strResult.empty() && strResult.back() == '/') {
		strResult.pop_back();
	}

	if (!strEndpoint.empty() && strEndpoint.front() != '/') {
		strResult += '/';
	}

	strResult += strEndpoint;
	return strResult;
}

std::vector<std::pair<std::string, std::string>> CUtils::parseHeaders(std::string_view strHeaderString) {
	std::vector<std::pair<std::string, std::string>> vecHeaders;
	std::istringstream ssStream{std::string(strHeaderString)};
	std::string strLine;

	while (std::getline(ssStream, strLine)) {
		size_t iColonPos = strLine.find(':');
		if (iColonPos != std::string::npos) {
			std::string strKey = strLine.substr(0, iColonPos);
			std::string strValue = strLine.substr(iColonPos + 1);
			
			strKey.erase(0, strKey.find_first_not_of(" \t"));
			strKey.erase(strKey.find_last_not_of(" \t") + 1);
			strValue.erase(0, strValue.find_first_not_of(" \t"));
			strValue.erase(strValue.find_last_not_of(" \t") + 1);
			
			vecHeaders.emplace_back(std::move(strKey), std::move(strValue));
		}
	}

	return vecHeaders;
}