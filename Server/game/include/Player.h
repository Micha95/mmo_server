#pragma once
#include "Entity.h"
#include <string>
#include <memory>
#include <vector>
#include "../../common/include/MySQLClient.h"
#include "../../common/include/BaseServer.h"

// Player entity
class Player : public Entity {
public:
    std::string name;
    int32_t accountId;
    // Direct pointer to session for efficient access
    SessionInfo* session = nullptr;
    Player();
    void Update(float deltaTime) override;
    void InitFromDB(MySQLClient& mysql, int32_t charId, const std::string& name, int32_t accountId);
    void SaveToDB(MySQLClient& mysql);
};
