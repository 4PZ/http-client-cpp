#include "core/async_client.hpp"

#include <iostream>

#include <vector>

#include <chrono>
#include <iomanip>
#include <algorithm>
#include <numeric>

#include <thread>

constexpr double EXCELLENT_RPS_THRESHOLD = 100.0;
constexpr double GOOD_RPS_THRESHOLD = 50.0;
constexpr double AVERAGE_RPS_THRESHOLD = 20.0;

void runPerformanceTest() {
    constexpr int iTotalRequests = 1000000; 
    const int iNumWorkers = std::thread::hardware_concurrency(); 

    std::cout << "configuration:" << std::endl;
    std::cout << "total requests: " << iTotalRequests << std::endl;
    std::cout << "worker threads: " << iNumWorkers << std::endl;
    std::cout << "target: https://instagram.com/ajax/bz/" << std::endl;
    std::cout << std::endl;
    
    CWorkerPool pool(iNumWorkers);
    pool.setTimeout(std::chrono::milliseconds(1000)); 
    
    std::cout << "starting performance test..." << std::endl;

    std::vector<std::pair<std::string, std::string>> vecHeaders;
    // not giving these ones 
    vecHeaders.push_back(std::make_pair("Cookie", "shbid=9985;shbts=1"));

    auto timeStartTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<Response>> vecFutures;
    vecFutures.reserve(iTotalRequests);
    
    for (int i = 0; i < iTotalRequests; ++i) {
        auto future = pool.getAsync("https://instagram.com", "/ajax/bz/");
        vecFutures.push_back(std::move(future));
    }
    
    auto timeEndTime = std::chrono::high_resolution_clock::now();
    auto timeTotalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(timeEndTime - timeStartTime);
    
    std::cout << "all " << iTotalRequests << " requests submitted in " << timeTotalDuration.count() << "ms" << std::endl;
    std::cout << "not waiting for responses - measuring submission rate only" << std::endl;
    
    double dRequestsPerSecond = (iTotalRequests * 1000.0) / timeTotalDuration.count();
    double dRequestsPerMinute = dRequestsPerSecond * 60.0;
    
    std::cout << std::endl;
    std::cout << "performance results" << std::endl;
    std::cout << std::fixed << std::setprecision(2);

    std::cout << "total time: " << timeTotalDuration.count() << " ms ("
              << (timeTotalDuration.count() / 1000.0) << " seconds)" << std::endl;

    std::cout << "throughput: " << dRequestsPerSecond << " requests/second" << std::endl;
    std::cout << "throughput: " << dRequestsPerMinute << " requests/minute" << std::endl;

    std::cout << std::endl;
    std::cout << "submission statistics" << std::endl;
    std::cout << "total requests submitted: " << iTotalRequests << std::endl;
    std::cout << "submission time: " << timeTotalDuration.count() << " ms" << std::endl;
    
    std::cout << std::endl;
    std::cout << "pool statistics" << std::endl;
    std::cout << "active workers: " << pool.getActiveWorkerCount() << std::endl;
    std::cout << "pending requests: " << pool.getPendingRequestCount() << std::endl;
    std::cout << "pool running: " << (pool.isRunning() ? "yes" : "no") << std::endl;

    std::cout << std::endl;
    std::cout << "performance assessment" << std::endl;

    if (dRequestsPerSecond > EXCELLENT_RPS_THRESHOLD) {
        std::cout << "excellent: " << dRequestsPerSecond << " r/s" << std::endl;
    } else if (dRequestsPerSecond > GOOD_RPS_THRESHOLD) {
        std::cout << "good: " << dRequestsPerSecond << " r/s" << std::endl;
    } else if (dRequestsPerSecond > AVERAGE_RPS_THRESHOLD) {
        std::cout << "avg: " << dRequestsPerSecond << " r/s" << std::endl;
    } else {
        std::cout << "slow: " << dRequestsPerSecond << " r/s" << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "test completed successfully" << std::endl;
}

int main() {
    try {
        runPerformanceTest();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "performance test failed: " << e.what() << std::endl;
        return 1;
    }
}