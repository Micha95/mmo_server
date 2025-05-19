#include "BaseServer.h"
#include "Packets.h"
#include "Log.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <csignal>
#include <sstream> // NEW: Include sstream for std::ostringstream
#include <nlohmann/json.hpp>
#include <cstring> // NEW: Include cstring for std::memset

using nlohmann::json;

BaseServer* g_lastBaseServerInstance = nullptr; // ADD: Define global pointer

BaseServer::BaseServer(const std::string& serverType)
    : serverType(serverType), num_sessions(0) {
    g_lastBaseServerInstance = this; // SET: Update global pointer in constructor
}

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
        LOG_ERROR("Failed to connect to MySQL! Retrying in 5 seconds...");
        for (int i = 0; i < 50 && running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int BaseServer::run(int argc, char** argv) {
    uniqueId = GenerateUniqueId();
    redisKey = serverType + "_" + uniqueId;
    // Generate timestamp string: YYYYMMDD_HH_MM
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%Y%m%d_%H_%M", &tm);
    SetLogFileName(serverType + "_" + uniqueId + "_" + timebuf + ".log");
    if (!loadConfig(argc, argv)) return 1;
    if (!startSocket()) return 1;
    connectRedisWithRetry();
    connectMySQLWithRetry(); // connect to MySQL on startup
    PrintMySQLDiagnostics(); // Print MySQL diagnostics
    // Register connection/disconnection handlers
    server->setConnectionHandler(
        [this](intptr_t clientSock) {
            SessionInfo info;
            info.connected = true;
            info.lastHeartbeat = std::chrono::steady_clock::now();
            sessionMap[clientSock] = info;
            onClientConnected(clientSock);
            num_sessions = (int)sessionMap.size();
            LOG_INFO(std::string("Client connected: fd=") + std::to_string(clientSock));
        },
        [this](intptr_t clientSock) {
            onClientDisconnected(clientSock);
        }
    );
    // Always set the packet handler to decrypt/decompress before dispatch
    server->setPacketHandler([this](const std::vector<uint8_t>& d, intptr_t clientSock) {
        // No connection logic here, only packet handling
        // Log incoming encrypted/compressed packet
        {
            std::ostringstream oss;
            oss << "Incoming (encrypted/compressed) packet from fd=" << clientSock << ", size=" << d.size() << ": ";
            for (auto b : d) oss << std::hex << (int)b << " ";
            LOG_DEBUG(oss.str());
        }
        // Decrypt and decompress before passing to handler
        std::vector<uint8_t> decompressed;
        if (!Compression::decompress(d, decompressed)) {
            LOG_ERROR(std::string("Failed to decompress packet from fd=") + std::to_string(clientSock) + ". Dropping packet.");
            return;
        }
        {
            std::ostringstream oss;
            oss << "Decompressed packet from fd=" << clientSock << ", size=" << decompressed.size() << ": ";
            for (auto b : decompressed) oss << std::hex << (int)b << " ";
            LOG_DEBUG(oss.str());
        }
        std::vector<uint8_t> plain;
        if (!Crypto::decrypt(decompressed, plain, PACKET_CRYPTO_KEY)) return;
        {
            std::ostringstream oss;
            oss << "Decrypted packet from fd=" << clientSock << ", size=" << plain.size() << ": ";
            for (auto b : plain) oss << std::hex << (int)b << " ";
            LOG_DEBUG(oss.str());
        }
        
        // Defensive: check minimum size before handlePacket
        if (plain.size() < sizeof(PacketHeader)) {
            LOG_ERROR(std::string("Packet too small from fd=") + std::to_string(clientSock) + ". Dropping packet.");
            return;
        }
        //Unpack the packet header
        PacketHeader header;
        std::memcpy(&header, plain.data(), sizeof(PacketHeader));
        const char* packetName = PacketTypeToString(header.packetId);
        LOG_DEBUG("Received packet from fd=" + std::to_string(clientSock) + ", packetId=" + std::to_string(header.packetId) + " (" + packetName + "), size=" + std::to_string(plain.size()));
        // Check for heartbeat packet
        if (header.packetId == PACKET_C_HEARTBEAT) {
            handleHeartbeatPacket(plain, clientSock);
            return;
        }
        handlePacket(header,plain, clientSock);
    });
    mainLoop();
    LOG_DEBUG("mainLoop() is returning.");
    PrintMySQLDiagnostics();
    return 0;
}

void BaseServer::SignalHandlerStatic(int signal) {
    running = false;
}

bool BaseServer::loadConfig(int argc, char** argv) {
    configPath = getConfigPathFromArgs(argc, argv, "../common/" +serverType + ".cfg");
    if (!config.load(configPath)) {
        LOG_ERROR(std::string("Failed to load config: ") + configPath);
        return false;
    }
    // Set log level from config if present
    std::string logLevelStr = config.get("log_level");
    if (!logLevelStr.empty()) {
        SetLogLevel(LogLevelFromString(logLevelStr));
        LOG_INFO("Log level set from config: " + logLevelStr);
    } else {
        SetLogLevel(LogLevel::DEBUG);
        LOG_INFO("Log level defaulted to DEBUG");
    }

    // Set log file options from config if present
    bool LOG_TO_FILE = config.getInt("log_to_file") > 0;
    if (LOG_TO_FILE) {
       
        std::string logFileLevel = config.get("log_file_level");
        if (!logFileLevel.empty()) {
            SetFileLogLevel(LogLevelFromString(logFileLevel));
            LOG_INFO("Log file level set from config: " + logFileLevel);
        } else {
            SetFileLogLevel(LogLevel::DEBUG);
            LOG_INFO("Log file level defaulted to DEBUG");
        }
        std::string logFileDir = config.get("log_file_dir");
        if (logFileDir.empty()) {
            logFileDir = "./logs";
            LOG_INFO("Log file directory defaulted to: " + logFileDir);
        }
        SetLogToFile(true);
        SetLogFileDir(logFileDir);
        
        LOG_INFO("Logging to file: " + logFileDir + "/" + GetLogFileName());
    } else {
        SetLogToFile(false);
        LOG_INFO("Logging to console only.");
    }
    LOG_INFO("[" + serverType + "] Starting with config: " + configPath);
    return true;
}

bool BaseServer::startSocket() {
    server = CreateSocketServer();
    server->setPacketHandler(packetHandler);
    std::string ip = config.get("bind_ip");
    int port = config.getInt("bind_port");
    {
        std::ostringstream oss;
        oss << "Attempting to bind to IP: '" << ip << "' Port: " << port << ". IP bytes: ";
        for (char c : ip) oss << std::hex << (int)(unsigned char)c << " ";
        LOG_DEBUG(oss.str());
    }
    if (!server->start(ip.c_str(), port)) {
        LOG_ERROR("Failed to start TCP server! (bind_ip='" + ip + "', bind_port=" + std::to_string(port) + ")");
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
        LOG_ERROR("Failed to connect to Redis! Retrying in 5 seconds...");
        for (int i = 0; i < 50 && running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void BaseServer::handleHeartbeatPacket(const std::vector<uint8_t>& data, intptr_t clientSock) {
    if (data.size() < sizeof(C_Heartbeat)) return;
    const C_Heartbeat* pkt = reinterpret_cast<const C_Heartbeat*>(data.data());
    // Update last heartbeat time in sessionMap
    auto it = sessionMap.find(clientSock);
    if (it != sessionMap.end()) {
        if(!it->second.sessionKey.empty()) {
           LOG_DEBUG("Received heartbeat from client fd=" + std::to_string(clientSock) + ", sessionKey=" + it->second.sessionKey);
           redis.expire("session_" + it->second.sessionKey, HEARTBEAT_TIMEOUT_SEC + 1); // Update Redis session expiration
           it->second.lastHeartbeat = std::chrono::steady_clock::now();
            // Respond with S_Heartbeat
            S_Heartbeat resp;
            std::memset(&resp, 0, sizeof(resp));
            resp.header.packetId = PACKET_S_HEARTBEAT;
            resp.timestamp = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            sendToClient(&resp, sizeof(resp), clientSock);
        }
    }
    
}

void BaseServer::checkHeartbeatTimeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<intptr_t> toDisconnect;
    for (const auto& kv : sessionMap) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - kv.second.lastHeartbeat).count();
        if (elapsed > HEARTBEAT_TIMEOUT_SEC) {
            toDisconnect.push_back(kv.first);
        }
    }
    for (intptr_t sock : toDisconnect) {
        LOG_ERROR("Heartbeat timeout for client fd=" + std::to_string(sock) + ". Disconnecting.");
        if (server) server->disconnect(sock); // Close the socket
    }
}

void BaseServer::mainLoop() {
    int port = config.getInt("bind_port");
    std::string ip = config.get("bind_ip");
    int tick = 0;
    RegisterServerInRedis(redis, redisKey, ip, port, num_sessions);
    LOG_INFO(serverType + " server running on " + ip + ":" + std::to_string(port) + ".\nUse system signals to shut down.");
    const int tickRate = 30; // 30 ticks per second
    const std::chrono::milliseconds tickDuration(1000 / tickRate);
    auto lastRedisUpdate = std::chrono::steady_clock::now();
    while (running) {
        auto tickStart = std::chrono::steady_clock::now();
        // ...game/server logic per tick could go here...
        tick++;
        
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
        }
        
        num_sessions = (int)sessionMap.size();
        checkHeartbeatTimeouts();
        // Update Redis every 15 seconds
        if (std::chrono::steady_clock::now() - lastRedisUpdate >= std::chrono::seconds(15)) {
            RegisterServerInRedis(redis, redisKey, ip, port, num_sessions);
            lastRedisUpdate = std::chrono::steady_clock::now();
        }


        auto tickEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tickEnd - tickStart);
        if (elapsed < tickDuration) {
            std::this_thread::sleep_for(tickDuration - elapsed);
        }
    }
    server->stop();
    delete server;
    LOG_DEBUG("mainLoop() is returning.");
}

