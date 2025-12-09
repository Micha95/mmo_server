#pragma once
#include "Config.h"
#include "RedisClient.h"
#include "SocketServer.h"
#include "MySQLClient.h" // Include MySQLClient header
#include "Packets.h"
#include "WebGuiServer.h" // Include WebGuiServer header
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <csignal>
#include <iostream>
#include <unordered_map>
#include <random>
#include <chrono>
#include <memory> // For std::shared_ptr
#include <netinet/in.h> // For sockaddr_in
#include <arpa/inet.h>
#include <sstream>
#include <string>

class Player; // Forward declaration of Player

struct SessionInfo {
    bool connected = true;
    std::chrono::steady_clock::time_point lastHeartbeat;
    int32_t playerId = -1;
    int32_t charId = -1;
    std::string username;
    std::string sessionKey;
    std::shared_ptr<Player> playerEntity; // Pointer to the Player entity for this session
    sockaddr_in clientAddr{}; // Store the original sockaddr_in for robust disconnect/timeout handling
    intptr_t clientSock = 0; // Store the last known clientSock (for UDP, for logging/consistency)
};

class BaseServer {
public:
    BaseServer(const std::string& serverType);
    int run(int argc, char** argv);
    static void SignalHandlerStatic(int signal);
    SocketServer* getSocketServer() { return server; }

    // UDP session management hooks (now use endpoint)
    virtual void onClientConnected(intptr_t clientSock, const sockaddr_in& clientAddr);
    virtual void onClientDisconnected(intptr_t clientSock, const sockaddr_in& clientAddr);
    virtual void handlePacket(const PacketHeader& header, const std::vector<uint8_t>& data, intptr_t clientSock, const sockaddr_in& clientAddr) = 0;

    void sendToClient(const void* packet, size_t size, intptr_t clientSock);

    // Heartbeat constants
    static constexpr int HEARTBEAT_INTERVAL_SEC = 5;
    static constexpr int HEARTBEAT_TIMEOUT_SEC = 12;

    // Provide public accessors for shutdown logic
    std::unordered_map<std::string, SessionInfo>& GetSessionMap() { return sessionMap; }
    MySQLClient& GetMySQL() { return mysql; }
    Config& GetConfig() { return config; } // NEW: Provide public accessor for config
    bool IsWebGuiEnabled() const { return webGuiEnabled; } // NEW: Accessor for web GUI enabled flag

    bool loadConfig(int argc, char** argv);

protected:
    
    bool startSocket();
    void connectRedisWithRetry();
    void connectMySQLWithRetry(); // NEW
    void mainLoop();
    void PrintMySQLDiagnostics(); // NEW: MySQL diagnostics

    static std::atomic<bool> running;
    std::string serverType;
    SocketPacketHandler packetHandler;
    Config config;
    std::string configPath;
    SocketServer* server = nullptr;
    RedisClient redis;
    MySQLClient mysql; // NEW: shared MySQL client for all servers
    std::string uniqueId;
    std::string redisKey;
    int num_sessions;
    bool mysqlConnected = false; // NEW: shared connection state
    bool webGuiEnabled = false; // NEW: web GUI enabled flag
    std::unordered_map<std::string, SessionInfo> sessionMap; // Track connected clients by endpoint (IP:port)
    std::unique_ptr<WebGuiServer> webGuiServer; // NEW: web GUI server instance

    static std::string GenerateUniqueId();
    static void RegisterServerInRedis(RedisClient& redis, const std::string& key, const std::string& ip, int port, int num_sessions);

    // Heartbeat helpers
    void handleHeartbeatPacket(const std::vector<uint8_t>& data, intptr_t clientSock, const sockaddr_in& clientAddr);
    void checkHeartbeatTimeouts();

    virtual ~BaseServer();
};

// Inline static member definition for header-only usage
inline std::atomic<bool> BaseServer::running{true};

inline std::string EndpointToString(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
    std::ostringstream oss;
    oss << ip << ":" << ntohs(addr.sin_port);
    return oss.str();
}
