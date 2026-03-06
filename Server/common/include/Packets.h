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
    PACKET_C_CONNECT_REQUEST = 3,
    PACKET_S_CONNECT_RESULT = 4,
    PACKET_S_DISCONNECT = 5, // Server-initiated disconnect notice (e.g. for server shutdown or kick)

    // Login/Auth/Char Select Packets (1000+)
    PACKET_C_LOGIN_REQUEST = 1000,
    PACKET_S_LOGIN_RESPONSE = 1001,
    PACKET_C_CHAR_CREATE = 1010,
    PACKET_S_CHAR_CREATE_RESULT = 1011,
    PACKET_C_CHAR_SELECT = 1020,
    PACKET_S_CHAR_SELECT_RESULT = 1021,
    PACKET_C_CHAR_DELETE = 1030,
    PACKET_S_CHAR_DELETE_RESULT = 1031,
    PACKET_C_CHAR_LIST_REQUEST = 1040,
    PACKET_S_CHAR_LIST_RESULT = 1041,

    // Game Packets (2000+)
    PACKET_C_MOVE = 2000,
    PACKET_S_MOVE = 2001,
    PACKET_C_COMBAT_ACTION = 2010,
    PACKET_S_COMBAT_RESULT = 2011,
    PACKET_S_ITEM_LIST = 2030,
    PACKET_C_SHOP_BUY = 2040,
    PACKET_S_SHOP_BUY_RESULT = 2041,
    // Entity Spawn/Despawn Packets (2100+)
    PACKET_S_PLAYER_SPAWN = 2102,
    PACKET_S_PLAYER_DESPAWN = 2103,
    PACKET_S_MOB_SPAWN = 2104,
    PACKET_S_MOB_DESPAWN = 2105,
    PACKET_S_NPC_SPAWN = 2106,
    PACKET_S_NPC_DESPAWN = 2107,
    PACKET_S_ITEM_SPAWN = 2108,
    PACKET_S_ITEM_DESPAWN = 2109,



    // Chat Packets (5000+)
    PACKET_C_CHAT_MESSAGE = 5000,
    PACKET_S_CHAT_MESSAGE = 5001,

    
};

// Helper to get packet type name as string
inline const char* PacketTypeToString(int16_t packetId) {
    switch (packetId) {
        case PACKET_C_HEARTBEAT: return "PACKET_C_HEARTBEAT";
        case PACKET_S_HEARTBEAT: return "PACKET_S_HEARTBEAT";
        case PACKET_S_ERROR: return "PACKET_S_ERROR";
        case PACKET_C_LOGIN_REQUEST: return "PACKET_C_LOGIN_REQUEST";
        case PACKET_S_LOGIN_RESPONSE: return "PACKET_S_LOGIN_RESPONSE";
        case PACKET_C_CHAR_CREATE: return "PACKET_C_CHAR_CREATE";
        case PACKET_S_CHAR_CREATE_RESULT: return "PACKET_S_CHAR_CREATE_RESULT";
        case PACKET_C_CHAR_SELECT: return "PACKET_C_CHAR_SELECT";
        case PACKET_S_CHAR_SELECT_RESULT: return "PACKET_S_CHAR_SELECT_RESULT";
        case PACKET_C_CHAR_DELETE: return "PACKET_C_CHAR_DELETE";
        case PACKET_S_CHAR_DELETE_RESULT: return "PACKET_S_CHAR_DELETE_RESULT";
        case PACKET_C_CHAR_LIST_REQUEST: return "PACKET_C_CHAR_LIST_REQUEST";
        case PACKET_S_CHAR_LIST_RESULT: return "PACKET_S_CHAR_LIST_RESULT";
        case PACKET_C_MOVE: return "PACKET_C_MOVE";
        case PACKET_S_MOVE: return "PACKET_S_MOVE";
        case PACKET_C_COMBAT_ACTION: return "PACKET_C_COMBAT_ACTION";
        case PACKET_S_COMBAT_RESULT: return "PACKET_S_COMBAT_RESULT";
        case PACKET_S_ITEM_LIST: return "PACKET_S_ITEM_LIST";
        case PACKET_C_SHOP_BUY: return "PACKET_C_SHOP_BUY";
        case PACKET_S_SHOP_BUY_RESULT: return "PACKET_S_SHOP_BUY_RESULT";
        case PACKET_C_CHAT_MESSAGE: return "PACKET_C_CHAT_MESSAGE";
        case PACKET_S_CHAT_MESSAGE: return "PACKET_S_CHAT_MESSAGE";
        case PACKET_C_CONNECT_REQUEST: return "PACKET_C_CONNECT_REQUEST";
        case PACKET_S_CONNECT_RESULT: return "PACKET_S_CONNECT_RESULT";
        case PACKET_S_PLAYER_SPAWN: return "PACKET_S_PLAYER_SPAWN";
        case PACKET_S_PLAYER_DESPAWN: return "PACKET_S_PLAYER_DESPAWN";
        case PACKET_S_MOB_SPAWN: return "PACKET_S_MOB_SPAWN";
        case PACKET_S_MOB_DESPAWN: return "PACKET_S_MOB_DESPAWN";
        case PACKET_S_NPC_SPAWN: return "PACKET_S_NPC_SPAWN";
        case PACKET_S_NPC_DESPAWN: return "PACKET_S_NPC_DESPAWN";
        case PACKET_S_ITEM_SPAWN: return "PACKET_S_ITEM_SPAWN";
        case PACKET_S_ITEM_DESPAWN: return "PACKET_S_ITEM_DESPAWN";
        case PACKET_S_DISCONNECT: return "PACKET_S_DISCONNECT";
        default: return "UNKNOWN_PACKET";
    }
}



