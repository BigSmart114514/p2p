#include "p2p/p2p_client.hpp"
#include "protocol.hpp"

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <thread>
#include <regex>

namespace p2p {

using json = nlohmann::json;

// 解析 TURN URL 辅助函数
// 支持格式: turn:hostname:port 或 turns:hostname:port
struct ParsedTurnUrl {
    std::string hostname;
    uint16_t port = 3478;
    bool isTls = false;
    bool valid = false;
};

static ParsedTurnUrl parseTurnUrl(const std::string& url) {
    ParsedTurnUrl result;
    
    // 简单解析 turn:host:port 或 turns:host:port
    std::string remaining = url;
    
    if (remaining.substr(0, 6) == "turns:") {
        result.isTls = true;
        remaining = remaining.substr(6);
    } else if (remaining.substr(0, 5) == "turn:") {
        result.isTls = false;
        remaining = remaining.substr(5);
    } else {
        return result; // 无效格式
    }
    
    // 查找端口
    size_t colonPos = remaining.rfind(':');
    if (colonPos != std::string::npos) {
        result.hostname = remaining.substr(0, colonPos);
        try {
            result.port = static_cast<uint16_t>(std::stoi(remaining.substr(colonPos + 1)));
        } catch (...) {
            result.port = result.isTls ? 5349 : 3478;
        }
    } else {
        result.hostname = remaining;
        result.port = result.isTls ? 5349 : 3478;
    }
    
    result.valid = !result.hostname.empty();
    return result;
}

// ==================== 实现类 ====================
class P2PClientImpl {
public:
    explicit P2PClientImpl(const ClientConfig& config)
        : config_(config)
        , state_(ConnectionState::Disconnected)
        , running_(false)
    {
        // 配置 RTC - STUN 服务器
        for (const auto& server : config_.stunServers) {
            rtcConfig_.iceServers.emplace_back(server);
        }
        
        // 配置 TURN 服务器 - 需要解析 URL 并使用正确的构造函数
        for (const auto& turn : config_.turnServers) {
            auto parsed = parseTurnUrl(turn.url);
            if (parsed.valid) {
                // 使用 5 参数构造函数: hostname, port, username, password, RelayType
                rtc::IceServer::RelayType relayType = parsed.isTls ? 
                    rtc::IceServer::RelayType::TurnTls : 
                    rtc::IceServer::RelayType::TurnUdp;
                rtcConfig_.iceServers.emplace_back(
                    parsed.hostname, 
                    parsed.port, 
                    turn.username, 
                    turn.credential,
                    relayType
                );
            }
        }
    }
    
    ~P2PClientImpl() {
        disconnect();
    }
    
    bool connect() {
        try {
            running_ = true;
            setState(ConnectionState::Connecting);
            
            ws_ = std::make_shared<rtc::WebSocket>();
            
            ws_->onOpen([this]() {
                std::cout << "[P2P] Connected to signaling server" << std::endl;
                setState(ConnectionState::Connected);
                
                // 注册
                SignalingMessage msg;
                msg.type = MessageType::Register;
                msg.payload = config_.peerId;
                ws_->send(msg.serialize());
                
                if (onConnected_) {
                    onConnected_();
                }
            });
            
            ws_->onMessage([this](auto message) {
                if (std::holds_alternative<std::string>(message)) {
                    handleSignalingMessage(std::get<std::string>(message));
                }
            });
            
            ws_->onClosed([this]() {
                std::cout << "[P2P] Disconnected from signaling server" << std::endl;
                setState(ConnectionState::Disconnected);
                
                if (onDisconnected_) {
                    onDisconnected_(Error{ErrorCode::None, "Connection closed"});
                }
            });
            
            ws_->onError([this](const std::string& error) {
                std::cerr << "[P2P] WebSocket error: " << error << std::endl;
                setState(ConnectionState::Failed);
                
                if (onError_) {
                    onError_(Error{ErrorCode::SignalingError, error});
                }
            });
            
            ws_->open(config_.signalingUrl);
            
            // 等待连接
            auto timeout = std::chrono::milliseconds(config_.connectionTimeout);
            auto start = std::chrono::steady_clock::now();
            
            while (state_ == ConnectionState::Connecting) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                    setState(ConnectionState::Failed);
                    if (onError_) {
                        onError_(Error{ErrorCode::Timeout, "Connection timeout"});
                    }
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            
            return state_ == ConnectionState::Connected;
        } catch (const std::exception& e) {
            setState(ConnectionState::Failed);
            if (onError_) {
                onError_(Error{ErrorCode::ConnectionFailed, e.what()});
            }
            return false;
        }
    }
    
