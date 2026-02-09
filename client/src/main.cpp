// client/src/main.cpp
#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <queue>
#include <vector>

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include "protocol.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace p2p;
using json = nlohmann::json;

class P2PClient {
public:
    P2PClient(const std::string& signalingUrl) 
        : signalingUrl_(signalingUrl), connected_(false), running_(false) {}
    
    ~P2PClient() {
        disconnect();
    }
    
    bool connect(const std::string& peerId = "") {
        try {
            running_ = true;
            requestedPeerId_ = peerId;
            
            // 配置RTC
            rtc::Configuration rtcConfig;
            for (const auto& server : getDefaultStunServers()) {
                rtcConfig.iceServers.emplace_back(server);
            }
            rtcConfig_ = rtcConfig;
            
            // 连接信令服务器
            ws_ = std::make_shared<rtc::WebSocket>();
            
            ws_->onOpen([this]() {
                std::cout << "[Client] Connected to signaling server" << std::endl;
                connected_ = true;
                
                // 注册
                SignalingMessage msg;
                msg.type = MessageType::Register;
                msg.payload = requestedPeerId_;
                ws_->send(msg.serialize());
            });
            
            ws_->onMessage([this](auto message) {
                if (std::holds_alternative<std::string>(message)) {
                    handleSignalingMessage(std::get<std::string>(message));
                }
            });
            
            ws_->onClosed([this]() {
                std::cout << "[Client] Disconnected from signaling server" << std::endl;
                connected_ = false;
            });
            
            ws_->onError([](const std::string& error) {
                std::cerr << "[Client] WebSocket error: " << error << std::endl;
            });
            
            ws_->open(signalingUrl_);
            
            // 等待连接
            for (int i = 0; i < 50 && !connected_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            return connected_;
        } catch (const std::exception& e) {
            std::cerr << "[Client] Connection error: " << e.what() << std::endl;
            return false;
        }
    }
    
    void disconnect() {
        running_ = false;
        
        // 关闭所有peer连接
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            for (auto& [id, pc] : peerConnections_) {
                if (pc) pc->close();
            }
            peerConnections_.clear();
            dataChannels_.clear();
        }
        
        if (ws_ && ws_->isOpen()) {
            ws_->close();
        }
        connected_ = false;
    }
    
    void connectToPeer(const std::string& peerId) {
        std::cout << "[Client] Initiating connection to " << peerId << std::endl;
        createPeerConnection(peerId, true);
    }
    
    void sendMessage(const std::string& peerId, const std::string& message) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto it = dataChannels_.find(peerId);
        if (it != dataChannels_.end() && it->second && it->second->isOpen()) {
            it->second->send(message);
            std::cout << "[Client] Sent to " << peerId << ": " << message << std::endl;
        } else {
            std::cerr << "[Client] Cannot send - channel not open to " << peerId << std::endl;
        }
    }
    
    void broadcastMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        for (auto& [peerId, dc] : dataChannels_) {
            if (dc && dc->isOpen()) {
                dc->send(message);
                std::cout << "[Client] Broadcast to " << peerId << ": " << message << std::endl;
            }
        }
    }
    
    void requestPeerList() {
        if (ws_ && ws_->isOpen()) {
            SignalingMessage msg;
            msg.type = MessageType::PeerList;
            ws_->send(msg.serialize());
        }
    }
    
    std::string getLocalId() const { return localId_; }
    bool isConnected() const { return connected_; }
    
    void setOnMessageCallback(std::function<void(const std::string&, const std::string&)> callback) {
        onMessageCallback_ = callback;
    }
    
    void setOnPeerConnectedCallback(std::function<void(const std::string&)> callback) {
        onPeerConnectedCallback_ = callback;
    }
    
    void setOnPeerDisconnectedCallback(std::function<void(const std::string&)> callback) {
        onPeerDisconnectedCallback_ = callback;
    }
    
    void setOnPeerListCallback(std::function<void(const std::vector<std::string>&)> callback) {
        onPeerListCallback_ = callback;
    }

