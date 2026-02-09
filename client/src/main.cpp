/**
 * P2P Client Library Example
 * 
 * 这是一个演示如何使用 P2P 客户端库的示例程序
 */

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <iomanip>  // 添加这个头文件

#include "p2p/p2p_client.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

std::vector<std::string> splitString(const std::string& str, char delimiter = ' ') {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

void printHelp() {
    std::cout << "\n=== P2P Client Commands ===" << std::endl;
    std::cout << "  list              - List online peers" << std::endl;
    std::cout << "  peers             - List connected peers" << std::endl;
    std::cout << "  connect <id>      - Connect to a peer" << std::endl;
    std::cout << "  send <id> <msg>   - Send text message to peer" << std::endl;
    std::cout << "  binary <id> <hex> - Send binary data (hex string)" << std::endl;
    std::cout << "  broadcast <msg>   - Send to all connected peers" << std::endl;
    std::cout << "  disconnect <id>   - Disconnect from peer" << std::endl;
    std::cout << "  help              - Show this help" << std::endl;
    std::cout << "  quit              - Exit" << std::endl;
    std::cout << "===========================\n" << std::endl;
}

// 将十六进制字符串转换为二进制数据
p2p::BinaryData hexToBytes(const std::string& hex) {
    p2p::BinaryData bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

// 将二进制数据转换为十六进制字符串
std::string bytesToHex(const p2p::BinaryData& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // 解析命令行参数
    std::string serverUrl = "ws://localhost:8080";
    std::string peerId;
    
    if (argc > 1) {
        serverUrl = argv[1];
    }
    if (argc > 2) {
        peerId = argv[2];
    }
    
    std::cout << "[Example] P2P Client Library Demo" << std::endl;
    std::cout << "[Example] Server: " << serverUrl << std::endl;
    
    // 设置日志级别
    p2p::P2PClient::setLogLevel(2); // Warning
    
    // 创建配置
    p2p::ClientConfig config;
    config.signalingUrl = serverUrl;
    config.peerId = peerId;
    config.connectionTimeout = 10000;
    
    // 创建客户端
    p2p::P2PClient client(config);
    
    // 设置回调
    client.setOnConnected([]() {
        std::cout << "\n[Event] Connected to signaling server" << std::endl;
    });
    
    client.setOnDisconnected([](const p2p::Error& error) {
        std::cout << "\n[Event] Disconnected: " << error.message << std::endl;
    });
    
    client.setOnPeerConnected([](const std::string& peerId) {
        std::cout << "\n[Event] Peer connected: " << peerId << std::endl;
        std::cout << "  You can now send messages with: send " << peerId << " <message>" << std::endl;
    });
    
    client.setOnPeerDisconnected([](const std::string& peerId) {
        std::cout << "\n[Event] Peer disconnected: " << peerId << std::endl;
    });
    
    client.setOnTextMessage([](const std::string& from, const std::string& msg) {
        std::cout << "\n[Message] From " << from << ": " << msg << std::endl;
    });
    
    client.setOnBinaryMessage([](const std::string& from, const p2p::BinaryData& data) {
        std::cout << "\n[Binary] From " << from << ": " << bytesToHex(data) 
                  << " (" << data.size() << " bytes)" << std::endl;
    });
    
    client.setOnPeerList([](const std::vector<std::string>& peers) {
        std::cout << "\n[PeerList] Online peers (" << peers.size() << "):" << std::endl;
        for (const auto& p : peers) {
            std::cout << "  - " << p << std::endl;
        }
    });
    
    client.setOnError([](const p2p::Error& error) {
        std::cerr << "\n[Error] " << error.message << std::endl;
    });
    
    client.setOnStateChange([](p2p::ConnectionState state) {
        const char* stateStr[] = {"Disconnected", "Connecting", "Connected", "Failed"};
        std::cout << "[State] " << stateStr[static_cast<int>(state)] << std::endl;
    });
    
    // 连接
    if (!client.connect()) {
        std::cerr << "[Example] Failed to connect to signaling server" << std::endl;
        return 1;
    }
    
    std::cout << "[Example] My ID: " << client.getLocalId() << std::endl;
    
    printHelp();
    
    // 交互式命令循环
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        auto tokens = splitString(line);
        if (tokens.empty()) continue;
        
        std::string command = tokens[0];
        
        if (command == "quit" || command == "exit") {
            break;
        } else if (command == "help") {
            printHelp();
        } else if (command == "list") {
            client.requestPeerList();
        } else if (command == "peers") {
            auto peers = client.getConnectedPeers();
            std::cout << "Connected peers (" << peers.size() << "):" << std::endl;
            for (const auto& p : peers) {
                std::cout << "  - " << p << std::endl;
            }
        } else if (command == "connect") {
            if (tokens.size() >= 2) {
                client.connectToPeer(tokens[1]);
            } else {
                std::cout << "Usage: connect <peer_id>" << std::endl;
            }
        } else if (command == "disconnect") {
            if (tokens.size() >= 2) {
                client.disconnectFromPeer(tokens[1]);
            } else {
                std::cout << "Usage: disconnect <peer_id>" << std::endl;
            }
        } else if (command == "send") {
            if (tokens.size() >= 3) {
                std::string targetPeer = tokens[1];
                size_t msgStart = line.find(targetPeer);
                if (msgStart != std::string::npos) {
                    msgStart += targetPeer.length();
                    while (msgStart < line.length() && line[msgStart] == ' ') {
                        msgStart++;
                    }
                    if (msgStart < line.length()) {
                        std::string message = line.substr(msgStart);
                        if (client.sendText(targetPeer, message)) {
                            std::cout << "Sent to " << targetPeer << ": " << message << std::endl;
                        }
                    }
                }
            } else {
                std::cout << "Usage: send <peer_id> <message>" << std::endl;
            }
        } else if (command == "binary") {
            if (tokens.size() >= 3) {
                std::string targetPeer = tokens[1];
                std::string hexData = tokens[2];
                auto data = hexToBytes(hexData);
                if (client.sendBinary(targetPeer, data)) {
                    std::cout << "Sent binary to " << targetPeer << ": " << hexData << std::endl;
                }
            } else {
                std::cout << "Usage: binary <peer_id> <hex_data>" << std::endl;
                std::cout << "Example: binary peer_1 48454c4c4f" << std::endl;
            }
        } else if (command == "broadcast") {
            if (tokens.size() >= 2) {
                size_t msgStart = line.find(' ');
                if (msgStart != std::string::npos) {
                    std::string message = line.substr(msgStart + 1);
                    size_t count = client.broadcastText(message);
                    std::cout << "Broadcast to " << count << " peers: " << message << std::endl;
                }
            } else {
                std::cout << "Usage: broadcast <message>" << std::endl;
            }
        } else {
            std::cout << "Unknown command: " << command << ". Type 'help' for commands." << std::endl;
        }
    }
    
    client.disconnect();
    std::cout << "[Example] Goodbye!" << std::endl;
    
    return 0;
}