    std::future<bool> connectAsync() {
        return std::async(std::launch::async, [this]() {
            return connect();
        });
    }
    
    void disconnect() {
        running_ = false;
        
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
        
        setState(ConnectionState::Disconnected);
    }
    
    bool isConnected() const {
        return state_ == ConnectionState::Connected && ws_ && ws_->isOpen();
    }
    
    ConnectionState getState() const {
        return state_;
    }
    
    std::string getLocalId() const {
        return localId_;
    }
    
    bool connectToPeer(const std::string& peerId) {
        if (!isConnected()) {
            if (onError_) {
                onError_(Error{ErrorCode::ConnectionFailed, "Not connected to signaling server"});
            }
            return false;
        }
        
        std::cout << "[P2P] Initiating connection to " << peerId << std::endl;
        createPeerConnection(peerId, true);
        return true;
    }
    
    std::future<bool> connectToPeerAsync(const std::string& peerId, std::chrono::milliseconds timeout) {
        return std::async(std::launch::async, [this, peerId, timeout]() {
            if (!connectToPeer(peerId)) {
                return false;
            }
            
            auto start = std::chrono::steady_clock::now();
            while (!isPeerConnected(peerId)) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return true;
        });
    }
    
    void disconnectFromPeer(const std::string& peerId) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto dcIt = dataChannels_.find(peerId);
        if (dcIt != dataChannels_.end()) {
            if (dcIt->second) dcIt->second->close();
            dataChannels_.erase(dcIt);
        }
        
        auto pcIt = peerConnections_.find(peerId);
        if (pcIt != peerConnections_.end()) {
            if (pcIt->second) pcIt->second->close();
            peerConnections_.erase(pcIt);
        }
    }
    
    void requestPeerList() {
        if (ws_ && ws_->isOpen()) {
            SignalingMessage msg;
            msg.type = MessageType::PeerList;
            ws_->send(msg.serialize());
        }
    }
    
    std::vector<std::string> getConnectedPeers() const {
        std::lock_guard<std::mutex> lock(peerMutex_);
        std::vector<std::string> peers;
        for (const auto& [id, dc] : dataChannels_) {
            if (dc && dc->isOpen()) {
                peers.push_back(id);
            }
        }
        return peers;
    }
    
    bool isPeerConnected(const std::string& peerId) const {
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = dataChannels_.find(peerId);
        return it != dataChannels_.end() && it->second && it->second->isOpen();
    }
    
    std::optional<PeerInfo> getPeerInfo(const std::string& peerId) const {
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = dataChannels_.find(peerId);
        if (it == dataChannels_.end()) {
            return std::nullopt;
        }
        
        PeerInfo info;
        info.id = peerId;
        if (it->second) {
            if (it->second->isOpen()) {
                info.channelState = ChannelState::Open;
            } else if (it->second->isClosed()) {
                info.channelState = ChannelState::Closed;
            } else {
                info.channelState = ChannelState::Connecting;
            }
        } else {
            info.channelState = ChannelState::Closed;
        }
        return info;
    }
    
    bool sendText(const std::string& peerId, const std::string& message) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto it = dataChannels_.find(peerId);
        if (it == dataChannels_.end() || !it->second || !it->second->isOpen()) {
            if (onError_) {
                onError_(Error{ErrorCode::ChannelNotOpen, "Channel not open to " + peerId});
            }
            return false;
        }
        
