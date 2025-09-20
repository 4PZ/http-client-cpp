#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <chrono>

#include <future>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <thread>
#include <memory>

#include <functional>

#include <curl/curl.h>
#include "utils/utils.hpp"

struct Response {
	unsigned int iStatusCode{0};
	std::string strBody;
	std::vector<std::pair<std::string, std::string>> vecHeaders;
	
	std::chrono::high_resolution_clock::time_point timeRequestTime;
	std::chrono::high_resolution_clock::time_point timeResponseTime;
	
	constexpr bool isSuccess() const noexcept {
		return CUtils::isSuccessStatusCode(iStatusCode);
	}
	
	constexpr bool isError() const noexcept {
		return CUtils::isErrorStatusCode(iStatusCode);
	}
	
	constexpr bool isRedirect() const noexcept {
		return CUtils::isRedirectStatusCode(iStatusCode);
	}
	
	std::chrono::milliseconds getDuration() const noexcept {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			timeResponseTime - timeRequestTime
		);
	}
	
	Response() = default;
	Response(Response&&) = default;
	Response& operator=(Response&&) = default;
	Response(const Response&) = default;
	Response& operator=(const Response&) = default;
};

struct Request {
	std::string strURL;
	std::string strEndpoint;
	std::vector<std::pair<std::string, std::string>> vecHeaders;
	std::string strMethod;
	std::string strBody;
	
	std::chrono::high_resolution_clock::time_point timeRequestTime;
	std::promise<Response> promiseResponse;
	
	template<typename StringType1, typename StringType2, typename StringType3, typename StringType4>
	Request(StringType1&& strURL, StringType2&& strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders, StringType3&& strMethod, StringType4&& strBody) : strURL(std::forward<StringType1>(strURL)), strEndpoint(std::forward<StringType2>(strEndpoint)), vecHeaders(vecHeaders), strMethod(std::forward<StringType3>(strMethod)), strBody(std::forward<StringType4>(strBody)), timeRequestTime(std::chrono::high_resolution_clock::now()), promiseResponse(std::promise<Response>{}) {}
	
	Request(Request&&) = default;
	Request& operator=(Request&&) = default;
	Request(const Request&) = delete;
	Request& operator=(const Request&) = delete;
	~Request() = default;
};

class CFastQueue {
private:
	std::queue<Request> queueRequests;
	mutable std::mutex mutexQueue;
	std::condition_variable conditionQueue;
	
public:
	CFastQueue() = default;
	~CFastQueue() = default;
	
	CFastQueue(const CFastQueue&) = delete;
	CFastQueue& operator=(const CFastQueue&) = delete;
	
	CFastQueue(CFastQueue&& other) noexcept {
		std::lock_guard<std::mutex> lock(other.mutexQueue);
		queueRequests = std::move(other.queueRequests);
	}
	
	CFastQueue& operator=(CFastQueue&& other) noexcept {
		if (this != &other) {
			std::lock(mutexQueue, other.mutexQueue);
			std::lock_guard<std::mutex> lock1(mutexQueue, std::adopt_lock);
			std::lock_guard<std::mutex> lock2(other.mutexQueue, std::adopt_lock);
			queueRequests = std::move(other.queueRequests);
		}
		return *this;
	}
	
	void enqueue(Request requestItem) {
		{
			std::lock_guard<std::mutex> lock(mutexQueue);
			queueRequests.push(std::move(requestItem));
		}
		conditionQueue.notify_one();
	}
	
	bool dequeue(Request& resultRequest) {
		std::unique_lock<std::mutex> lock(mutexQueue);
		if (queueRequests.empty()) {
			return false;
		}
		resultRequest = std::move(queueRequests.front());
		queueRequests.pop();
		return true;
	}
	
	bool dequeue_wait(Request& resultRequest, std::chrono::milliseconds timeout = std::chrono::milliseconds(1)) {
		std::unique_lock<std::mutex> lock(mutexQueue);
		if (conditionQueue.wait_for(lock, timeout, [this] { return !queueRequests.empty(); })) {
			resultRequest = std::move(queueRequests.front());
			queueRequests.pop();
			return true;
		}
		return false;
	}
	
	bool empty() const noexcept {
		std::lock_guard<std::mutex> lock(mutexQueue);
		return queueRequests.empty();
	}
	
	size_t size() const noexcept {
		std::lock_guard<std::mutex> lock(mutexQueue);
		return queueRequests.size();
	}
};

class CConnectionPool {
private:
	struct Connection {
		CURL* pHandle{nullptr};
		std::string strHost;
		std::chrono::high_resolution_clock::time_point timeLastUsed;
		bool bInUse{false};
		
		Connection() = default;
		~Connection() {
			if (pHandle) {
				curl_easy_cleanup(pHandle);
			}
		}
		
		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;
		
		Connection(Connection&& other) noexcept
			: pHandle(other.pHandle)
			, strHost(std::move(other.strHost))
			, timeLastUsed(other.timeLastUsed)
			, bInUse(other.bInUse)
		{
			other.pHandle = nullptr;
		}
		
