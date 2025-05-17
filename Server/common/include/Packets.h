#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include "Security.h"

// Packet type enum for unique packet IDs
enum PacketType : int16_t {
    // Common Packets (0+)
    PACKET_C_HEARTBEAT = 0,
    PACKET_S_HEARTBEAT = 1,
    PACKET_S_ERROR = 2,

    // Login/Auth/Char Select Packets (1000+)
    PACKET_C_LOGIN_REQUEST = 1000,
    PACKET_S_LOGIN_RESPONSE = 1001,
    PACKET_C_CHAR_CREATE = 1010,
    PACKET_S_CHAR_CREATE_RESULT = 1011,
    PACKET_C_CHAR_SELECT = 1020,
    PACKET_S_CHAR_SELECT_RESULT = 1021,

    // Game Packets (2000+)
    PACKET_C_MOVE = 2000,
    PACKET_S_MOVE = 2001,
    PACKET_C_COMBAT_ACTION = 2010,
    PACKET_S_COMBAT_RESULT = 2011,
    PACKET_S_NPC_SPAWN = 2020,
    PACKET_S_ITEM_LIST = 2030,
    PACKET_C_SHOP_BUY = 2040,
    PACKET_S_SHOP_BUY_RESULT = 2041,

    // Chat Packets (5000+)
    PACKET_C_CHAT_MESSAGE = 5000,
    PACKET_S_CHAT_MESSAGE = 5001
};

// Packet header for all packets
struct PacketHeader {
    PacketType packetId; // Unique ID for each packet type
};

#pragma pack(push, 1)
// All packets must be structs, no functions, no unsigned types
// Example packet: Login request from client
struct C_LoginRequest {
    static constexpr PacketType PACKET_ID = PACKET_C_LOGIN_REQUEST;
    PacketHeader header{PACKET_ID};
    int32_t usernameLength;
    int32_t passwordLength;
    char username[64];
    char password[64];
};

// Example packet: Login response from server
struct S_LoginResponse {
    static constexpr PacketType PACKET_ID = PACKET_S_LOGIN_RESPONSE;
    PacketHeader header{PACKET_ID};
    int8_t resultCode; // 0 = OK, 1 = fail
    int8_t sessionKeyLength;
    char sessionKey[64];
};

// Auth Packets
struct C_CharCreate {
    static constexpr PacketType PACKET_ID = PACKET_C_CHAR_CREATE;
    PacketHeader header{PACKET_ID};
    int8_t nameLength;
    char name[32];
    int16_t classId;
};

struct S_CharCreateResult {
    static constexpr PacketType PACKET_ID = PACKET_S_CHAR_CREATE_RESULT;
    PacketHeader header{PACKET_ID};
    int8_t resultCode; // 0 = OK, 1 = fail
    int32_t charId;
};

struct C_CharSelect {
    static constexpr PacketType PACKET_ID = PACKET_C_CHAR_SELECT;
    PacketHeader header{PACKET_ID};
    int32_t charId;
};

struct S_CharSelectResult {
    static constexpr PacketType PACKET_ID = PACKET_S_CHAR_SELECT_RESULT;
    PacketHeader header{PACKET_ID};
    int8_t resultCode; // 0 = OK, 1 = fail
    char gameServerAddress[64];
    int32_t gameServerPort;
};

// Game Packets
struct C_Move {
    static constexpr PacketType PACKET_ID = PACKET_C_MOVE;
    PacketHeader header{PACKET_ID};
    float x;
    float y;
    float z;
};

struct S_Move {
    static constexpr PacketType PACKET_ID = PACKET_S_MOVE;
    PacketHeader header{PACKET_ID};
    int32_t charId;
    float x;
    float y;
    float z;
};

struct C_CombatAction {
    static constexpr PacketType PACKET_ID = PACKET_C_COMBAT_ACTION;
    PacketHeader header{PACKET_ID};
    int32_t targetId;
    int16_t skillId;
};

struct S_CombatResult {
    static constexpr PacketType PACKET_ID = PACKET_S_COMBAT_RESULT;
    PacketHeader header{PACKET_ID};
    int32_t attackerId;
    int32_t targetId;
    int16_t damage;
    int8_t resultType; // 0 = hit, 1 = miss, 2 = crit
};

struct S_NPCSpawn {
    static constexpr PacketType PACKET_ID = PACKET_S_NPC_SPAWN;
    PacketHeader header{PACKET_ID};
    int32_t npcId;
    int32_t x;
    int32_t y;
    int32_t z;
};

struct S_ItemList {
    static constexpr PacketType PACKET_ID = PACKET_S_ITEM_LIST;
    PacketHeader header{PACKET_ID};
    int16_t itemCount;
    int32_t itemIds[64];
    int16_t itemCounts[64];
};

struct C_ShopBuy {
    static constexpr PacketType PACKET_ID = PACKET_C_SHOP_BUY;
    PacketHeader header{PACKET_ID};
    int32_t shopId;
    int32_t itemId;
    int16_t count;
};

struct S_ShopBuyResult {
    static constexpr PacketType PACKET_ID = PACKET_S_SHOP_BUY_RESULT;
    PacketHeader header{PACKET_ID};
    int8_t resultCode;
    int32_t itemId;
    int16_t count;
};

// Chat Packets
struct C_ChatMessage {
    static constexpr PacketType PACKET_ID = PACKET_C_CHAT_MESSAGE;
    PacketHeader header{PACKET_ID};
    int8_t type; // 0=private,1=guild,2=party,3=local,4=channel,5=world
    int32_t targetId; // for private/guild/party
    int16_t messageLength;
    char message[256];
};

struct S_ChatMessage {
    static constexpr PacketType PACKET_ID = PACKET_S_CHAT_MESSAGE;
    PacketHeader header{PACKET_ID};
    int8_t type;
    int32_t fromId;
    int32_t toId;
    int16_t messageLength;
    char message[256];
};

// Heartbeat/Connection
struct C_Heartbeat {
    static constexpr PacketType PACKET_ID = PACKET_C_HEARTBEAT;
    PacketHeader header{PACKET_ID};
    int32_t timestamp;
};

struct S_Heartbeat {
    static constexpr PacketType PACKET_ID = PACKET_S_HEARTBEAT;
    PacketHeader header{PACKET_ID};
    int32_t timestamp;
};

struct S_Error {
    static constexpr PacketType PACKET_ID = PACKET_S_ERROR;
    PacketHeader header{PACKET_ID};
    int16_t errorCode;
    char errorMsg[128];
};
#pragma pack(pop)

// Static key for all packet encryption (32 bytes for AES-256)
static const std::vector<uint8_t> PACKET_CRYPTO_KEY = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00
};

// Serialization helpers
inline std::vector<uint8_t> SerializePacketRaw(const void* packet, size_t size) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(packet);
    std::vector<uint8_t> out(ptr, ptr + size);
    // Always compress
    std::vector<uint8_t> compressed;
    if (Compression::compress(out, compressed)) {
        out = std::move(compressed);
    }
    // Always encrypt
    std::vector<uint8_t> encrypted;
    if (Crypto::encrypt(out, encrypted, PACKET_CRYPTO_KEY)) {
        out = std::move(encrypted);
    }
    return out;
}

inline bool DeserializePacketRaw(const std::vector<uint8_t>& data, void* out, size_t size) {
    if (data.size() < size) return false;
    std::memcpy(out, data.data(), size);
    return true;
}

// Static asserts for struct size validation
static_assert(sizeof(PacketHeader) == 2, "PacketHeader size mismatch!");
static_assert(sizeof(C_LoginRequest) == 138, "C_LoginRequest size mismatch!");