        try {
            it->second->send(message);
            return true;
        } catch (const std::exception& e) {
            if (onError_) {
                onError_(Error{ErrorCode::InternalError, e.what()});
            }
            return false;
        }
    }
    
    bool sendBinary(const std::string& peerId, const BinaryData& data) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto it = dataChannels_.find(peerId);
        if (it == dataChannels_.end() || !it->second || !it->second->isOpen()) {
            if (onError_) {
                onError_(Error{ErrorCode::ChannelNotOpen, "Channel not open to " + peerId});
            }
            return false;
        }
        
        try {
            it->second->send(reinterpret_cast<const std::byte*>(data.data()), data.size());
            return true;
        } catch (const std::exception& e) {
            if (onError_) {
                onError_(Error{ErrorCode::InternalError, e.what()});
            }
            return false;
        }
    }
    
    bool sendBinary(const std::string& peerId, const void* data, size_t size) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        
        auto it = dataChannels_.find(peerId);
        if (it == dataChannels_.end() || !it->second || !it->second->isOpen()) {
            if (onError_) {
                onError_(Error{ErrorCode::ChannelNotOpen, "Channel not open to " + peerId});
            }
            return false;
        }
        
        try {
            it->second->send(static_cast<const std::byte*>(data), size);
            return true;
        } catch (const std::exception& e) {
            if (onError_) {
                onError_(Error{ErrorCode::InternalError, e.what()});
            }
            return false;
        }
    }
    
    bool send(const std::string& peerId, const Message& message) {
        if (message.type == Message::Type::Text) {
            return sendText(peerId, message.text);
        } else {
            return sendBinary(peerId, message.binary);
        }
    }
    
    size_t broadcastText(const std::string& message) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        size_t count = 0;
        
        for (auto& [peerId, dc] : dataChannels_) {
            if (dc && dc->isOpen()) {
                try {
                    dc->send(message);
                    ++count;
                } catch (...) {}
            }
        }
        return count;
    }
    
    size_t broadcastBinary(const BinaryData& data) {
        std::lock_guard<std::mutex> lock(peerMutex_);
        size_t count = 0;
        
        for (auto& [peerId, dc] : dataChannels_) {
            if (dc && dc->isOpen()) {
                try {
                    dc->send(reinterpret_cast<const std::byte*>(data.data()), data.size());
                    ++count;
                } catch (...) {}
            }
        }
        return count;
    }
    
    // 回调设置
    void setOnConnected(OnConnectedCallback cb) { onConnected_ = std::move(cb); }
    void setOnDisconnected(OnDisconnectedCallback cb) { onDisconnected_ = std::move(cb); }
    void setOnPeerConnected(OnPeerConnectedCallback cb) { onPeerConnected_ = std::move(cb); }
    void setOnPeerDisconnected(OnPeerDisconnectedCallback cb) { onPeerDisconnected_ = std::move(cb); }
    void setOnTextMessage(OnTextMessageCallback cb) { onTextMessage_ = std::move(cb); }
    void setOnBinaryMessage(OnBinaryMessageCallback cb) { onBinaryMessage_ = std::move(cb); }
    void setOnMessage(OnMessageCallback cb) { onMessage_ = std::move(cb); }
    void setOnPeerList(OnPeerListCallback cb) { onPeerList_ = std::move(cb); }
    void setOnError(OnErrorCallback cb) { onError_ = std::move(cb); }
    void setOnStateChange(OnStateChangeCallback cb) { onStateChange_ = std::move(cb); }
    