		Connection& operator=(Connection&& other) noexcept {
			if (this != &other) {
				if (pHandle) {
					curl_easy_cleanup(pHandle);
				}
				pHandle = other.pHandle;
				strHost = std::move(other.strHost);
				timeLastUsed = other.timeLastUsed;
				bInUse = other.bInUse;
				other.pHandle = nullptr;
			}
			return *this;
		}
	};
	
	static constexpr size_t MAX_CONNECTIONS_PER_HOST = 10;
	static constexpr size_t MAX_TOTAL_CONNECTIONS = 100;
	
	std::vector<std::unique_ptr<Connection>> vecConnections;
	std::mutex mutexConnections;
	std::atomic<size_t> iTotalConnections{0};
	
public:
	CConnectionPool() {
		vecConnections.reserve(MAX_TOTAL_CONNECTIONS);
	}
	
	~CConnectionPool() = default;
	
	CURL* getConnection(const std::string& strHost) {
		std::lock_guard<std::mutex> lock(mutexConnections);
		
		for (auto& pConn : vecConnections) {
			if (!pConn->bInUse && pConn->strHost == strHost) {
				pConn->bInUse = true;
				pConn->timeLastUsed = std::chrono::high_resolution_clock::now();
				return pConn->pHandle;
			}
		}
		
		if (iTotalConnections.load() < MAX_TOTAL_CONNECTIONS) {
			auto pConn = std::make_unique<Connection>();
			pConn->pHandle = curl_easy_init();
			pConn->strHost = strHost;
			pConn->bInUse = true;
			pConn->timeLastUsed = std::chrono::high_resolution_clock::now();
			
			curl_easy_setopt(pConn->pHandle, CURLOPT_TCP_NODELAY, 1L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_TCP_FASTOPEN, 1L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_MAXREDIRS, 3L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_SSL_VERIFYHOST, 0L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_TIMEOUT_MS, 1000L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_CONNECTTIMEOUT_MS, 500L);
			curl_easy_setopt(pConn->pHandle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
			
			CURL* pHandle = pConn->pHandle;
			vecConnections.push_back(std::move(pConn));
			iTotalConnections.fetch_add(1);
			return pHandle;
		}
		
		return nullptr;
	}
	
	void returnConnection(CURL* pHandle) {
		if (!pHandle) return;
		
		std::lock_guard<std::mutex> lock(mutexConnections);
		for (auto& pConn : vecConnections) {
			if (pConn->pHandle == pHandle) {
				pConn->bInUse = false;
				pConn->timeLastUsed = std::chrono::high_resolution_clock::now();
				break;
			}
		}
	}
};

class CWorkerPool {
public:
	explicit CWorkerPool(size_t iNumWorkers = std::thread::hardware_concurrency());
	~CWorkerPool();
	
	CWorkerPool(const CWorkerPool&) = delete;
	CWorkerPool& operator=(const CWorkerPool&) = delete;
	CWorkerPool(CWorkerPool&&) = default;
	CWorkerPool& operator=(CWorkerPool&&) = default;
	
	void submitRequest(Request&& request);
	std::future<Response> submitRequestAsync(Request&& request);
	
	std::future<Response> getAsync(std::string_view strURL, std::string_view strEndpoint,const std::vector<std::pair<std::string, std::string>>& vecHeaders = {});
	std::future<Response> postAsync(std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders = {}, std::string_view strBody = "");
	std::future<Response> requestAsync(std::string_view strMethod, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders = {}, std::string_view strBody = "");
	
	void getWithCallback(std::function<void(Response)> callback, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders = {});
	void postWithCallback(std::function<void(Response)> callback, std::string_view strURL, std::string_view strEndpoint, const std::vector<std::pair<std::string, std::string>>& vecHeaders = {}, std::string_view strBody = "");
	
	void setTimeout(std::chrono::milliseconds timeout) noexcept;
	void setMaxRetries(size_t iMaxRetries) noexcept;
	void setConnectionPoolSize(size_t iPoolSize) noexcept;
	
	constexpr size_t getPendingRequestCount() const noexcept;
	constexpr size_t getActiveWorkerCount() const noexcept;
	constexpr bool isRunning() const noexcept;
	
	void shutdown();
	void waitForCompletion();
	
private:
	void workerLoop(size_t iWorkerId);
	void processRequest(Request&& request);
	Response executeHttpRequest(const Request& request);
	
	std::vector<std::thread> vecWorkers;
	CFastQueue queueRequests;
	std::atomic<bool> bShutdownFlag{false};
	std::atomic<size_t> iPendingRequests{0};
	
	std::unique_ptr<CConnectionPool> pConnectionPool;
	
	std::chrono::milliseconds timeTimeout{1000};
	size_t iMaxRetries{1};
	size_t iConnectionPoolSize{50};
	
	mutable std::mutex mutexStats;
	std::atomic<size_t> iTotalRequests{0};
	std::atomic<size_t> iSuccessfulRequests{0};
	std::atomic<size_t> iFailedRequests{0};
};

extern std::unique_ptr<CWorkerPool> pGlobalPool;

class CPoolManager {
public:
	explicit CPoolManager(size_t iNumWorkers = std::thread::hardware_concurrency());
	~CPoolManager();
	
	CWorkerPool& getPool() noexcept { return *pPool; }
	const CWorkerPool& getPool() const noexcept { return *pPool; }
	
private:
	std::unique_ptr<CWorkerPool> pPool;
};