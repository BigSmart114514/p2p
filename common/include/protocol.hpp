// common/include/protocol.hpp
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace p2p {

// 消息类型
enum class MessageType {
    Register,       // 客户端注册
    PeerList,       // 获取在线用户列表
    Offer,          // SDP Offer
    Answer,         // SDP Answer
    Candidate,      // ICE Candidate
    Connect,        // 请求连接到某个peer
    Error,          // 错误消息
    Chat            // 聊天消息（通过DataChannel）
};

// 消息类型转换
inline std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::Register: return "register";
        case MessageType::PeerList: return "peer_list";
        case MessageType::Offer: return "offer";
        case MessageType::Answer: return "answer";
        case MessageType::Candidate: return "candidate";
        case MessageType::Connect: return "connect";
        case MessageType::Error: return "error";
        case MessageType::Chat: return "chat";
        default: return "unknown";
    }
}

inline MessageType stringToMessageType(const std::string& str) {
    if (str == "register") return MessageType::Register;
    if (str == "peer_list") return MessageType::PeerList;
    if (str == "offer") return MessageType::Offer;
    if (str == "answer") return MessageType::Answer;
    if (str == "candidate") return MessageType::Candidate;
    if (str == "connect") return MessageType::Connect;
    if (str == "error") return MessageType::Error;
    if (str == "chat") return MessageType::Chat;
    return MessageType::Error;
}

// 信令消息结构
struct SignalingMessage {
    MessageType type;
    std::string from;
    std::string to;
    std::string payload;
    
    nlohmann::json toJson() const {
        return {
            {"type", messageTypeToString(type)},
            {"from", from},
            {"to", to},
            {"payload", payload}
        };
    }
    
    static SignalingMessage fromJson(const nlohmann::json& j) {
        SignalingMessage msg;
        msg.type = stringToMessageType(j.value("type", "error"));
        msg.from = j.value("from", "");
        msg.to = j.value("to", "");
        msg.payload = j.value("payload", "");
        return msg;
    }
    
    std::string serialize() const {
        return toJson().dump();
    }
    
    static SignalingMessage deserialize(const std::string& str) {
        return fromJson(nlohmann::json::parse(str));
    }
};

// ICE服务器配置
struct IceServerConfig {
    std::vector<std::string> urls;
    std::string username;
    std::string credential;
};

// 默认STUN服务器
inline std::vector<std::string> getDefaultStunServers() {
    return {
        "stun:stun.l.google.com:19302",
        "stun:stun1.l.google.com:19302",
        "stun:stun2.l.google.com:19302"
    };
}

} // namespace p2p