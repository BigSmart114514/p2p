#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <chrono>
#include <optional>
#include <variant>

namespace p2p {

// 连接状态
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

// 数据通道状态
enum class ChannelState {
    Connecting,
    Open,
    Closing,
    Closed
};

// 中继状态
enum class RelayState {
    NotAuthenticated,
    Authenticating,
    Authenticated,
    AuthFailed
};

// 错误代码
enum class ErrorCode {
    None = 0,
    ConnectionFailed,
    SignalingError,
    PeerNotFound,
    ChannelNotOpen,
    Timeout,
    InvalidData,
    InternalError,
    RelayAuthFailed,      // 中继认证失败
    RelayNotAuthenticated // 未进行中继认证
};

// 错误信息
struct Error {
    ErrorCode code;
    std::string message;
    
    operator bool() const { return code != ErrorCode::None; }
};

// 二进制数据类型
using BinaryData = std::vector<uint8_t>;

// 消息类型 - 可以是文本或二进制
struct Message {
    enum class Type { Text, Binary };
    
    Type type;
    std::string text;
    BinaryData binary;
    
    // 构造文本消息
    static Message fromText(const std::string& str) {
        Message msg;
        msg.type = Type::Text;
        msg.text = str;
        return msg;
    }
    
    // 构造二进制消息
    static Message fromBinary(const BinaryData& data) {
        Message msg;
        msg.type = Type::Binary;
        msg.binary = data;
        return msg;
    }
    
    static Message fromBinary(const void* data, size_t size) {
        Message msg;
        msg.type = Type::Binary;
        msg.binary.assign(static_cast<const uint8_t*>(data), 
                          static_cast<const uint8_t*>(data) + size);
        return msg;
    }
};

// Peer 信息
struct PeerInfo {
    std::string id;
    ChannelState channelState;
    bool relayMode = false;  // 是否通过中继连接
    bool isConnected() const { return channelState == ChannelState::Open; }
};

// 客户端配置
struct ClientConfig {
    // 信令服务器URL
    std::string signalingUrl = "ws://localhost:8080";
    
    // 请求的Peer ID (可选，为空则服务器自动分配)
    std::string peerId;
    
    // STUN 服务器列表
    std::vector<std::string> stunServers = {
        "stun:stun.l.google.com:19302",
        "stun:stun1.l.google.com:19302"
    };
    
    // TURN 服务器 (可选)
    struct TurnServer {
        std::string url;
        std::string username;
        std::string credential;
    };
    std::vector<TurnServer> turnServers;
    
    // 连接超时 (毫秒)
    uint32_t connectionTimeout = 10000;
    
    // 自动重连
    bool autoReconnect = false;
    uint32_t reconnectInterval = 5000;
};

// 回调函数类型
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void(const Error&)>;
using OnPeerConnectedCallback = std::function<void(const std::string& peerId)>;
using OnPeerDisconnectedCallback = std::function<void(const std::string& peerId)>;
using OnTextMessageCallback = std::function<void(const std::string& peerId, const std::string& message)>;
using OnBinaryMessageCallback = std::function<void(const std::string& peerId, const BinaryData& data)>;
using OnMessageCallback = std::function<void(const std::string& peerId, const Message& message)>;
using OnPeerListCallback = std::function<void(const std::vector<std::string>& peers)>;
using OnErrorCallback = std::function<void(const Error& error)>;
using OnStateChangeCallback = std::function<void(ConnectionState state)>;

// 中继相关回调
using OnRelayAuthResultCallback = std::function<void(bool success, const std::string& message)>;
using OnRelayConnectedCallback = std::function<void(const std::string& peerId)>;
using OnRelayDisconnectedCallback = std::function<void(const std::string& peerId)>;

} // namespace p2p