private:
    void setState(ConnectionState newState) {
        if (state_ != newState) {
            state_ = newState;
            if (onStateChange_) {
                onStateChange_(state_);
            }
        }
    }
    
    void handleSignalingMessage(const std::string& msgStr) {
        try {
            auto msg = SignalingMessage::deserialize(msgStr);
            
            switch (msg.type) {
                case MessageType::Register:
                    localId_ = msg.payload;
                    std::cout << "[P2P] Registered as: " << localId_ << std::endl;
                    requestPeerList();
                    break;
                    
                case MessageType::PeerList: {
                    auto peers = json::parse(msg.payload);
                    std::vector<std::string> peerList;
                    for (const auto& peer : peers) {
                        peerList.push_back(peer.get<std::string>());
                    }
                    if (onPeerList_) {
                        onPeerList_(peerList);
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
                    
                case MessageType::Error:
                    if (onError_) {
                        onError_(Error{ErrorCode::SignalingError, msg.payload});
                    }
                    break;
                    
                default:
                    break;
            }
        } catch (const std::exception& e) {
            if (onError_) {
                onError_(Error{ErrorCode::InvalidData, e.what()});
            }
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
            if (state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                if (onPeerDisconnected_) {
                    onPeerDisconnected_(peerId);
                }
            }
        });
        
        pc->onDataChannel([this, peerId](std::shared_ptr<rtc::DataChannel> dc) {
            setupDataChannel(peerId, dc);
        });
        
        if (initiator) {
            auto dc = pc->createDataChannel("p2p-channel");
            setupDataChannel(peerId, dc);
        }
    }
    
    void setupDataChannel(const std::string& peerId, std::shared_ptr<rtc::DataChannel> dc) {
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            dataChannels_[peerId] = dc;
        }
        
        dc->onOpen([this, peerId]() {
            std::cout << "[P2P] DataChannel opened with " << peerId << std::endl;
            if (onPeerConnected_) {
                onPeerConnected_(peerId);
            }
        });
        
        dc->onClosed([this, peerId]() {
            std::cout << "[P2P] DataChannel closed with " << peerId << std::endl;
            if (onPeerDisconnected_) {
                onPeerDisconnected_(peerId);
            }
        });
        
        dc->onMessage([this, peerId](auto message) {
            if (std::holds_alternative<std::string>(message)) {
                std::string text = std::get<std::string>(message);
                
                if (onTextMessage_) {
                    onTextMessage_(peerId, text);
                }
                if (onMessage_) {
                    onMessage_(peerId, Message::fromText(text));
                }
            } else if (std::holds_alternative<rtc::binary>(message)) {
                auto& binary = std::get<rtc::binary>(message);
                BinaryData data(reinterpret_cast<const uint8_t*>(binary.data()),
                               reinterpret_cast<const uint8_t*>(binary.data()) + binary.size());
                
                if (onBinaryMessage_) {
                    onBinaryMessage_(peerId, data);
                }
                if (onMessage_) {
                    onMessage_(peerId, Message::fromBinary(data));
                }
            }
        });
        
        dc->onError([this, peerId](const std::string& error) {
            if (onError_) {
                onError_(Error{ErrorCode::InternalError, "DataChannel error with " + peerId + ": " + error});
            }
        });
    }
    
    void handleOffer(const SignalingMessage& msg) {
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
    ClientConfig config_;
    std::atomic<ConnectionState> state_;
    std::atomic<bool> running_;
    std::string localId_;
    
    std::shared_ptr<rtc::WebSocket> ws_;
    rtc::Configuration rtcConfig_;
    
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> peerConnections_;
    std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>> dataChannels_;
    mutable std::mutex peerMutex_;
    
    // 回调
    OnConnectedCallback onConnected_;
    OnDisconnectedCallback onDisconnected_;
    OnPeerConnectedCallback onPeerConnected_;
    OnPeerDisconnectedCallback onPeerDisconnected_;
    OnTextMessageCallback onTextMessage_;
    OnBinaryMessageCallback onBinaryMessage_;
    OnMessageCallback onMessage_;
    OnPeerListCallback onPeerList_;
    OnErrorCallback onError_;
    OnStateChangeCallback onStateChange_;
};

// ==================== P2PClient 实现 ====================

P2PClient::P2PClient(const ClientConfig& config)
    : impl_(std::make_unique<P2PClientImpl>(config)) {}

P2PClient::P2PClient(const std::string& signalingUrl) {
    ClientConfig config;
    config.signalingUrl = signalingUrl;
    impl_ = std::make_unique<P2PClientImpl>(config);
}

P2PClient::~P2PClient() = default;

P2PClient::P2PClient(P2PClient&&) noexcept = default;
P2PClient& P2PClient::operator=(P2PClient&&) noexcept = default;

