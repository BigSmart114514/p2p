# server/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(signaling-server VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖 - 使用多种方式查找 libdatachannel
find_package(LibDataChannel CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    find_package(libdatachannel CONFIG QUIET)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(signaling-server
    src/main.cpp
)

target_include_directories(signaling-server PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
)

# 根据找到的target名称链接
if(TARGET LibDataChannel::LibDataChannel)
    target_link_libraries(signaling-server PRIVATE LibDataChannel::LibDataChannel)
elseif(TARGET LibDataChannel::LibDataChannelStatic)
    target_link_libraries(signaling-server PRIVATE LibDataChannel::LibDataChannelStatic)
elseif(TARGET datachannel)
    target_link_libraries(signaling-server PRIVATE datachannel)
elseif(TARGET datachannel-static)
    target_link_libraries(signaling-server PRIVATE datachannel-static)
elseif(TARGET libdatachannel::datachannel)
    target_link_libraries(signaling-server PRIVATE libdatachannel::datachannel)
elseif(TARGET libdatachannel::datachannel-static)
    target_link_libraries(signaling-server PRIVATE libdatachannel::datachannel-static)
else()
    message(FATAL_ERROR "Could not find libdatachannel target")
endif()

target_link_libraries(signaling-server PRIVATE
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