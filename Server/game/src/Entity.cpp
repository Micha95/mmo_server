#include <cmath>
#include <limits>
#include "Entity.h"
#include "ZoneManager.h"
#include "../../common/include/Log.h"

void Entity::MoveTo(float newX, float newY, float newZ) {

    LOG_DEBUG_EXT("Entity::MoveTo called: " + std::to_string(id) + " from (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ") to (" + std::to_string(newX) + ", " + std::to_string(newY) + ", " + std::to_string(newZ) + ")");
    float oldX = x, oldY = y;
    int32_t oldZone = zoneId;
    x = newX;
    y = newY;
    z = newZ;

    ZoneManager* zm = ZoneManager::Get();
    int32_t newZone = zm ? zm->CalculateZoneId(x, y) : 0;
    if (zoneManager && newZone != oldZone) {
        LOG_DEBUG("Entity::MoveTo zone change: " + std::to_string(id) + " from zone " + std::to_string(oldZone) + " to zone " + std::to_string(newZone));
        zoneManager->RemoveEntityFromZone(oldZone, id);
        zoneManager->AddEntityToZone(newZone, shared_from_this());
        zoneId = newZone;
    }
}