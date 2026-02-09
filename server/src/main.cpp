// server/src/main.cpp
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <thread>
#include <csignal>
#include <atomic>

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include "protocol.hpp"

using namespace p2p;
using json = nlohmann::json;

class SignalingServer {
public:
    SignalingServer(uint16_t port) : port_(port), running_(false) {}
    
    void start() {
        running_ = true;
        
        rtc::WebSocketServer::Configuration config;
        config.port = port_;
        config.enableTls = false;
        
        server_ = std::make_unique<rtc::WebSocketServer>(config);
        
        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
            std::cout << "[Server] New client connected" << std::endl;
            
            auto clientId = std::make_shared<std::string>();
            
            ws->onOpen([ws]() {
                std::cout << "[Server] WebSocket opened" << std::endl;
            });
            
            ws->onMessage([this, ws, clientId](auto message) {
                if (std::holds_alternative<std::string>(message)) {
                    handleMessage(ws, std::get<std::string>(message), clientId);
                }
            });
            
            ws->onClosed([this, clientId]() {
                if (!clientId->empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    clients_.erase(*clientId);
                    std::cout << "[Server] Client disconnected: " << *clientId << std::endl;
                    broadcastPeerList();
                }
            });
            
            ws->onError([clientId](const std::string& error) {
                std::cerr << "[Server] WebSocket error: " << error << std::endl;
            });
        });
        
        std::cout << "[Server] Signaling server started on port " << port_ << std::endl;
    }
    
    void stop() {
        running_ = false;
        if (server_) {
            server_->stop();
        }
    }
    
    bool isRunning() const { return running_; }

private:
    void handleMessage(std::shared_ptr<rtc::WebSocket> ws, 
                       const std::string& msgStr,
                       std::shared_ptr<std::string> clientId) {
        try {
            auto msg = SignalingMessage::deserialize(msgStr);
            
            switch (msg.type) {
                case MessageType::Register:
                    handleRegister(ws, msg, clientId);
                    break;
                    
                case MessageType::PeerList:
                    sendPeerList(ws, *clientId);
                    break;
                    
                case MessageType::Offer:
                case MessageType::Answer:
                case MessageType::Candidate:
                    forwardMessage(msg);
                    break;
                    
                case MessageType::Connect:
                    handleConnectRequest(ws, msg, *clientId);
                    break;
                    
                default:
                    std::cerr << "[Server] Unknown message type" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Server] Error handling message: " << e.what() << std::endl;
        }
    }
    
    void handleRegister(std::shared_ptr<rtc::WebSocket> ws, 
                        const SignalingMessage& msg,
                        std::shared_ptr<std::string> clientId) {
        std::string peerId = msg.payload;
        
        if (peerId.empty()) {
            // 生成唯一ID
            static std::atomic<int> counter{0};
            peerId = "peer_" + std::to_string(++counter);
        }
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // 检查是否已存在
            if (clients_.find(peerId) != clients_.end()) {
                SignalingMessage errorMsg;
                errorMsg.type = MessageType::Error;
                errorMsg.payload = "Peer ID already exists";
                ws->send(errorMsg.serialize());
                return;
            }
            
            *clientId = peerId;
            clients_[peerId] = ws;
        }
        
        std::cout << "[Server] Client registered: " << peerId << std::endl;
        
        // 发送注册确认
        SignalingMessage response;
        response.type = MessageType::Register;
        response.payload = peerId;
        ws->send(response.serialize());
        
        // 广播新的peer列表
        broadcastPeerList();
    }
    
    void sendPeerList(std::shared_ptr<rtc::WebSocket> ws, const std::string& excludeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        json peerList = json::array();
        for (const auto& [id, client] : clients_) {
            if (id != excludeId) {
                peerList.push_back(id);
            }
        }
        
        SignalingMessage msg;
        msg.type = MessageType::PeerList;
        msg.payload = peerList.dump();
        ws->send(msg.serialize());
    }
    
    void broadcastPeerList() {
        json peerList = json::array();
        for (const auto& [id, client] : clients_) {
            peerList.push_back(id);
        }
        
        for (const auto& [id, client] : clients_) {
            json filteredList = json::array();
            for (const auto& peerId : peerList) {
                if (peerId != id) {
                    filteredList.push_back(peerId);
                }
            }
            
            SignalingMessage msg;
            msg.type = MessageType::PeerList;
            msg.payload = filteredList.dump();
            
            if (client && client->isOpen()) {
                client->send(msg.serialize());
            }
        }
    }
    
    void forwardMessage(const SignalingMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = clients_.find(msg.to);
        if (it != clients_.end() && it->second && it->second->isOpen()) {
            it->second->send(msg.serialize());
            std::cout << "[Server] Forwarded " << messageTypeToString(msg.type) 
                      << " from " << msg.from << " to " << msg.to << std::endl;
        } else {
            std::cerr << "[Server] Target peer not found: " << msg.to << std::endl;
        }
    }
    
    void handleConnectRequest(std::shared_ptr<rtc::WebSocket> ws,
                              const SignalingMessage& msg,
                              const std::string& fromId) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = clients_.find(msg.to);
        if (it != clients_.end()) {
            SignalingMessage response;
            response.type = MessageType::Connect;
            response.from = fromId;
            response.to = msg.to;
            response.payload = "connect_request";
            
            if (it->second && it->second->isOpen()) {
                it->second->send(response.serialize());
            }
        } else {
            SignalingMessage errorMsg;
            errorMsg.type = MessageType::Error;
            errorMsg.payload = "Peer not found: " + msg.to;
            ws->send(errorMsg.serialize());
        }
    }

private:
    uint16_t port_;
    std::atomic<bool> running_;
    std::unique_ptr<rtc::WebSocketServer> server_;
    std::unordered_map<std::string, std::shared_ptr<rtc::WebSocket>> clients_;
    std::mutex mutex_;
};

std::unique_ptr<SignalingServer> g_server;

void signalHandler(int signal) {
    std::cout << "\n[Server] Shutting down..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        rtc::InitLogger(rtc::LogLevel::Info);
        
        g_server = std::make_unique<SignalingServer>(port);
        g_server->start();
        
        // 保持运行
        while (g_server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        std::cerr << "[Server] Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}