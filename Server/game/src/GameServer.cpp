#include "../include/GameServer.h"
#include "Player.h"
#include "../../common/include/Log.h"
#include "../../common/include/Packets.h"
#include <iostream>
#include <cstring>

GameServer::GameServer() : BaseServer("game") {
    zoneManager.SetServer(this);
    ZoneManager::SetInstance(&zoneManager); // Register singleton instance for global access
}

void GameServer::handlePacket(const PacketHeader& header, const std::vector<uint8_t>& data, intptr_t clientSock, const sockaddr_in& clientAddr) {
    std::string endpointKey = EndpointToString(clientAddr);
    auto& session = sessionMap[endpointKey];
    session.clientSock = clientSock;
    //CHECK FOR LOGIN
    if(session.playerId == -1 && header.packetId != PACKET_C_CONNECT_REQUEST) {
        LOG_DEBUG("Session not logged in, dropping packet.");
        return;
    }
    switch (header.packetId) {
        case PACKET_C_CONNECT_REQUEST: {
            C_ConnectRequest req;
            if (data.size() < sizeof(C_ConnectRequest)) return;
            std::memcpy(&req, data.data(), sizeof(C_ConnectRequest));
            LOG_DEBUG(std::string("Received C_ConnectRequest from socket ") + std::to_string(clientSock));
            std::string sessionKey(req.sessionKey, strnlen(req.sessionKey, sizeof(req.sessionKey)));
            std::string redisSessionKey = "session_" + sessionKey;
            std::map<std::string, std::string> sessionData;
            bool redisOk = redis.hgetall(redisSessionKey, sessionData);
            if (!redisOk || sessionData.empty()) {
                LOG_ERROR("Session not found in Redis for key: " + redisSessionKey);
                S_ConnectResult resp{};
                resp.header.packetId = PACKET_S_CONNECT_RESULT;
                resp.resultCode = 1; // fail
                sendToClient(&resp, sizeof(resp), clientSock);
                server->disconnect(clientSock);
                sessionMap.erase(endpointKey);
                return;
            }
            if (sessionData.count("playerId")) session.playerId = std::stoi(sessionData["playerId"]);
            if (sessionData.count("charId")) session.charId = std::stoi(sessionData["charId"]);
            if (sessionData.count("username")) session.username = sessionData["username"];
            if (sessionData.count("sessionKey")) session.sessionKey = sessionData["sessionKey"];
            if(sessionData.count("charName")) session.charName = sessionData["charName"];
            else session.sessionKey = sessionKey;
            std::vector<std::pair<std::string, std::string>> updateFields = {
                {"fd", std::to_string(clientSock)}
            };
            redis.hset(redisSessionKey, updateFields);
            session.connected = true;
            session.lastHeartbeat = std::chrono::steady_clock::now();
            session.clientAddr = clientAddr;
            LOG_DEBUG("Session updated in sessionMap for endpoint " + endpointKey + ", sessionKey: " + session.sessionKey + ", clientSock: " + std::to_string(clientSock));
            S_ConnectResult resp{};
            resp.header.packetId = PACKET_S_CONNECT_RESULT;
            resp.resultCode = 0; // success
            sendToClient(&resp, sizeof(resp), clientSock);
            if (resp.resultCode == 0) {
                auto player = std::make_shared<Player>();
                player->InitFromDB(mysql, session.charId, session.charName, session.playerId);
                player->session = &session; // Set direct session pointer
                zoneManager.AddEntityToZone(player->zoneId, player);
                session.playerEntity = player;
            }
            break;
        }
        case PACKET_C_MOVE: {
            C_Move req;
            if (data.size() < sizeof(C_Move)) return;
            std::memcpy(&req, data.data(), sizeof(C_Move));
            LOG_DEBUG_EXT(std::string("Received C_Move from socket ") + std::to_string(clientSock));
            auto player = session.playerEntity;
            if (player) {
                player->MoveTo(req.x, req.y, req.z);
            }
            break;
        }
        default:
            LOG_DEBUG(std::string("Unknown or unhandled packet: ") + std::to_string(header.packetId) + " from socket " + std::to_string(clientSock));
            break;
    }
}
bool GameServer::loadConfig(int argc, char** argv) {
    LOG("Loading game server configuration...");
    BaseServer::loadConfig(argc, argv);
    ZoneManager::Get()->SetZoneSize(config.getInt("zone_size", 100));
    return true;
}

int GameServer::run(int argc, char** argv) {
    // Call our own loadConfig, then BaseServer::run for the rest
    if (!loadConfig(argc, argv)) return 1;
    return BaseServer::BaseServer::run(argc, argv);
}

void GameServer::onClientDisconnected(intptr_t clientSock, const sockaddr_in& clientAddr) {
    std::string endpointKey = EndpointToString(clientAddr);
    auto it = sessionMap.find(endpointKey);
    if (it != sessionMap.end()) {
        //save to mysql 
        if (it->second.playerEntity) {
            it->second.playerEntity->SaveToDB(mysql);
            zoneManager.RemoveEntityFromZone(it->second.playerEntity->zoneId, it->second.playerEntity->id);
        }
        sessionMap.erase(it); // Remove session to avoid stale sockets
    }
    BaseServer::onClientDisconnected(clientSock, clientAddr);
}
