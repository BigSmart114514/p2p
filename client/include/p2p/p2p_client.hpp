#pragma once

#include "export.hpp"
#include "types.hpp"
#include <memory>
#include <string>
#include <vector>
#include <future>

namespace p2p {

// 前向声明实现类
class P2PClientImpl;

/**
 * P2P 客户端类
 * 
 * 提供 P2P 通信功能，支持文本和二进制数据传输。
 * 支持直接 P2P 连接和通过服务端中继连接两种模式。
 * 
 * 使用示例:
 * @code
 * p2p::ClientConfig config;
 * config.signalingUrl = "ws://server:8080";
 * 
 * p2p::P2PClient client(config);
 * 
 * client.setOnTextMessage([](const std::string& from, const std::string& msg) {
 *     std::cout << "Message from " << from << ": " << msg << std::endl;
 * });
 * 
 * if (client.connect()) {
 *     // 直接 P2P 连接
 *     client.connectToPeer("other_peer");
 *     client.sendText("other_peer", "Hello!");
 *     
 *     // 或者使用中继模式
 *     if (client.authenticateRelay("password123")) {
 *         client.connectToPeerViaRelay("other_peer");
 *         client.sendTextViaRelay("other_peer", "Hello via relay!");
 *     }
 * }
 * @endcode
 */
class P2P_API P2PClient {
public:
    /**
     * 使用配置构造客户端
     * @param config 客户端配置
     */
    explicit P2PClient(const ClientConfig& config = ClientConfig());
    
    /**
     * 使用信令服务器URL构造客户端
     * @param signalingUrl 信令服务器 WebSocket URL
     */
    explicit P2PClient(const std::string& signalingUrl);
    
    ~P2PClient();
    
    // 禁止拷贝
    P2PClient(const P2PClient&) = delete;
    P2PClient& operator=(const P2PClient&) = delete;
    
    // 允许移动
    P2PClient(P2PClient&&) noexcept;
    P2PClient& operator=(P2PClient&&) noexcept;
    
    // ==================== 连接管理 ====================
    
    /**
     * 连接到信令服务器
     * @return 成功返回 true
     */
    bool connect();
    
    /**
     * 异步连接到信令服务器
     * @return future 包含连接结果
     */
    std::future<bool> connectAsync();
    
    /**
     * 断开所有连接
     */
    void disconnect();
    
    /**
     * 检查是否已连接到信令服务器
     */
    bool isConnected() const;
    
    /**
     * 获取当前连接状态
     */
    ConnectionState getState() const;
    
    /**
     * 获取本地 Peer ID
     */
    std::string getLocalId() const;
    
    // ==================== Peer 管理 ====================
    
    /**
     * 连接到指定的 Peer
     * @param peerId 目标 Peer ID
     * @return 成功发起连接返回 true
     */
    bool connectToPeer(const std::string& peerId);
    
    /**
     * 异步连接到 Peer，等待数据通道打开
     * @param peerId 目标 Peer ID
     * @param timeout 超时时间
     * @return future 包含连接结果
     */
    std::future<bool> connectToPeerAsync(const std::string& peerId, 
                                          std::chrono::milliseconds timeout = std::chrono::seconds(30));
    
    /**
     * 断开与指定 Peer 的连接
     * @param peerId 目标 Peer ID
     */
    void disconnectFromPeer(const std::string& peerId);
    
    /**
     * 请求获取在线 Peer 列表
     */
    void requestPeerList();
    
    /**
     * 获取已连接的 Peer 列表
     */
    std::vector<std::string> getConnectedPeers() const;
    
    /**
     * 检查是否与指定 Peer 建立了数据通道
     * @param peerId 目标 Peer ID
     */
    bool isPeerConnected(const std::string& peerId) const;
    
    /**
     * 获取 Peer 信息
     * @param peerId 目标 Peer ID
     */
    std::optional<PeerInfo> getPeerInfo(const std::string& peerId) const;
    
    // ==================== 消息发送 ====================
    
    /**
     * 发送文本消息
     * @param peerId 目标 Peer ID
     * @param message 文本内容
     * @return 发送成功返回 true
     */
    bool sendText(const std::string& peerId, const std::string& message);
    
    /**
     * 发送二进制数据
     * @param peerId 目标 Peer ID
     * @param data 二进制数据
     * @return 发送成功返回 true
     */
    bool sendBinary(const std::string& peerId, const BinaryData& data);
    
    /**
     * 发送二进制数据 (原始指针版本)
     * @param peerId 目标 Peer ID
     * @param data 数据指针
     * @param size 数据大小
     * @return 发送成功返回 true
     */
    bool sendBinary(const std::string& peerId, const void* data, size_t size);
    
    /**
     * 发送通用消息
     * @param peerId 目标 Peer ID
     * @param message 消息对象
     * @return 发送成功返回 true
     */
    bool send(const std::string& peerId, const Message& message);
    
    /**
     * 广播文本消息给所有已连接的 Peer
     * @param message 文本内容
     * @return 成功发送的 Peer 数量
     */
    size_t broadcastText(const std::string& message);
    
    /**
     * 广播二进制数据给所有已连接的 Peer
     * @param data 二进制数据
     * @return 成功发送的 Peer 数量
     */
    size_t broadcastBinary(const BinaryData& data);
    
    // ==================== 中继模式 ====================
    
    /**
     * 进行中继认证
     * @param password 中继密码
     * @return 认证成功返回 true
     */
    bool authenticateRelay(const std::string& password);
    
