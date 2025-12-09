#pragma once
#include <unordered_map>
#include <vector>
#include <set>
#include <memory>
#include "../../common/include/BaseServer.h"

class Entity;
class Player;

// ZoneManager splits the world into zones for network culling
class ZoneManager {
public:
    // Singleton accessor
    static ZoneManager* Get();
    static void SetInstance(ZoneManager* instance);

    // Assign entity to a zone
    void AddEntityToZone(int32_t zoneId, std::shared_ptr<Entity> entity);
    // Remove entity from a zone
    void RemoveEntityFromZone(int32_t zoneId, int32_t entityId);
    // Get all entities in a zone (returns map of id -> entity)
    const std::unordered_map<int32_t, std::shared_ptr<Entity>>& GetEntitiesInZone(int32_t zoneId) const;
    // Get all players in a zone
    const std::unordered_map<int32_t, std::shared_ptr<Player>>& GetPlayersInZone(int32_t zoneId) const;
    // Get zones near a position (for AOI/network culling)
    std::vector<int32_t> GetNearbyZones(float x, float y, float z) const;
    // Calculate zone id from position
    int32_t CalculateZoneId(float x, float y) const;
    // Set zone size from config
    void SetZoneSize(int size);
    int GetZoneSize() const;
    // Set the server pointer
    void SetServer(BaseServer* srv) { server = srv; }
    // Get a summary of all zones: (zoneId, entityCount, playerCount)
    std::vector<std::tuple<int32_t, int, int>> GetZoneSummary() const;

private:
    static ZoneManager* s_instance;
    int zoneSize = 100;
    // zoneId -> (entityId -> shared_ptr<Entity>) for all entities
    std::unordered_map<int32_t, std::unordered_map<int32_t, std::shared_ptr<Entity>>> zoneEntities;
    // zoneId -> (entityId -> shared_ptr<Player>)
    std::unordered_map<int32_t, std::unordered_map<int32_t, std::shared_ptr<Player>>> zonePlayers;
    // zoneId -> (entityId -> shared_ptr<Entity>) for mobs
    std::unordered_map<int32_t, std::unordered_map<int32_t, std::shared_ptr<Entity>>> zoneMobs;
    // zoneId -> (entityId -> shared_ptr<Entity>) for npcs
    std::unordered_map<int32_t, std::unordered_map<int32_t, std::shared_ptr<Entity>>> zoneNPCs;
    // zoneId -> (entityId -> shared_ptr<Entity>) for items
    std::unordered_map<int32_t, std::unordered_map<int32_t, std::shared_ptr<Entity>>> zoneItems;
    BaseServer* server = nullptr;
};
