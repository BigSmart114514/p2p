
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

### 1.1 依赖

- libdatachannel
- OpenSSL
- nlohmann_json

### 1.2 支持平台

- Windows (MSVC)
- Linux (GCC/Clang)
- macOS (Clang)

---

## 2. 快速开始

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

### 4.4 ErrorCode

错误代码枚举。

```cpp
enum class ErrorCode {
    None = 0,           // 无错误
    ConnectionFailed,   // 连接失败
    SignalingError,     // 信令错误
    PeerNotFound,       // Peer 不存在
    ChannelNotOpen,     // 通道未打开
    Timeout,            // 超时
    InvalidData,        // 无效数据
    InternalError       // 内部错误
};
```

### 4.5 Error

错误信息结构体。

```cpp
struct Error {
    ErrorCode code;      // 错误代码
    std::string message; // 错误描述
    
    operator bool() const; // code != None 时返回 true
};
```

### 4.6 BinaryData

二进制数据类型别名。

```cpp
using BinaryData = std::vector<uint8_t>;
```

### 4.7 Message

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

### 4.8 PeerInfo

Peer 信息结构体。

```cpp
struct PeerInfo {
    std::string id;           // Peer ID
    ChannelState channelState; // 通道状态
    
    bool isConnected() const; // channelState == Open
};
```

### 4.9 ClientConfig

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

断开所有连接。

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

### 5.3 Peer 管理

#### connectToPeer()

发起与指定 Peer 的连接。

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

断开与指定 Peer 的连接。

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

获取已建立数据通道的 Peer 列表。

```cpp
std::vector<std::string> getConnectedPeers() const;
```

---

#### isPeerConnected()

检查是否与指定 Peer 建立了数据通道。

```cpp
bool isPeerConnected(const std::string& peerId) const;
```

---

#### getPeerInfo()

获取 Peer 详细信息。

```cpp
std::optional<PeerInfo> getPeerInfo(const std::string& peerId) const;
```

**返回值:** 如果 Peer 存在返回 `PeerInfo`，否则返回 `std::nullopt`。

---

### 5.4 消息发送

#### sendText()

发送文本消息。

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

发送二进制数据。

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

发送通用消息。

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

广播文本消息给所有已连接的 Peer。

```cpp
size_t broadcastText(const std::string& message);
```

**返回值:** 成功发送的 Peer 数量。

---

#### broadcastBinary()

广播二进制数据给所有已连接的 Peer。

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

### 5.5 静态方法

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
```

### 6.2 设置回调

#### setOnConnected()

连接到信令服务器成功时触发。

```cpp
void setOnConnected(OnConnectedCallback callback);
```

---

#### setOnDisconnected()

与信令服务器断开连接时触发。

```cpp
void setOnDisconnected(OnDisconnectedCallback callback);
```

---

#### setOnPeerConnected()

与 Peer 的数据通道建立成功时触发。

```cpp
void setOnPeerConnected(OnPeerConnectedCallback callback);
```

**重要:** 此回调触发后才能向该 Peer 发送消息。

---

#### setOnPeerDisconnected()

与 Peer 的连接断开时触发。

```cpp
void setOnPeerDisconnected(OnPeerDisconnectedCallback callback);
```

---

#### setOnTextMessage()

收到文本消息时触发。

```cpp
void setOnTextMessage(OnTextMessageCallback callback);
```

---

#### setOnBinaryMessage()

收到二进制消息时触发。

```cpp
void setOnBinaryMessage(OnBinaryMessageCallback callback);
```

---

#### setOnMessage()

收到任意消息时触发（文本和二进制）。

```cpp
void setOnMessage(OnMessageCallback callback);
```

**注意:** 可以与 `setOnTextMessage` / `setOnBinaryMessage` 同时使用。

---

#### setOnPeerList()

收到在线 Peer 列表时触发。

```cpp
void setOnPeerList(OnPeerListCallback callback);
```

---

#### setOnError()

发生错误时触发。

```cpp
void setOnError(OnErrorCallback callback);
```

---

#### setOnStateChange()

连接状态变化时触发。

```cpp
void setOnStateChange(OnStateChangeCallback callback);
```

---

### 6.3 回调示例

```cpp
p2p::P2PClient client("ws://localhost:8080");

client.setOnConnected([]() {
    std::cout << "已连接到服务器" << std::endl;
});

client.setOnDisconnected([](const p2p::Error& error) {
    std::cout << "已断开: " << error.message << std::endl;
});

client.setOnPeerConnected([&client](const std::string& peerId) {
    std::cout << "Peer 已连接: " << peerId << std::endl;
    client.sendText(peerId, "欢迎！");
});

client.setOnPeerDisconnected([](const std::string& peerId) {
    std::cout << "Peer 已断开: " << peerId << std::endl;
});

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

client.setOnError([](const p2p::Error& error) {
    std::cerr << "错误: " << error.message << std::endl;
});