    /**
     * 异步进行中继认证
     * @param password 中继密码
     * @param timeout 超时时间
     * @return future 包含认证结果
     */
    std::future<bool> authenticateRelayAsync(const std::string& password,
                                              std::chrono::milliseconds timeout = std::chrono::seconds(10));
    
    /**
     * 获取中继认证状态
     */
    RelayState getRelayState() const;
    
    /**
     * 检查是否已完成中继认证
     */
    bool isRelayAuthenticated() const;
    
    /**
     * 通过中继连接到 Peer
     * @param peerId 目标 Peer ID
     * @return 成功返回 true
     */
    bool connectToPeerViaRelay(const std::string& peerId);
    
    /**
     * 断开中继连接
     * @param peerId 目标 Peer ID
     */
    void disconnectFromPeerViaRelay(const std::string& peerId);
    
    /**
     * 通过中继发送文本消息
     * @param peerId 目标 Peer ID
     * @param message 文本内容
     * @return 发送成功返回 true
     */
    bool sendTextViaRelay(const std::string& peerId, const std::string& message);
    
    /**
     * 通过中继发送二进制数据
     * @param peerId 目标 Peer ID
     * @param data 二进制数据
     * @return 发送成功返回 true
     */
    bool sendBinaryViaRelay(const std::string& peerId, const BinaryData& data);
    
    /**
     * 通过中继发送二进制数据 (原始指针版本)
     * @param peerId 目标 Peer ID
     * @param data 数据指针
     * @param size 数据大小
     * @return 发送成功返回 true
     */
    bool sendBinaryViaRelay(const std::string& peerId, const void* data, size_t size);
    
    /**
     * 通过中继发送通用消息
     * @param peerId 目标 Peer ID
     * @param message 消息对象
     * @return 发送成功返回 true
     */
    bool sendViaRelay(const std::string& peerId, const Message& message);
    
    /**
     * 通过中继广播文本消息
     * @param message 文本内容
     * @return 成功发送的 Peer 数量
     */
    size_t broadcastTextViaRelay(const std::string& message);
    
    /**
     * 通过中继广播二进制数据
     * @param data 二进制数据
     * @return 成功发送的 Peer 数量
     */
    size_t broadcastBinaryViaRelay(const BinaryData& data);
    
    /**
     * 获取通过中继连接的 Peer 列表
     */
    std::vector<std::string> getRelayConnectedPeers() const;
    
    /**
     * 检查是否通过中继与 Peer 连接
     * @param peerId 目标 Peer ID
     */
    bool isPeerRelayConnected(const std::string& peerId) const;
    
    // ==================== 序列化辅助方法 ====================
    
    /**
     * 发送可序列化对象 (模板方法)
     * 需要对象实现 serialize() 方法返回 BinaryData 或 std::string
     */
    template<typename T>
    bool sendObject(const std::string& peerId, const T& obj) {
        if constexpr (std::is_same_v<decltype(obj.serialize()), std::string>) {
            return sendText(peerId, obj.serialize());
        } else {
            return sendBinary(peerId, obj.serialize());
        }
    }
    
    // ==================== 回调设置 ====================
    
    /**
     * 设置连接成功回调
     */
    void setOnConnected(OnConnectedCallback callback);
    
    /**
     * 设置断开连接回调
     */
    void setOnDisconnected(OnDisconnectedCallback callback);
    
    /**
     * 设置 Peer 连接成功回调
     */
    void setOnPeerConnected(OnPeerConnectedCallback callback);
    
    /**
     * 设置 Peer 断开连接回调
     */
    void setOnPeerDisconnected(OnPeerDisconnectedCallback callback);
    
    /**
     * 设置接收文本消息回调
     */
    void setOnTextMessage(OnTextMessageCallback callback);
    
    /**
     * 设置接收二进制消息回调
     */
    void setOnBinaryMessage(OnBinaryMessageCallback callback);
    
    /**
     * 设置接收通用消息回调 (同时处理文本和二进制)
     */
    void setOnMessage(OnMessageCallback callback);
    
    /**
     * 设置 Peer 列表更新回调
     */
    void setOnPeerList(OnPeerListCallback callback);
    
    /**
     * 设置错误回调
     */
    void setOnError(OnErrorCallback callback);
    
    /**
     * 设置状态变更回调
     */
    void setOnStateChange(OnStateChangeCallback callback);
    
    /**
     * 设置中继认证结果回调
     */
    void setOnRelayAuthResult(OnRelayAuthResultCallback callback);
    
    /**
     * 设置中继 Peer 连接回调
     */
    void setOnRelayConnected(OnRelayConnectedCallback callback);
    
    /**
     * 设置中继 Peer 断开回调
     */
    void setOnRelayDisconnected(OnRelayDisconnectedCallback callback);
    
    // ==================== 工具方法 ====================
    
    /**
     * 设置日志级别
     * @param level 日志级别 (0=None, 1=Error, 2=Warning, 3=Info, 4=Debug, 5=Verbose)
     */
    static void setLogLevel(int level);
    
    /**
     * 获取库版本
     */
    static std::string getVersion();
    
private:
    std::unique_ptr<P2PClientImpl> impl_;
};

// 便捷函数：创建客户端
inline std::unique_ptr<P2PClient> createClient(const ClientConfig& config = ClientConfig()) {
    return std::make_unique<P2PClient>(config);
}

inline std::unique_ptr<P2PClient> createClient(const std::string& signalingUrl) {
    return std::make_unique<P2PClient>(signalingUrl);
}

} // namespace p2p