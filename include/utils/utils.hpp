#ifndef HTTP_CLIENT_CPP_INCLUDE_UTILS_UTILS_H_
#define HTTP_CLIENT_CPP_INCLUDE_UTILS_UTILS_H_

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CUtils {
 public:
	static std::string urlEncode(std::string_view strInput);
	static std::string urlDecode(std::string_view strInput);
	static std::string buildUrl(std::string_view strBase, std::string_view strEndpoint);
	static std::vector<std::pair<std::string, std::string>> parseHeaders(std::string_view strHeaderString);
	
	static constexpr bool isValidHttpMethod(std::string_view strMethod) noexcept {
		return strMethod == "GET" || strMethod == "POST" || 
		       strMethod == "PUT" || strMethod == "DELETE" || 
		       strMethod == "HEAD" || strMethod == "OPTIONS";
	}
	
	static constexpr bool isSuccessStatusCode(unsigned int iStatusCode) noexcept {
		return iStatusCode >= 200 && iStatusCode < 300;
	}
	
	static constexpr bool isErrorStatusCode(unsigned int iStatusCode) noexcept {
		return iStatusCode >= 400;
	}
	
	static constexpr bool isRedirectStatusCode(unsigned int iStatusCode) noexcept {
		return iStatusCode >= 300 && iStatusCode < 400;
	}
	
	static bool isValidUrl(std::string_view strUrl) noexcept;
	static bool isValidHttpUrl(std::string_view strUrl) noexcept;
	static bool isValidHttpsUrl(std::string_view strUrl) noexcept;
	
	static bool isValidHeaderName(std::string_view strHeaderName) noexcept;
	static bool isValidHeaderValue(std::string_view strHeaderValue) noexcept;
	static bool isValidHeader(std::string_view strHeaderName, std::string_view strHeaderValue) noexcept;
	
	static bool isValidRequestSize(size_t iSize) noexcept;
	static bool isValidTimeout(std::chrono::milliseconds timeout) noexcept;
	static bool isValidWorkerCount(size_t iWorkerCount) noexcept;
	
	static constexpr size_t MAX_URL_LENGTH = 2048;
	static constexpr size_t MAX_HEADER_NAME_LENGTH = 256;
	static constexpr size_t MAX_HEADER_VALUE_LENGTH = 4096;
	static constexpr size_t MAX_REQUEST_BODY_SIZE = 10 * 1024 * 1024;
	static constexpr size_t MIN_TIMEOUT_MS = 100;
	static constexpr size_t MAX_TIMEOUT_MS = 300000; 
	static constexpr size_t MIN_WORKER_COUNT = 1;
	static constexpr size_t MAX_WORKER_COUNT = 100;
};

#endif  // HTTP_CLIENT_CPP_INCLUDE_UTILS_UTILS_H_