// Packet header for all packets
struct PacketHeader {
    PacketType packetId; // Unique ID for each packet type
};

#pragma pack(push, 1)
// All packets must be structs, no functions, no unsigned types

struct C_ConnectRequest {
    PacketHeader header{PACKET_C_CONNECT_REQUEST};
    char sessionKey[64]; // Session key for authentication
};

struct S_ConnectResult {
    PacketHeader header{PACKET_S_CONNECT_RESULT};
    int8_t resultCode; // 0 = OK, 1 = fail
};


// Example packet: Login request from client
struct C_LoginRequest {
    PacketHeader header{PACKET_C_LOGIN_REQUEST};
    int32_t usernameLength;
    int32_t passwordLength;
    char username[64];
    char password[64];
};

// Example packet: Login response from server
struct S_LoginResponse {
    PacketHeader header{PACKET_S_LOGIN_RESPONSE};
    int8_t resultCode; // 0 = OK, 1 = fail
    int8_t sessionKeyLength;
    char sessionKey[64];
};

// Auth Packets
struct C_CharCreate {
    PacketHeader header{PACKET_C_CHAR_CREATE};
    int8_t nameLength;
    char name[32];
    int16_t classId;
};

struct S_CharCreateResult {
    PacketHeader header{PACKET_S_CHAR_CREATE_RESULT};
    int8_t resultCode; // 0 = OK, 1 = fail
    int32_t charId;
};

struct C_CharSelect {
    PacketHeader header{PACKET_C_CHAR_SELECT};
    int32_t charId;
};

struct S_CharSelectResult {
    PacketHeader header{PACKET_S_CHAR_SELECT_RESULT};
    int8_t resultCode; // 0 = OK, 1 = fail
    char gameServerAddress[64];
    int32_t gameServerPort;
};

struct C_CharDelete {
    PacketHeader header{PACKET_C_CHAR_DELETE};
    int32_t charId;
};

struct S_CharDeleteResult {
    PacketHeader header{PACKET_S_CHAR_DELETE_RESULT};
    int8_t resultCode; // 0 = OK, 1 = fail
    int32_t charId;
};

// Game Packets
struct C_Move {
    PacketHeader header{PACKET_C_MOVE};
    float x;
    float y;
    float z;
};

struct S_Move {
    PacketHeader header{PACKET_S_MOVE};
    int64_t entityId; // Was charId, now supports all entity types
    int8_t entityType; // ENetworkedEntityType (Player, Mob, NPC, Item)
    int32_t shardId; // Add shardId for MMO sharding support
    float x;
    float y;
    float z;
    float yaw; // Add yaw for rotation (optional, but common in MMO movement)
};

