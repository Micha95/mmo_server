#include "ZoneManager.h"
#include "Entity.h"
#include "Player.h"
#include "../../common/include/Log.h"

ZoneManager* ZoneManager::s_instance = nullptr;

ZoneManager* ZoneManager::Get() {
    return s_instance;
}

void ZoneManager::SetInstance(ZoneManager* instance) {
    s_instance = instance;
}

void ZoneManager::AddEntityToZone(int32_t zoneId, std::shared_ptr<Entity> entity) {
    zoneEntities[zoneId][entity->id] = entity;
    switch (entity->type) {
        case EntityType::PLAYER:
            zonePlayers[zoneId][entity->id] = std::static_pointer_cast<Player>(entity);
            break;
        case EntityType::MOB:
            zoneMobs[zoneId][entity->id] = entity;
            break;
        case EntityType::NPC:
            zoneNPCs[zoneId][entity->id] = entity;
            break;
        case EntityType::ITEM:
            zoneItems[zoneId][entity->id] = entity;
            break;
        default:
            break;
    }
    // Broadcast spawn packet to all players in the zone for each entity type
    if (server) {
        const auto& players = GetPlayersInZone(zoneId);
        //Players in zone
        LOG_DEBUG("Adding entity " + std::to_string(entity->id) + " of type " + std::to_string(static_cast<int>(entity->type)) + " to zone " + std::to_string(zoneId));
        switch (entity->type) {
            case EntityType::PLAYER: {
                S_PlayerSpawn spawn{};
                spawn.header.packetId = PACKET_S_PLAYER_SPAWN;
                spawn.entityId = entity->id;
                spawn.shardId = zoneId;
                spawn.x = entity->x;
                spawn.y = entity->y;
                spawn.z = entity->z;
                spawn.yaw = 0.0f; // TODO: Add rotation if available
                Player* player = dynamic_cast<Player*>(entity.get());
                if (player) {
                    strncpy(spawn.name, player->name.c_str(), sizeof(spawn.name)-1);
                    spawn.classId = 0; // TODO: Set actual classId
                    spawn.level = 1;   // TODO: Set actual level
                } else {
                    spawn.name[0] = '\0';
                    spawn.classId = 0;
                    spawn.level = 1;
                }
                for (const auto& pair : players) {
                    auto& p = pair.second;
                    if (p && p->session) {
                        LOG_DEBUG("Sending player spawn packet to player " + p->session->username + " (EntityId: " + std::to_string(entity->id) + ")");
                        server->sendToClient(&spawn, sizeof(spawn), p->session->clientSock);
                        
                    }
                }
                break;
            }
            case EntityType::MOB: {
                S_MobSpawn spawn{};
                spawn.header.packetId = PACKET_S_MOB_SPAWN;
                spawn.entityId = entity->id;
                spawn.shardId = zoneId;
                spawn.x = entity->x;
                spawn.y = entity->y;
                spawn.z = entity->z;
                spawn.yaw = 0.0f; // TODO: Add rotation if available
                spawn.mobTypeId = 0; // TODO: Set actual mob type
                spawn.level = 1;     // TODO: Set actual level
                for (const auto& pair : players) {
                    auto& p = pair.second;
                    if (p && p->session) {
                        server->sendToClient(&spawn, sizeof(spawn), p->session->clientSock);
                    }
                }
                break;
            }
            case EntityType::NPC: {
                S_NPCSpawn spawn{};
                spawn.header.packetId = PACKET_S_NPC_SPAWN;
                spawn.entityId = entity->id;
                spawn.shardId = zoneId;
                spawn.x = entity->x;
                spawn.y = entity->y;
                spawn.z = entity->z;
                spawn.yaw = 0.0f; // TODO: Add rotation if available
                spawn.npcTypeId = 0; // TODO: Set actual npc type
                for (const auto& pair : players) {
                    auto& p = pair.second;
                    if (p && p->session) {
                        server->sendToClient(&spawn, sizeof(spawn), p->session->clientSock);
                    }
                }
                break;
            }
            case EntityType::ITEM: {
                S_ItemSpawn spawn{};
                spawn.header.packetId = PACKET_S_ITEM_SPAWN;
                spawn.entityId = entity->id;
                spawn.shardId = zoneId;
                spawn.x = entity->x;
                spawn.y = entity->y;
                spawn.z = entity->z;
                spawn.itemId = 0; // TODO: Set actual item id
                spawn.count = 1;  // TODO: Set actual count
                for (const auto& pair : players) {
                    auto& p = pair.second;
                    if (p && p->session) {
                        server->sendToClient(&spawn, sizeof(spawn), p->session->clientSock);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void ZoneManager::RemoveEntityFromZone(int32_t zoneId, int32_t entityId) {
    auto it = zoneEntities.find(zoneId);
    if (it != zoneEntities.end()) {
        it->second.erase(entityId);
        if (it->second.empty()) {
            zoneEntities.erase(it);
        }
    }
    // Remove from type-specific maps
    auto pit = zonePlayers.find(zoneId);
    if (pit != zonePlayers.end()) {
        pit->second.erase(entityId);
        if (pit->second.empty()) zonePlayers.erase(pit);
    }
    auto mit = zoneMobs.find(zoneId);
    if (mit != zoneMobs.end()) {
        mit->second.erase(entityId);
        if (mit->second.empty()) zoneMobs.erase(mit);
    }
    auto nit = zoneNPCs.find(zoneId);
    if (nit != zoneNPCs.end()) {
        nit->second.erase(entityId);
        if (nit->second.empty()) zoneNPCs.erase(nit);
    }
    auto iit = zoneItems.find(zoneId);
    if (iit != zoneItems.end()) {
        iit->second.erase(entityId);
        if (iit->second.empty()) zoneItems.erase(iit);
    }
}

const std::unordered_map<int32_t, std::shared_ptr<Entity>>& ZoneManager::GetEntitiesInZone(int32_t zoneId) const {
    static const std::unordered_map<int32_t, std::shared_ptr<Entity>> emptyMap;
    auto it = zoneEntities.find(zoneId);
    if (it != zoneEntities.end()) {
        return it->second;
    }
    return emptyMap;
}

const std::unordered_map<int32_t, std::shared_ptr<Player>>& ZoneManager::GetPlayersInZone(int32_t zoneId) const {
    static const std::unordered_map<int32_t, std::shared_ptr<Player>> emptyMap;
    auto it = zonePlayers.find(zoneId);
    if (it != zonePlayers.end()) {
        return it->second;
    }
    return emptyMap;
}

// Get nearby zones based on the entity's position
// This function returns a vector of zone IDs that are within a 3x3 grid around the entity's position
// The center zone is the one the entity is currently in, and the surrounding zones are also included
std::vector<int32_t> ZoneManager::GetNearbyZones(float x, float y, float z) const {

    int zx = static_cast<int>(x) / GetZoneSize();
    int zy = static_cast<int>(y) / GetZoneSize();
    std::vector<int32_t> result;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            int zoneId = (zx + dx) + 1000 * (zy + dy);
            if (zoneEntities.count(zoneId)) {
                result.push_back(zoneId);
            }
        }
    }
    return result;
}



int32_t ZoneManager::CalculateZoneId(float x, float y) const {
    return static_cast<int32_t>(x) / zoneSize + 1000 * (static_cast<int32_t>(y) / zoneSize);
}

void ZoneManager::SetZoneSize(int size) {
    zoneSize = size;
    LOG("Zone size set to " + std::to_string(zoneSize));
    if (zoneSize <= 0) {
        LOG_ERROR("Invalid zone size: " + std::to_string(zoneSize) + ". Resetting to default 100.");
        zoneSize = 100;
    }
}

int ZoneManager::GetZoneSize() const {
    return zoneSize;
}

std::vector<std::tuple<int32_t, int, int>> ZoneManager::GetZoneSummary() const {
    std::vector<std::tuple<int32_t, int, int>> result;
    for (const auto& zonePair : zoneEntities) {
        int32_t zoneId = zonePair.first;
        int entityCount = zonePair.second.size();
        int playerCount = 0;
        auto it = zonePlayers.find(zoneId);
        if (it != zonePlayers.end()) playerCount = it->second.size();
        result.emplace_back(zoneId, entityCount, playerCount);
    }
    return result;
}