std::string BaseServer::GenerateUniqueId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    uint64_t id = dis(gen);
    return std::to_string(id);
}

void BaseServer::RegisterServerInRedis(RedisClient& redis, const std::string& key, const std::string& ip, int port, int num_sessions) {
    extern BaseServer* g_lastBaseServerInstance;
    std::string mapName = "";
    int maxPlayers = 100;
    if (g_lastBaseServerInstance) {
        mapName = g_lastBaseServerInstance->config.get("map", "");
        maxPlayers = g_lastBaseServerInstance->config.getInt("max_players", 0);
    }
    std::vector<std::pair<std::string, std::string>> fields = {
        {"ip", ip},
        {"port", std::to_string(port)},
        {"map", mapName},
        {"max_players", std::to_string(maxPlayers)},
        {"current_players", std::to_string(num_sessions)}
    };
    redis.hset(key, fields);
    redis.expire(key, 30);
}

void BaseServer::sendToClient(const void* packet, size_t size, intptr_t clientSock) {
    std::vector<uint8_t> out = SerializePacketRaw(packet, size);
    {
        std::ostringstream oss;
        int16_t packetId = -1;
        const char* packetName = "UNKNOWN_PACKET";
        if (size >= sizeof(PacketHeader)) {
            packetId = reinterpret_cast<const PacketHeader*>(packet)->packetId;
            packetName = PacketTypeToString(packetId);
        }
        oss << "Sending packet to fd=" << clientSock << ", size=" << out.size() << ", packetId=" << packetId << " (" << packetName << "): ";
        for (auto b : out) oss << std::hex << (int)b << " ";
        LOG_DEBUG(oss.str());
    }
    server->send(out, clientSock);
}