struct C_CombatAction {
    PacketHeader header{PACKET_C_COMBAT_ACTION};
    int32_t targetId;
    int16_t skillId;
};

struct S_CombatResult {
    PacketHeader header{PACKET_S_COMBAT_RESULT};
    int32_t attackerId;
    int32_t targetId;
    int16_t damage;
    int8_t resultType; // 0 = hit, 1 = miss, 2 = crit
};

// --- Entity Spawn/Despawn Packets ---
struct S_PlayerSpawn {
    PacketHeader header{PACKET_S_PLAYER_SPAWN};
    int64_t entityId;
    int32_t shardId;
    float x, y, z;
    float yaw;
    char name[32];
    int16_t classId;
    int32_t level;
};
struct S_PlayerDespawn {
    PacketHeader header{PACKET_S_PLAYER_DESPAWN};
    int64_t entityId;
};
struct S_MobSpawn {
    PacketHeader header{PACKET_S_MOB_SPAWN};
    int64_t entityId;
    int32_t shardId;
    float x, y, z;
    float yaw;
    int32_t mobTypeId;
    int32_t level;
};
struct S_MobDespawn {
    PacketHeader header{PACKET_S_MOB_DESPAWN};
    int64_t entityId;
};
struct S_NPCSpawn {
    PacketHeader header{PACKET_S_NPC_SPAWN};
    int64_t entityId;
    int32_t shardId;
    float x, y, z;
    float yaw;
    int32_t npcTypeId;
};
struct S_NPCDespawn {
    PacketHeader header{PACKET_S_NPC_DESPAWN};
    int64_t entityId;
};
struct S_ItemSpawn {
    PacketHeader header{PACKET_S_ITEM_SPAWN};
    int64_t entityId;
    int32_t shardId;
    float x, y, z;
    int32_t itemId;
    int32_t count;
};
struct S_ItemDespawn {
    PacketHeader header{PACKET_S_ITEM_DESPAWN};
    int64_t entityId;
};

// Chat Packets
struct C_ChatMessage {
    PacketHeader header{PACKET_C_CHAT_MESSAGE};
    int8_t type; // 0=private,1=guild,2=party,3=local,4=channel,5=world
    int32_t targetId; // for private/guild/party
    int16_t messageLength;
    char message[256];
};

struct S_ChatMessage {
    PacketHeader header{PACKET_S_CHAT_MESSAGE};
    int8_t type;
    int32_t fromId;
    int32_t toId;
    int16_t messageLength;
    char message[256];
};

// Character List Packets
struct C_CharListRequest {
    PacketHeader header{PACKET_C_CHAR_LIST_REQUEST};
    // No additional fields needed
};

// Character info for list
struct CharListEntry {
    int32_t charId;
    char name[32];
    int16_t classId;
    // Add more fields as needed (level, etc)
};

struct S_CharListResult {
    PacketHeader header{PACKET_S_CHAR_LIST_RESULT};
    int8_t charCount;
    CharListEntry entries[];
};

// Heartbeat/Connection
struct C_Heartbeat {
    PacketHeader header{PACKET_C_HEARTBEAT};
    int32_t timestamp;
};

struct S_Heartbeat {
    PacketHeader header{PACKET_S_HEARTBEAT};
    int32_t timestamp;
};

struct S_Error {
    PacketHeader header{PACKET_S_ERROR};
    int16_t errorCode;
    char errorMsg[128];
};

struct S_Disconnect {
    PacketHeader header{PACKET_S_DISCONNECT};
    int16_t reasonCode; // 0 = server shutdown, 1 = kick, etc , for now allways 0
    char message[128]; // Optional message to client
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
     // Always encrypt
    std::vector<uint8_t> encrypted;
    if (Crypto::encrypt(out, encrypted, PACKET_CRYPTO_KEY)) {
        out = std::move(encrypted);
    }
    // Always compress
    std::vector<uint8_t> compressed;
    if (Compression::compress(out, compressed)) {
        out = std::move(compressed);
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
static_assert(sizeof(S_Disconnect) == 132, "S_Disconnect size mismatch!");