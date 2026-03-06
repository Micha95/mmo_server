

// JSON serialization support
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <nlohmann/json.hpp>

// Cross-server packet type enum
enum CrossServerPacketType : int16_t {
    CROSSSERVER_SYSTEM_MESSAGE = 1,
    CROSSSERVER_PLAYER_KICK = 2,
    // Add more types as needed
};

// Helper to get packet type name as string
inline const char* CrossServerPacketTypeToString(int16_t packetId) {
    switch (packetId) {
        case CROSSSERVER_SYSTEM_MESSAGE: return "CROSSSERVER_SYSTEM_MESSAGE";
        case CROSSSERVER_PLAYER_KICK: return "CROSSSERVER_PLAYER_KICK";
        default: return "UNKNOWN_PACKET";
    }
}

// Packet header for cross-server packets
struct CrossServerPacketHeader {
    CrossServerPacketType packetId;
};

// Cross-server packet definitions
struct S_CrossServerSystemMessage {
    CrossServerPacketHeader header{CROSSSERVER_SYSTEM_MESSAGE};
    std::string payload; // Use std::string for JSON compatibility
};

struct S_CrossServerPlayerKick {
    CrossServerPacketHeader header{CROSSSERVER_PLAYER_KICK};
    int32_t playerId;
};


// nlohmann/json serialization macros (non-intrusive)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CrossServerPacketHeader, packetId)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(S_CrossServerSystemMessage, header, payload)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(S_CrossServerPlayerKick, header, playerId)