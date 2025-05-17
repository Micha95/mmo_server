#pragma once
#include <vector>
#include <cstdint>
#include <functional>

using SocketPacketHandler = std::function<void(const std::vector<uint8_t>&, intptr_t)>;

class SocketServer {
public:
    virtual ~SocketServer() = default;
    virtual bool start(const char* ip, int port) = 0;
    virtual void stop() = 0;
    virtual void send(const std::vector<uint8_t>& data, intptr_t clientSock) = 0;
    virtual void setPacketHandler(SocketPacketHandler handler) = 0;
};

SocketServer* CreateSocketServer();
