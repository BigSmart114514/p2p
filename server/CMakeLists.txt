# server/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(signaling-server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(signaling-server
    src/main.cpp
)

target_include_directories(signaling-server PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
)

target_link_libraries(signaling-server PRIVATE
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Linux需要pthread
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(signaling-server PRIVATE Threads::Threads)
endif()

# 安装
install(TARGETS signaling-server RUNTIME DESTINATION bin)