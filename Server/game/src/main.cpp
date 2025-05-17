#include "../../common/include/Config.h"
#include "../../common/include/MySQLClient.h"
#include "../../common/include/RedisClient.h"
#include "../../common/include/Packets.h"
#include "../../common/include/SocketServer.h"
#include "../../common/include/BaseServer.h"
#include <iostream>
#include <unordered_map>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>

class GameServer : public BaseServer {
  public:
    GameServer() : BaseServer("game") {}

    void handlePacket(const std::vector<uint8_t>& data, intptr_t clientSock) override {
        if (data.size() < sizeof(PacketHeader)) return;
        PacketHeader header;
        std::memcpy(&header, data.data(), sizeof(PacketHeader));
        switch (header.packetId) {
            case PACKET_C_MOVE: {
                C_Move req;
                if (data.size() < sizeof(C_Move)) return;
                std::memcpy(&req, data.data(), sizeof(C_Move));
                std::cout << "Received C_Move from socket " << clientSock << std::endl;
                // TODO: Handle movement logic
                break;
            }
            case PACKET_C_COMBAT_ACTION: {
                C_CombatAction req;
                if (data.size() < sizeof(C_CombatAction)) return;
                std::memcpy(&req, data.data(), sizeof(C_CombatAction));
                std::cout << "Received C_CombatAction from socket " << clientSock << std::endl;
                // TODO: Handle combat logic
                break;
            }
            case PACKET_C_SHOP_BUY: {
                C_ShopBuy req;
                if (data.size() < sizeof(C_ShopBuy)) return;
                std::memcpy(&req, data.data(), sizeof(C_ShopBuy));
                std::cout << "Received C_ShopBuy from socket " << clientSock << std::endl;
                // TODO: Handle shop buy logic
                break;
            }
            default:
                std::cout << "Unknown or unhandled packet: " << header.packetId << " from socket " << clientSock << std::endl;
                break;
        }
    }
};

int main(int argc, char** argv) {
    GameServer server;
    return server.run(argc, argv);
}
