cmake_minimum_required(VERSION 3.16)
project(p2p-hole-punching VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg 集成
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

option(BUILD_SERVER "Build signaling server" ON)
option(BUILD_CLIENT "Build P2P client library" ON)
option(P2P_BUILD_SHARED "Build shared library" ON)
option(P2P_BUILD_STATIC "Build static library" ON)
option(P2P_BUILD_EXAMPLE "Build example application" ON)

if(BUILD_SERVER)
    add_subdirectory(server)
endif()

if(BUILD_CLIENT)
    add_subdirectory(client)
endif()