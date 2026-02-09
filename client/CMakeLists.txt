# client/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(p2p-client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows特定设置
if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

# 查找依赖
find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(p2p-client
    src/main.cpp
)

target_include_directories(p2p-client PRIVATE
    ${CMAKE_SOURCE_DIR}/../common/include
)

target_link_libraries(p2p-client PRIVATE
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(p2p-client PRIVATE ws2_32)
endif()

# 安装
install(TARGETS p2p-client RUNTIME DESTINATION bin)