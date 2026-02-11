// server/src/main.cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include "protocol.hpp"

using json = nlohmann::json;

// 客户端信息
struct ClientInfo {
    std::shared_ptr<rtc::WebSocket> ws;
    std::string id;
    bool relayAuthenticated = false;
};

// 中继连接对（用于快速查找）
struct RelayPair {
    std::string peer1;
    std::string peer2;
    
    bool contains(const std::string& id) const {
        return peer1 == id || peer2 == id;
    }
    
    std::string getOther(const std::string& id) const {
        return (peer1 == id) ? peer2 : peer1;
    }
    
    // 用于 set 排序
    bool operator<(const RelayPair& other) const {
        auto a1 = std::min(peer1, peer2);
        auto a2 = std::max(peer1, peer2);
        auto b1 = std::min(other.peer1, other.peer2);
        auto b2 = std::max(other.peer1, other.peer2);
        return std::tie(a1, a2) < std::tie(b1, b2);
    }
    
    bool operator==(const RelayPair& other) const {
        return (peer1 == other.peer1 && peer2 == other.peer2) ||
               (peer1 == other.peer2 && peer2 == other.peer1);
    }
};

class SignalingServer {
public:
    SignalingServer(uint16_t port) : port_(port) {
        loadEnvFile();
    }
    
    void run() {
        rtc::WebSocketServer::Configuration config;
        config.port = port_;
        config.enableTls = false;
        
        server_ = std::make_unique<rtc::WebSocketServer>(config);
        
        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
            std::cout << "[Server] New client connected" << std::endl;
            
            auto clientId = std::make_shared<std::string>();
            
            ws->onOpen([this, ws, clientId]() {
                std::cout << "[Server] WebSocket opened" << std::endl;
            });
            
            ws->onMessage([this, ws, clientId](auto message) {
                if (std::holds_alternative<std::string>(message)) {
                    handleMessage(ws, *clientId, std::get<std::string>(message));
                }
            });
            
            ws->onClosed([this, clientId]() {
                if (!clientId->empty()) {
                    std::cout << "[Server] Client disconnected: " << *clientId << std::endl;
                    removeClient(*clientId);
                }
            });
            
            ws->onError([clientId](const std::string& error) {
                std::cerr << "[Server] WebSocket error for " << *clientId << ": " << error << std::endl;
            });
        });
        
        std::cout << "[Server] Signaling server started on port " << port_ << std::endl;
        std::cout << "[Server] Relay password: " << (relayPassword_.empty() ? "(not set)" : "(configured)") << std::endl;
        
        // 保持运行
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit" || line == "exit") {
                break;
            } else if (line == "list") {
                listClients();
            } else if (line == "relay") {
                listRelayConnections();
            } else if (line == "help") {
                std::cout << "Commands: list, relay, quit" << std::endl;
            }
        }
        
        std::cout << "[Server] Shutting down..." << std::endl;
    }
    
