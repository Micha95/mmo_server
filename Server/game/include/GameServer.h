#pragma once
#include "../../common/include/BaseServer.h"
#include "MobManager.h"
#include "ZoneManager.h"
#include <netinet/in.h>
#include <vector>
#include <cstdint>
#include <string>

class GameServer : public BaseServer {
public:
    GameServer();
    MobManager mobManager;
    ZoneManager zoneManager;
    void handlePacket(const PacketHeader& header, const std::vector<uint8_t>& data, intptr_t clientSock, const sockaddr_in& clientAddr);
    bool loadConfig(int argc, char** argv) override;
    void onClientDisconnected(intptr_t clientSock, const sockaddr_in& clientAddr);
};
