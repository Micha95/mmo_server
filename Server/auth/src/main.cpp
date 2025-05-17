#include "../../common/include/Config.h"
#include "../../common/include/MySQLClient.h"
#include "../../common/include/RedisClient.h"
#include "../../common/include/Packets.h"
#include "../../common/include/SocketServer.h"
#include "../../common/include/BaseServer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <unordered_map>
#include <random>
#include <map>
#include <boost/asio.hpp>

struct SessionInfo {
    int32_t playerId;
    std::string username;
    std::string sessionKey;
    intptr_t clientSock;
};

class AuthServer : public BaseServer {
public:
    AuthServer() : BaseServer("auth") {}

    void handlePacket(const std::vector<uint8_t>& data, intptr_t clientSock) override {
        if (data.size() < sizeof(PacketHeader)) return;
        PacketHeader header;
        std::memcpy(&header, data.data(), sizeof(PacketHeader));
        SessionInfo& session = sessionMap_[clientSock];
        switch (header.packetId) {
            case PACKET_C_LOGIN_REQUEST: {
                C_LoginRequest req;
                if (data.size() < sizeof(C_LoginRequest)) return;
                std::memcpy(&req, data.data(), sizeof(C_LoginRequest));
                std::string username(req.username, req.usernameLength);
                std::string password(req.password, req.passwordLength);
                std::cout << "Received C_LoginRequest from fd=" << clientSock << " user='" << username << "' (len=" << req.usernameLength << ")" << std::endl;
                std::cout << "Username hex: ";
                for (int i = 0; i < req.usernameLength; ++i) std::cout << std::hex << (int)(unsigned char)req.username[i] << " ";
                std::cout << std::dec << std::endl;
                // MySQL connection is now handled by BaseServer
                std::vector<std::vector<std::string>> result;
                std::string query = "SELECT id, password FROM accounts WHERE username='" + username + "'";
                std::cout << "Query: " << query << std::endl;
                bool queryResult = mysql.query(query, result);
                std::cout << "mysql.query returned: " << (queryResult ? "true" : "false") << ", result.size(): " << result.size() << std::endl;
                if (!queryResult) {
                    std::cout << "MySQL query failed. Error: " << mysql.getLastError() << std::endl;
                    PrintMySQLDiagnostics();
                }
                if (!queryResult || result.empty()) {
                    std::cout << "Account not found: " << username << std::endl;
                    S_LoginResponse resp{};
                    resp.header.packetId = PACKET_S_LOGIN_RESPONSE;
                    resp.resultCode = 1;
                    resp.sessionKeyLength = 0;
                    sendToClient(&resp, sizeof(resp), clientSock);
                    break;
                }
                std::string dbPassword = result[0][1];
                int32_t playerId = std::stoi(result[0][0]);
                if (dbPassword != password) {
                    std::cout << "Password mismatch for: " << username << std::endl;
                    S_LoginResponse resp{};
                    resp.header.packetId = PACKET_S_LOGIN_RESPONSE;
                    resp.resultCode = 1;
                    resp.sessionKeyLength = 0;
                    sendToClient(&resp, sizeof(resp), clientSock);
                    break;
                }
                // Success: create session
                std::string sessionKey = GenerateSessionKey();
                std::string redisSessionKey = "session_" + sessionKey;
                std::string redisSessionVal = std::to_string(playerId) + "," + username + ",fd=" + std::to_string(clientSock);
                redis.set(redisSessionKey, redisSessionVal);
                redis.expire(redisSessionKey, 3600);
                session.playerId = playerId;
                session.username = username;
                session.sessionKey = sessionKey;
                S_LoginResponse resp{};
                resp.header.packetId = PACKET_S_LOGIN_RESPONSE;
                resp.resultCode = 0;
                resp.sessionKeyLength = (int8_t)sessionKey.size();
                std::memset(resp.sessionKey, 0, sizeof(resp.sessionKey));
                std::memcpy(resp.sessionKey, sessionKey.data(), sessionKey.size());
                sendToClient(&resp, sizeof(resp), clientSock);
            }
            break;
            case PACKET_C_CHAR_CREATE: {
                C_CharCreate req;
                if (DeserializePacketRaw(data, &req, sizeof(C_CharCreate))) {
                    std::cout << "Received C_CharCreate from " << clientSock << std::endl;
                    // TODO: Handle char create logic
                }
                break;
            }
            case PACKET_C_CHAR_SELECT: {
                C_CharSelect req;
                if (DeserializePacketRaw(data, &req, sizeof(C_CharSelect))) {
                    std::cout << "Received C_CharSelect from " << clientSock << std::endl;
                    // TODO: Handle char select logic
                }
                break;
            }
            default:
                std::cout << "Unknown or unhandled packet: " << header.packetId << " from fd=" << clientSock << std::endl;
                break;
        }
    }

    std::string GenerateSessionKey() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        uint64_t id = dis(gen);
        char buf[32];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)id);
        return std::string(buf);
    }

private:
    std::map<intptr_t, SessionInfo> sessionMap_;
    RedisClient redis;
};

int main(int argc, char** argv) {
    boost::asio::io_context io_context;
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int signal_num){
        std::cout << "\n[INFO] Signal received, shutting down... (signal=" << signal_num << ", ec=" << ec.message() << ")" << std::endl;
        BaseServer::SignalHandlerStatic(signal_num);
        io_context.stop();
    });
    AuthServer server;
    // Start server thread
    std::thread server_thread([&](){
        std::cout << "[DEBUG] server_thread: starting server.run()" << std::endl;
        server.run(argc, argv);
        std::cout << "[DEBUG] server_thread: server.run() returned, thread exiting." << std::endl;
    });
    // Run io_context in main thread
    io_context.run();
    std::cout << "[DEBUG] io_context ended run." << std::endl;
    // After signal, wait for server to finish
    server_thread.join();
    std::cout << "[DEBUG] Server Thread joined." << std::endl;
    // Now stop io_context to ensure all handlers are done
    io_context.stop();
    std::cout << "[DEBUG] main() is returning, process should exit now." << std::endl;
    std::_Exit(0);
    return 0;
}