private:
    void loadEnvFile() {
        std::ifstream envFile(".env");
        if (!envFile.is_open()) {
            std::cout << "[Server] No .env file found, relay will be disabled" << std::endl;
            return;
        }
        
        std::string line;
        while (std::getline(envFile, line)) {
            // 跳过空行和注释
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // 移除首尾空白
            size_t start = line.find_first_not_of(" \t");
            size_t end = line.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) {
                continue;
            }
            line = line.substr(start, end - start + 1);
            
            // 解析 KEY=VALUE
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) {
                continue;
            }
            
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // 移除可能的引号
            if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
                value = value.substr(1);
            }
            if (!value.empty() && (value.back() == '"' || value.back() == '\'')) {
                value = value.substr(0, value.size() - 1);
            }
            
            if (key == "RELAY_PASSWORD") {
                relayPassword_ = value;
                std::cout << "[Server] Relay password loaded from .env" << std::endl;
            }
        }
    }
    
    void handleMessage(std::shared_ptr<rtc::WebSocket> ws, std::string& clientId, 
                       const std::string& msgStr) {
        try {
            auto msg = p2p::SignalingMessage::deserialize(msgStr);
            
            switch (msg.type) {
                case p2p::MessageType::Register:
                    handleRegister(ws, clientId, msg);
                    break;
                    
                case p2p::MessageType::PeerList:
                    handlePeerList(ws, clientId);
                    break;
                    
                case p2p::MessageType::Offer:
                case p2p::MessageType::Answer:
                case p2p::MessageType::Candidate:
                    handleSignaling(clientId, msg);
                    break;
                    
                case p2p::MessageType::RelayAuth:
                    handleRelayAuth(ws, clientId, msg);
                    break;
                    
                case p2p::MessageType::RelayConnect:
                    handleRelayConnect(clientId, msg);
                    break;
                    
                case p2p::MessageType::RelayData:
                    handleRelayData(clientId, msg);
                    break;
                    
                case p2p::MessageType::RelayDisconnect:
                    handleRelayDisconnect(clientId, msg);
                    break;
                    
                default:
                    break;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Server] Error handling message: " << e.what() << std::endl;
        }
    }
    
    void handleRegister(std::shared_ptr<rtc::WebSocket> ws, std::string& clientId, 
                        const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string requestedId = msg.payload;
        
        // 如果请求特定ID，检查是否可用
        if (!requestedId.empty()) {
            if (clients_.count(requestedId)) {
                // ID已被使用，生成新ID
                clientId = generateClientId();
            } else {
                clientId = requestedId;
            }
        } else {
            clientId = generateClientId();
        }
        
        ClientInfo info;
        info.ws = ws;
        info.id = clientId;
        info.relayAuthenticated = false;
        clients_[clientId] = info;
        
        std::cout << "[Server] Client registered: " << clientId << std::endl;
        
        // 发送注册确认
        p2p::SignalingMessage response;
        response.type = p2p::MessageType::Register;
        response.payload = clientId;
        ws->send(response.serialize());
    }
    
    void handlePeerList(std::shared_ptr<rtc::WebSocket> ws, const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        json peers = json::array();
        for (const auto& [id, info] : clients_) {
            if (id != clientId) {
                peers.push_back(id);
            }
        }
        
        p2p::SignalingMessage response;
        response.type = p2p::MessageType::PeerList;
        response.payload = peers.dump();
        ws->send(response.serialize());
    }
    
    void handleSignaling(const std::string& fromId, const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = clients_.find(msg.to);
        if (it != clients_.end()) {
            p2p::SignalingMessage fwdMsg = msg;
            fwdMsg.from = fromId;
            it->second.ws->send(fwdMsg.serialize());
        } else {
            // 目标不存在，发送错误
            auto clientIt = clients_.find(fromId);
            if (clientIt != clients_.end()) {
                p2p::SignalingMessage errorMsg;
                errorMsg.type = p2p::MessageType::Error;
                errorMsg.payload = "Peer not found: " + msg.to;
                clientIt->second.ws->send(errorMsg.serialize());
            }
        }
    }
    
    void handleRelayAuth(std::shared_ptr<rtc::WebSocket> ws, const std::string& clientId,
                         const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string providedPassword = msg.payload;
        bool success = false;
        std::string message;
        
        if (relayPassword_.empty()) {
            success = false;
            message = "Relay is not configured on this server";
        } else if (providedPassword == relayPassword_) {
            success = true;
            message = "Authentication successful";
            
            auto it = clients_.find(clientId);
            if (it != clients_.end()) {
                it->second.relayAuthenticated = true;
            }
            
            std::cout << "[Server] Relay auth successful for: " << clientId << std::endl;
        } else {
            success = false;
            message = "Invalid password";
            std::cout << "[Server] Relay auth failed for: " << clientId << std::endl;
        }
        
        p2p::SignalingMessage response;
        response.type = p2p::MessageType::RelayAuthResult;
        response.payload = json({
            {"success", success},
            {"message", message}
        }).dump();
        ws->send(response.serialize());
    }
    
    void handleRelayConnect(const std::string& fromId, const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查发送者是否已认证
        auto fromIt = clients_.find(fromId);
        if (fromIt == clients_.end() || !fromIt->second.relayAuthenticated) {
            sendError(fromId, "Not authenticated for relay");
            return;
        }
        
        // 检查目标是否存在
        auto toIt = clients_.find(msg.to);
        if (toIt == clients_.end()) {
            sendError(fromId, "Peer not found: " + msg.to);
            return;
        }
        
        // 建立中继连接对
        RelayPair pair{fromId, msg.to};
        relayConnections_.insert(pair);
        
        // 通知目标客户端有新的中继连接
        p2p::SignalingMessage notifyMsg;
        notifyMsg.type = p2p::MessageType::RelayConnect;
        notifyMsg.from = fromId;
        notifyMsg.to = msg.to;
        toIt->second.ws->send(notifyMsg.serialize());
        
        std::cout << "[Server] Relay connection established: " << fromId << " <-> " << msg.to << std::endl;
    }
    
    void handleRelayData(const std::string& fromId, const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查是否存在中继连接（不再检查发送者是否认证！）
        RelayPair pair{fromId, msg.to};
        bool hasRelayConnection = false;
        
        for (const auto& conn : relayConnections_) {
            if (conn == pair) {
                hasRelayConnection = true;
                break;
            }
        }
        
        if (!hasRelayConnection) {
            sendError(fromId, "No relay connection with " + msg.to);
            return;
        }
        
        // 转发数据到目标
        auto toIt = clients_.find(msg.to);
        if (toIt == clients_.end()) {
            sendError(fromId, "Peer not found: " + msg.to);
            return;
        }
        
        // 转发消息
        p2p::SignalingMessage fwdMsg = msg;
        fwdMsg.from = fromId;
        toIt->second.ws->send(fwdMsg.serialize());
    }
    
    void handleRelayDisconnect(const std::string& fromId, const p2p::SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 移除中继连接对
        RelayPair pair{fromId, msg.to};
        relayConnections_.erase(pair);
        
        // 通知目标客户端中继连接断开
        auto toIt = clients_.find(msg.to);
        if (toIt != clients_.end()) {
            p2p::SignalingMessage notifyMsg;
            notifyMsg.type = p2p::MessageType::RelayDisconnect;
            notifyMsg.from = fromId;
            notifyMsg.to = msg.to;
            toIt->second.ws->send(notifyMsg.serialize());
        }
        
        std::cout << "[Server] Relay disconnect: " << fromId << " <-> " << msg.to << std::endl;
    }
    
    void sendError(const std::string& clientId, const std::string& message) {
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            p2p::SignalingMessage errorMsg;
            errorMsg.type = p2p::MessageType::Error;
            errorMsg.payload = message;
            it->second.ws->send(errorMsg.serialize());
        }
    }
    
    void removeClient(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 清理该客户端的所有中继连接
        std::vector<RelayPair> toRemove;
        for (const auto& conn : relayConnections_) {
            if (conn.contains(clientId)) {
                toRemove.push_back(conn);
                
                // 通知另一端断开
                std::string otherId = conn.getOther(clientId);
                auto otherIt = clients_.find(otherId);
                if (otherIt != clients_.end()) {
                    p2p::SignalingMessage notifyMsg;
                    notifyMsg.type = p2p::MessageType::RelayDisconnect;
                    notifyMsg.from = clientId;
                    notifyMsg.to = otherId;
                    otherIt->second.ws->send(notifyMsg.serialize());
                }
            }
        }
        
        for (const auto& conn : toRemove) {
            relayConnections_.erase(conn);
        }
        
        clients_.erase(clientId);
    }
    
    std::string generateClientId() {
        static int counter = 0;
        return "peer_" + std::to_string(++counter);
    }
    
    void listClients() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "Connected clients (" << clients_.size() << "):" << std::endl;
        for (const auto& [id, info] : clients_) {
            std::cout << "  - " << id 
                      << (info.relayAuthenticated ? " [relay-auth]" : "") 
                      << std::endl;
        }
    }
    
    void listRelayConnections() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "Active relay connections (" << relayConnections_.size() << "):" << std::endl;
        for (const auto& conn : relayConnections_) {
            std::cout << "  - " << conn.peer1 << " <-> " << conn.peer2 << std::endl;
        }
    }

private:
    uint16_t port_;
    std::string relayPassword_;
    std::unique_ptr<rtc::WebSocketServer> server_;
    std::unordered_map<std::string, ClientInfo> clients_;
    std::set<RelayPair> relayConnections_;  // 中继连接对
    std::mutex mutex_;
};

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    try {
        rtc::InitLogger(rtc::LogLevel::Warning);
        
        SignalingServer server(port);
        server.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}