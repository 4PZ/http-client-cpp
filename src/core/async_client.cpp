#include "core/async_client.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>

#include "utils/utils.hpp"

std::unique_ptr<CWorkerPool> pGlobalPool = nullptr;

static size_t writeCallback(void* pContents, size_t iSize, size_t iNmemb, std::string* pData) {
	size_t iTotalSize = iSize * iNmemb;
	pData->append(static_cast<char*>(pContents), iTotalSize);
	return iTotalSize;
}

static size_t headerCallback(char* pBuffer, size_t iSize, size_t iNmemb, std::vector<std::pair<std::string, std::string>>* pHeaders) {
	size_t iTotalSize = iSize * iNmemb;
    std::string_view strHeader(pBuffer, iTotalSize);
  
    if (!strHeader.empty() && strHeader.back() == '\n') {
    	strHeader.remove_suffix(1);
  	}
  	if (!strHeader.empty() && strHeader.back() == '\r') {
    	strHeader.remove_suffix(1);
  	}
  
  	size_t iColonPos = strHeader.find(':');
  	if (iColonPos != std::string_view::npos) {
    	std::string strKey(strHeader.substr(0, iColonPos));
    	std::string strValue(strHeader.substr(iColonPos + 1));
    
    	strKey.erase(0, strKey.find_first_not_of(" \t"));
    	strKey.erase(strKey.find_last_not_of(" \t") + 1);
    	strValue.erase(0, strValue.find_first_not_of(" \t"));
    	strValue.erase(strValue.find_last_not_of(" \t") + 1);
    
    	pHeaders->emplace_back(std::move(strKey), std::move(strValue));
  	}
  
  	return iTotalSize;
}

CWorkerPool::CWorkerPool(size_t iNumWorkers) : pConnectionPool(std::make_unique<CConnectionPool>()),vecWorkers(), bShutdownFlag(false), iPendingRequests(0), timeTimeout(1000), iMaxRetries(1), iConnectionPoolSize(50), iTotalRequests(0), iSuccessfulRequests(0), iFailedRequests(0) {
  	if (!CUtils::isValidWorkerCount(iNumWorkers)) {
    	throw std::invalid_argument("Invalid worker count: " + std::to_string(iNumWorkers) + 
        	" (must be between " + std::to_string(CUtils::MIN_WORKER_COUNT) + 
        	" and " + std::to_string(CUtils::MAX_WORKER_COUNT) + ")");
  	}
  
  	curl_global_init(CURL_GLOBAL_DEFAULT);
  	vecWorkers.reserve(iNumWorkers);
  
  	for (size_t i = 0; i < iNumWorkers; ++i) {
    	vecWorkers.emplace_back(&CWorkerPool::workerLoop, this, i);
  	}
}

CWorkerPool::~CWorkerPool() {
	shutdown();
	curl_global_cleanup();
}

void CWorkerPool::workerLoop(size_t iWorkerId) {
	Request request("", "", {}, "", "");
	
	while (!bShutdownFlag.load(std::memory_order_relaxed)) {
		if (queueRequests.dequeue_wait(request, std::chrono::milliseconds(100))) {  
			try {
				processRequest(std::move(request));
			} catch (const std::exception& e) {
				std::cerr << "Worker " << iWorkerId << " exception: " << e.what() << std::endl;
			}
			
			iPendingRequests.fetch_sub(1, std::memory_order_relaxed);
		}
	}
}

