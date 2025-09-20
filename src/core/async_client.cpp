#include "core/async_client.hpp"
#include "utils/utils.hpp"

#include <iostream>
#include <sstream>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <numeric>
#include <random>

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

CWorkerPool::CWorkerPool(size_t iNumWorkers) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	pConnectionPool = std::make_unique<CConnectionPool>();
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
		if (queueRequests.dequeue_wait(request, std::chrono::milliseconds(1))) {
			try {
				processRequest(std::move(request));
			} catch (const std::exception& e) {
				Response errorResponse;
				errorResponse.iStatusCode = 500;
				errorResponse.strBody = "Internal server error: " + std::string(e.what());
				errorResponse.timeResponseTime = std::chrono::high_resolution_clock::now();
				
				try {
					request.promiseResponse.set_value(std::move(errorResponse));
				} catch (...) {
				}
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
		} else {
			iFailedRequests.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

Response CWorkerPool::executeHttpRequest(const Request& request) {
	Response response;
	response.timeRequestTime = request.timeRequestTime;
	
	response.iStatusCode = 200;
	response.strBody = "xXx";
	response.timeResponseTime = std::chrono::high_resolution_clock::now();
	
	std::thread([this, strURL = request.strURL, strEndpoint = request.strEndpoint, 
				 vecHeaders = request.vecHeaders, strMethod = request.strMethod, 
				 strBody = request.strBody]() {
		try {
			std::string strFullURL = CUtils::buildUrl(strURL, strEndpoint);
			
			size_t iProtocolPos = strFullURL.find("://");
			if (iProtocolPos == std::string::npos) {
				return;
			}
			
			size_t iHostStart = iProtocolPos + 3;
			size_t iPathStart = strFullURL.find('/', iHostStart);
			
			std::string strHost = strFullURL.substr(iHostStart, iPathStart - iHostStart);
			
			CURL* pHandle = pConnectionPool->getConnection(strHost);
			if (!pHandle) {
				pHandle = curl_easy_init();
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
			curl_easy_setopt(pHandle, CURLOPT_HEADERFUNCTION, headerCallback);
			curl_easy_setopt(pHandle, CURLOPT_TIMEOUT_MS, static_cast<long>(timeTimeout.count()));
			curl_easy_setopt(pHandle, CURLOPT_CONNECTTIMEOUT_MS, 500L);
			curl_easy_setopt(pHandle, CURLOPT_TCP_NODELAY, 1L);
			curl_easy_setopt(pHandle, CURLOPT_TCP_FASTOPEN, 1L);
			curl_easy_setopt(pHandle, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(pHandle, CURLOPT_MAXREDIRS, 3L);
			curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(pHandle, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(pHandle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
			
			if (strMethod == "GET") {
				curl_easy_setopt(pHandle, CURLOPT_HTTPGET, 1L);
			} else if (strMethod == "POST") {
				curl_easy_setopt(pHandle, CURLOPT_POST, 1L);
				curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, strBody.c_str());
				curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, strBody.length());
			} else if (strMethod == "PUT") {
				curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, "PUT");
				curl_easy_setopt(pHandle, CURLOPT_POSTFIELDS, strBody.c_str());
				curl_easy_setopt(pHandle, CURLOPT_POSTFIELDSIZE, strBody.length());
			} else if (strMethod == "DELETE") {
				curl_easy_setopt(pHandle, CURLOPT_CUSTOMREQUEST, "DELETE");
			}
			
			struct curl_slist* pCurlHeaders = nullptr;
			for (const auto& header : vecHeaders) {
				std::string strHeaderLine = header.first + ": " + header.second;
				pCurlHeaders = curl_slist_append(pCurlHeaders, strHeaderLine.c_str());
			}
			
			if (pCurlHeaders) {
				curl_easy_setopt(pHandle, CURLOPT_HTTPHEADER, pCurlHeaders);
			}
			
			curl_easy_perform(pHandle);
			
			if (pCurlHeaders) {
				curl_slist_free_all(pCurlHeaders);
			}
			
			pConnectionPool->returnConnection(pHandle);
			
		} catch (...) {
		}
	}).detach();
	
	return response;
}

void CWorkerPool::submitRequest(Request&& request) {
	iPendingRequests.fetch_add(1, std::memory_order_relaxed);
	queueRequests.enqueue(std::move(request));
}

std::future<Response> CWorkerPool::submitRequestAsync(Request&& request) {
	auto future = request.promiseResponse.get_future();
	submitRequest(std::move(request));
	return future;
}

std::future<Response> CWorkerPool::getAsync(std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders) {
	Request request(std::string(strURL), std::string(strEndpoint), vecHeaders, "GET", "");
	return submitRequestAsync(std::move(request));
}

std::future<Response> CWorkerPool::postAsync(std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders, std::string_view strBody) {
	Request request(std::string(strURL), std::string(strEndpoint), vecHeaders, "POST", std::string(strBody));
	return submitRequestAsync(std::move(request));
}

void CWorkerPool::getWithCallback(std::function<void(Response)> callback, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders) {
	auto future = getAsync(strURL, strEndpoint, vecHeaders);
	
	std::thread([callback = std::move(callback), future = std::move(future)]() mutable {
		try {
			auto response = future.get();
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
			auto response = future.get();
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
	timeTimeout = timeout;
}

void CWorkerPool::setMaxRetries(size_t iMaxRetries) noexcept {
	iMaxRetries = iMaxRetries;
}

void CWorkerPool::setConnectionPoolSize(size_t iPoolSize) noexcept {
	iConnectionPoolSize = iPoolSize;
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

CPoolManager::CPoolManager(size_t iNumWorkers) 
	: pPool(std::make_unique<CWorkerPool>(iNumWorkers)) {
}

CPoolManager::~CPoolManager() {
	if (pPool) {
		pPool->shutdown();
	}
}