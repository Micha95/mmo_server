#include "BaseServer.h"
#include "Packets.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <csignal>

BaseServer::BaseServer(const std::string& serverType)
    : serverType(serverType), num_sessions(0) {}

void BaseServer::connectMySQLWithRetry() {
    std::string mysql_host = config.get("mysql_host");
    int mysql_port = config.getInt("mysql_port");
    std::string mysql_user = config.get("mysql_user");
    std::string mysql_password = config.get("mysql_password");
    std::string mysql_db = config.get("mysql_db");
    while (running) {
        if (mysql.connect(mysql_host, mysql_port, mysql_user, mysql_password, mysql_db)) {
            mysqlConnected = true;
            return;
        }
        std::cerr << "Failed to connect to MySQL! Retrying in 5 seconds..." << std::endl;
        for (int i = 0; i < 50 && running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int BaseServer::run(int argc, char** argv) {
   // std::signal(SIGINT, SignalHandlerStatic);
   // std::signal(SIGTERM, SignalHandlerStatic);
    if (!loadConfig(argc, argv)) return 1;
    if (!startSocket()) return 1;
    connectRedisWithRetry();
    connectMySQLWithRetry(); // NEW: connect to MySQL on startup
    uniqueId = GenerateUniqueId();
    redisKey = serverType + "_" + uniqueId;
    // Always set the packet handler to decrypt/decompress before dispatch
    server->setPacketHandler([this](const std::vector<uint8_t>& d, intptr_t clientSock) {
        if (sessionMap.find(clientSock) == sessionMap.end()) {
            sessionMap[clientSock] = true;
            onClientConnected(clientSock);
            num_sessions = (int)sessionMap.size();
            std::cout << "[INFO] Client connected: fd=" << clientSock << std::endl;
        }
        // Log incoming encrypted/compressed packet
        std::cout << "[DEBUG] Incoming (encrypted/compressed) packet from fd=" << clientSock << ", size=" << d.size() << ": ";
        for (auto b : d) std::cout << std::hex << (int)b << " ";
        std::cout << std::dec << std::endl;
        // Decrypt and decompress before passing to handler
        std::vector<uint8_t> plain;
        if (!Crypto::decrypt(d, plain, PACKET_CRYPTO_KEY)) return;
        std::cout << "[DEBUG] Decrypted packet from fd=" << clientSock << ", size=" << plain.size() << ": ";
        for (auto b : plain) std::cout << std::hex << (int)b << " ";
        std::cout << std::dec << std::endl;
        std::vector<uint8_t> decompressed;
        if (!Compression::decompress(plain, decompressed)) {
            std::cerr << "[ERROR] Failed to decompress packet from fd=" << clientSock << ". Dropping packet." << std::endl;
            return;
        }
        std::cout << "[DEBUG] Decompressed packet from fd=" << clientSock << ", size=" << decompressed.size() << ": ";
        for (auto b : decompressed) std::cout << std::hex << (int)b << " ";
        std::cout << std::dec << std::endl;
        // Defensive: check minimum size before handlePacket
        if (decompressed.size() < sizeof(PacketHeader)) {
            std::cerr << "[ERROR] Decompressed packet too small from fd=" << clientSock << ". Dropping packet." << std::endl;
            return;
        }
        handlePacket(decompressed, clientSock);
    });
    mainLoop();
    std::cout << "[DEBUG] run() is returning." << std::endl;
    PrintMySQLDiagnostics();
    return 0;
}

void BaseServer::SignalHandlerStatic(int signal) {
    running = false;
}

bool BaseServer::loadConfig(int argc, char** argv) {
    configPath = getConfigPathFromArgs(argc, argv);
    if (!config.load(configPath)) {
        std::cerr << "Failed to load config: " << configPath << std::endl;
        return false;
    }
    std::cout << "[" << serverType << "] Starting with config: " << configPath << std::endl;
    return true;
}

bool BaseServer::startSocket() {
    server = CreateSocketServer();
    server->setPacketHandler(packetHandler);
    std::string ip = config.get("bind_ip");
    int port = config.getInt("bind_port");
    std::cout << "[DEBUG] Attempting to bind to IP: '" << ip << "' Port: " << port << std::endl;
    for (char c : ip) std::cout << std::hex << (int)(unsigned char)c << " ";
    std::cout << std::dec << std::endl;
    if (!server->start(ip.c_str(), port)) {
        std::cerr << "Failed to start TCP server! (bind_ip='" << ip << "', bind_port=" << port << ")" << std::endl;
        return false;
    }
    return true;
}

void BaseServer::connectRedisWithRetry() {
    std::string redis_host = config.get("redis_host");
    int redis_port = config.getInt("redis_port");
    std::string redis_user = config.get("redis_user");
    std::string redis_password = config.get("redis_password");
    while (running) {
        if (redis.connect(redis_host, redis_port, redis_user, redis_password)) {
            return;
        }
        std::cerr << "Failed to connect to Redis! Retrying in 5 seconds..." << std::endl;
        for (int i = 0; i < 50 && running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void BaseServer::mainLoop() {
    int port = config.getInt("bind_port");
    std::string ip = config.get("bind_ip");
    int tick = 0;
    RegisterServerInRedis(redis, redisKey, ip, port, num_sessions);
    std::cout << "TCP server running. Use system signals to shut down." << std::endl;
    const int tickRate = 30; // 30 ticks per second
    const std::chrono::milliseconds tickDuration(1000 / tickRate);
    auto lastRedisUpdate = std::chrono::steady_clock::now();
    while (running) {
        auto tickStart = std::chrono::steady_clock::now();
        // ...game/server logic per tick could go here...
        tick++;
        // Update Redis every 15 seconds
        if (std::chrono::steady_clock::now() - lastRedisUpdate >= std::chrono::seconds(15)) {
            RegisterServerInRedis(redis, redisKey, ip, port, num_sessions);
            lastRedisUpdate = std::chrono::steady_clock::now();
        }
        // Remove disconnected clients
        std::vector<intptr_t> disconnected;
        for (auto it = sessionMap.begin(); it != sessionMap.end(); ) {
            // If the client socket is no longer valid, mark for removal (TODO: improve detection)
            // For now, rely on SocketServerImpl to erase on disconnect
            ++it;
        }
        for (intptr_t sock : disconnected) {
            sessionMap.erase(sock);
            onClientDisconnected(sock);
            num_sessions = (int)sessionMap.size();
        }
        auto tickEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tickEnd - tickStart);
        if (elapsed < tickDuration) {
            std::this_thread::sleep_for(tickDuration - elapsed);
        }
    }
    server->stop();
    delete server;
    std::cout << "[DEBUG] mainLoop() is returning." << std::endl;
}

std::string BaseServer::GenerateUniqueId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    uint64_t id = dis(gen);
    return std::to_string(id);
}

void BaseServer::RegisterServerInRedis(RedisClient& redis, const std::string& key, const std::string& ip, int port, int num_sessions) {
    std::string value = ip + "," + std::to_string(port) + "," + std::to_string(num_sessions);
    redis.set(key, value);
    redis.expire(key, 30);
}

void BaseServer::sendToClient(const void* packet, size_t size, intptr_t clientSock) {
    std::vector<uint8_t> out = SerializePacketRaw(packet, size);
    std::cout << "[DEBUG] Sending packet to fd=" << clientSock << ", size=" << out.size() << ": ";
    for (auto b : out) std::cout << std::hex << (int)b << " ";
    std::cout << std::dec << std::endl;
    server->send(out, clientSock);
}

void BaseServer::onClientDisconnected(intptr_t clientSock) {
    sessionMap.erase(clientSock);
    num_sessions = (int)sessionMap.size();
}

void BaseServer::PrintMySQLDiagnostics() {
    std::vector<std::vector<std::string>> dbResult;
    if (mysql.query("SELECT DATABASE();", dbResult) && !dbResult.empty()) {
        std::cout << "[MySQL] Connected to database: " << dbResult[0][0] << std::endl;
    } else {
        std::cout << "[MySQL] Could not determine current database (server connection)." << std::endl;
    }
    std::vector<std::vector<std::string>> tablesResult;
    if (mysql.query("SHOW TABLES;", tablesResult)) {
        std::cout << "[MySQL] Tables (server connection):";
        for (const auto& row : tablesResult) {
            if (!row.empty()) std::cout << " " << row[0];
        }
        std::cout << std::endl;
    } else {
        std::cout << "[MySQL] SHOW TABLES failed (server connection)." << std::endl;
    }
}
