cmake_minimum_required(VERSION 3.12)
project(OJSystem VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# 查找依赖
find_package(Threads REQUIRED)

# 检查是否有nlohmann/json
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(STATUS "nlohmann/json not found, downloading...")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
    set(JSON_TARGET nlohmann_json::nlohmann_json)
else()
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
    set(JSON_TARGET nlohmann_json)
endif()

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
set(CORE_SOURCES
    src/core/http_server.cpp
    src/core/http_request.cpp
    src/core/http_response.cpp
    src/core/middleware.cpp
    src/core/config_manager.cpp
    src/core/logger.cpp
)

# 创建核心库
add_library(oj_core STATIC ${CORE_SOURCES})
target_link_libraries(oj_core ${JSON_TARGET} Threads::Threads)

# 主程序
add_executable(oj_server src/main.cpp)
target_link_libraries(oj_server oj_core)

# 安装目标
install(TARGETS oj_server DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
install(DIRECTORY public/ DESTINATION public)

# 创建运行时目录
install(DIRECTORY DESTINATION logs)
install(DIRECTORY DESTINATION uploads)
install(DIRECTORY DESTINATION judge)