client.setOnStateChange([](p2p::ConnectionState state) {
    const char* names[] = {"断开", "连接中", "已连接", "失败"};
    std::cout << "状态: " << names[static_cast<int>(state)] << std::endl;
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
        default:
            std::cerr << "错误: " << error.message << std::endl;
    }
});
```

---

## 8. 完整示例

### 8.1 简单聊天客户端

```cpp
#include <p2p/p2p_client.hpp>
#include <iostream>
#include <string>
#include <thread>

int main() {
    p2p::P2PClient::setLogLevel(2);
    
    p2p::ClientConfig config;
    config.signalingUrl = "ws://localhost:8080";
    
    p2p::P2PClient client(config);
    
    // 设置回调
    client.setOnConnected([]() {
        std::cout << ">>> 已连接到服务器" << std::endl;
    });
    
    client.setOnPeerConnected([](const std::string& peerId) {
        std::cout << ">>> " << peerId << " 已上线" << std::endl;
    });
    
    client.setOnPeerDisconnected([](const std::string& peerId) {
        std::cout << ">>> " << peerId << " 已下线" << std::endl;
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
    
    // 连接
    if (!client.connect()) {
        std::cerr << "连接失败" << std::endl;
        return 1;
    }
    
    std::cout << "我的 ID: " << client.getLocalId() << std::endl;
    std::cout << "命令: list | connect <id> | send <id> <msg> | quit" << std::endl;
    
    // 命令循环
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
        
        if (line == "list") {
            client.requestPeerList();
        }
        else if (line.substr(0, 8) == "connect ") {
            std::string peerId = line.substr(8);
            client.connectToPeer(peerId);
        }
        else if (line.substr(0, 5) == "send ") {
            size_t space = line.find(' ', 5);
            if (space != std::string::npos) {
                std::string peerId = line.substr(5, space - 5);
                std::string msg = line.substr(space + 1);
                client.sendText(peerId, msg);
            }
        }
    }
    
    client.disconnect();
    return 0;
}
```

### 8.2 文件传输示例

```cpp
#include <p2p/p2p_client.hpp>
#include <fstream>
#include <vector>

// 发送文件
void sendFile(p2p::P2PClient& client, const std::string& peerId, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开文件" << std::endl;
        return;
    }
    
    // 读取文件
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    
    // 发送文件名 (JSON格式)
    std::string header = R"({"type":"file","name":")" + filename + 
                         R"(","size":)" + std::to_string(data.size()) + "}";
    client.sendText(peerId, header);
    
    // 发送文件内容
    client.sendBinary(peerId, data);
    
    std::cout << "文件已发送: " << filename << " (" << data.size() << " bytes)" << std::endl;
}

// 接收文件
std::string pendingFileName;
size_t pendingFileSize = 0;

void onTextMessage(const std::string& from, const std::string& msg) {
    // 解析文件头
    if (msg.find(R"("type":"file")") != std::string::npos) {
        // 简化解析
        auto namePos = msg.find(R"("name":")") + 8;
        auto nameEnd = msg.find('"', namePos);
        pendingFileName = msg.substr(namePos, nameEnd - namePos);
        
        auto sizePos = msg.find(R"("size":)") + 7;
        auto sizeEnd = msg.find('}', sizePos);
        pendingFileSize = std::stoull(msg.substr(sizePos, sizeEnd - sizePos));
        
        std::cout << "准备接收文件: " << pendingFileName << std::endl;
    }
}

void onBinaryMessage(const std::string& from, const p2p::BinaryData& data) {
    if (!pendingFileName.empty()) {
        std::ofstream file("received_" + pendingFileName, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << "文件已保存: received_" << pendingFileName << std::endl;
        pendingFileName.clear();
    }
}
```

### 8.3 带重试的连接

```cpp
#include <p2p/p2p_client.hpp>
#include <chrono>
#include <thread>

bool connectWithRetry(p2p::P2PClient& client, int maxRetries = 3) {
    for (int i = 0; i < maxRetries; ++i) {
        std::cout << "连接尝试 " << (i + 1) << "/" << maxRetries << std::endl;
        
        if (client.connect()) {
            return true;
        }
        
        if (i < maxRetries - 1) {
            std::cout << "等待 3 秒后重试..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    
    return false;
}

bool connectToPeerWithTimeout(p2p::P2PClient& client, 
                               const std::string& peerId,
                               std::chrono::seconds timeout) {
    client.connectToPeer(peerId);
    
    auto start = std::chrono::steady_clock::now();
    while (!client.isPeerConnected(peerId)) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return true;
}
```

---

## 附录 A: 线程安全

- 所有公共方法都是**线程安全**的
- 回调函数在**内部工作线程**中执行，如需更新 UI 请注意线程同步
- 避免在回调中进行长时间阻塞操作

## 附录 B: 性能建议

1. **大文件传输**: 分块发送，每块 16KB-64KB
2. **高频消息**: 考虑合并多条消息
3. **二进制优先**: 结构化数据优先使用二进制格式

## 附录 C: 常见问题

**Q: 为什么 `sendText` 返回 false？**
A: 检查 `isPeerConnected()` 确保通道已打开。

**Q: 连接建立后多久可以发送消息？**
A: 收到 `OnPeerConnected` 回调后立即可以发送。

**Q: 最大消息大小是多少？**
A: 理论上无限制，但建议单条消息不超过 256KB。