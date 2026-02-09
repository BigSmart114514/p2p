cmake_minimum_required(VERSION 3.16)
project(p2p-client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 选项
option(P2P_BUILD_SHARED "Build shared library" ON)
option(P2P_BUILD_STATIC "Build static library" ON)
option(P2P_BUILD_EXAMPLE "Build example application" ON)

# Windows特定设置
if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_definitions(-D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN)
endif()

# 查找依赖
find_package(LibDataChannel CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    find_package(libdatachannel CONFIG QUIET)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# 库源文件
set(P2P_LIB_SOURCES
    src/p2p_client.cpp
)

# 库头文件
set(P2P_LIB_HEADERS
    include/p2p/p2p_client.hpp
    include/p2p/types.hpp
    include/p2p/export.hpp
)

# 确定 libdatachannel target
if(TARGET LibDataChannel::LibDataChannel)
    set(DATACHANNEL_TARGET LibDataChannel::LibDataChannel)
elseif(TARGET LibDataChannel::LibDataChannelStatic)
    set(DATACHANNEL_TARGET LibDataChannel::LibDataChannelStatic)
elseif(TARGET datachannel)
    set(DATACHANNEL_TARGET datachannel)
elseif(TARGET datachannel-static)
    set(DATACHANNEL_TARGET datachannel-static)
elseif(TARGET libdatachannel::datachannel)
    set(DATACHANNEL_TARGET libdatachannel::datachannel)
elseif(TARGET libdatachannel::datachannel-static)
    set(DATACHANNEL_TARGET libdatachannel::datachannel-static)
else()
    message(FATAL_ERROR "Could not find libdatachannel target")
endif()

# 公共链接库
set(P2P_LINK_LIBRARIES
    ${DATACHANNEL_TARGET}
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 平台特定链接
if(WIN32)
    list(APPEND P2P_LINK_LIBRARIES ws2_32 crypt32)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    list(APPEND P2P_LINK_LIBRARIES Threads::Threads)
endif()

# ==================== 静态库 ====================
if(P2P_BUILD_STATIC)
    add_library(p2p-client-static STATIC ${P2P_LIB_SOURCES})
    
    target_include_directories(p2p-client-static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/common/include
    )
    
    target_compile_definitions(p2p-client-static PUBLIC P2P_CLIENT_STATIC)
    
    target_link_libraries(p2p-client-static
        PUBLIC ${P2P_LINK_LIBRARIES}
    )
    
    set_target_properties(p2p-client-static PROPERTIES
        OUTPUT_NAME p2p-client
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    
    # 设置符号可见性
    if(NOT MSVC)
        target_compile_options(p2p-client-static PRIVATE -fvisibility=hidden)
    endif()
endif()

# ==================== 动态库 ====================
if(P2P_BUILD_SHARED)
    add_library(p2p-client-shared SHARED ${P2P_LIB_SOURCES})
    
    target_include_directories(p2p-client-shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_SOURCE_DIR}/common/include
    )
    
    target_compile_definitions(p2p-client-shared PRIVATE P2P_CLIENT_EXPORTS)
    
    target_link_libraries(p2p-client-shared
        PUBLIC ${P2P_LINK_LIBRARIES}
    )
    
    set_target_properties(p2p-client-shared PROPERTIES
        OUTPUT_NAME p2p-client
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    
    # Windows DLL导出
    if(WIN32)
        set_target_properties(p2p-client-shared PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS OFF
        )
    endif()
    
    # 设置符号可见性
    if(NOT MSVC)
        target_compile_options(p2p-client-shared PRIVATE -fvisibility=hidden)
    endif()
endif()

# ==================== 示例程序 ====================
if(P2P_BUILD_EXAMPLE)
    add_executable(p2p-example src/main.cpp)
    
    target_include_directories(p2p-example PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/common/include
    )
    
    # 优先链接静态库
    if(P2P_BUILD_STATIC)
        target_link_libraries(p2p-example PRIVATE p2p-client-static)
    else()
        target_link_libraries(p2p-example PRIVATE p2p-client-shared)
    endif()
    
    set_target_properties(p2p-example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ==================== 安装规则 ====================
include(GNUInstallDirs)

# 安装头文件
install(DIRECTORY include/p2p
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# 安装静态库
if(P2P_BUILD_STATIC)
    install(TARGETS p2p-client-static
        EXPORT p2p-client-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

# 安装动态库
if(P2P_BUILD_SHARED)
    install(TARGETS p2p-client-shared
        EXPORT p2p-client-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# 安装示例程序
if(P2P_BUILD_EXAMPLE)
    install(TARGETS p2p-example
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# 导出 CMake 配置
install(EXPORT p2p-client-targets
    FILE p2p-client-targets.cmake
    NAMESPACE p2p::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/p2p-client
)

# 生成配置文件
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/p2p-client-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/p2p-client-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/p2p-client
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/p2p-client-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/p2p-client-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/p2p-client-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/p2p-client
)