private:
    void handleSignalingMessage(const std::string& msgStr) {
        try {
            auto msg = SignalingMessage::deserialize(msgStr);
            
            switch (msg.type) {
                case MessageType::Register:
                    localId_ = msg.payload;
                    std::cout << "[Client] Registered as: " << localId_ << std::endl;
                    requestPeerList();
                    break;
                    
                case MessageType::PeerList: {
                    auto peers = json::parse(msg.payload);
                    std::vector<std::string> peerList;
                    for (const auto& peer : peers) {
                        peerList.push_back(peer.get<std::string>());
                    }
                    std::cout << "[Client] Online peers: " << peerList.size() << std::endl;
                    for (const auto& p : peerList) {
                        std::cout << "  - " << p << std::endl;
                    }
                    if (onPeerListCallback_) {
                        onPeerListCallback_(peerList);
                    }
                    break;
                }
                    
                case MessageType::Offer:
                    handleOffer(msg);
                    break;
                    
                case MessageType::Answer:
                    handleAnswer(msg);
                    break;
                    
                case MessageType::Candidate:
                    handleCandidate(msg);
                    break;
                    
                case MessageType::Connect:
                    std::cout << "[Client] Connection request from " << msg.from << std::endl;
                    break;
                    
                case MessageType::Error:
                    std::cerr << "[Client] Server error: " << msg.payload << std::endl;
                    break;
                    
                default:
                    break;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Client] Error parsing message: " << e.what() << std::endl;
        }
    }
    
    void createPeerConnection(const std::string& peerId, bool initiator) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto pc = std::make_shared<rtc::PeerConnection>(rtcConfig_);
        peerConnections_[peerId] = pc;
        
        pc->onLocalDescription([this, peerId, initiator](rtc::Description description) {
            SignalingMessage msg;
            msg.type = initiator ? MessageType::Offer : MessageType::Answer;
            msg.from = localId_;
            msg.to = peerId;
            
            json descJson = {
                {"type", description.typeString()},
                {"sdp", std::string(description)}
            };
            msg.payload = descJson.dump();
            
            if (ws_ && ws_->isOpen()) {
                ws_->send(msg.serialize());
            }
        });
        
        pc->onLocalCandidate([this, peerId](rtc::Candidate candidate) {
            SignalingMessage msg;
            msg.type = MessageType::Candidate;
            msg.from = localId_;
            msg.to = peerId;
            
            json candJson = {
                {"candidate", std::string(candidate)},
                {"mid", candidate.mid()}
            };
            msg.payload = candJson.dump();
            
            if (ws_ && ws_->isOpen()) {
                ws_->send(msg.serialize());
            }
        });
        
        pc->onStateChange([this, peerId](rtc::PeerConnection::State state) {
            std::cout << "[Client] Connection state with " << peerId << ": " 
                      << static_cast<int>(state) << std::endl;
                      
            if (state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                if (onPeerDisconnectedCallback_) {
                    onPeerDisconnectedCallback_(peerId);
                }
            }
        });
        
        pc->onGatheringStateChange([peerId](rtc::PeerConnection::GatheringState state) {
            std::cout << "[Client] Gathering state with " << peerId << ": " 
                      << static_cast<int>(state) << std::endl;
        });
        
        pc->onDataChannel([this, peerId](std::shared_ptr<rtc::DataChannel> dc) {
            std::cout << "[Client] DataChannel received from " << peerId << std::endl;
            setupDataChannel(peerId, dc);
        });
        
        if (initiator) {
            // 创建DataChannel
            auto dc = pc->createDataChannel("p2p-channel");
            setupDataChannel(peerId, dc);
        }
    }
    
    void setupDataChannel(const std::string& peerId, std::shared_ptr<rtc::DataChannel> dc) {
        dataChannels_[peerId] = dc;
        
        dc->onOpen([this, peerId]() {
            std::cout << "[Client] DataChannel opened with " << peerId << std::endl;
            if (onPeerConnectedCallback_) {
                onPeerConnectedCallback_(peerId);
            }
        });
        
        dc->onClosed([this, peerId]() {
            std::cout << "[Client] DataChannel closed with " << peerId << std::endl;
        });
        
        dc->onMessage([this, peerId](auto message) {
            if (std::holds_alternative<std::string>(message)) {
                std::string msg = std::get<std::string>(message);
                std::cout << "[Client] Received from " << peerId << ": " << msg << std::endl;
                
                if (onMessageCallback_) {
                    onMessageCallback_(peerId, msg);
                }
            }
        });
        
        dc->onError([peerId](const std::string& error) {
            std::cerr << "[Client] DataChannel error with " << peerId << ": " << error << std::endl;
        });
    }
    
    void handleOffer(const SignalingMessage& msg) {
        std::cout << "[Client] Received offer from " << msg.from << std::endl;
        
        createPeerConnection(msg.from, false);
        
        auto descJson = json::parse(msg.payload);
        rtc::Description description(descJson["sdp"].get<std::string>(), 
                                      descJson["type"].get<std::string>());
        
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = peerConnections_.find(msg.from);
        if (it != peerConnections_.end()) {
            it->second->setRemoteDescription(description);
        }
    }
    
    void handleAnswer(const SignalingMessage& msg) {
        std::cout << "[Client] Received answer from " << msg.from << std::endl;
        
        auto descJson = json::parse(msg.payload);
        rtc::Description description(descJson["sdp"].get<std::string>(), 
                                      descJson["type"].get<std::string>());
        
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = peerConnections_.find(msg.from);
        if (it != peerConnections_.end()) {
            it->second->setRemoteDescription(description);
        }
    }
    
    void handleCandidate(const SignalingMessage& msg) {
        auto candJson = json::parse(msg.payload);
        rtc::Candidate candidate(candJson["candidate"].get<std::string>(),
                                  candJson["mid"].get<std::string>());
        
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = peerConnections_.find(msg.from);
        if (it != peerConnections_.end()) {
            it->second->addRemoteCandidate(candidate);
        }
    }

