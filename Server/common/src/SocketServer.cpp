#include "../include/BaseServer.h"
#include "SocketServer.h"
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <cstring>
#include <boost/asio.hpp>
#include <iostream>
#include <map>
#include <unordered_map>
#include "../include/Log.h"

extern BaseServer* g_lastBaseServerInstance;

// High-performance UDP server (cross-platform, non-blocking, multithreaded, failsafe, using Boost.Asio)
class SocketServerImpl : public SocketServer {
public:
    SocketServerImpl() : ioContext(), socket(ioContext), running(false) {}
    ~SocketServerImpl() { stop(); }
    bool start(const char* ip, int port) override {
        try {
            boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
            socket.open(endpoint.protocol());
            socket.bind(endpoint);
            LOG_DEBUG("UDP socket successfully bound to " + std::string(ip) + ":" + std::to_string(port));
            running = true;
            doRead();
            threads.emplace_back([this]() { ioContext.run(); });
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("SocketServer start error: " + std::string(e.what()));
            return false;
        }
    }
    void stop() override {
        running = false;
        ioContext.stop();
        LOG_DEBUG("Stopping SocketServerImpl..., joining threads...");
        for (auto& t : threads) if (t.joinable()) t.join();
        LOG_DEBUG(" ALL SocketServerImpl threads joined.");
        threads.clear();
    }
    void send(const std::vector<uint8_t>& data, intptr_t clientSock) override {
        // reinterpret clientSock as endpoint pointer
        auto remote_endpoint = reinterpret_cast<boost::asio::ip::udp::endpoint*>(clientSock);
        if (remote_endpoint) {
            // Prepend 4-byte length prefix to the payload for UDP framing
            uint32_t packetLen = data.size();
            uint32_t netPacketLen = htonl(packetLen);
            std::vector<uint8_t> sendBuf(4 + packetLen);
            std::memcpy(sendBuf.data(), &netPacketLen, 4);
            std::memcpy(sendBuf.data() + 4, data.data(), packetLen);
            socket.async_send_to(boost::asio::buffer(sendBuf), *remote_endpoint,
                [](const boost::system::error_code&, std::size_t){});
        }
    }
    void setPacketHandler(SocketPacketHandler handler) override {
        this->handler = handler;
    }
    void setConnectionHandler(SocketConnectionHandler onConnect, SocketConnectionHandler onDisconnect) override {
        this->onConnect = onConnect;
        this->onDisconnect = onDisconnect;
    }
    void disconnect(intptr_t /*clientSock*/) override {
        // UDP is connectionless; nothing to do
    }
private:
    void doRead() {
        auto buf = std::make_shared<std::vector<uint8_t>>(2048);
        auto remote_endpoint = std::make_shared<boost::asio::ip::udp::endpoint>();
        socket.async_receive_from(boost::asio::buffer(*buf), *remote_endpoint,
            [this, buf, remote_endpoint](const boost::system::error_code& ec, std::size_t n) {
                LOG_DEBUG_EXT("UDP receive lambda called: ec=" + std::to_string(ec.value()) + ", n=" + std::to_string(n));
                if (!ec && n > 0) {
                    // Emulate TCP framing: expect [4-byte length][payload]
                    if (n < 4) {
                        LOG_ERROR("Received UDP packet too small for length prefix");
                    } else {
                        uint32_t packetLen = 0;
                        std::memcpy(&packetLen, buf->data(), 4);
                        packetLen = ntohl(packetLen);
                        if (n < 4 + packetLen) {
                            LOG_ERROR("Received UDP packet smaller than stated length");
                        } else {
                            std::vector<uint8_t> payload(buf->begin() + 4, buf->begin() + 4 + packetLen);
                            // Convert boost::asio::ip::udp::endpoint to sockaddr_in
                            sockaddr_in clientAddr{};
                            clientAddr.sin_family = AF_INET;
                            clientAddr.sin_port = htons(remote_endpoint->port());
                            clientAddr.sin_addr.s_addr = htonl(remote_endpoint->address().to_v4().to_ulong());
                            if (handler) handler(payload, reinterpret_cast<intptr_t>(remote_endpoint.get()), clientAddr);
                        }
                    }
                }
                doRead();
            });
    }
    boost::asio::io_context ioContext;
    boost::asio::ip::udp::socket socket;
    std::atomic<bool> running;
    std::vector<std::thread> threads;
    SocketPacketHandler handler;
};

SocketServer* CreateSocketServer() { return new SocketServerImpl(); }
