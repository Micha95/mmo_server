#include "../include/GameServer.h"
#include <boost/asio.hpp>
#include <thread>
#include <iostream>
#include <csignal>
#include "../include/Log.h"
#include "../include/Player.h"

int main(int argc, char** argv) {
    boost::asio::io_context io_context;
    GameServer server; // Move this above signals.async_wait so it is in scope
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int signal_num){
        LOG_INFO("Signal received, shutting down... (signal=" + std::to_string(signal_num) + ", ec=" + ec.message() + ")");

        // Save all players to DB
        for (auto& kv : server.GetSessionMap()) {
            auto& session = kv.second;
            if (session.playerEntity) {
                session.playerEntity->SaveToDB(server.GetMySQL());
            }
        }

        BaseServer::SignalHandlerStatic(signal_num);
        io_context.stop();
    });
    // Start server thread
    std::thread server_thread([&](){
        LOG_DEBUG("server_thread: starting server.run()");
        server.run(argc, argv);
        LOG_DEBUG("server_thread: server.run() returned, thread exiting.");
    });
    // Run io_context in main thread
    io_context.run();
    LOG_DEBUG("io_context ended run.");
    // After signal, wait for server to finish
    server_thread.join();
    LOG_DEBUG("Server Thread joined.");
    // Now stop io_context to ensure all handlers are done
    io_context.stop();
    LOG_DEBUG("main() is returning, process should exit now.");
    std::_Exit(0);
    return 0;
}
