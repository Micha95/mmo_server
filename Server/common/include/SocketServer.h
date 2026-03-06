#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include <netinet/in.h>

using SocketPacketHandler = std::function<void(const std::vector<uint8_t>&, intptr_t, const sockaddr_in&)>;
using SocketConnectionHandler = std::function<void(intptr_t, const sockaddr_in&)>;

class SocketServer {
public:
    virtual ~SocketServer() = default;
    virtual bool start(const char* ip, int port) = 0;
    virtual void stop() = 0;
    virtual void send(const std::vector<uint8_t>& data, intptr_t clientSock) = 0;
    virtual void setPacketHandler(SocketPacketHandler handler) = 0;
    // New: connection event handlers
    virtual void setConnectionHandler(SocketConnectionHandler onConnect, SocketConnectionHandler onDisconnect) = 0;
    // Add disconnect method
    virtual void disconnect(intptr_t clientSock, const std::string& reason, int16_t reasonCode = 0) = 0;

    SocketConnectionHandler onConnect;
    SocketConnectionHandler onDisconnect;
};

SocketServer* CreateSocketServer();