void CWorkerPool::processRequest(Request&& request) {
	Response response = executeHttpRequest(request);
	request.promiseResponse.set_value(std::move(response));
	
	{
		std::lock_guard<std::mutex> lock(mutexStats);
		iTotalRequests.fetch_add(1, std::memory_order_relaxed);
		
		if (response.isSuccess()) {
			iSuccessfulRequests.fetch_add(1, std::memory_order_relaxed);
		} else if (response.isError()) {
			iFailedRequests.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

Response CWorkerPool::executeHttpRequest(const Request& request) {
	Response response;
	response.timeRequestTime = request.timeRequestTime;
	
	try {
		std::string strFullURL = CUtils::buildUrl(request.strURL, request.strEndpoint);
		
		size_t iProtocolPos = strFullURL.find("://");
		if (iProtocolPos == std::string::npos) {
			response.iStatusCode = 400;
			response.strBody = "Invalid URL";
			response.timeResponseTime = std::chrono::high_resolution_clock::now();
			return response;
		}
		
		size_t iHostStart = iProtocolPos + 3;
		size_t iPathStart = strFullURL.find('/', iHostStart);
		
		std::string strHost = strFullURL.substr(iHostStart, iPathStart - iHostStart);
		
		CURL* pHandle = pConnectionPool->getConnection(strHost);
		if (!pHandle) {
			pHandle = curl_easy_init();
			if (!pHandle) {
				response.iStatusCode = 500;
				response.strBody = "Failed to initialize CURL";
				response.timeResponseTime = std::chrono::high_resolution_clock::now();
				return response;
			}
			curl_easy_setopt(pHandle, CURLOPT_TCP_NODELAY, 1L);
			curl_easy_setopt(pHandle, CURLOPT_TCP_FASTOPEN, 1L);
			curl_easy_setopt(pHandle, CURLOPT_MAXREDIRS, 3L);
			curl_easy_setopt(pHandle, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(pHandle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeTimeout.count()));
			curl_easy_setopt(pHandle, CURLOPT_CONNECTTIMEOUT_MS, 500L);
			curl_easy_setopt(pHandle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
		}
		
		curl_easy_reset(pHandle);
		
		curl_easy_setopt(pHandle, CURLOPT_URL, strFullURL.c_str());
		curl_easy_setopt(pHandle, CURLOPT_WRITEFUNCTION, writeCallback);
		curl_easy_setopt(pHandle, CURLOPT_WRITEDATA, &response.strBody);
		curl_easy_setopt(pHandle, CURLOPT_HEADERFUNCTION, headerCallback);
		curl_easy_setopt(pHandle, CURLOPT_HEADERDATA, &response.vecHeaders);
		curl_easy_setopt(pHandle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeTimeout.count()));
		curl_easy_setopt(pHandle, CURLOPT_CONNECTTIMEOUT_MS, 500L);
		curl_easy_setopt(pHandle, CURLOPT_TCP_NODELAY, 1L);
		curl_easy_setopt(pHandle, CURLOPT_TCP_FASTOPEN, 1L);
		curl_easy_setopt(pHandle, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(pHandle, CURLOPT_MAXREDIRS, 3L);
		curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(pHandle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
		
		if (!CUtils::isValidHttpMethod(request.strMethod)) {
			response.iStatusCode = 400;
			response.strBody = "Invalid HTTP method: " + request.strMethod;
			response.timeResponseTime = std::chrono::high_resolution_clock::now();
			pConnectionPool->returnConnection(pHandle);
			return response;
		}
		
		if (request.strMethod == "GET") {
			curl_easy_setopt(pHandle, CURLOPT_HTTPGET, 1L);
		} else if (request.strMethod == "POST") {
			curl_easy_setopt(pHandle, CURLOPT_POST, 1L);
			curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, request.strBody.c_str());
			curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, request.strBody.length());
		} else if (request.strMethod == "PUT") {
			curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, "PUT");
			curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, request.strBody.c_str());
			curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, request.strBody.length());
		} else if (request.strMethod == "DELETE") {
			curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, "DELETE");
		} else if (request.strMethod == "HEAD") {
			curl_easy_setopt(pHandle, CURLOPT_NOBODY, 1L);
		} else if (request.strMethod == "OPTIONS") {
			curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, "OPTIONS");
		}
		
		struct curl_slist* pCurlHeaders = nullptr;
		for (const auto& header : request.vecHeaders) {
			std::string strHeaderLine = header.first + ": " + header.second;
			pCurlHeaders = curl_slist_append(pCurlHeaders, strHeaderLine.c_str());
		}
		
		if (pCurlHeaders) {
			curl_easy_setopt(pHandle, CURLOPT_HTTPHEADER, pCurlHeaders);
		}
		
		CURLcode res = curl_easy_perform(pHandle);
		
		if (pCurlHeaders) {
			curl_slist_free_all(pCurlHeaders);
		}
		
		if (res == CURLE_OK) {
			long httpCode = 0;
			curl_easy_getinfo(pHandle, CURLINFO_RESPONSE_CODE, &httpCode);
			response.iStatusCode = static_cast<unsigned int>(httpCode);
		} else {
			switch (res) {
				case CURLE_OPERATION_TIMEDOUT:
					response.iStatusCode = 408;  
					response.strBody = "Request timeout";
					break;
				case CURLE_COULDNT_CONNECT:
				case CURLE_COULDNT_RESOLVE_HOST:
					response.iStatusCode = 503; 
					response.strBody = "Connection failed";
					break;
				case CURLE_SSL_CONNECT_ERROR:
					response.iStatusCode = 502;
					response.strBody = "SSL connection error";
					break;
				default:
					response.iStatusCode = 500; 
					response.strBody = "CURL error: " + std::string(curl_easy_strerror(res));
					break;
			}
		}
		
		response.timeResponseTime = std::chrono::high_resolution_clock::now();
		
		pConnectionPool->returnConnection(pHandle);
		
	} catch (const std::exception& e) {
		response.iStatusCode = 500;
		response.strBody = "Exception: " + std::string(e.what());
		response.timeResponseTime = std::chrono::high_resolution_clock::now();
	} catch (...) {
		response.iStatusCode = 500;
		response.strBody = "Unknown exception occurred";
		response.timeResponseTime = std::chrono::high_resolution_clock::now();
	}
	
	return response;
}

