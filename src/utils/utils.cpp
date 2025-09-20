#include "utils/utils.hpp"

#include <sstream>
#include <cctype>
#include <iomanip>
#include <regex>
#include <algorithm>

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

bool CUtils::isValidUrl(std::string_view strUrl) noexcept {
	if (strUrl.empty() || strUrl.length() > MAX_URL_LENGTH) {
		return false;
	}
	
	std::regex urlPattern(R"(^https?://[^\s/$.?#].[^\s]*$)");
	return std::regex_match(strUrl.begin(), strUrl.end(), urlPattern);
}

bool CUtils::isValidHttpUrl(std::string_view strUrl) noexcept {
	if (strUrl.empty() || strUrl.length() > MAX_URL_LENGTH) {
		return false;
	}
	
	return strUrl.substr(0, 7) == "http://" && isValidUrl(strUrl);
}

bool CUtils::isValidHttpsUrl(std::string_view strUrl) noexcept {
	if (strUrl.empty() || strUrl.length() > MAX_URL_LENGTH) {
		return false;
	}
	
	return strUrl.substr(0, 8) == "https://" && isValidUrl(strUrl);
}

bool CUtils::isValidHeaderName(std::string_view strHeaderName) noexcept {
	if (strHeaderName.empty() || strHeaderName.length() > MAX_HEADER_NAME_LENGTH) {
		return false;
	}
	
	for (char c : strHeaderName) {
		if (c <= 31 || c == 127 || c == ' ' || c == ':') {
			return false;
		}
	}
	
	return true;
}

bool CUtils::isValidHeaderValue(std::string_view strHeaderValue) noexcept {
	if (strHeaderValue.length() > MAX_HEADER_VALUE_LENGTH) {
		return false;
	}
	
	for (char c : strHeaderValue) {
		if ((c <= 31 && c != 9) || c == 127) {
			return false;
		}
	}
	
	return true;
}

bool CUtils::isValidHeader(std::string_view strHeaderName, std::string_view strHeaderValue) noexcept {
	return isValidHeaderName(strHeaderName) && isValidHeaderValue(strHeaderValue);
}

bool CUtils::isValidRequestSize(size_t iSize) noexcept {
	return iSize <= MAX_REQUEST_BODY_SIZE;
}

bool CUtils::isValidTimeout(std::chrono::milliseconds timeout) noexcept {
	auto timeoutMs = timeout.count();
	return timeoutMs >= MIN_TIMEOUT_MS && timeoutMs <= MAX_TIMEOUT_MS;
}

bool CUtils::isValidWorkerCount(size_t iWorkerCount) noexcept {
	return iWorkerCount >= MIN_WORKER_COUNT && iWorkerCount <= MAX_WORKER_COUNT;
}