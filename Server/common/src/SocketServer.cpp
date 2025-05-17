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

// High-performance TCP server (cross-platform, non-blocking, multithreaded, failsafe, using Boost.Asio)
class SocketServerImpl : public SocketServer {
public:
    SocketServerImpl() : ioContext(), acceptor(ioContext), running(false) {}
    ~SocketServerImpl() { stop(); }
    bool start(const char* ip, int port) override {
        try {
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
            acceptor.open(endpoint.protocol());
            acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen();
            running = true;
            doAccept();
            threads.emplace_back([this]() { ioContext.run(); });
            return true;
        } catch (const std::exception& e) {
            std::cerr << "SocketServer start error: " << e.what() << std::endl;
            return false;
        }
    }
    void stop() override {
        running = false;
        ioContext.stop();
        std::cout << "[DEBUG] Stopping SocketServerImpl, joining threads..." << std::endl;
        for (auto& t : threads) if (t.joinable()) t.join();
        std::cout << "[DEBUG] All SocketServerImpl threads joined." << std::endl;
        threads.clear();
    }
    void send(const std::vector<uint8_t>& data, intptr_t clientSock) override {
        auto it = clients.find(clientSock);
        if (it != clients.end()) {
            boost::asio::async_write(*it->second, boost::asio::buffer(data),
                [](const boost::system::error_code&, std::size_t){});
        }
    }
    void setPacketHandler(SocketPacketHandler handler) override {
        this->handler = handler;
    }
private:
    void doAccept() {
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioContext);
        acceptor.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
            if (!ec && running) {
                intptr_t sockId = reinterpret_cast<intptr_t>(socket.get());
                clients[sockId] = socket;
                doRead(socket, sockId);
            }
            if (running) doAccept();
        });
    }
    void doRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket, intptr_t sockId) {
        auto buf = std::make_shared<std::vector<uint8_t>>(2048);
        socket->async_read_some(boost::asio::buffer(*buf),
            [this, socket, buf, sockId](const boost::system::error_code& ec, std::size_t n) {
                if (!ec && n > 0 && handler) {
                    buf->resize(n);
                    handler(*buf, sockId);
                    doRead(socket, sockId);
                } else {
                    clients.erase(sockId);
                }
            });
    }
    boost::asio::io_context ioContext;
    boost::asio::ip::tcp::acceptor acceptor;
    std::atomic<bool> running;
    std::vector<std::thread> threads;
    std::map<intptr_t, std::shared_ptr<boost::asio::ip::tcp::socket>> clients;
    SocketPacketHandler handler;
};

SocketServer* CreateSocketServer() { return new SocketServerImpl(); }