void CWorkerPool::submitRequest(Request&& request) {
	constexpr size_t MAX_QUEUE_SIZE = 10000;
	
	if (queueRequests.size() >= MAX_QUEUE_SIZE) {
		Response errorResponse;
		errorResponse.iStatusCode = 503;
		errorResponse.strBody = "Service temporarily unavailable - queue full";
		errorResponse.timeResponseTime = std::chrono::high_resolution_clock::now();
		request.promiseResponse.set_value(std::move(errorResponse));
		return;
	}
	
	size_t pendingCount = iPendingRequests.load(std::memory_order_relaxed);
	if (pendingCount > 5000) { 
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	
	iPendingRequests.fetch_add(1, std::memory_order_relaxed);
	queueRequests.enqueue(std::move(request));
}

std::future<Response> CWorkerPool::submitRequestAsync(Request&& request) {
	auto future = request.promiseResponse.get_future();
	submitRequest(std::move(request));
	return future;
}

std::future<Response> CWorkerPool::getAsync(std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders) {
	if (!CUtils::isValidHttpMethod("GET")) {
		throw std::invalid_argument("Invalid HTTP method: GET");
	}
	
	std::string strFullURL = CUtils::buildUrl(strURL, strEndpoint);
	if (!CUtils::isValidUrl(strFullURL)) {
		throw std::invalid_argument("Invalid URL: " + strFullURL);
	}
	
	for (const auto& header : vecHeaders) {
		if (!CUtils::isValidHeader(header.first, header.second)) {
			throw std::invalid_argument("Invalid header: " + header.first + ": " + header.second);
		}
	}
	
	Request request(std::string(strURL), std::string(strEndpoint), vecHeaders, "GET", "");
	return submitRequestAsync(std::move(request));
}

std::future<Response> CWorkerPool::postAsync(std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders, std::string_view strBody) {
	if (!CUtils::isValidHttpMethod("POST")) {
		throw std::invalid_argument("Invalid HTTP method: POST");
	}
	
	std::string strFullURL = CUtils::buildUrl(strURL, strEndpoint);
	if (!CUtils::isValidUrl(strFullURL)) {
		throw std::invalid_argument("Invalid URL: " + strFullURL);
	}
	
	if (!CUtils::isValidRequestSize(strBody.length())) {
		throw std::invalid_argument("Request body too large: " + std::to_string(strBody.length()) + " bytes");
	}
	
	for (const auto& header : vecHeaders) {
		if (!CUtils::isValidHeader(header.first, header.second)) {
			throw std::invalid_argument("Invalid header: " + header.first + ": " + header.second);
		}
	}
	
	Request request(std::string(strURL), std::string(strEndpoint), vecHeaders, "POST", std::string(strBody));
	return submitRequestAsync(std::move(request));
}

std::future<Response> CWorkerPool::requestAsync(std::string_view strMethod, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders, std::string_view strBody) {
	if (!CUtils::isValidHttpMethod(strMethod)) {
		throw std::invalid_argument("Invalid HTTP method: " + std::string(strMethod));
	}
	
	std::string strFullURL = CUtils::buildUrl(strURL, strEndpoint);
	if (!CUtils::isValidUrl(strFullURL)) {
		throw std::invalid_argument("Invalid URL: " + strFullURL);
	}
	
	if (!CUtils::isValidRequestSize(strBody.length())) {
		throw std::invalid_argument("Request body too large: " + std::to_string(strBody.length()) + " bytes");
	}
	
	for (const auto& header : vecHeaders) {
		if (!CUtils::isValidHeader(header.first, header.second)) {
			throw std::invalid_argument("Invalid header: " + header.first + ": " + header.second);
		}
	}
	
	Request request(std::string(strURL), std::string(strEndpoint), vecHeaders, std::string(strMethod), std::string(strBody));
	return submitRequestAsync(std::move(request));
}

void CWorkerPool::getWithCallback(std::function<void(Response)> callback, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders) {
	auto future = getAsync(strURL, strEndpoint, vecHeaders);
	
	std::thread([callback = std::move(callback), future = std::move(future)]() mutable {
		try {
			const auto response = future.get();
			callback(std::move(response));
		} catch (const std::exception& e) {
			Response errorResponse;
			errorResponse.iStatusCode = 500;
			errorResponse.strBody = "Callback error: " + std::string(e.what());
			errorResponse.timeResponseTime = std::chrono::high_resolution_clock::now();
			callback(std::move(errorResponse));
		}
	}).detach();
}

void CWorkerPool::postWithCallback(std::function<void(Response)> callback, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders, std::string_view strBody) {
	auto future = postAsync(strURL, strEndpoint, vecHeaders, strBody);
	
	std::thread([callback = std::move(callback), future = std::move(future)]() mutable {
		try {
			const auto response = future.get();
			callback(std::move(response));
		} catch (const std::exception& e) {
			Response errorResponse;
			errorResponse.iStatusCode = 500;
			errorResponse.strBody = "Callback error: " + std::string(e.what());
			errorResponse.timeResponseTime = std::chrono::high_resolution_clock::now();
			callback(std::move(errorResponse));
		}
	}).detach();
}

void CWorkerPool::setTimeout(std::chrono::milliseconds timeout) noexcept {
	if (CUtils::isValidTimeout(timeout)) {
		timeTimeout = timeout;
	} else {
		timeTimeout = std::chrono::milliseconds(1000);
	}
}

void CWorkerPool::setMaxRetries(size_t iMaxRetries) noexcept {
	if (iMaxRetries <= 10) {
		this->iMaxRetries = iMaxRetries;
	} else {
		this->iMaxRetries = 3; 
	}
}

void CWorkerPool::setConnectionPoolSize(size_t iPoolSize) noexcept {
	if (iPoolSize >= 1 && iPoolSize <= 1000) {
		iConnectionPoolSize = iPoolSize;
	} else {
		iConnectionPoolSize = 50; 
	}
}

size_t CWorkerPool::getPendingRequestCount() const noexcept {
	return iPendingRequests.load(std::memory_order_relaxed);
}

size_t CWorkerPool::getActiveWorkerCount() const noexcept {
	return vecWorkers.size();
}

bool CWorkerPool::isRunning() const noexcept {
	return !bShutdownFlag.load(std::memory_order_relaxed);
}

void CWorkerPool::shutdown() {
	bShutdownFlag.store(true, std::memory_order_relaxed);
	
	for (auto& worker : vecWorkers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
	
	vecWorkers.clear();
}

void CWorkerPool::waitForCompletion() {
	while (iPendingRequests.load(std::memory_order_relaxed) > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

CPoolManager::CPoolManager(size_t iNumWorkers) : pPool(std::make_unique<CWorkerPool>(iNumWorkers)) {
}

CPoolManager::~CPoolManager() {
	if (pPool) {
		pPool->shutdown();
	}
}