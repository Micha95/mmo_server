#pragma once
#include "Config.h"
#include "RedisClient.h"
#include "SocketServer.h"
#include "MySQLClient.h" // Include MySQLClient header
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <csignal>
#include <iostream>
#include <unordered_map>
#include <random>

class BaseServer {
public:
    BaseServer(const std::string& serverType);
    int run(int argc, char** argv);
    static void SignalHandlerStatic(int signal);
    SocketServer* getSocketServer() { return server; }

    // New: TCP session management hooks
    virtual void onClientConnected(intptr_t clientSock) {}
    virtual void onClientDisconnected(intptr_t clientSock);
    virtual void handlePacket(const std::vector<uint8_t>& data, intptr_t clientSock) = 0;

    void sendToClient(const void* packet, size_t size, intptr_t clientSock);

protected:
    bool loadConfig(int argc, char** argv);
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
    std::unordered_map<intptr_t, bool> sessionMap; // Track connected clients

    static std::string GenerateUniqueId();
    static void RegisterServerInRedis(RedisClient& redis, const std::string& key, const std::string& ip, int port, int num_sessions);
};

// Inline static member definition for header-only usage
inline std::atomic<bool> BaseServer::running{true};
