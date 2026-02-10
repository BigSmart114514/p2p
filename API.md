# P2P Client Library API 手册

## 目录

1. [概述](#1-概述)
2. [快速开始](#2-快速开始)
3. [编译与链接](#3-编译与链接)
4. [类型定义](#4-类型定义)
5. [P2PClient 类](#5-p2pclient-类)
6. [回调函数](#6-回调函数)
7. [错误处理](#7-错误处理)
8. [完整示例](#8-完整示例)

---

## 1. 概述

P2P Client Library 是一个基于 WebRTC DataChannel 的点对点通信库，支持：

- 文本消息传输
- 二进制数据传输
- NAT 穿透 (通过 STUN/TURN)
- 多 Peer 同时连接
- 异步事件回调
- **服务端中继模式** (需密码认证)

### 1.1 通信模式

| 模式 | 描述 | 优点 | 缺点 |
|-----|------|------|------|
| P2P 直连 | 通过 WebRTC DataChannel 直接连接 | 低延迟、无服务器负载 | 需要 NAT 穿透 |
| 服务端中继 | 数据通过信令服务器转发 | 100% 连通性 | 延迟较高、服务器负载 |

### 1.2 依赖

- libdatachannel
- OpenSSL
- nlohmann_json

### 1.3 支持平台

- Windows (MSVC)
- Linux (GCC/Clang)
- macOS (Clang)

---

## 2. 快速开始

### 2.1 P2P 直连模式

```cpp
#include <p2p/p2p_client.hpp>
#include <iostream>

int main() {
    // 创建客户端
    p2p::P2PClient client("ws://localhost:8080");
    
    // 设置消息回调
    client.setOnTextMessage([](const std::string& from, const std::string& msg) {
        std::cout << "收到消息 [" << from << "]: " << msg << std::endl;
    });
    
    // 连接到信令服务器
    if (!client.connect()) {
        std::cerr << "连接失败" << std::endl;
        return 1;
    }
    
    std::cout << "我的 ID: " << client.getLocalId() << std::endl;
    
    // 连接到其他 Peer 并发送消息
    client.connectToPeer("peer_2");
    // ... 等待连接建立 ...
    client.sendText("peer_2", "Hello!");
    
    // 保持运行
    std::cin.get();
    
    return 0;
}
```

### 2.2 服务端中继模式

```cpp
#include <p2p/p2p_client.hpp>
#include <iostream>

int main() {
    p2p::P2PClient client("ws://localhost:8080");
    
    // 设置消息回调 (中继消息也会触发这些回调)
    client.setOnTextMessage([](const std::string& from, const std::string& msg) {
        std::cout << "收到消息 [" << from << "]: " << msg << std::endl;
    });
    
    // 设置中继回调
    client.setOnRelayConnected([](const std::string& peerId) {
        std::cout << "中继已连接: " << peerId << std::endl;
    });
    
    if (!client.connect()) {
        return 1;
    }
    
    // 进行中继认证
    if (!client.authenticateRelay("your_password")) {
        std::cerr << "中继认证失败" << std::endl;
        return 1;
    }
    
    // 通过中继连接到 Peer
    client.connectToPeerViaRelay("peer_2");
    
    // 通过中继发送消息
    client.sendTextViaRelay("peer_2", "Hello via relay!");
    
    std::cin.get();
    return 0;
}
```

---

## 3. 编译与链接

### 3.1 CMake 项目

```cmake
# 方法 1: find_package (库已安装)
find_package(p2p-client CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE p2p::p2p-client-static)

# 方法 2: 作为子目录
add_subdirectory(path/to/p2p-client)
target_link_libraries(your_app PRIVATE p2p-client-static)
```

### 3.2 手动链接 (Windows MSVC)

```
编译选项: /std:c++17 /I"path/to/include"
链接库:   p2p-client.lib ws2_32.lib crypt32.lib
```

### 3.3 手动链接 (Linux)

```bash
g++ -std=c++17 -I/path/to/include main.cpp -lp2p-client -lpthread -lssl -lcrypto
```

---

## 4. 类型定义

### 4.1 命名空间

所有类型都在 `p2p` 命名空间下。

```cpp
namespace p2p { ... }
```

### 4.2 ConnectionState

连接状态枚举。

```cpp
enum class ConnectionState {
    Disconnected,  // 未连接
    Connecting,    // 连接中
    Connected,     // 已连接
    Failed         // 连接失败
};
```

### 4.3 ChannelState

数据通道状态枚举。

```cpp
enum class ChannelState {
    Connecting,  // 通道建立中
    Open,        // 通道已打开
    Closing,     // 通道关闭中
    Closed       // 通道已关闭
};
```

### 4.4 RelayState

中继认证状态枚举。

```cpp
enum class RelayState {
    NotAuthenticated,  // 未认证
    Authenticating,    // 认证中
    Authenticated,     // 已认证
    AuthFailed         // 认证失败
};
```

### 4.5 ErrorCode

错误代码枚举。

```cpp
enum class ErrorCode {
    None = 0,              // 无错误
    ConnectionFailed,      // 连接失败
    SignalingError,        // 信令错误
    PeerNotFound,          // Peer 不存在
    ChannelNotOpen,        // 通道未打开
    Timeout,               // 超时
    InvalidData,           // 无效数据
    InternalError,         // 内部错误
    RelayAuthFailed,       // 中继认证失败
    RelayNotAuthenticated  // 未进行中继认证
};
```

### 4.6 Error

错误信息结构体。

```cpp
struct Error {
    ErrorCode code;      // 错误代码
    std::string message; // 错误描述
    
    operator bool() const; // code != None 时返回 true
};
```

### 4.7 BinaryData

二进制数据类型别名。

```cpp
using BinaryData = std::vector<uint8_t>;
```

### 4.8 Message

通用消息结构体。

```cpp
struct Message {
    enum class Type { Text, Binary };
    
    Type type;           // 消息类型
    std::string text;    // 文本内容 (type == Text 时有效)
    BinaryData binary;   // 二进制内容 (type == Binary 时有效)
    
    // 静态工厂方法
    static Message fromText(const std::string& str);
    static Message fromBinary(const BinaryData& data);
    static Message fromBinary(const void* data, size_t size);
};
```

### 4.9 PeerInfo

Peer 信息结构体。

```cpp
struct PeerInfo {
    std::string id;            // Peer ID
    ChannelState channelState; // 通道状态
    bool relayMode = false;    // 是否通过中继连接
    
    bool isConnected() const;  // channelState == Open
};
```

### 4.10 ClientConfig

客户端配置结构体。

```cpp
struct ClientConfig {
    std::string signalingUrl;  // 信令服务器 URL (默认: "ws://localhost:8080")
    std::string peerId;        // 请求的 Peer ID (可选，为空则自动分配)
    
    std::vector<std::string> stunServers;  // STUN 服务器列表
    
    struct TurnServer {
        std::string url;
        std::string username;
        std::string credential;
    };
    std::vector<TurnServer> turnServers;   // TURN 服务器列表
    
    uint32_t connectionTimeout = 10000;    // 连接超时 (毫秒)
    bool autoReconnect = false;            // 自动重连
    uint32_t reconnectInterval = 5000;     // 重连间隔 (毫秒)
};
```

**默认 STUN 服务器:**
```cpp
{
    "stun:stun.l.google.com:19302",
    "stun:stun1.l.google.com:19302"
}
```

---

## 5. P2PClient 类

### 5.1 构造函数

```cpp
explicit P2PClient(const ClientConfig& config = ClientConfig());
explicit P2PClient(const std::string& signalingUrl);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `config` | `ClientConfig` | 客户端配置 |
| `signalingUrl` | `std::string` | 信令服务器 URL |

**示例:**
```cpp
// 使用默认配置
p2p::P2PClient client1;

// 指定服务器 URL
p2p::P2PClient client2("ws://example.com:8080");

// 使用完整配置
p2p::ClientConfig config;
config.signalingUrl = "ws://example.com:8080";
config.peerId = "my_custom_id";
config.connectionTimeout = 5000;
p2p::P2PClient client3(config);
```

---

### 5.2 连接管理

#### connect()

连接到信令服务器（阻塞）。

```cpp
bool connect();
```

**返回值:** 成功返回 `true`，失败返回 `false`。

**示例:**
```cpp
if (client.connect()) {
    std::cout << "连接成功，ID: " << client.getLocalId() << std::endl;
} else {
    std::cerr << "连接失败" << std::endl;
}
```

---

#### connectAsync()

异步连接到信令服务器。

```cpp
std::future<bool> connectAsync();
```

**返回值:** `std::future<bool>`，包含连接结果。

**示例:**
```cpp
auto future = client.connectAsync();
// 可以做其他事情...
if (future.get()) {
    std::cout << "连接成功" << std::endl;
}
```

---

#### disconnect()

断开所有连接（包括 P2P 和中继连接）。

```cpp
void disconnect();
```

---

#### isConnected()

检查是否已连接到信令服务器。

```cpp
bool isConnected() const;
```

---

#### getState()

获取当前连接状态。

```cpp
ConnectionState getState() const;
```

---

#### getLocalId()

获取本地 Peer ID。

```cpp
std::string getLocalId() const;
```

**注意:** 在 `connect()` 成功后才有效。

---

### 5.3 Peer 管理 (P2P 直连)

#### connectToPeer()

发起与指定 Peer 的 P2P 连接。

```cpp
bool connectToPeer(const std::string& peerId);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `peerId` | `std::string` | 目标 Peer ID |

**返回值:** 成功发起连接返回 `true`。

**注意:** 返回 `true` 仅表示发起连接，不表示连接已建立。连接建立后会触发 `OnPeerConnected` 回调。

---

#### connectToPeerAsync()

异步连接到 Peer，等待数据通道打开。

```cpp
std::future<bool> connectToPeerAsync(
    const std::string& peerId, 
    std::chrono::milliseconds timeout = std::chrono::seconds(30)
);
```

**示例:**
```cpp
auto future = client.connectToPeerAsync("peer_2", std::chrono::seconds(10));
if (future.get()) {
    client.sendText("peer_2", "连接成功！");
}
```

---

#### disconnectFromPeer()

断开与指定 Peer 的 P2P 连接。

```cpp
void disconnectFromPeer(const std::string& peerId);
```

---

#### requestPeerList()

请求在线 Peer 列表。结果通过 `OnPeerList` 回调返回。

```cpp
void requestPeerList();
```

---

#### getConnectedPeers()

获取已建立 P2P 数据通道的 Peer 列表。

```cpp
std::vector<std::string> getConnectedPeers() const;
```

---

#### isPeerConnected()

检查是否与指定 Peer 建立了 P2P 数据通道。

```cpp
bool isPeerConnected(const std::string& peerId) const;
```

---

#### getPeerInfo()

获取 Peer 详细信息（包括 P2P 和中继连接）。

```cpp
std::optional<PeerInfo> getPeerInfo(const std::string& peerId) const;
```

**返回值:** 如果 Peer 存在返回 `PeerInfo`，否则返回 `std::nullopt`。

**示例:**
```cpp
auto info = client.getPeerInfo("peer_2");
if (info) {
    std::cout << "Peer: " << info->id << std::endl;
    std::cout << "连接模式: " << (info->relayMode ? "中继" : "P2P") << std::endl;
    std::cout << "状态: " << (info->isConnected() ? "已连接" : "未连接") << std::endl;
}
```

---

### 5.4 消息发送 (P2P 直连)

#### sendText()

通过 P2P 发送文本消息。

```cpp
bool sendText(const std::string& peerId, const std::string& message);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `peerId` | `std::string` | 目标 Peer ID |
| `message` | `std::string` | 文本内容 |

**返回值:** 发送成功返回 `true`。

---

#### sendBinary()

通过 P2P 发送二进制数据。

```cpp
bool sendBinary(const std::string& peerId, const BinaryData& data);
bool sendBinary(const std::string& peerId, const void* data, size_t size);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `peerId` | `std::string` | 目标 Peer ID |
| `data` | `BinaryData` / `void*` | 二进制数据 |
| `size` | `size_t` | 数据大小 (仅指针版本) |

**示例:**
```cpp
// 使用 vector
p2p::BinaryData data = {0x01, 0x02, 0x03, 0x04};
client.sendBinary("peer_2", data);

// 使用原始指针
uint8_t buffer[] = {0x05, 0x06, 0x07, 0x08};
client.sendBinary("peer_2", buffer, sizeof(buffer));

// 发送结构体
struct MyData { int x; float y; };
MyData myData{42, 3.14f};
client.sendBinary("peer_2", &myData, sizeof(myData));
```

---

#### send()

通过 P2P 发送通用消息。

```cpp
bool send(const std::string& peerId, const Message& message);
```

**示例:**
```cpp
client.send("peer_2", p2p::Message::fromText("Hello"));
client.send("peer_2", p2p::Message::fromBinary({0x01, 0x02}));
```

---

#### broadcastText()

通过 P2P 广播文本消息给所有已连接的 Peer。

```cpp
size_t broadcastText(const std::string& message);
```

**返回值:** 成功发送的 Peer 数量。

---

#### broadcastBinary()

通过 P2P 广播二进制数据给所有已连接的 Peer。

```cpp
size_t broadcastBinary(const BinaryData& data);
```

---

#### sendObject() (模板方法)

发送可序列化对象。

```cpp
template<typename T>
bool sendObject(const std::string& peerId, const T& obj);
```

**要求:** 对象必须实现 `serialize()` 方法，返回 `std::string` 或 `BinaryData`。

**示例:**
```cpp
struct MyMessage {
    std::string data;
    std::string serialize() const { return data; }
};

MyMessage msg{"Hello"};
client.sendObject("peer_2", msg);
```

---

### 5.5 中继模式

#### authenticateRelay()

进行中继认证（阻塞）。

```cpp
bool authenticateRelay(const std::string& password);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `password` | `std::string` | 中继密码 |

**返回值:** 认证成功返回 `true`。

**示例:**
```cpp
if (client.authenticateRelay("your_password")) {
    std::cout << "中继认证成功" << std::endl;
} else {
    std::cerr << "中继认证失败" << std::endl;
}
```

---

#### authenticateRelayAsync()

异步进行中继认证。

```cpp
std::future<bool> authenticateRelayAsync(
    const std::string& password,
    std::chrono::milliseconds timeout = std::chrono::seconds(10)
);
```

**示例:**
```cpp
auto future = client.authenticateRelayAsync("password", std::chrono::seconds(5));
if (future.get()) {
    std::cout << "认证成功" << std::endl;
}
```

---

#### getRelayState()

获取中继认证状态。

```cpp
RelayState getRelayState() const;
```

---

#### isRelayAuthenticated()

检查是否已完成中继认证。

```cpp
bool isRelayAuthenticated() const;
```

---

#### connectToPeerViaRelay()

通过中继连接到 Peer。

```cpp
bool connectToPeerViaRelay(const std::string& peerId);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `peerId` | `std::string` | 目标 Peer ID |

**返回值:** 成功返回 `true`。

**注意:** 需要先完成 `authenticateRelay()`。

---

#### disconnectFromPeerViaRelay()

断开中继连接。

```cpp
void disconnectFromPeerViaRelay(const std::string& peerId);
```

---

#### sendTextViaRelay()

通过中继发送文本消息。

```cpp
bool sendTextViaRelay(const std::string& peerId, const std::string& message);
```

| 参数 | 类型 | 描述 |
|-----|------|------|
| `peerId` | `std::string` | 目标 Peer ID |
| `message` | `std::string` | 文本内容 |

**返回值:** 发送成功返回 `true`。

---

#### sendBinaryViaRelay()

通过中继发送二进制数据。

```cpp
bool sendBinaryViaRelay(const std::string& peerId, const BinaryData& data);
bool sendBinaryViaRelay(const std::string& peerId, const void* data, size_t size);
```

---

#### sendViaRelay()

通过中继发送通用消息。

```cpp
bool sendViaRelay(const std::string& peerId, const Message& message);
```

---

#### broadcastTextViaRelay()

通过中继广播文本消息给所有中继连接的 Peer。

```cpp
size_t broadcastTextViaRelay(const std::string& message);
```

**返回值:** 成功发送的 Peer 数量。

---

#### broadcastBinaryViaRelay()

通过中继广播二进制数据给所有中继连接的 Peer。

```cpp
size_t broadcastBinaryViaRelay(const BinaryData& data);
```

---

#### getRelayConnectedPeers()

获取通过中继连接的 Peer 列表。

```cpp
std::vector<std::string> getRelayConnectedPeers() const;
```

---

#### isPeerRelayConnected()

检查是否通过中继与 Peer 连接。

```cpp
bool isPeerRelayConnected(const std::string& peerId) const;
```

---

### 5.6 静态方法

#### setLogLevel()

设置日志级别。

```cpp
static void setLogLevel(int level);
```

| 级别 | 描述 |
|-----|------|
| 0 | None - 无日志 |
| 1 | Error - 仅错误 |
| 2 | Warning - 警告及以上 |
| 3 | Info - 信息及以上 |
| 4 | Debug - 调试及以上 |
| 5 | Verbose - 全部 |

---

#### getVersion()

获取库版本。

```cpp
static std::string getVersion();
```

---

## 6. 回调函数

### 6.1 回调类型定义

```cpp
// 基础回调
using OnConnectedCallback = std::function<void()>;
using OnDisconnectedCallback = std::function<void(const Error&)>;
using OnErrorCallback = std::function<void(const Error& error)>;
using OnStateChangeCallback = std::function<void(ConnectionState state)>;

// P2P 连接回调
using OnPeerConnectedCallback = std::function<void(const std::string& peerId)>;
using OnPeerDisconnectedCallback = std::function<void(const std::string& peerId)>;

// 消息回调 (P2P 和中继消息都会触发)
using OnTextMessageCallback = std::function<void(const std::string& peerId, const std::string& message)>;
using OnBinaryMessageCallback = std::function<void(const std::string& peerId, const BinaryData& data)>;
using OnMessageCallback = std::function<void(const std::string& peerId, const Message& message)>;

// Peer 列表回调
using OnPeerListCallback = std::function<void(const std::vector<std::string>& peers)>;

// 中继回调
using OnRelayAuthResultCallback = std::function<void(bool success, const std::string& message)>;
using OnRelayConnectedCallback = std::function<void(const std::string& peerId)>;
using OnRelayDisconnectedCallback = std::function<void(const std::string& peerId)>;
```

### 6.2 设置回调

#### 基础回调

```cpp
void setOnConnected(OnConnectedCallback callback);
void setOnDisconnected(OnDisconnectedCallback callback);
void setOnError(OnErrorCallback callback);
void setOnStateChange(OnStateChangeCallback callback);
```

#### P2P 连接回调

```cpp
void setOnPeerConnected(OnPeerConnectedCallback callback);
void setOnPeerDisconnected(OnPeerDisconnectedCallback callback);
```

#### 消息回调

```cpp
void setOnTextMessage(OnTextMessageCallback callback);
void setOnBinaryMessage(OnBinaryMessageCallback callback);
void setOnMessage(OnMessageCallback callback);
```

**注意:** 消息回调对 P2P 和中继消息都会触发。

#### Peer 列表回调

```cpp
void setOnPeerList(OnPeerListCallback callback);
```

#### 中继回调

```cpp
void setOnRelayAuthResult(OnRelayAuthResultCallback callback);
void setOnRelayConnected(OnRelayConnectedCallback callback);
void setOnRelayDisconnected(OnRelayDisconnectedCallback callback);
```

---

### 6.3 回调示例

```cpp
p2p::P2PClient client("ws://localhost:8080");

// ========== 基础回调 ==========
client.setOnConnected([]() {
    std::cout << "已连接到服务器" << std::endl;
});

client.setOnDisconnected([](const p2p::Error& error) {
    std::cout << "已断开: " << error.message << std::endl;
});

client.setOnError([](const p2p::Error& error) {
    std::cerr << "错误: " << error.message << std::endl;
});

client.setOnStateChange([](p2p::ConnectionState state) {
    const char* names[] = {"断开", "连接中", "已连接", "失败"};
    std::cout << "状态: " << names[static_cast<int>(state)] << std::endl;
});

// ========== P2P 回调 ==========
client.setOnPeerConnected([&client](const std::string& peerId) {
    std::cout << "P2P 已连接: " << peerId << std::endl;
    client.sendText(peerId, "欢迎！");
});

client.setOnPeerDisconnected([](const std::string& peerId) {
    std::cout << "P2P 已断开: " << peerId << std::endl;
});

// ========== 消息回调 ==========
client.setOnTextMessage([](const std::string& from, const std::string& msg) {
    std::cout << "[" << from << "]: " << msg << std::endl;
});

client.setOnBinaryMessage([](const std::string& from, const p2p::BinaryData& data) {
    std::cout << "收到二进制数据: " << data.size() << " 字节" << std::endl;
});

client.setOnPeerList([](const std::vector<std::string>& peers) {
    std::cout << "在线用户: " << peers.size() << " 人" << std::endl;
    for (const auto& p : peers) {
        std::cout << "  - " << p << std::endl;
    }
});

// ========== 中继回调 ==========
client.setOnRelayAuthResult([](bool success, const std::string& message) {
    if (success) {
        std::cout << "中继认证成功: " << message << std::endl;
    } else {
        std::cerr << "中继认证失败: " << message << std::endl;
    }
});

client.setOnRelayConnected([](const std::string& peerId) {
    std::cout << "中继已连接: " << peerId << std::endl;
});

client.setOnRelayDisconnected([](const std::string& peerId) {
    std::cout << "中继已断开: " << peerId << std::endl;
});
```

---

## 7. 错误处理

### 7.1 同步错误

同步方法（如 `sendText`）返回 `bool` 表示成功或失败：

```cpp
if (!client.sendText("peer_2", "Hello")) {
    std::cerr << "发送失败" << std::endl;
}
```

### 7.2 异步错误

异步错误通过 `OnError` 回调报告：

```cpp
client.setOnError([](const p2p::Error& error) {
    switch (error.code) {
        case p2p::ErrorCode::ChannelNotOpen:
            std::cerr << "通道未打开" << std::endl;
            break;
        case p2p::ErrorCode::PeerNotFound:
            std::cerr << "Peer 不存在" << std::endl;
            break;
        case p2p::ErrorCode::Timeout:
            std::cerr << "操作超时" << std::endl;
            break;
        case p2p::ErrorCode::RelayAuthFailed:
            std::cerr << "中继认证失败" << std::endl;
            break;
        case p2p::ErrorCode::RelayNotAuthenticated:
            std::cerr << "未进行中继认证" << std::endl;
            break;
        default:
            std::cerr << "错误: " << error.message << std::endl;
    }
});
```

### 7.3 中继认证错误

```cpp
client.setOnRelayAuthResult([](bool success, const std::string& message) {
    if (!success) {
        // 可能的错误:
        // - "Relay is not configured on this server" (服务器未配置中继)
        // - "Invalid password" (密码错误)
        std::cerr << "认证失败: " << message << std::endl;
    }
});
```

---

## 8. 完整示例

### 8.1 简单聊天客户端 (支持 P2P 和中继)

```cpp
#include <p2p/p2p_client.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

std::vector<std::string> split(const std::string& s, char delim = ' ') {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

int main() {
    p2p::P2PClient::setLogLevel(2);
    
    p2p::P2PClient client("ws://localhost:8080");
    
    // 设置回调
    client.setOnConnected([]() {
        std::cout << ">>> 已连接到服务器" << std::endl;
    });
    
    client.setOnPeerConnected([](const std::string& peerId) {
        std::cout << ">>> P2P 连接: " << peerId << std::endl;
    });
    
    client.setOnRelayConnected([](const std::string& peerId) {
        std::cout << ">>> 中继连接: " << peerId << std::endl;
    });
    
    client.setOnTextMessage([](const std::string& from, const std::string& msg) {
        std::cout << "[" << from << "]: " << msg << std::endl;
    });
    
    client.setOnPeerList([](const std::vector<std::string>& peers) {
        std::cout << ">>> 在线用户 (" << peers.size() << "):" << std::endl;
        for (const auto& p : peers) {
            std::cout << "    - " << p << std::endl;
        }
    });
    
    client.setOnError([](const p2p::Error& err) {
        std::cerr << ">>> 错误: " << err.message << std::endl;
    });
    
    client.setOnRelayAuthResult([](bool success, const std::string& message) {
        if (success) {
            std::cout << ">>> 中继认证成功" << std::endl;
        } else {
            std::cout << ">>> 中继认证失败: " << message << std::endl;
        }
    });
    
    // 连接
    if (!client.connect()) {
        std::cerr << "连接失败" << std::endl;
        return 1;
    }
    
    std::cout << "我的 ID: " << client.getLocalId() << std::endl;
    std::cout << "\n命令:" << std::endl;
    std::cout << "  list                  - 列出在线用户" << std::endl;
    std::cout << "  connect <id>          - P2P 连接" << std::endl;
    std::cout << "  send <id> <msg>       - P2P 发送" << std::endl;
    std::cout << "  relayauth <password>  - 中继认证" << std::endl;
    std::cout << "  relayconnect <id>     - 中继连接" << std::endl;
    std::cout << "  relaysend <id> <msg>  - 中继发送" << std::endl;
    std::cout << "  peers                 - 列出已连接的 Peer" << std::endl;
    std::cout << "  quit                  - 退出\n" << std::endl;
    
    // 命令循环
    std::string line;
    while (std::getline(std::cin, line)) {
        auto tokens = split(line);
        if (tokens.empty()) continue;
        
        std::string cmd = tokens[0];
        
        if (cmd == "quit") {
            break;
        }
        else if (cmd == "list") {
            client.requestPeerList();
        }
        else if (cmd == "peers") {
            auto p2pPeers = client.getConnectedPeers();
            auto relayPeers = client.getRelayConnectedPeers();
            
            std::cout << "P2P 连接 (" << p2pPeers.size() << "):" << std::endl;
            for (const auto& p : p2pPeers) {
                std::cout << "  - " << p << std::endl;
            }
            
            std::cout << "中继连接 (" << relayPeers.size() << "):" << std::endl;
            for (const auto& p : relayPeers) {
                std::cout << "  - " << p << std::endl;
            }
        }
        else if (cmd == "connect" && tokens.size() >= 2) {
            client.connectToPeer(tokens[1]);
            std::cout << "正在连接..." << std::endl;
        }
        else if (cmd == "send" && tokens.size() >= 3) {
            std::string msg = line.substr(line.find(tokens[1]) + tokens[1].length() + 1);
            if (client.sendText(tokens[1], msg)) {
                std::cout << "已发送" << std::endl;
            }
        }
        else if (cmd == "relayauth" && tokens.size() >= 2) {
            client.authenticateRelay(tokens[1]);
        }
        else if (cmd == "relayconnect" && tokens.size() >= 2) {
            if (client.connectToPeerViaRelay(tokens[1])) {
                std::cout << "中继连接成功" << std::endl;
            }
        }
        else if (cmd == "relaysend" && tokens.size() >= 3) {
            std::string msg = line.substr(line.find(tokens[1]) + tokens[1].length() + 1);
            if (client.sendTextViaRelay(tokens[1], msg)) {
                std::cout << "已通过中继发送" << std::endl;
            }
        }
        else {
            std::cout << "未知命令: " << cmd << std::endl;
        }
    }
    
    client.disconnect();
    return 0;
}
```

### 8.2 自动选择连接模式

```cpp
#include <p2p/p2p_client.hpp>
#include <chrono>

class SmartP2PClient {
public:
    SmartP2PClient(const std::string& url, const std::string& relayPassword = "")
        : client_(url), relayPassword_(relayPassword) {}
    
    bool connect() {
        if (!client_.connect()) return false;
        
        // 如果提供了中继密码，尝试认证
        if (!relayPassword_.empty()) {
            client_.authenticateRelay(relayPassword_);
        }
        
        return true;
    }
    
    // 智能连接：优先尝试 P2P，失败则使用中继
    bool connectToPeer(const std::string& peerId, std::chrono::seconds timeout = std::chrono::seconds(10)) {
        // 尝试 P2P 连接
        auto future = client_.connectToPeerAsync(peerId, 
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
        
        if (future.get()) {
            std::cout << "P2P 连接成功" << std::endl;
            return true;
        }
        
        // P2P 失败，尝试中继
        if (client_.isRelayAuthenticated()) {
            std::cout << "P2P 失败，尝试中继..." << std::endl;
            if (client_.connectToPeerViaRelay(peerId)) {
                std::cout << "中继连接成功" << std::endl;
                return true;
            }
        }
        
        return false;
    }
    
    // 智能发送：自动选择可用通道
    bool sendText(const std::string& peerId, const std::string& message) {
        if (client_.isPeerConnected(peerId)) {
            return client_.sendText(peerId, message);
        } else if (client_.isPeerRelayConnected(peerId)) {
            return client_.sendTextViaRelay(peerId, message);
        }
        return false;
    }
    
    p2p::P2PClient& getClient() { return client_; }
    
private:
    p2p::P2PClient client_;
    std::string relayPassword_;
};

// 使用示例
int main() {
    SmartP2PClient client("ws://localhost:8080", "relay_password");
    
    client.getClient().setOnTextMessage([](const std::string& from, const std::string& msg) {
        std::cout << "[" << from << "]: " << msg << std::endl;
    });
    
    if (!client.connect()) {
        return 1;
    }
    
    // 自动选择最佳连接方式
    if (client.connectToPeer("peer_2")) {
        client.sendText("peer_2", "Hello!");
    }
    
    std::cin.get();
    return 0;
}
```

### 8.3 文件传输 (支持中继)

```cpp
#include <p2p/p2p_client.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class FileTransfer {
public:
    FileTransfer(p2p::P2PClient& client) : client_(client) {
        setupCallbacks();
    }
    
    // 发送文件 (自动选择 P2P 或中继)
    bool sendFile(const std::string& peerId, const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        // 读取文件
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        
        // 发送文件头
        json header = {
            {"type", "file"},
            {"name", filename},
            {"size", data.size()}
        };
        
        bool useRelay = !client_.isPeerConnected(peerId) && 
                        client_.isPeerRelayConnected(peerId);
        
        if (useRelay) {
            client_.sendTextViaRelay(peerId, header.dump());
            client_.sendBinaryViaRelay(peerId, data);
        } else {
            client_.sendText(peerId, header.dump());
            client_.sendBinary(peerId, data);
        }
        
        return true;
    }
    
private:
    void setupCallbacks() {
        client_.setOnTextMessage([this](const std::string& from, const std::string& msg) {
            try {
                auto j = json::parse(msg);
                if (j["type"] == "file") {
                    pendingFile_ = j["name"];
                    pendingSize_ = j["size"];
                    pendingFrom_ = from;
                    std::cout << "准备接收文件: " << pendingFile_ << std::endl;
                }
            } catch (...) {}
        });
        
        client_.setOnBinaryMessage([this](const std::string& from, const p2p::BinaryData& data) {
            if (!pendingFile_.empty() && from == pendingFrom_) {
                std::ofstream file("received_" + pendingFile_, std::ios::binary);
                file.write(reinterpret_cast<const char*>(data.data()), data.size());
                std::cout << "文件已保存: received_" << pendingFile_ << std::endl;
                pendingFile_.clear();
            }
        });
    }
    
    p2p::P2PClient& client_;
    std::string pendingFile_;
    std::string pendingFrom_;
    size_t pendingSize_ = 0;
};
```

---

## 9. 服务端配置

### 9.1 .env 文件

服务端需要配置 `.env` 文件以启用中继功能：

```env
# 放在服务器可执行文件同目录下
RELAY_PASSWORD=your_secure_password_here
```

### 9.2 服务端命令

运行服务端后，可使用以下命令：

| 命令 | 描述 |
|-----|------|
| `list` | 列出所有连接的客户端 |
| `relay` | 列出已认证中继的客户端 |
| `quit` | 关闭服务器 |

---

## 附录 A: P2P vs 中继对比

| 特性 | P2P 直连 | 服务端中继 |
|-----|---------|-----------|
| 连通性 | 依赖 NAT 穿透 | 100% |
| 延迟 | 低 | 较高 |
| 服务器负载 | 无 | 有 |
| 需要认证 | 否 | 是 |
| 适用场景 | 常规通信 | NAT 穿透失败时 |

## 附录 B: 线程安全

- 所有公共方法都是**线程安全**的
- 回调函数在**内部工作线程**中执行，如需更新 UI 请注意线程同步
- 避免在回调中进行长时间阻塞操作

## 附录 C: 性能建议

1. **大文件传输**: 分块发送，每块 16KB-64KB
2. **高频消息**: 考虑合并多条消息
3. **二进制优先**: 结构化数据优先使用二进制格式
4. **中继模式**: 仅在 P2P 失败时使用，避免服务器过载

## 附录 D: 常见问题

**Q: 为什么 `sendText` 返回 false？**
A: 检查 `isPeerConnected()` 或 `isPeerRelayConnected()` 确保通道已打开。

**Q: 连接建立后多久可以发送消息？**
A: 收到 `OnPeerConnected` 或 `OnRelayConnected` 回调后立即可以发送。

**Q: 最大消息大小是多少？**
A: P2P 理论上无限制，建议单条消息不超过 256KB。中继模式会经过 Base64 编码，实际负载会增加约 33%。

**Q: 中继认证失败怎么办？**
A: 检查服务器是否配置了 `.env` 文件，以及密码是否正确。

**Q: P2P 和中继可以同时使用吗？**
A: 可以。一个客户端可以同时维护 P2P 连接和中继连接，甚至对同一个 Peer 可以同时有两种连接。