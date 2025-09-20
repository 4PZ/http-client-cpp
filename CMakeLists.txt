cmake_minimum_required(VERSION 3.16)
project(AsyncHttpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -O3)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

set(CORE_SOURCES
    src/core/async_client.cpp
)

set(UTILS_SOURCES
    src/utils/utils.cpp
)

set(HEADERS
    include/core/async_client.hpp
    include/utils/utils.hpp
    include/http_client.hpp
)

add_library(async_http_client STATIC ${CORE_SOURCES} ${UTILS_SOURCES} ${HEADERS})

target_include_directories(async_http_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(async_http_client 
    PUBLIC
    Threads::Threads 
    ${CURL_LIBRARIES}
)

target_compile_features(async_http_client PUBLIC cxx_std_20)

add_executable(performance_test examples/performance_test.cpp)
target_link_libraries(performance_test async_http_client)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()