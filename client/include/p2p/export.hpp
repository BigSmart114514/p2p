#pragma once

// 库导出宏定义
#ifdef _WIN32
    #ifdef P2P_CLIENT_EXPORTS
        #define P2P_API __declspec(dllexport)
    #elif defined(P2P_CLIENT_STATIC)
        #define P2P_API
    #else
        #define P2P_API __declspec(dllimport)
    #endif
#else
    #if __GNUC__ >= 4
        #define P2P_API __attribute__((visibility("default")))
    #else
        #define P2P_API
    #endif
#endif