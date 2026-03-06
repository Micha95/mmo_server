#include "../../common/include/Config.h"
#include "../../common/include/MySQLClient.h"
#include "../../common/include/RedisClient.h"
#include "../../common/include/Packets.h"
#include "../../common/include/SocketServer.h"
#include "../../common/include/BaseServer.h"
#include "../../common/include/Log.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <unordered_map>
#include <random>
#include <map>
#include <boost/asio.hpp>
#include <string>
#include <sstream>

class AuthServer : public BaseServer {
public:

    AuthServer() : BaseServer("auth") {}

    void handlePacket(const PacketHeader& header, const std::vector<uint8_t>& data, intptr_t clientSock, const sockaddr_in& clientAddr) override {
        std::string endpointKey = EndpointToString(clientAddr);
        auto& session = sessionMap[endpointKey];
        session.clientSock = clientSock; // Update clientSock on packet receipt

        // Revert: Do not attempt session recovery by sessionKey in packet. Only use endpoint for session tracking.

        //CHECK FOR LOGIN
        if(session.playerId == -1 && header.packetId != PACKET_C_LOGIN_REQUEST && header.packetId != PACKET_C_CONNECT_REQUEST) {
            // If not logged in, only allow login or connect requests
            LOG_DEBUG("Session not logged in, dropping packet.");
            return;
        }

        switch (header.packetId) {
            case PACKET_C_LOGIN_REQUEST: {
                C_LoginRequest req;
                if (data.size() < sizeof(C_LoginRequest)) return;
                std::memcpy(&req, data.data(), sizeof(C_LoginRequest));
                std::string username(req.username, req.usernameLength);
                // Remove null bytes and whitespace
                username.erase(std::find(username.begin(), username.end(), '\0'), username.end());
                username.erase(0, username.find_first_not_of(" \t\r\n"));
                username.erase(username.find_last_not_of(" \t\r\n") + 1);
                std::string password(req.password, req.passwordLength);
                LOG_DEBUG("Received C_LoginRequest from fd=" + std::to_string(clientSock) + " user='" + username + "' (len=" + std::to_string(req.usernameLength) + ")");
                {
                    std::ostringstream oss;
                    oss << "Username hex: ";
                    for (int i = 0; i < req.usernameLength; ++i) oss << std::hex << (int)(unsigned char)req.username[i] << " ";
                    LOG_DEBUG(oss.str());
                }
                // Check if user is already logged in (by username)
                bool alreadyLoggedIn = false;
                for (const auto& [key, sess] : sessionMap) {
                    if (sess.username == username && sess.playerId != -1 && sess.connected) {
                        alreadyLoggedIn = true;
                        break;
                    }
                }
                if (alreadyLoggedIn) {
                    LOG_DEBUG("Login failed: user is already logged in: " + username);
                    S_LoginResponse resp{};
                    resp.header.packetId = PACKET_S_LOGIN_RESPONSE;
                    resp.resultCode = 2; // Custom code: already logged in
                    resp.sessionKeyLength = 0;
                    sendToClient(&resp, sizeof(resp), clientSock);
                    // Publish kick message to other servers
                    S_CrossServerPlayerKick kickMsg{};
                    kickMsg.header.packetId = CROSSSERVER_PLAYER_KICK;
                    // Query playerId from DB to get correct id
                    std::string query = "SELECT id FROM accounts WHERE username='" + username + "'";
                    std::vector<std::vector<std::string>> result;
                    if (mysql.query(query, result) && !result.empty()) {
                        kickMsg.playerId = std::stoi(result[0][0]);
                        publishMessage("system", &kickMsg, sizeof(kickMsg));
                        LOG_DEBUG("Published kick message for playerId=" + std::to_string(kickMsg.playerId));
                    } else {
                        LOG_DEBUG("Could not publish kick: playerId not found for username=" + username);
                    }
                    break;
                }
                std::string query = "SELECT id, password FROM accounts WHERE username='" + username + "'";
                LOG_DEBUG("Query: " + query);
                std::vector<std::vector<std::string>> result;
                bool queryResult = mysql.query(query, result);
                LOG_DEBUG(std::string("mysql.query returned: ") + (queryResult ? "true" : "false") + ", result.size(): " + std::to_string(result.size()));
                if (!queryResult) {
                    LOG_DEBUG(std::string("MySQL query failed. Error: ") + mysql.getLastError());
                    PrintMySQLDiagnostics();
                }
                if (!queryResult || result.empty()) {
                    LOG_DEBUG("Account not found: " + username);
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
                    LOG_DEBUG("Password mismatch for: " + username);
                    S_LoginResponse resp{};
                    resp.header.packetId = PACKET_S_LOGIN_RESPONSE;
                    resp.resultCode = 1;
                    resp.sessionKeyLength = 0;
                    sendToClient(&resp, sizeof(resp), clientSock);
                    break;
                }
                // Success: use the sessionKey generated in onClientConnected
                std::string sessionKey = session.sessionKey;
                std::string redisSessionKey = "session_" + sessionKey;
                std::vector<std::pair<std::string, std::string>> sessionFields = {
                    {"playerId", std::to_string(playerId)},
                    {"username", username},
                    {"charId", std::to_string(session.charId)},
                    {"sessionKey", sessionKey},
                    {"fd", std::to_string(clientSock)}
                };
                redis.hset(redisSessionKey, sessionFields);
                redis.expire(redisSessionKey, HEARTBEAT_TIMEOUT_SEC + 2); // 1 hour expiration
                session.playerId = playerId;
                session.username = username;
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
                    LOG_DEBUG("Received C_CharCreate from fd=" + std::to_string(clientSock));
                    std::string charName(req.name, req.nameLength);
                    // Remove null bytes and whitespace
                    charName.erase(std::find(charName.begin(), charName.end(), '\0'), charName.end());
                    charName.erase(0, charName.find_first_not_of(" \t\r\n"));
                    charName.erase(charName.find_last_not_of(" \t\r\n") + 1);
                    int16_t classId = req.classId;
                    LOG_DEBUG("CharCreate request: name='" + charName + "', classId=" + std::to_string(classId) + ", playerId=" + std::to_string(session.playerId));

                    // 1. Validate name (length, allowed chars)
                    if (charName.empty() || charName.length() > 31) {
                        LOG_DEBUG("CharCreate failed: invalid name length");
                        S_CharCreateResult resp{};
                        resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                        resp.resultCode = 1; // fail
                        resp.charId = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // 2. Check for duplicate name for this account
                    std::string checkQuery = "SELECT id FROM characters WHERE name='" + charName + "'";
                    std::ostringstream oss;
                    oss << "Username hex: ";
                    for (int i = 0; i < req.nameLength; ++i) oss << std::hex << (int)(unsigned char)req.name[i] << " ";
                    LOG_DEBUG(oss.str());
                    LOG_DEBUG("CheckQuery: " + checkQuery);
                    std::vector<std::vector<std::string>> checkResult;
                    bool checkOk = mysql.query(checkQuery, checkResult);
                    if (!checkOk) {
                        LOG_DEBUG("[MYSQL] error during char name check: " + mysql.getLastError());
                        S_CharCreateResult resp{};
                        resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                        resp.resultCode = 1; // fail
                        resp.charId = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    if (!checkResult.empty()) {
                        LOG_DEBUG("[MYSQL] CharCreate failed: duplicate name");
                        S_CharCreateResult resp{};
                        resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                        resp.resultCode = 1; // fail
                        resp.charId = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // 3. Insert new character
                    std::vector<std::string> columns = {"account_id", "name", "class_id"};
                    std::vector<std::string> values = {std::to_string(session.playerId), charName, std::to_string(classId)};
                    bool insertOk = mysql.insert("characters", columns, values);
                    if (!insertOk) {
                        LOG_DEBUG("[MYSQL] error during char insert: " + mysql.getLastError());
                        S_CharCreateResult resp{};
                        resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                        resp.resultCode = 1; // fail
                        resp.charId = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // 4. Get new character ID
                    std::string idQuery = "SELECT id FROM characters WHERE name='" + charName + "' AND account_id=" + std::to_string(session.playerId);
                    std::vector<std::vector<std::string>> idResult;
                    int32_t charId = 0;
                    if (mysql.query(idQuery, idResult) && !idResult.empty()) {
                        charId = std::stoi(idResult[0][0]);
                    } else {
                        LOG_DEBUG("[MYSQL] error: could not retrieve new charId after insert");
                        S_CharCreateResult resp{};
                        resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                        resp.resultCode = 1; // fail
                        resp.charId = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // 5. Success response
                    LOG_DEBUG("CharCreate success: name='" + charName + "', charId=" + std::to_string(charId));
                    S_CharCreateResult resp{};
                    resp.header.packetId = PACKET_S_CHAR_CREATE_RESULT;
                    resp.resultCode = 0; // success
                    resp.charId = charId;
                    sendToClient(&resp, sizeof(resp), clientSock);
                }
                break;
            }
            case PACKET_C_CHAR_SELECT: {
                C_CharSelect req;
                if (DeserializePacketRaw(data, &req, sizeof(C_CharSelect))) {
                    LOG_DEBUG("Received C_CharSelect from " + std::to_string(clientSock));
                    int32_t charId = req.charId;
                    // Check if character belongs to this account
                    std::string query = "SELECT id, name FROM characters WHERE id=" + std::to_string(charId) + " AND account_id=" + std::to_string(session.playerId);
                    std::vector<std::vector<std::string>> result;
                    bool ok = mysql.query(query, result);
                    S_CharSelectResult resp{};
                    resp.header.packetId = PACKET_S_CHAR_SELECT_RESULT;
                    if (!ok) {
                        LOG_DEBUG("[MYSQL] error during char select: " + mysql.getLastError());
                        resp.resultCode = 1; // fail
                        std::memset(resp.gameServerAddress, 0, sizeof(resp.gameServerAddress));
                        resp.gameServerPort = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    if (result.empty()) {
                        LOG_DEBUG("CharSelect failed: character does not belong to account or does not exist");
                        resp.resultCode = 1; // fail
                        std::memset(resp.gameServerAddress, 0, sizeof(resp.gameServerAddress));
                        resp.gameServerPort = 0;
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // Success: update the session and assign a game server (dynamic via Redis)
                    {
                        std::vector<std::string> gameServerKeys;
                        if (redis.keys("game_*", gameServerKeys)) {
                            std::string selectedGameIp = "";
                            int selectedGamePort = 0;
                            bool found = false;
                            for (const auto& key : gameServerKeys) {
                                std::map<std::string, std::string> fields;
                                if (!redis.hgetall(key, fields)) continue;
                                std::string ip = fields["ip"];
                                LOG_DEBUG("Game server found: " + ip + ":" + fields["port"] + ", max_players=" + fields["max_players"] + ", current_players=" + fields["current_players"]);

                                int port = fields.count("port") ? std::stoi(fields["port"]) : 0;
                               

                                int maxPlayers = fields.count("max_players") ? std::stoi(fields["max_players"]) : 0;
                                int currentPlayers = fields.count("current_players") ? std::stoi(fields["current_players"]) : 0;
                                if (maxPlayers == 0 || currentPlayers < maxPlayers) {
                                    selectedGameIp = ip;
                                    selectedGamePort = port;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                LOG_ERROR("No available game server found for char select!");
                                resp.resultCode = 2; // Custom code: no available server
                                std::memset(resp.gameServerAddress, 0, sizeof(resp.gameServerAddress));
                                resp.gameServerPort = 0;
                                sendToClient(&resp, sizeof(resp), clientSock);
                                break;
                            }
                            session.charId = charId;
                            session.charName = result[0][1];
                            std::string redisSessionKey = "session_" + session.sessionKey;
                            std::vector<std::pair<std::string, std::string>> sessionFields = {
                                {"playerId", std::to_string(session.playerId)},
                                {"username", session.username},
                                {"charId", std::to_string(session.charId)},
                                {"charName", session.charName},
                                {"sessionKey", session.sessionKey},
                                {"fd", std::to_string(clientSock)}
                            };
                            redis.hset(redisSessionKey, sessionFields);
                            resp.resultCode = 0;
                            std::memset(resp.gameServerAddress, 0, sizeof(resp.gameServerAddress));
                            std::strncpy(resp.gameServerAddress, selectedGameIp.c_str(), sizeof(resp.gameServerAddress) - 1);
                            resp.gameServerPort = selectedGamePort;
                            LOG_DEBUG("CharSelect success: charId=" + std::to_string(charId) + ", gameServer=" + selectedGameIp + ":" + std::to_string(selectedGamePort));
                            sendToClient(&resp, sizeof(resp), clientSock);
                            break;
                        } else {
                            LOG_ERROR("Failed to query Redis for game servers!");
                            resp.resultCode = 3; // Custom code: redis error
                            std::memset(resp.gameServerAddress, 0, sizeof(resp.gameServerAddress));
                            resp.gameServerPort = 0;
                            sendToClient(&resp, sizeof(resp), clientSock);
                            break;
                        }
                    }
                }
                break;
            }
            case PACKET_C_CHAR_DELETE: {
                C_CharDelete req;
                if (DeserializePacketRaw(data, &req, sizeof(C_CharDelete))) {
                    LOG_DEBUG("Received C_CharDelete from " + std::to_string(clientSock));
                    int32_t charId = req.charId;
                    // Check if character belongs to this account
                    std::string query = "SELECT id FROM characters WHERE id=" + std::to_string(charId) + " AND account_id=" + std::to_string(session.playerId);
                    std::vector<std::vector<std::string>> result;
                    bool ok = mysql.query(query, result);
                    S_CharDeleteResult resp{};
                    resp.header.packetId = PACKET_S_CHAR_DELETE_RESULT;
                    resp.charId = charId;
                    if (!ok) {
                        LOG_DEBUG("[MYSQL] error during char delete check: " + mysql.getLastError());
                        resp.resultCode = 1; // fail
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    if (result.empty()) {
                        LOG_DEBUG("CharDelete failed: character does not belong to account or does not exist");
                        resp.resultCode = 1; // fail
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    // Delete character
                    std::string delQuery = "DELETE FROM characters WHERE id=" + std::to_string(charId) + " AND account_id=" + std::to_string(session.playerId);
                    bool delOk = mysql.exec(delQuery);
                    if (!delOk) {
                        LOG_DEBUG("[MYSQL] error during char delete: " + mysql.getLastError());
                        resp.resultCode = 1; // fail
                        sendToClient(&resp, sizeof(resp), clientSock);
                        break;
                    }
                    LOG_DEBUG("CharDelete success: charId=" + std::to_string(charId));
                    resp.resultCode = 0; // success
                    sendToClient(&resp, sizeof(resp), clientSock);
                }
                break;
            }
            case PACKET_C_CHAR_LIST_REQUEST: {
                C_CharListRequest req;
                if (DeserializePacketRaw(data, &req, sizeof(C_CharListRequest))) {
                    LOG_DEBUG("Received C_CharListRequest from fd=" + std::to_string(clientSock));
                    // Query all characters for this account
                    std::string query = "SELECT id, name, class_id FROM characters WHERE account_id=" + std::to_string(session.playerId);
                    std::vector<std::vector<std::string>> result;
                    bool ok = mysql.query(query, result);
                    int count = static_cast<int>(result.size());
                    if (!ok) {
                        LOG_DEBUG("[MYSQL] error during char list: " + mysql.getLastError());
                        // Send empty result
                        size_t respSize = sizeof(S_CharListResult);
                        auto* respBuf = (S_CharListResult*)std::malloc(respSize);
                        std::memset(respBuf, 0, respSize);
                        respBuf->header.packetId = PACKET_S_CHAR_LIST_RESULT;
                        respBuf->charCount = 0;
                        sendToClient(respBuf, respSize, clientSock);
                        std::free(respBuf);
                        break;
                    }
                    // Allocate buffer for header + count + all entries
                    size_t respSize = sizeof(S_CharListResult) + count * sizeof(CharListEntry);
                    auto* respBuf = (S_CharListResult*)std::malloc(respSize);
                    std::memset(respBuf, 0, respSize);
                    respBuf->header.packetId = PACKET_S_CHAR_LIST_RESULT;
                    respBuf->charCount = static_cast<int8_t>(count);
                    CharListEntry* entries = reinterpret_cast<CharListEntry*>(reinterpret_cast<uint8_t*>(respBuf) + sizeof(S_CharListResult));
                    for (int i = 0; i < count; ++i) {
                        entries[i].charId = std::stoi(result[i][0]);
                        std::memset(entries[i].name, 0, sizeof(entries[i].name));
                        std::strncpy(entries[i].name, result[i][1].c_str(), sizeof(entries[i].name) - 1);
                        entries[i].classId = static_cast<int16_t>(std::stoi(result[i][2]));
                    }
                    LOG_DEBUG("CharListResult: " + std::to_string(count) + " chars for playerId=" + std::to_string(session.playerId));
                    sendToClient(respBuf, respSize, clientSock);
                    std::free(respBuf);
                }
                break;
            }
            case PACKET_C_CONNECT_REQUEST: {
                // UDP handshake: treat as connection event, start heartbeat
                LOG_DEBUG("Received C_ConnectRequest from fd=" + std::to_string(clientSock));
                // Directly call the onConnect event handler in the socket server for this client
                if (server && server->onConnect) server->onConnect(clientSock, clientAddr);
                break;
            }
            default:
                LOG_DEBUG("Unknown or unhandled packet: " + std::to_string(header.packetId) + " from fd=" + std::to_string(clientSock));
                break;
        }
    }
    void onClientConnected(intptr_t clientSock, const sockaddr_in& clientAddr) {
        LOG_DEBUG("onClientConnected Auth Server: fd=" + std::to_string(clientSock) + ", endpoint=" + EndpointToString(clientAddr));
        std::string endpointKey = EndpointToString(clientAddr);
        auto& session = sessionMap[endpointKey];
        session.sessionKey = GenerateSessionKey();
        session.connected = true;
        session.clientAddr = clientAddr; // Store sockaddr_in for this session
        LOG_DEBUG("Current sessionMap state: " + std::to_string(sessionMap.size()) + " entries");
            for (const auto& [key, sess] : sessionMap) {
                LOG_DEBUG_EXT("  Session[" + key + "]: playerId=" + std::to_string(sess.playerId) + 
                     ", charId=" + std::to_string(sess.charId) + 
                     ", sessionKey=" + sess.sessionKey + 
                     ", username=" + sess.username + 
                     ", connected=" + (sess.connected ? "true" : "false") +
                     ", clientSock=" + std::to_string(sess.clientSock));
            }
        BaseServer::onClientConnected(clientSock, clientAddr);

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
};

int main(int argc, char** argv) {
    boost::asio::io_context io_context;
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int signal_num){
        LOG_INFO("Signal received, shutting down... (signal=" + std::to_string(signal_num) + ", ec=" + ec.message() + ")");
        BaseServer::SignalHandlerStatic(signal_num);
        io_context.stop();
    });
    AuthServer server;
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
