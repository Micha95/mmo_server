#include "../../common/include/Config.h"
#include "../../common/include/MySQLClient.h"
#include "../../common/include/RedisClient.h"
#include "../../common/include/Packets.h"
#include "../../common/include/SocketServer.h"
#include "../../common/include/BaseServer.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

std::atomic<bool> running{true};

void SignalHandler(int signal) {
    running = false;
}

class ChatServer : public BaseServer {
public:
    ChatServer() : BaseServer("chat") {}

    void handlePacket(const std::vector<uint8_t>& data, intptr_t clientSock) override {
        if (data.size() < sizeof(PacketHeader)) return;
        PacketHeader header;
        std::memcpy(&header, data.data(), sizeof(PacketHeader));
        switch (header.packetId) {
            case PACKET_C_CHAT_MESSAGE: {
                C_ChatMessage req;
                if (data.size() < sizeof(C_ChatMessage)) return;
                std::memcpy(&req, data.data(), sizeof(C_ChatMessage));
                std::cout << "Received C_ChatMessage from socket " << clientSock << std::endl;
                // TODO: Handle chat message logic
                break;
            }
            default:
                std::cout << "Unknown or unhandled packet: " << header.packetId << " from socket " << clientSock << std::endl;
                break;
        }
    }
};

int main(int argc, char** argv) {
    ChatServer server;
    return server.run(argc, argv);
}
