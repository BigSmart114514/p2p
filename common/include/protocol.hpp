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
    Chat,           // 聊天消息（通过DataChannel）
    
    // 中继相关消息类型
    RelayAuth,      // 中继认证请求
    RelayAuthResult,// 中继认证结果
    RelayConnect,   // 通过中继连接到peer
    RelayData,      // 中继数据
    RelayDisconnect // 断开中继连接
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
        case MessageType::RelayAuth: return "relay_auth";
        case MessageType::RelayAuthResult: return "relay_auth_result";
        case MessageType::RelayConnect: return "relay_connect";
        case MessageType::RelayData: return "relay_data";
        case MessageType::RelayDisconnect: return "relay_disconnect";
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
    if (str == "relay_auth") return MessageType::RelayAuth;
    if (str == "relay_auth_result") return MessageType::RelayAuthResult;
    if (str == "relay_connect") return MessageType::RelayConnect;
    if (str == "relay_data") return MessageType::RelayData;
    if (str == "relay_disconnect") return MessageType::RelayDisconnect;
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

// 中继数据消息结构
struct RelayDataMessage {
    bool isBinary;
    std::string textData;
    std::string binaryBase64;  // Base64编码的二进制数据
    
    nlohmann::json toJson() const {
        nlohmann::json j = {
            {"is_binary", isBinary}
        };
        if (isBinary) {
            j["data"] = binaryBase64;
        } else {
            j["data"] = textData;
        }
        return j;
    }
    
    static RelayDataMessage fromJson(const nlohmann::json& j) {
        RelayDataMessage msg;
        msg.isBinary = j.value("is_binary", false);
        if (msg.isBinary) {
            msg.binaryBase64 = j.value("data", "");
        } else {
            msg.textData = j.value("data", "");
        }
        return msg;
    }
    
    std::string serialize() const {
        return toJson().dump();
    }
    
    static RelayDataMessage deserialize(const std::string& str) {
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

// Base64 编码/解码辅助函数
inline std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = i < data.size() ? data[i++] : 0;
        uint32_t octet_b = i < data.size() ? data[i++] : 0;
        uint32_t octet_c = i < data.size() ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        result += chars[(triple >> 18) & 0x3F];
        result += chars[(triple >> 12) & 0x3F];
        result += chars[(triple >> 6) & 0x3F];
        result += chars[triple & 0x3F];
    }
    
    size_t mod = data.size() % 3;
    if (mod == 1) {
        result[result.size() - 1] = '=';
        result[result.size() - 2] = '=';
    } else if (mod == 2) {
        result[result.size() - 1] = '=';
    }
    
    return result;
}

inline std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static const int decodeTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    
    std::vector<uint8_t> result;
    
    size_t padding = 0;
    if (!encoded.empty() && encoded[encoded.size() - 1] == '=') padding++;
    if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') padding++;
    
    size_t outLen = (encoded.size() / 4) * 3 - padding;
    result.reserve(outLen);
    
    uint32_t val = 0;
    int valb = -8;
    for (char c : encoded) {
        if (c == '=') break;
        int v = decodeTable[static_cast<unsigned char>(c)];
        if (v == -1) continue;
        val = (val << 6) + v;
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return result;
}

} // namespace p2p