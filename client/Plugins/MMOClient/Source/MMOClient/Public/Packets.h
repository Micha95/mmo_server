#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

// Packet type enum for unique packet IDs
enum PacketType : int16_t {
    PACKET_C_HEARTBEAT = 0,
    PACKET_S_HEARTBEAT = 1,
    PACKET_S_ERROR = 2,
    PACKET_C_LOGIN_REQUEST = 1000,
    PACKET_S_LOGIN_RESPONSE = 1001,
    PACKET_C_CHAR_CREATE = 1010,
    PACKET_S_CHAR_CREATE_RESULT = 1011,
    PACKET_C_CHAR_SELECT = 1020,
    PACKET_S_CHAR_SELECT_RESULT = 1021,
    PACKET_C_MOVE = 2000,
    PACKET_S_MOVE = 2001,
    PACKET_C_COMBAT_ACTION = 2010,
    PACKET_S_COMBAT_RESULT = 2011,
    PACKET_S_NPC_SPAWN = 2020,
    PACKET_S_ITEM_LIST = 2030,
    PACKET_C_SHOP_BUY = 2040,
    PACKET_S_SHOP_BUY_RESULT = 2041,
    PACKET_C_CHAT_MESSAGE = 5000,
    PACKET_S_CHAT_MESSAGE = 5001
};

struct PacketHeader {
    PacketType packetId;
};
#pragma pack(push, 1) 
struct C_LoginRequest {
    PacketHeader header{PACKET_C_LOGIN_REQUEST};
    int32_t usernameLength;
    int32_t passwordLength;
    char username[64];
    char password[64];
};
struct S_LoginResponse {
    PacketHeader header{PACKET_S_LOGIN_RESPONSE};
    int8_t resultCode;
    int8_t sessionKeyLength;
    char sessionKey[64];
};
struct C_CharCreate {
    PacketHeader header{PACKET_C_CHAR_CREATE};
    int8_t nameLength;
    char name[32];
    int16_t classId;
};
struct S_CharCreateResult {
    PacketHeader header{PACKET_S_CHAR_CREATE_RESULT};
    int8_t resultCode;
    int32_t charId;
};
struct C_CharSelect {
    PacketHeader header{PACKET_C_CHAR_SELECT};
    int32_t charId;
};
struct S_CharSelectResult {
    PacketHeader header{PACKET_S_CHAR_SELECT_RESULT};
    int8_t resultCode;
    char gameServerAddress[64];
    int32_t gameServerPort;
};
struct C_Move {
    PacketHeader header{PACKET_C_MOVE};
    float x, y, z;
};
struct S_Move {
    PacketHeader header{PACKET_S_MOVE};
    int32_t charId;
    float x, y, z;
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
    int8_t resultType;
};
struct S_NPCSpawn {
    PacketHeader header{PACKET_S_NPC_SPAWN};
    int32_t npcId;
    int32_t x, y, z;
};
struct S_ItemList {
    PacketHeader header{PACKET_S_ITEM_LIST};
    int16_t itemCount;
    int32_t itemIds[64];
    int16_t itemCounts[64];
};
struct C_ShopBuy {
    PacketHeader header{PACKET_C_SHOP_BUY};
    int32_t shopId;
    int32_t itemId;
    int16_t count;
};
struct S_ShopBuyResult {
    PacketHeader header{PACKET_S_SHOP_BUY_RESULT};
    int8_t resultCode;
    int32_t itemId;
    int16_t count;
};
struct C_ChatMessage {
    PacketHeader header{PACKET_C_CHAT_MESSAGE};
    int8_t type;
    int32_t targetId;
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
#pragma pack(pop)
static const std::vector<uint8_t> PACKET_CRYPTO_KEY = {
    0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE,
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00
};
static_assert(sizeof(PacketHeader) == 2, "PacketHeader size mismatch!");
static_assert(sizeof(C_LoginRequest) == 138, "C_LoginRequest size mismatch!");