bool P2PClient::connect() { return impl_->connect(); }
std::future<bool> P2PClient::connectAsync() { return impl_->connectAsync(); }
void P2PClient::disconnect() { impl_->disconnect(); }
bool P2PClient::isConnected() const { return impl_->isConnected(); }
ConnectionState P2PClient::getState() const { return impl_->getState(); }
std::string P2PClient::getLocalId() const { return impl_->getLocalId(); }

bool P2PClient::connectToPeer(const std::string& peerId) { return impl_->connectToPeer(peerId); }
std::future<bool> P2PClient::connectToPeerAsync(const std::string& peerId, std::chrono::milliseconds timeout) {
    return impl_->connectToPeerAsync(peerId, timeout);
}
void P2PClient::disconnectFromPeer(const std::string& peerId) { impl_->disconnectFromPeer(peerId); }
void P2PClient::requestPeerList() { impl_->requestPeerList(); }
std::vector<std::string> P2PClient::getConnectedPeers() const { return impl_->getConnectedPeers(); }
bool P2PClient::isPeerConnected(const std::string& peerId) const { return impl_->isPeerConnected(peerId); }
std::optional<PeerInfo> P2PClient::getPeerInfo(const std::string& peerId) const { return impl_->getPeerInfo(peerId); }

bool P2PClient::sendText(const std::string& peerId, const std::string& message) {
    return impl_->sendText(peerId, message);
}
bool P2PClient::sendBinary(const std::string& peerId, const BinaryData& data) {
    return impl_->sendBinary(peerId, data);
}
bool P2PClient::sendBinary(const std::string& peerId, const void* data, size_t size) {
    return impl_->sendBinary(peerId, data, size);
}
bool P2PClient::send(const std::string& peerId, const Message& message) {
    return impl_->send(peerId, message);
}
size_t P2PClient::broadcastText(const std::string& message) { return impl_->broadcastText(message); }
size_t P2PClient::broadcastBinary(const BinaryData& data) { return impl_->broadcastBinary(data); }

void P2PClient::setOnConnected(OnConnectedCallback cb) { impl_->setOnConnected(std::move(cb)); }
void P2PClient::setOnDisconnected(OnDisconnectedCallback cb) { impl_->setOnDisconnected(std::move(cb)); }
void P2PClient::setOnPeerConnected(OnPeerConnectedCallback cb) { impl_->setOnPeerConnected(std::move(cb)); }
void P2PClient::setOnPeerDisconnected(OnPeerDisconnectedCallback cb) { impl_->setOnPeerDisconnected(std::move(cb)); }
void P2PClient::setOnTextMessage(OnTextMessageCallback cb) { impl_->setOnTextMessage(std::move(cb)); }
void P2PClient::setOnBinaryMessage(OnBinaryMessageCallback cb) { impl_->setOnBinaryMessage(std::move(cb)); }
void P2PClient::setOnMessage(OnMessageCallback cb) { impl_->setOnMessage(std::move(cb)); }
void P2PClient::setOnPeerList(OnPeerListCallback cb) { impl_->setOnPeerList(std::move(cb)); }
void P2PClient::setOnError(OnErrorCallback cb) { impl_->setOnError(std::move(cb)); }
void P2PClient::setOnStateChange(OnStateChangeCallback cb) { impl_->setOnStateChange(std::move(cb)); }

void P2PClient::setLogLevel(int level) {
    rtc::LogLevel rtcLevel;
    switch (level) {
        case 0: rtcLevel = rtc::LogLevel::None; break;
        case 1: rtcLevel = rtc::LogLevel::Error; break;
        case 2: rtcLevel = rtc::LogLevel::Warning; break;
        case 3: rtcLevel = rtc::LogLevel::Info; break;
        case 4: rtcLevel = rtc::LogLevel::Debug; break;
        default: rtcLevel = rtc::LogLevel::Verbose; break;
    }
    rtc::InitLogger(rtcLevel);
}

std::string P2PClient::getVersion() {
    return "1.0.0";
}

} // namespace p2p