void BaseServer::onClientConnected(intptr_t clientSock) {
    // Send S_Heartbeat immediately on connection
    S_Heartbeat resp;
    std::memset(&resp, 0, sizeof(resp));
    resp.header.packetId = PACKET_S_HEARTBEAT;
    resp.timestamp = static_cast<int32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    sendToClient(&resp, sizeof(resp), clientSock);
}

void BaseServer::onClientDisconnected(intptr_t clientSock) {
    LOG_DEBUG("Client disconnected: fd=" + std::to_string(clientSock));
    sessionMap.erase(clientSock);
    num_sessions = (int)sessionMap.size();
}

void BaseServer::PrintMySQLDiagnostics() {
    std::vector<std::vector<std::string>> dbResult;
    if (mysql.query("SELECT DATABASE();", dbResult) && !dbResult.empty()) {
        LOG_DEBUG("[MySQL] Connected to database: " + dbResult[0][0]);
    } else {
        LOG_ERROR("[MySQL] Could not determine current database (server connection).");
    }
    std::vector<std::vector<std::string>> tablesResult;
    if (mysql.query("SHOW TABLES;", tablesResult)) {
        LOG_DEBUG("[MySQL] Tables (server connection):");
        for (const auto& row : tablesResult) {
            if (!row.empty()){
                LOG_DEBUG("[MySQL] Table: " + row[0]);
            }
        }
        std::cout << std::endl;
    } else {
        LOG_ERROR("[MySQL] SHOW TABLES failed (server connection).");
    }
}