private:
    std::string signalingUrl_;
    std::string requestedPeerId_;
    std::string localId_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    
    std::shared_ptr<rtc::WebSocket> ws_;
    rtc::Configuration rtcConfig_;
    
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> peerConnections_;
    std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>> dataChannels_;
    std::mutex peerMutex_;
    
    std::function<void(const std::string&, const std::string&)> onMessageCallback_;
    std::function<void(const std::string&)> onPeerConnectedCallback_;
    std::function<void(const std::string&)> onPeerDisconnectedCallback_;
    std::function<void(const std::vector<std::string>&)> onPeerListCallback_;
};

// 简单的字符串分割函数
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

// 获取第一个空格后的所有内容
std::string getRestOfLine(const std::string& line, size_t startPos) {
    if (startPos >= line.length()) return "";
    size_t pos = line.find(' ', startPos);
    if (pos == std::string::npos) return "";
    pos++; // 跳过空格
    if (pos >= line.length()) return "";
    return line.substr(pos);
}

// 交互式命令行界面
void runInteractiveMode(P2PClient& client) {
    std::cout << "\n=== P2P Client Commands ===" << std::endl;
    std::cout << "  list          - List online peers" << std::endl;
    std::cout << "  connect <id>  - Connect to a peer" << std::endl;
    std::cout << "  send <id> <msg> - Send message to peer" << std::endl;
    std::cout << "  broadcast <msg> - Send to all connected peers" << std::endl;
    std::cout << "  quit          - Exit" << std::endl;
    std::cout << "=========================\n" << std::endl;
    
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        // 使用简单的字符串处理代替 istringstream
        auto tokens = splitString(line);
        if (tokens.empty()) continue;
        
        std::string command = tokens[0];
        
        if (command == "quit" || command == "exit") {
            break;
        } else if (command == "list") {
            client.requestPeerList();
        } else if (command == "connect") {
            if (tokens.size() >= 2) {
                client.connectToPeer(tokens[1]);
            } else {
                std::cout << "Usage: connect <peer_id>" << std::endl;
            }
        } else if (command == "send") {
            if (tokens.size() >= 3) {
                std::string peerId = tokens[1];
                // 获取 "send <id> " 之后的所有内容作为消息
                size_t msgStart = line.find(peerId);
                if (msgStart != std::string::npos) {
                    msgStart += peerId.length();
                    while (msgStart < line.length() && line[msgStart] == ' ') {
                        msgStart++;
                    }
                    if (msgStart < line.length()) {
                        std::string message = line.substr(msgStart);
                        client.sendMessage(peerId, message);
                    } else {
                        std::cout << "Usage: send <peer_id> <message>" << std::endl;
                    }
                }
            } else {
                std::cout << "Usage: send <peer_id> <message>" << std::endl;
            }
        } else if (command == "broadcast") {
            if (tokens.size() >= 2) {
                // 获取 "broadcast " 之后的所有内容
                size_t msgStart = line.find(' ');
                if (msgStart != std::string::npos) {
                    msgStart++;
                    std::string message = line.substr(msgStart);
                    client.broadcastMessage(message);
                }
            } else {
                std::cout << "Usage: broadcast <message>" << std::endl;
            }
        } else {
            std::cout << "Unknown command: " << command << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // 设置控制台UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::string serverUrl = "ws://localhost:8080";
    std::string peerId;
    
    if (argc > 1) {
        serverUrl = argv[1];
    }
    if (argc > 2) {
        peerId = argv[2];
    }
    
    std::cout << "[Client] Connecting to signaling server: " << serverUrl << std::endl;
    
    try {
        rtc::InitLogger(rtc::LogLevel::Warning);
        
        P2PClient client(serverUrl);
        
        client.setOnMessageCallback([](const std::string& from, const std::string& msg) {
            std::cout << "\n>>> Message from " << from << ": " << msg << std::endl;
        });
        
        client.setOnPeerConnectedCallback([](const std::string& peerId) {
            std::cout << "\n>>> Peer connected: " << peerId << std::endl;
        });
        
        client.setOnPeerDisconnectedCallback([](const std::string& peerId) {
            std::cout << "\n>>> Peer disconnected: " << peerId << std::endl;
        });
        
        if (client.connect(peerId)) {
            std::cout << "[Client] My ID: " << client.getLocalId() << std::endl;
            runInteractiveMode(client);
        } else {
            std::cerr << "[Client] Failed to connect to signaling server" << std::endl;
            return 1;
        }
        
        client.disconnect();
    } catch (const std::exception& e) {
        std::cerr << "[Client] Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}