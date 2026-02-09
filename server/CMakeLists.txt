# server/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(p2p-signaling-server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(LibDataChannel REQUIRED)
find_package(nlohmann_json REQUIRED)
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
    pthread
)

# 安装
install(TARGETS signaling-server RUNTIME DESTINATION bin)